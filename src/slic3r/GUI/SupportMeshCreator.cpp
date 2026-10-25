// See SupportMeshCreator.hpp for overview.

#include "SupportMeshCreator.hpp"

#include <vector>
#include <memory>
#include <algorithm>
#include <unordered_map>
#include <unordered_set>
#include <cmath>
#include <functional>
#include <limits>

#include <boost/filesystem.hpp>
#include <tbb/blocked_range.h>
#include <tbb/enumerable_thread_specific.h>
#include <tbb/parallel_for.h>

#include "GUI_App.hpp"             // wxGetApp()

#include "libslic3r/Model.hpp"
#include "libslic3r/ModelInstance.hpp"
#include "libslic3r/AABBMesh.hpp"
#include "libslic3r/TriangleMesh.hpp"
#include "libslic3r/Polygon.hpp"
#include "libslic3r/BoundingBox.hpp"
#include "libslic3r/ModelVolume.hpp"
#include "libslic3r/ModelObject.hpp"
#include "libslic3r/PrintConfig.hpp"
#include "libslic3r/Config.hpp"
#include "libslic3r/TriangleMesh.hpp"
#include "libslic3r/TriangleMesh.hpp"

namespace Slic3r { namespace GUI {

// Build a simple cuboid mesh from a BoundingBoxf3 in object coordinates.
static TriangleMesh make_box_mesh(const BoundingBoxf3& bbox)
{
    const Vec3d mn = bbox.min;
    const Vec3d mx = bbox.max;

    std::vector<Vec3f> verts;
    verts.reserve(8);
    verts.emplace_back(static_cast<float>(mn.x()), static_cast<float>(mn.y()), static_cast<float>(mn.z())); // v0
    verts.emplace_back(static_cast<float>(mx.x()), static_cast<float>(mn.y()), static_cast<float>(mn.z())); // v1
    verts.emplace_back(static_cast<float>(mx.x()), static_cast<float>(mx.y()), static_cast<float>(mn.z())); // v2
    verts.emplace_back(static_cast<float>(mn.x()), static_cast<float>(mx.y()), static_cast<float>(mn.z())); // v3
    verts.emplace_back(static_cast<float>(mn.x()), static_cast<float>(mn.y()), static_cast<float>(mx.z())); // v4
    verts.emplace_back(static_cast<float>(mx.x()), static_cast<float>(mn.y()), static_cast<float>(mx.z())); // v5
    verts.emplace_back(static_cast<float>(mx.x()), static_cast<float>(mx.y()), static_cast<float>(mx.z())); // v6
    verts.emplace_back(static_cast<float>(mn.x()), static_cast<float>(mx.y()), static_cast<float>(mx.z())); // v7

    std::vector<Vec3i32> faces;
    faces.reserve(12);
    // Bottom (z = min)
    faces.emplace_back(0, 1, 2);
    faces.emplace_back(0, 2, 3);
    // Top (z = max)
    faces.emplace_back(4, 6, 5);
    faces.emplace_back(4, 7, 6);
    // Front (y = min)
    faces.emplace_back(0, 4, 5);
    faces.emplace_back(0, 5, 1);
    // Back (y = max)
    faces.emplace_back(3, 2, 6);
    faces.emplace_back(3, 6, 7);
    // Left (x = min)
    faces.emplace_back(0, 3, 7);
    faces.emplace_back(0, 7, 4);
    // Right (x = max)
    faces.emplace_back(1, 5, 6);
    faces.emplace_back(1, 6, 2);

    return TriangleMesh(std::move(verts), std::move(faces));
}

// Helper to read a numeric option (float / int / bool) from model or printer preset.
// Returns default_value if the key is missing.
static double get_printer_float_option(const ModelConfigObject& model_config, const std::string& key, double default_value = 0.0)
{
    auto& process_cfg = wxGetApp().preset_bundle->prints.get_edited_preset().config;
    if (model_config.option(key))
        return model_config.opt_float(key);
    else if (auto* opt = process_cfg.option<ConfigOptionFloat>(key, /*create_if_missing=*/false))
        return opt->getFloat();
    else if(auto* opt = process_cfg.option<ConfigOptionInt>(key, /*create_if_missing=*/false))
        return static_cast<double>(opt->value);
    else if(auto* opt = process_cfg.option<ConfigOptionBool>(key, /*create_if_missing=*/false))
        return opt->value ? 1.0 : 0.0;
    return default_value;
}

static bool get_printer_bool_option(const std::string& key, bool default_value = false)
{
    auto& process_cfg = wxGetApp().preset_bundle->prints.get_edited_preset().config;
    if (auto* bopt = process_cfg.option<ConfigOptionBool>(key, /*create_if_missing=*/false))
        return bopt->value;
    return default_value;
}

TriangleMesh SupportMeshCreator::createSupportMesh(const std::string& node_name,
                                                   const std::vector<Vec3f>& node_vertices,
                                                   const std::vector<Vec3i32>& node_indices,
                                                   const ModelConfigObject& model_config)
{
    TriangleMesh empty;
    if (node_vertices.empty() || node_indices.empty())
        return empty;

    // Parameters from preset or defaults.
    const double support_angle_deg         = get_printer_float_option(model_config ,"support_threshold_angle", 45.0);
    const bool   filter_upwards            = get_printer_bool_option("support_mesh_filter_upwards_faces", true);
    const double bottom_cut_off            = get_printer_float_option(model_config ,"support_mesh_bottom_cut_off", 0.0);
    const double minimum_island_area       = get_printer_float_option(model_config ,"support_mesh_minimum_island_area", 5.0);
    const bool   support_on_build_plate_only   = get_printer_bool_option("support_on_build_plate_only", true);
    const double top_gap                   = get_printer_float_option(model_config, "support_top_z_distance", 0.0);

    std::vector<Vec3f>        extended_vertices;
    const std::vector<Vec3f>* mesh_vertices = &node_vertices;

    // Down vector along -Z (use Z as vertical axis).
    const Vec3f  down_vec(0.f, 0.f, -1.f);
    constexpr double deg2rad = 3.14159265358979323846 / 180.0;
    const double cos_support_angle     = std::cos(deg2rad * support_angle_deg);
    const double cos_positive_y_angle  = std::cos(deg2rad * 75.0); // Fixed 10° allowance for special faces.

    // Compute face normals using ITS utility.
    indexed_triangle_set its;
    its.vertices = node_vertices;
    its.indices  = node_indices;
    const std::vector<Vec3f> face_normals = its_face_normals(its);

    std::unique_ptr<AABBMesh> raycast_mesh;
    std::function<bool(int,int,int)> triangle_reaches_build_plate = [](int, int, int){ return true; };
    constexpr double subdivision_target = 2.0; // mm edge length target for grid subdivision.
    if (support_on_build_plate_only) {
        extended_vertices = node_vertices;
        mesh_vertices = &extended_vertices;
        raycast_mesh = std::make_unique<AABBMesh>(its);
        const Vec3d downward_ray(0.0, 0.0, -1.0);
        triangle_reaches_build_plate = [&](int ia, int ib, int ic) {
            constexpr double ray_offset = 1e-4;
            constexpr double build_plate_eps = 1e-4;
            const auto& verts = *mesh_vertices;
            auto point_reaches_build_plate = [&](double px, double py, double sample_z) {
                if (sample_z <= build_plate_eps)
                    return true;
                Vec3d origin(px, py, sample_z - ray_offset);
                auto hit = raycast_mesh->query_ray_hit(origin, downward_ray);
                if (!hit.is_hit())
                    return true;
                const double hit_z = origin.z() + hit.direction().z() * hit.distance();
                return hit_z <= build_plate_eps;
            };
            const Vec3f& a = verts[size_t(ia)];
            const Vec3f& b = verts[size_t(ib)];
            const Vec3f& c = verts[size_t(ic)];
            if (!point_reaches_build_plate(double(a.x()), double(a.y()), double(a.z())))
                return false;
            if (!point_reaches_build_plate(double(b.x()), double(b.y()), double(b.z())))
                return false;
            if (!point_reaches_build_plate(double(c.x()), double(c.y()), double(c.z())))
                return false;
            const double cx = (double(a.x()) + double(b.x()) + double(c.x())) / 3.0;
            const double cy = (double(a.y()) + double(b.y()) + double(c.y())) / 3.0;
            const double cz = (double(a.z()) + double(b.z()) + double(c.z())) / 3.0;
            return point_reaches_build_plate(cx, cy, cz);
        };
    }

    // Select faces needing support.
    std::vector<int> faces_needing_support;
    if (!face_normals.empty()) {
        tbb::enumerable_thread_specific<std::vector<int>> tls_faces;
        tbb::parallel_for(tbb::blocked_range<size_t>(0, face_normals.size(), 512),
            [&](const tbb::blocked_range<size_t>& range) {
                std::vector<int>& local = tls_faces.local();
                const size_t chunk = range.end() - range.begin();
                local.reserve(local.size() + chunk);
                for (size_t idx = range.begin(); idx < range.end(); ++idx) {
                    const Vec3f& n = face_normals[idx];
                    double d = double(n.dot(down_vec));
                    const bool standard_support = d >= cos_support_angle;
                    const bool positive_y_down  = n.y() > 0.f && d > 0.0 && d >= cos_positive_y_angle;
                    if (standard_support || positive_y_down)
                        local.push_back(int(idx));
                }
            });
        size_t total = 0;
        for (const std::vector<int>& local : tls_faces)
            total += local.size();
        faces_needing_support.reserve(total);
        for (std::vector<int>& local : tls_faces)
            faces_needing_support.insert(faces_needing_support.end(), local.begin(), local.end());
    }
    if (faces_needing_support.empty() && filter_upwards) {
        tbb::enumerable_thread_specific<std::vector<int>> tls_down_faces;
        tbb::parallel_for(tbb::blocked_range<size_t>(0, face_normals.size(), 512),
            [&](const tbb::blocked_range<size_t>& range) {
                std::vector<int>& local = tls_down_faces.local();
                const size_t chunk = range.end() - range.begin();
                local.reserve(local.size() + chunk);
                for (size_t idx = range.begin(); idx < range.end(); ++idx)
                    if (face_normals[idx].z() < 0)
                        local.push_back(int(idx));
            });
        std::vector<int> faces_facing_down;
        size_t total_down = 0;
        for (const std::vector<int>& local : tls_down_faces)
            total_down += local.size();
        faces_facing_down.reserve(total_down);
        for (std::vector<int>& local : tls_down_faces)
            faces_facing_down.insert(faces_facing_down.end(), local.begin(), local.end());
        // Intersection (will be empty in this exact port if faces_needing_support is empty).
        std::vector<int> inter;
        std::sort(faces_facing_down.begin(), faces_facing_down.end());
        std::sort(faces_needing_support.begin(), faces_needing_support.end());
        std::set_intersection(faces_facing_down.begin(), faces_facing_down.end(),
                              faces_needing_support.begin(), faces_needing_support.end(),
                              std::back_inserter(inter));
        faces_needing_support.swap(inter);
    }
    if (faces_needing_support.empty())
        return empty;

    // Build initial roof faces from selected faces and filter by bottom cut off.
    std::vector<Vec3i32> roof_faces;
    roof_faces.reserve(faces_needing_support.size());
    for (int fi : faces_needing_support) {
        const Vec3i32& f = node_indices[size_t(fi)];
        const Vec3f v0 = node_vertices[size_t(f[0])];
        const Vec3f v1 = node_vertices[size_t(f[1])];
        const Vec3f v2 = node_vertices[size_t(f[2])];
        if (v0.z() <= bottom_cut_off && v1.z() <= bottom_cut_off && v2.z() <= bottom_cut_off)
            continue;
        if (!support_on_build_plate_only) {
            roof_faces.push_back(f);
            continue;
        }
        const double l01 = (v0 - v1).norm();
        const double l12 = (v1 - v2).norm();
        const double l20 = (v2 - v0).norm();
        const double max_edge = std::max({ l01, l12, l20 });
        const int subdivisions = std::min(4, std::max(1, int(std::ceil(max_edge / subdivision_target))));
        const int grid_size = subdivisions + 1;
        std::vector<int> grid(grid_size * grid_size, -1);
        auto grid_slot = [&](int i, int j) -> int& { return grid[i * grid_size + j]; };
        auto fetch_vertex = [&](int i, int j) -> int {
            int &slot = grid_slot(i, j);
            if (slot >= 0)
                return slot;
            if (i == 0 && j == 0)
                return slot = f[0];
            if (i == subdivisions && j == 0)
                return slot = f[1];
            if (i == 0 && j == subdivisions)
                return slot = f[2];
            int k = subdivisions - i - j;
            if (k < 0)
                return slot = -1;
            const double u = double(i) / subdivisions;
            const double v = double(j) / subdivisions;
            const double w = double(k) / subdivisions;
            Vec3f p = v0 * float(w) + v1 * float(u) + v2 * float(v);
            extended_vertices.push_back(p);
            slot = int(extended_vertices.size()) - 1;
            return slot;
        };
        for (int i = 0; i < subdivisions; ++i) {
            for (int j = 0; j < subdivisions - i; ++j) {
                int a = fetch_vertex(i, j);
                int b = fetch_vertex(i + 1, j);
                int c = fetch_vertex(i, j + 1);
                if (a >= 0 && b >= 0 && c >= 0 && triangle_reaches_build_plate(a, b, c))
                    roof_faces.emplace_back(a, b, c);
                if (i + j < subdivisions - 1) {
                    int d = fetch_vertex(i + 1, j + 1);
                    if (b >= 0 && d >= 0 && c >= 0 && triangle_reaches_build_plate(b, d, c))
                        roof_faces.emplace_back(b, d, c);
                }
            }
        }
    }
    if (roof_faces.empty())
        return empty;

    // Compact vertices referenced by roof_faces.
    std::vector<Vec3f> roof_vertices;
    roof_vertices.reserve(mesh_vertices->size());
    std::unordered_map<int, int> vmap;
    vmap.reserve(mesh_vertices->size());
    auto remap_index = [&](int idx) -> int {
        auto it = vmap.find(idx);
        if (it != vmap.end()) return it->second;
        int newid = int(roof_vertices.size());
        vmap.emplace(idx, newid);
        roof_vertices.push_back((*mesh_vertices)[size_t(idx)]);
        return newid;
    };
    std::vector<Vec3i32> roof_faces_compact;
    roof_faces_compact.reserve(roof_faces.size());
    for (const Vec3i32& f : roof_faces) {
        roof_faces_compact.emplace_back(remap_index(f[0]), remap_index(f[1]), remap_index(f[2]));
    }

    TriangleMesh roof_mesh(roof_vertices, roof_faces_compact);

    // Filter small islands if requested (project Z -> 0 onto XY plane and compute area).
    if (minimum_island_area > 0.0 && roof_mesh.is_splittable()) {
        std::vector<TriangleMesh> parts = roof_mesh.split();
        std::vector<Vec3f> merged_v; std::vector<Vec3i32> merged_f;
        int v_ofs = 0;
        auto projected_area = [](const TriangleMesh& m) {
            double area = 0.0;
            for (const Vec3i32& f : m.its.indices) {
                const Vec3f &a = m.its.vertices[size_t(f[0])];
                const Vec3f &b = m.its.vertices[size_t(f[1])];
                const Vec3f &c = m.its.vertices[size_t(f[2])];
                // Project to XY plane (set z=0)
                const double ax = a.x(), ay = a.y();
                const double bx = b.x(), by = b.y();
                const double cx = c.x(), cy = c.y();
                area += 0.5 * std::abs((bx - ax) * (cy - ay) - (by - ay) * (cx - ax));
            }
            return area;
        };
        bool first = true;
        for (const TriangleMesh& p : parts) {
            if (projected_area(p) >= minimum_island_area) {
                // Append to merged mesh.
                merged_v.insert(merged_v.end(), p.its.vertices.begin(), p.its.vertices.end());
                for (const Vec3i32& f : p.its.indices)
                    merged_f.emplace_back(f[0] + v_ofs, f[1] + v_ofs, f[2] + v_ofs);
                v_ofs += int(p.its.vertices.size());
                first = false;
            }
        }
        roof_mesh = TriangleMesh(std::move(merged_v), std::move(merged_f));
    }

    if (roof_mesh.its.vertices.empty())
        return empty;

    // Apply tilt-offset (45° in YZ plane). Ground points keep Z and move only in Y, and Y is clamped to original max.
    if (top_gap > 0.0) {
        const float min_z = static_cast<float>(bottom_cut_off);
        const Vec3f n(0.f, -std::sqrt(0.5f), std::sqrt(0.5f)); // normalized (0,-1,1) to flip Y
        const float gap = static_cast<float>(top_gap);
        float max_y_orig = -std::numeric_limits<float>::infinity();
        for (const Vec3f& v : roof_mesh.its.vertices)
            max_y_orig = std::max(max_y_orig, v.y());
        for (Vec3f& v : roof_mesh.its.vertices) {
            float orig_y = v.y();
            if (v.z() <= min_z + 1e-6f) {
                // On ground: shift only Y to keep Z on bed.
                v.y() = std::min(max_y_orig, orig_y + gap);
                continue;
            }
            float max_shift = (n.z() > 1e-6f) ? std::max(0.f, (v.z() - min_z) / n.z()) : gap;
            float shift = std::min(gap, max_shift);
            v -= n * shift;
            v.z() = std::max(min_z, v.z());
            v.y() = std::min(max_y_orig, v.y());
        }
    }

    const int num_roof_vertices = int(roof_mesh.its.vertices.size());
    // Build boundary edges of the roof mesh.
    struct Edge { int a, b; };
    auto norm_edge = [](int u, int v){ return Edge{ std::min(u,v), std::max(u,v) }; };
    struct EdgeHash { size_t operator()(const Edge& e) const { return (size_t(e.a) << 32) ^ size_t(uint32_t(e.b)); } };
    struct EdgeEq { bool operator()(const Edge& l, const Edge& r) const { return l.a==r.a && l.b==r.b; } };
    std::unordered_map<Edge,int,EdgeHash,EdgeEq> edge_count;
    std::vector<std::pair<int,int>> boundary_edges;
    for (const Vec3i32& f : roof_mesh.its.indices) {
        int a=f[0], b=f[1], c=f[2];
        Edge es[3] = { norm_edge(a,b), norm_edge(b,c), norm_edge(c,a) };
        for (const Edge& e : es) edge_count[e]++;
    }
    for (const auto& kv : edge_count) if (kv.second == 1) boundary_edges.emplace_back(kv.first.a, kv.first.b);
    // Build adjacency from boundary edges.
    std::unordered_multimap<int,int> adj;
    for (auto& pr : boundary_edges) { adj.emplace(pr.first, pr.second); adj.emplace(pr.second, pr.first); }
    std::unordered_set<uint64_t> used;
    auto key_edge = [](int u,int v)->uint64_t{ return (uint64_t(uint32_t(u))<<32) ^ uint32_t(v); };
    auto mark_edge = [&](int u, int v) {
        used.insert(key_edge(u, v));
        used.insert(key_edge(v, u));
    };
    auto is_marked = [&](int u, int v) {
        return used.count(key_edge(u, v)) != 0;
    };

    // Construct connecting faces along polylines.
    std::vector<Vec3i32> connecting_faces;
    connecting_faces.reserve(boundary_edges.size()*2);
    for (auto& pr : boundary_edges) {
        int start = pr.first, next = pr.second;
        if (is_marked(start, next))
            continue;
        int prev = -1;
        int u = start, v = next;
        while (true) {
            if (is_marked(u, v))
                break;
            mark_edge(u, v);
            // Connect (u -> v)
            connecting_faces.emplace_back(u, v + num_roof_vertices, u + num_roof_vertices);
            connecting_faces.emplace_back(u, v, v + num_roof_vertices);
            // Find next neighbor of v not equal to u.
            int w = -1;
            auto range = adj.equal_range(v);
            for (auto it = range.first; it != range.second; ++it) if (it->second != u) { w = it->second; break; }
            if (w < 0)
                break; // open polyline end
            u = v; v = w;
            if (u == start)
                break; // closed loop
            if (is_marked(u, v))
                break;
        }
    }

    // Compose final support mesh: top roof, bottom copy at z=0 (XY plane), and side walls.
    std::vector<Vec3f> support_vertices;
    support_vertices.reserve(size_t(num_roof_vertices) * 2);
    support_vertices.insert(support_vertices.end(), roof_mesh.its.vertices.begin(), roof_mesh.its.vertices.end());
    for (const Vec3f& p : roof_mesh.its.vertices)
        support_vertices.emplace_back(p.x(), p.y(), 0.f);

    std::vector<Vec3i32> support_faces;
    support_faces.reserve(roof_mesh.its.indices.size()*2 + connecting_faces.size());
    support_faces.insert(support_faces.end(), roof_mesh.its.indices.begin(), roof_mesh.its.indices.end());
    for (const Vec3i32& f : roof_mesh.its.indices)
        support_faces.emplace_back(f[0] + num_roof_vertices, f[1] + num_roof_vertices, f[2] + num_roof_vertices);
    support_faces.insert(support_faces.end(), connecting_faces.begin(), connecting_faces.end());

    TriangleMesh support_mesh(std::move(support_vertices), std::move(support_faces));
    return support_mesh;
}

void SupportMeshCreator::createSupportMeshForNode(ModelObject* node)
{
    if (node == nullptr)
        return;

    ModelObject* src_obj = node;
    Model* model = src_obj->get_model();
    if (model == nullptr)
        return;

    // Work on a snapshot of the current volumes to avoid modifying the container
    // we're iterating (new support volumes are appended later in this function).
    const ModelVolumePtrs source_volumes = src_obj->volumes;

    int inst_idx = 0;
    for (ModelInstance* inst : src_obj->instances) {
        if (!inst)
            continue;
        const Transform3d& T_inst = inst->get_matrix();
        size_t vol_idx = 0;
        for (ModelVolume* vol_src : source_volumes) {
            if (!vol_src) { ++vol_idx; continue; }
            // Only generate supports for model parts.
            if (vol_src->type() != ModelVolumeType::MODEL_PART) { ++vol_idx; continue; }
            const TriangleMesh& vol_mesh = vol_src->mesh();
            if (vol_mesh.its.vertices.empty() || vol_mesh.its.indices.empty()) { ++vol_idx; continue; }

            const Transform3d T_vol = vol_src->get_transformation().get_matrix();
            const Transform3d T_world = T_inst * T_vol;
            std::vector<Vec3f> verts_world; verts_world.reserve(vol_mesh.its.vertices.size());
            for (const Vec3f& v : vol_mesh.its.vertices) {
                Vec3d p = T_world * Vec3d(double(v.x()), double(v.y()), double(v.z()));
                verts_world.emplace_back(float(p.x()), float(p.y()), float(p.z()));
            }


            TriangleMesh support_world = createSupportMesh(vol_src->name, verts_world, vol_mesh.its.indices, node->config);
            support_world.repair();
            if (support_world.its.vertices.empty()) { ++vol_idx; continue; }

            // Transform back to object-local and add SUPPORT_ENFORCER volume.
            support_world.transform(T_inst.inverse());
            its_remove_degenerate_faces(support_world.its, /*shrink_to_fit=*/true);
            its_merge_vertices(support_world.its, /*shrink_to_fit=*/true);
            its_compactify_vertices(support_world.its, /*shrink_to_fit=*/true);

            ModelVolume* vol = src_obj->add_volume(std::move(support_world), ModelVolumeType::SUPPORT_ENFORCER);
            if (vol)
                vol->name = vol_src->name + std::string("_support");
            ++vol_idx;
        }
        ++inst_idx;
    }
}



}} // namespace Slic3r::GUI
