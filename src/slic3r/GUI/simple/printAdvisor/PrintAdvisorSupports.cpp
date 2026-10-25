#include "libslic3r/Model.hpp"
#include "libslic3r/ModelInstance.hpp"
#include "libslic3r/ModelObject.hpp"
#include "libslic3r/ModelVolume.hpp"
#include "PrintAdvisorInternal.hpp"

#include "slic3r/GUI/GUI_App.hpp"
#include "slic3r/GUI/GUI_ObjectList.hpp"
#include "slic3r/GUI/ObjectDataViewModel.hpp"
#include "slic3r/GUI/Plater.hpp"
#include "libslic3r/AABBTreeIndirect.hpp"
#include "libslic3r/Model.hpp"
#include "libslic3r/PresetBundle.hpp"
#include "libslic3r/TriangleMesh.hpp"

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <limits>
#include <locale>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace Slic3r {
namespace GUI {
namespace advisor {

// Support routes: precise overhang data and support enforcer/blocker volumes. The MCP client
// decides where supports go; this module only measures and applies confirmed primitives.

namespace {

using json = nlohmann::json;

constexpr size_t      max_triangles      = 1500000;
constexpr size_t      max_clusters       = 60;
constexpr double      bed_contact_mm     = 0.2;
constexpr double      ray_start_offset   = 0.05;
constexpr double      bbox_margin_mm     = 5.0;
const char* const     enforcer_prefix    = "Advisor enforcer";
const char* const     blocker_prefix     = "Advisor blocker";

const char* const support_keys[] = {"enable_support", "support_type", "support_threshold_angle", "support_top_z_distance",
                                    "support_interface_top_layers", "support_on_build_plate_only", "enforce_support_layers"};

double round2(double value) { return std::round(value * 100.0) / 100.0; }

json vec_json(const Vec3d& v) { return json::array({round2(v.x()), round2(v.y()), round2(v.z())}); }

std::string format_mm(double value)
{
    std::ostringstream out;
    out.imbue(std::locale::classic());
    out << std::fixed << std::setprecision(1) << value;
    return out.str();
}

std::string format_vec(const Vec3d& v) { return format_mm(v.x()) + ", " + format_mm(v.y()) + ", " + format_mm(v.z()); }

// Finite JSON number (negative allowed, unlike json_number), or throws.
double finite_number(const json& body, const char* key, double fallback, bool required)
{
    if (!body.contains(key) || body[key].is_null()) {
        if (required)
            throw std::runtime_error(std::string(key) + " is required");
        return fallback;
    }
    if (!body[key].is_number())
        throw std::runtime_error(std::string(key) + " must be a number");
    const double value = body[key].get<double>();
    if (!std::isfinite(value))
        throw std::runtime_error(std::string(key) + " must be finite");
    return value;
}

Vec3d vec3_from(const json& body, const char* key)
{
    if (!body.contains(key) || !body[key].is_array() || body[key].size() != 3)
        throw std::runtime_error(std::string(key) + " must be an array of 3 numbers");
    Vec3d out;
    for (size_t i = 0; i < 3; ++i) {
        const json& item = body[key][i];
        if (!item.is_number() || !std::isfinite(item.get<double>()))
            throw std::runtime_error(std::string(key) + " must contain 3 finite numbers");
        out[i] = item.get<double>();
    }
    return out;
}

struct ResolvedObject {
    Plater*      plater{nullptr};
    size_t       index{0};
    ModelObject* object{nullptr};
    ObjectID     object_id; // identity: undo/redo may recreate the object at another address
};

// Same resolution as the object settings route: index into the plater's model objects.
ResolvedObject resolve_object(const json& body)
{
    Plater* plater = wxGetApp().plater();
    if (!plater || !wxGetApp().obj_list())
        throw std::runtime_error("MentorSlicer is not ready");
    if (!body.contains("object_index") || !body["object_index"].is_number_integer() || body["object_index"].get<long long>() < 0)
        throw std::runtime_error("object_index must be a non-negative integer");
    const size_t index = body["object_index"].get<size_t>();
    Model& model = plater->model();
    if (index >= model.objects.size() || !model.objects[index] || model.objects[index]->instances.empty())
        throw std::runtime_error("object_index is not valid");
    return {plater, index, model.objects[index], model.objects[index]->id()};
}

// Throws when the object at `resolved.index` was replaced (for example while a dialog was open).
// Returns the current object: same ObjectID, possibly at another address after undo/redo.
ModelObject* check_object_unchanged(const ResolvedObject& resolved)
{
    const Model& model = resolved.plater->model();
    if (resolved.index >= model.objects.size() || !model.objects[resolved.index] ||
        model.objects[resolved.index]->id() != resolved.object_id || model.objects[resolved.index]->instances.empty())
        throw std::runtime_error("The objects changed while the dialog was open; try again");
    return model.objects[resolved.index];
}

size_t model_part_triangles(const ModelObject& object)
{
    size_t count = 0;
    for (const ModelVolume* volume : object.volumes)
        if (volume && volume->is_model_part())
            count += volume->mesh().its.indices.size();
    return count;
}

// Support settings of the edited process profile and the object's own overrides.
json support_settings(const ModelObject& object)
{
    json out = json::object();
    const PresetBundle* bundle = wxGetApp().preset_bundle;
    const DynamicPrintConfig* process = bundle ? &bundle->prints.get_edited_preset().config : nullptr;
    for (const char* key : support_keys) {
        json entry = json::object();
        const ConfigOption* global = process ? process->option(key) : nullptr;
        entry["process"] = global ? json(global->serialize()) : json(nullptr);
        const ConfigOption* override_option = object.config.option(key);
        if (override_option)
            entry["object_override"] = override_option->serialize();
        entry["effective"] = override_option ? json(override_option->serialize()) : entry["process"];
        out[key] = entry;
    }
    return out;
}

bool is_advisor_volume_name(const std::string& name)
{
    return name.rfind(enforcer_prefix, 0) == 0 || name.rfind(blocker_prefix, 0) == 0;
}

const char* support_type_name(const ModelVolume& volume) { return volume.is_support_enforcer() ? "enforcer" : "blocker"; }

// --- /v1/overhang_regions -------------------------------------------------------------------

json overhang_regions(const json& body, const std::atomic<bool>&)
{
    const ResolvedObject resolved = resolve_object(body);
    const ModelObject& object = *resolved.object;
    const double threshold = finite_number(body, "threshold_deg", 45.0, false);
    if (threshold < 10.0 || threshold > 89.0)
        throw std::runtime_error("threshold_deg must be between 10 and 89");
    const double min_area = finite_number(body, "min_area_mm2", 5.0, false);
    if (min_area < 0.0 || min_area > 100000.0)
        throw std::runtime_error("min_area_mm2 must be between 0 and 100000");

    const size_t triangles = model_part_triangles(object);
    if (triangles == 0)
        throw std::runtime_error("The object has no mesh");
    if (triangles > max_triangles)
        throw std::runtime_error("The object has " + std::to_string(triangles) +
                                 " triangles (limit 1500000): simplify it first (right click > Simplify model)");

    // Mesh of the model parts in world coordinates (instance 0).
    indexed_triangle_set its = object.raw_indexed_triangle_set();
    const Transform3d instance = object.instances.front()->get_matrix();
    for (stl_vertex& vertex : its.vertices)
        vertex = (instance * vertex.cast<double>()).cast<float>().eval();
    if (instance.matrix().block<3, 3>(0, 0).determinant() < 0.0)
        for (stl_triangle_vertex_indices& face : its.indices)
            std::swap(face[1], face[2]);

    BoundingBoxf3 object_box;
    for (const stl_vertex& vertex : its.vertices)
        object_box.merge(vertex.cast<double>());
    const double lowest = object_box.min.z();

    // Classify faces. slope = angle between the face plane and the horizontal plane
    // (0 = flat ceiling, 90 = vertical wall), the quantity support_threshold_angle refers to.
    const size_t face_count = its.indices.size();
    std::vector<char>   candidate(face_count, 0);
    std::vector<double> face_area(face_count, 0.0);
    std::vector<double> face_angle(face_count, 0.0); // overhang angle from vertical = 90 - slope
    size_t bed_faces = 0;
    for (size_t i = 0; i < face_count; ++i) {
        const auto& face = its.indices[i];
        const Vec3d a = its.vertices[face[0]].cast<double>();
        const Vec3d b = its.vertices[face[1]].cast<double>();
        const Vec3d c = its.vertices[face[2]].cast<double>();
        const Vec3d cross = (b - a).cross(c - a);
        const double length = cross.norm();
        if (length < 1e-12)
            continue;
        const double nz = cross.z() / length;
        if (nz >= 0.0)
            continue;
        const double slope = std::acos(std::clamp(-nz, -1.0, 1.0)) * 180.0 / PI;
        if (slope >= threshold)
            continue;
        const double z_max = std::max({a.z(), b.z(), c.z()});
        if (z_max <= lowest + bed_contact_mm) {
            ++bed_faces;
            continue;
        }
        candidate[i]  = 1;
        face_area[i]  = 0.5 * length;
        face_angle[i] = 90.0 - slope;
    }

    // Connected components over shared edges.
    const std::vector<Vec3i32> neighbors = its_face_neighbors(its);
    struct Cluster {
        std::vector<size_t> faces;
        double area{0}, angle_sum{0}, max_angle{0};
        Vec3d  centroid_sum{Vec3d::Zero()};
        BoundingBoxf3 box;
    };
    std::vector<Cluster> clusters;
    std::vector<char> visited(face_count, 0);
    std::vector<size_t> stack;
    for (size_t seed = 0; seed < face_count; ++seed) {
        if (!candidate[seed] || visited[seed])
            continue;
        Cluster cluster;
        visited[seed] = 1;
        stack.assign(1, seed);
        while (!stack.empty()) {
            const size_t face = stack.back();
            stack.pop_back();
            cluster.faces.push_back(face);
            const auto& indices = its.indices[face];
            Vec3d center = Vec3d::Zero();
            for (int k = 0; k < 3; ++k) {
                const Vec3d point = its.vertices[indices[k]].cast<double>();
                cluster.box.merge(point);
                center += point;
            }
            center /= 3.0;
            cluster.area += face_area[face];
            cluster.angle_sum += face_angle[face] * face_area[face];
            cluster.max_angle = std::max(cluster.max_angle, face_angle[face]);
            cluster.centroid_sum += center * face_area[face];
            for (int k = 0; k < 3; ++k) {
                const int next = neighbors[face][k];
                if (next >= 0 && size_t(next) < face_count && candidate[next] && !visited[next]) {
                    visited[next] = 1;
                    stack.push_back(size_t(next));
                }
            }
        }
        clusters.push_back(std::move(cluster));
    }

    double total_area = 0.0, small_area = 0.0;
    size_t small_count = 0;
    std::vector<Cluster> kept;
    for (Cluster& cluster : clusters) {
        total_area += cluster.area;
        if (cluster.area < min_area || cluster.area <= 0.0) {
            ++small_count;
            small_area += cluster.area;
        } else
            kept.push_back(std::move(cluster));
    }
    std::sort(kept.begin(), kept.end(), [](const Cluster& l, const Cluster& r) { return l.area > r.area; });
    const size_t truncated = kept.size() > max_clusters ? kept.size() - max_clusters : 0;
    if (truncated)
        kept.resize(max_clusters);

    const auto tree = AABBTreeIndirect::build_aabb_tree_over_indexed_triangle_set(its.vertices, its.indices);
    json out_clusters = json::array();
    int id = 0;
    for (const Cluster& cluster : kept) {
        const Vec3d centroid = cluster.centroid_sum / cluster.area;
        // Probe from the cluster face closest to the area centroid, so the point lies on the
        // overhang even when the cluster is curved or concave.
        Vec3d probe = centroid;
        double best = std::numeric_limits<double>::max();
        for (size_t face : cluster.faces) {
            const auto& indices = its.indices[face];
            const Vec3d center = (its.vertices[indices[0]].cast<double>() + its.vertices[indices[1]].cast<double>() +
                                  its.vertices[indices[2]].cast<double>()) / 3.0;
            const double distance = (center - centroid).squaredNorm();
            if (distance < best) {
                best  = distance;
                probe = center;
            }
        }
        const Vec3d origin = probe - Vec3d(0.0, 0.0, ray_start_offset);
        igl::Hit hit;
        double clearance = std::max(0.0, probe.z());
        std::string below = "bed";
        if (AABBTreeIndirect::intersect_ray_first_hit(its.vertices, its.indices, tree, origin, Vec3d(0.0, 0.0, -1.0), hit) &&
            std::isfinite(hit.t) && origin.z() - hit.t > -1e-3) {
            clearance = hit.t + ray_start_offset;
            below = "object";
        }

        const Vec3d size = cluster.box.size();
        const double span = std::min(size.x(), size.y());
        const double mean_angle = cluster.angle_sum / cluster.area;
        const char* kind = mean_angle > 80.0 ? (span < 30.0 ? "bridge_like" : "ceiling") : "overhang";
        out_clusters.push_back({{"id", ++id},
                                {"area_mm2", round2(cluster.area)},
                                {"centroid", vec_json(centroid)},
                                {"probe_point", vec_json(probe)},
                                {"bbox_min", vec_json(cluster.box.min)},
                                {"bbox_max", vec_json(cluster.box.max)},
                                {"z_min", round2(cluster.box.min.z())},
                                {"z_max", round2(cluster.box.max.z())},
                                {"height_above_bed", round2(cluster.box.min.z())},
                                {"max_angle_deg", round2(cluster.max_angle)},
                                {"mean_angle_deg", round2(mean_angle)},
                                {"kind", kind},
                                {"span_mm", round2(span)},
                                {"face_count", cluster.faces.size()},
                                {"below_clearance_mm", round2(clearance)},
                                {"below", below}});
    }

    return {{"success", true},
            {"object_index", resolved.index},
            {"object", object.name},
            {"instances", object.instances.size()},
            {"triangles", triangles},
            {"object_bbox", {{"min", vec_json(object_box.min)}, {"max", vec_json(object_box.max)}}},
            {"threshold_deg", threshold},
            {"min_area_mm2", min_area},
            {"convention",
             {{"threshold_deg", "slope of the face from the horizontal plane, same meaning as support_threshold_angle: a "
                                "downward face is an overhang when its slope is below threshold_deg (0 = flat ceiling, "
                                "90 = vertical wall). The slicer internally uses support_threshold_angle + 1."},
              {"angles", "max_angle_deg / mean_angle_deg are measured from vertical: 90 - slope (90 = flat ceiling); "
                         "mean is area weighted"},
              {"coordinates", "world mm of instance 0 (bed at z = 0); other instances share the object geometry"},
              {"bed_faces", "faces entirely within 0.2 mm of the object's lowest point are excluded; sloped faces "
                            "that start at the bed are kept (height_above_bed about 0)"},
              {"kind", "heuristic: mean angle > 80 deg and span < 30 mm = bridge_like, mean > 80 deg and wider = "
                       "ceiling, otherwise overhang. bridge_like does not verify that both ends are supported"},
              {"below_clearance_mm", "straight-down distance from probe_point (cluster face nearest to the "
                                     "centroid) to the object itself ('object') or to the bed ('bed')"},
              {"model_parts_only", "modifiers, negative volumes and support volumes are ignored"}}},
            {"support_settings", support_settings(object)},
            {"overhang_area_total_mm2", round2(total_area)},
            {"cluster_count_total", clusters.size()},
            {"filtered_small", {{"count", small_count}, {"area_mm2", round2(small_area)}}},
            {"bed_contact_faces_excluded", bed_faces},
            {"returned", out_clusters.size()},
            {"truncated_count", truncated},
            {"clusters", out_clusters}};
}

// --- /v1/support_volume ---------------------------------------------------------------------

json add_support_volume(const json& body, const std::atomic<bool>& stopping)
{
    const ResolvedObject resolved = resolve_object(body);
    ModelObject* object = resolved.object;
    const std::string type = body.value("type", std::string());
    if (type != "enforcer" && type != "blocker")
        throw std::runtime_error("type must be enforcer or blocker");
    const std::string shape = body.value("shape", std::string());
    if (shape != "box" && shape != "cylinder")
        throw std::runtime_error("shape must be box or cylinder");
    const Vec3d center = vec3_from(body, "center");
    const Vec3d size   = vec3_from(body, "size");
    for (int i = 0; i < 3; ++i)
        if (size[i] < 0.5 || size[i] > 300.0)
            throw std::runtime_error("size values must be between 0.5 and 300 mm");
    if (shape == "cylinder" && std::abs(size.x() - size.y()) > 1e-6)
        throw std::runtime_error("cylinder size must be [diameter, diameter, height]");
    std::string reason;
    if (body.contains("reason")) {
        if (!body["reason"].is_string() || body["reason"].get<std::string>().size() > 500)
            throw std::runtime_error("reason must be a string of at most 500 characters");
        reason = body["reason"].get<std::string>();
    }
    BoundingBoxf3 allowed = object->instance_bounding_box(0);
    allowed.min -= Vec3d::Constant(bbox_margin_mm);
    allowed.max += Vec3d::Constant(bbox_margin_mm);
    if (!allowed.contains(center))
        throw std::runtime_error("center must lie inside the object's bounding box expanded by 5 mm (" +
                                 format_vec(allowed.min) + " .. " + format_vec(allowed.max) + ")");

    const bool enforcer = type == "enforcer";
    const char* prefix  = enforcer ? enforcer_prefix : blocker_prefix;
    int number = 0;
    for (const ModelVolume* volume : object->volumes) {
        if (!volume || volume->name.rfind(prefix, 0) != 0)
            continue;
        try {
            number = std::max(number, std::stoi(volume->name.substr(std::string(prefix).size())));
        } catch (const std::exception&) {}
    }
    const std::string name = std::string(prefix) + " " + std::to_string(number + 1);

    const std::string type_text  = enforcer ? "Rinforzo supporti (enforcer)" : "Blocco supporti (blocker)";
    const std::string shape_text = shape == "box" ? "Parallelepipedo" : "Cilindro";
    const std::string size_text  = shape == "box" ? format_vec(size) + " mm"
                                                  : "diametro " + format_mm(size.x()) + ", altezza " + format_mm(size.z()) + " mm";
    std::vector<AdvisorChangeRow> rows = {{"Tipo", "-", type_text},
                                          {"Forma", "-", shape_text},
                                          {"Posizione (centro)", "-", format_vec(center) + " mm"},
                                          {"Dimensioni", "-", size_text}};
    std::string question_reason;
    if (const std::string shown = reason_text(reason); !shown.empty())
        question_reason = "\n\n" + shown;
    std::string question = "Aggiungere il volume '" + name + "' all'oggetto '" + sanitized_text(object->name) + "'?\n\n";
    question += enforcer ? "I supporti verranno generati dentro questo volume (servono i supporti attivi).\n"
                         : "Nessun supporto verrà generato dentro questo volume.\n";
    question += "I profili non cambiano. Puoi annullare con Ctrl+Z." + question_reason;
    if (!ask_user(enforcer ? "Aggiungi rinforzo supporti" : "Aggiungi blocco supporti", question, stopping, rows))
        return {{"success", false}, {"cancelled", true}};
    object = check_object_unchanged(resolved);

    Plater* plater = resolved.plater;
    ObjectList* list = wxGetApp().obj_list();
    if (!list)
        throw std::runtime_error("MentorSlicer is not ready");
    Plater::SingleSnapshot single(plater);
    plater->take_snapshot("Print Advisor: supporto");

    TriangleMesh mesh = shape == "box" ? TriangleMesh(its_make_cube(size.x(), size.y(), size.z()))
                                       : TriangleMesh(its_make_cylinder(0.5 * size.x(), size.z(), 2.0 * PI / 64.0));
    // add_volume centers the mesh on its bounding box. Like ObjectList::load_generic_subobject,
    // the volume is axis-aligned in world space: instance * volume = translation(center).
    ModelVolume* volume = object->add_volume(std::move(mesh), enforcer ? ModelVolumeType::SUPPORT_ENFORCER
                                                                       : ModelVolumeType::SUPPORT_BLOCKER);
    const Transform3d instance = object->instances.front()->get_matrix();
    volume->set_transformation(Transform3d(instance.inverse() * Geometry::translation_transform(center)));
    volume->name = name;
    volume->config.set_key_value("extruder", new ConfigOptionInt(0));
    volume->source.is_from_builtin_objects = true;
    object->invalidate_bounding_box();

    const int obj_idx = int(resolved.index);
    list->reorder_volumes_and_get_selection(obj_idx); // sorts volumes, rebuilds the list, changed_object
    list->notify_instance_updated(obj_idx);
    plater->changed_object(obj_idx);

    append_history({{"action", "support_volume_added"}, {"object", object->name}, {"name", name}, {"type", type},
                    {"shape", shape}, {"center", vec_json(center)}, {"size", vec_json(size)}, {"reason", reason}});
    return {{"success", true},
            {"volume_name", name},
            {"note", "Ctrl+Z in MentorSlicer undoes it. In this fork support is generated only when the object has "
                     "support (enable_support = 1 or enforce_support_layers > 0): with enable_support = 0 the enforcer "
                     "is ignored and the slicer warns 'Support enforcers are used but support is not enabled'. With "
                     "support_type normal(manual)/tree(manual) only enforced areas get support; with (auto) "
                     "enforcers add to the automatic areas and blockers remove from them."}};
}

// --- /v1/support_volumes --------------------------------------------------------------------

json list_support_volumes(const json& body, const std::atomic<bool>&)
{
    const ResolvedObject resolved = resolve_object(body);
    const ModelObject& object = *resolved.object;
    const Transform3d instance = object.instances.front()->get_matrix();
    json volumes = json::array();
    for (size_t i = 0; i < object.volumes.size(); ++i) {
        const ModelVolume* volume = object.volumes[i];
        if (!volume || !volume->is_support_modifier())
            continue;
        const BoundingBoxf3 box = volume->mesh().transformed_bounding_box(instance * volume->get_matrix());
        const size_t facets = volume->mesh().its.indices.size();
        volumes.push_back({{"name", volume->name},
                           {"type", support_type_name(*volume)},
                           {"shape", facets == 12 ? "box" : "other"},
                           {"center", vec_json(box.center())},
                           {"size", vec_json(box.size())},
                           {"advisor_created", is_advisor_volume_name(volume->name)}});
    }
    return {{"success", true}, {"object_index", resolved.index}, {"object", object.name}, {"volumes", volumes},
            {"note", "center and size are the world axis-aligned bounding box of instance 0; only advisor_created "
                     "volumes can be removed through Print Advisor"}};
}

// --- /v1/remove_support_volume --------------------------------------------------------------

int find_advisor_volume(const ModelObject& object, const std::string& name)
{
    for (size_t i = 0; i < object.volumes.size(); ++i) {
        const ModelVolume* volume = object.volumes[i];
        if (volume && volume->is_support_modifier() && volume->name == name)
            return int(i);
    }
    return -1;
}

json remove_support_volume(const json& body, const std::atomic<bool>& stopping)
{
    const ResolvedObject resolved = resolve_object(body);
    ModelObject* object = resolved.object;
    if (!body.contains("volume_name") || !body["volume_name"].is_string())
        throw std::runtime_error("volume_name is required");
    const std::string name = body["volume_name"].get<std::string>();
    if (!is_advisor_volume_name(name))
        throw std::runtime_error("Only volumes named 'Advisor enforcer N' or 'Advisor blocker N' can be removed");
    const int index = find_advisor_volume(*object, name);
    if (index < 0)
        throw std::runtime_error("The object has no support volume named '" + name + "'");
    const ModelVolume* volume = object->volumes[index];
    const ObjectID volume_id = volume->id();
    const std::string type = support_type_name(*volume);
    const BoundingBoxf3 box = volume->mesh().transformed_bounding_box(object->instances.front()->get_matrix() * volume->get_matrix());

    std::vector<AdvisorChangeRow> rows = {{"Volume", sanitized_text(name), "-"},
                                          {"Tipo", type == "enforcer" ? "Rinforzo supporti" : "Blocco supporti", "-"},
                                          {"Posizione (centro)", format_vec(box.center()) + " mm", "-"}};
    const std::string question = "Rimuovere il volume '" + sanitized_text(name) + "' dall'oggetto '" +
                                 sanitized_text(object->name) + "'?\n\nPuoi annullare con Ctrl+Z.";
    if (!ask_user("Rimuovi volume supporti", question, stopping, rows, true))
        return {{"success", false}, {"cancelled", true}};
    object = check_object_unchanged(resolved);
    const int current = find_advisor_volume(*object, name);
    if (current < 0 || object->volumes[current]->id() != volume_id)
        throw std::runtime_error("The volumes changed while the dialog was open; try again");

    ObjectList* list = wxGetApp().obj_list();
    if (!list)
        throw std::runtime_error("MentorSlicer is not ready");
    Plater* plater = resolved.plater;
    {
        Plater::SingleSnapshot single(plater);
        plater->take_snapshot("Print Advisor: rimuovi supporto");
        // Same path as deleting a part from the object list.
        list->delete_from_model_and_list(ItemType::itVolume, int(resolved.index), current);
    }
    if (find_advisor_volume(*object, name) >= 0)
        throw std::runtime_error("The volume could not be removed");

    append_history({{"action", "support_volume_removed"}, {"object", object->name}, {"name", name}, {"type", type},
                    {"center", vec_json(box.center())}, {"size", vec_json(box.size())}});
    return {{"success", true}, {"removed", name}, {"note", "Ctrl+Z in MentorSlicer restores it"}};
}

} // namespace

std::vector<AdvisorRoute> supports_routes()
{
    return {{"/v1/overhang_regions", "POST", true, overhang_regions},
            {"/v1/support_volume", "POST", true, add_support_volume},
            {"/v1/support_volumes", "POST", true, list_support_volumes},
            {"/v1/remove_support_volume", "POST", true, remove_support_volume}};
}

} // namespace advisor
} // namespace GUI
} // namespace Slic3r
