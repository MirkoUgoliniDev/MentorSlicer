#include "PrintAdvisorInternal.hpp"

#include "slic3r/GUI/GUI_App.hpp"
#include "slic3r/GUI/Plater.hpp"
#include "slic3r/GUI/PartPlate.hpp"
#include "slic3r/GUI/print_manage/App/DeviceTelemetryCache.h"
#include "slic3r/GUI/simple/bridge/SlicerBridge.hpp"
#include "libslic3r/Model.hpp"
#include "libslic3r/PresetBundle.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <stdexcept>
#include <string>
#include <vector>

// Plate type (curr_bed_type) of the project and of its plates: read it, change it after a
// confirmation, and the plate/material pre-print findings used by /v1/preflight.
//
// The plate type is a project setting, not a preset: the project default is
// PresetBundle::project_config["curr_bed_type"] (the sidebar combo box) and each plate may
// override it in its own config (Plate Settings dialog). The change goes through the same
// Plater entry points as the GUI, so the sidebar, the 3D view and the slice state follow.

namespace Slic3r {
namespace GUI {
namespace advisor {

namespace {

using json = nlohmann::json;

// API name of a plate type: the G-code string of the slicer, plus the two it has no name for.
std::string plate_type_name(BedType type)
{
    switch (type) {
    case btDefault: return "default";
    case btDEF: return "customized_plate";
    default: return bed_type_to_gcode_string(type);
    }
}

BedType plate_type_from_name(const std::string& name)
{
    for (int value = int(btDefault); value < int(btCount); ++value)
        if (plate_type_name(BedType(value)) == name)
            return BedType(value);
    throw std::runtime_error("bed_type must be one of: cool_plate, eng_plate, hot_plate, textured_plate, customized_plate, "
                             "epoxy_resin_plate (or default, for a plate: follow the project)");
}

// Label of the GUI ("Smooth PEI Plate / High Temp Plate") and enum value ("High Temp Plate") of
// a plate type, from the option definition. btDefault has neither.
const ConfigOptionDef* plate_type_def() { return print_config_def.get("curr_bed_type"); }

std::string plate_type_label(BedType type)
{
    if (type == btDefault)
        return "same as project";
    const ConfigOptionDef* def = plate_type_def();
    const size_t index = size_t(int(type) - 1);
    if (def && index < def->enum_labels.size())
        return def->enum_labels[index];
    return plate_type_name(type);
}

std::string plate_type_enum_value(BedType type)
{
    const ConfigOptionDef* def = plate_type_def();
    const size_t index = size_t(int(type) - 1);
    if (!def || type == btDefault || index >= def->enum_values.size())
        throw std::runtime_error("Unknown plate type");
    return def->enum_values[index];
}

// Plate types offered by the sidebar for the vendor of the selected printer: the same filter as
// SidebarPrinter::update_bed_type (Creality printers: no Cool/Engineering plate; others: no
// Epoxy Resin plate). Other values would leave the sidebar combo box out of sync.
std::vector<BedType> selectable_plate_types()
{
    const bool creality = wxGetApp().preset_bundle && wxGetApp().preset_bundle->is_cx_vendor();
    std::vector<BedType> types;
    for (int value = int(btPC); value < int(btCount); ++value) {
        const BedType type = BedType(value);
        if (creality && (type == btPC || type == btEP))
            continue;
        if (!creality && type == btER)
            continue;
        types.push_back(type);
    }
    return types;
}

// First-extruder value of an int/float vector option, or -1.
double first_number(const DynamicPrintConfig& config, const std::string& key)
{
    if (key.empty())
        return -1;
    if (const auto* ints = config.option<ConfigOptionInts>(key))
        return ints->values.empty() ? -1 : ints->values.front();
    if (const auto* floats = config.option<ConfigOptionFloats>(key))
        return floats->values.empty() ? -1 : floats->values.front();
    return -1;
}

// Bed temperatures the active filament uses on a plate type (its *_plate_temp keys).
json filament_temps_for(const DynamicPrintConfig& filament, BedType type)
{
    const std::string key = get_bed_temp_key(type);
    const std::string first_key = get_bed_temp_1st_layer_key(type);
    json temps = {{"temp_key", key}, {"initial_layer_temp_key", first_key}};
    temps["bed_temp"] = key.empty() ? json() : json(first_number(filament, key));
    temps["bed_temp_initial_layer"] = first_key.empty() ? json() : json(first_number(filament, first_key));
    return temps;
}

std::string temps_text(const DynamicPrintConfig& filament, BedType type)
{
    const double bed = first_number(filament, get_bed_temp_key(type));
    const double first = first_number(filament, get_bed_temp_1st_layer_key(type));
    if (bed < 0 && first < 0)
        return "?";
    return std::to_string(int(std::lround(first))) + " °C first layer, then " + std::to_string(int(std::lround(bed))) + " °C";
}

BedType project_plate_type()
{
    const DynamicPrintConfig& project = wxGetApp().preset_bundle->project_config;
    return project.has("curr_bed_type") ? project.opt_enum<BedType>("curr_bed_type") : btDefault;
}

// Project default, per-plate overrides, effective type of every plate and the temperatures the
// active filament uses on each selectable type.
json plate_type_state()
{
    Plater* plater = wxGetApp().plater();
    auto* bundle = wxGetApp().preset_bundle;
    if (!plater || !bundle)
        throw std::runtime_error("MentorSlicer is not ready");
    const DynamicPrintConfig& filament = bundle->filaments.get_edited_preset().config;
    PartPlateList& plates = plater->get_partplate_list();
    const BedType project_type = project_plate_type();
    json list = json::array();
    for (int index = 0; index < plates.get_plate_count(); ++index) {
        PartPlate* plate = plates.get_plate(index);
        if (!plate)
            continue;
        const BedType override_type = plate->get_bed_type(false);
        const BedType effective = plate->get_bed_type(true);
        list.push_back({{"plate_number", index + 1}, {"name", plate->get_plate_name()},
                        {"override", override_type == btDefault ? json() : json(plate_type_name(override_type))},
                        {"effective", plate_type_name(effective)}, {"effective_label", plate_type_label(effective)},
                        {"filament_temps", filament_temps_for(filament, effective)}});
    }
    json selectable = json::array();
    for (const BedType type : selectable_plate_types())
        selectable.push_back({{"bed_type", plate_type_name(type)}, {"label", plate_type_label(type)},
                              {"filament_temps", filament_temps_for(filament, type)}});
    return {{"success", true}, {"project_default", plate_type_name(project_type)},
            {"project_default_label", plate_type_label(project_type)},
            {"current_plate_number", plates.get_curr_plate_index() + 1}, {"plates", list},
            {"selectable", selectable}, {"filament", bundle->filaments.get_edited_preset().name},
            {"note", "effective = plate override when set, else the project default (the sidebar combo box)"}};
}

bool slicing_busy(Plater& plater)
{
    return plater.is_background_process_slicing() || plater.is_background_process_update_scheduled() ||
           plater.is_export_gcode_scheduled();
}

json get_plate_type(const json&, const std::atomic<bool>&) { return plate_type_state(); }

// Changes the plate type of the project (scope "project": sidebar combo box, used by every plate
// without an override) or of one plate (scope "plate": override, "default" removes it), after a
// confirmation that shows the bed temperatures of the active filament for the new type.
json set_plate_type(const json& body, const std::atomic<bool>& stopping)
{
    Plater* plater = wxGetApp().plater();
    auto* bundle = wxGetApp().preset_bundle;
    if (!plater || !bundle)
        throw std::runtime_error("MentorSlicer is not ready");
    if (!body.is_object() || !body.contains("bed_type") || !body["bed_type"].is_string())
        throw std::runtime_error("bed_type must be a string");
    const BedType requested = plate_type_from_name(body["bed_type"].get<std::string>());
    PartPlateList& plates = plater->get_partplate_list();
    const int plate_count = plates.get_plate_count();

    int plate_index = plates.get_curr_plate_index();
    const bool plate_given = body.contains("plate_number") && !body["plate_number"].is_null();
    if (plate_given) {
        const double number = json_number(body["plate_number"]);
        if (number < 1 || number > plate_count || number != std::floor(number))
            throw std::runtime_error("plate_number must be between 1 and " + std::to_string(plate_count));
        plate_index = int(number) - 1;
    }
    // Default scope: the project when it has one plate and no plate was named (the sidebar then
    // shows the type in use), otherwise the named or current plate.
    std::string scope = body.value("scope", std::string());
    if (scope.empty())
        scope = (!plate_given && plate_count == 1) ? "project" : "plate";
    if (scope != "project" && scope != "plate")
        throw std::runtime_error("scope must be project or plate");
    if (scope == "project" && requested == btDefault)
        throw std::runtime_error("The project default cannot be \"default\": choose a plate type");
    if (requested != btDefault) {
        const std::vector<BedType> selectable = selectable_plate_types();
        if (std::find(selectable.begin(), selectable.end(), requested) == selectable.end()) {
            std::string names;
            for (const BedType type : selectable)
                names += (names.empty() ? "" : ", ") + plate_type_name(type);
            throw std::runtime_error("This printer offers only: " + names);
        }
    }
    if (slicing_busy(*plater))
        throw std::runtime_error("Slicing or export in progress: wait for it to finish and retry");

    PartPlate* plate = plates.get_plate(plate_index);
    if (!plate)
        throw std::runtime_error("Plate not found");
    // Identity of the plate, checked after the dialog: a plate deleted and recreated meanwhile
    // can reuse the same address, never the same object id.
    const ObjectID plate_id = plate->id();
    const DynamicPrintConfig& filament = bundle->filaments.get_edited_preset().config;
    const std::string filament_name = sanitized_text(bundle->filaments.get_edited_preset().name);
    const BedType project_before = project_plate_type();
    const BedType override_before = plate->get_bed_type(false);
    const BedType effective_before = plate->get_bed_type(true);
    // Effective type of the plate after the change (for the temperatures shown).
    BedType effective_after = effective_before;
    if (scope == "project")
        effective_after = override_before == btDefault ? requested : override_before;
    else
        effective_after = requested == btDefault ? project_before : requested;

    const std::string where = scope == "project" ? "Plate type (project)"
                                                 : "Plate type (plate " + std::to_string(plate_index + 1) + ")";
    std::vector<AdvisorChangeRow> rows;
    rows.push_back({where, plate_type_label(scope == "project" ? project_before : override_before), plate_type_label(requested)});
    if (scope == "project" && override_before != btDefault)
        rows.push_back({"Plate " + std::to_string(plate_index + 1), "has its own type: " + plate_type_label(override_before), "unchanged"});
    rows.push_back({"Bed temperature (" + filament_name + ")", temps_text(filament, effective_before), temps_text(filament, effective_after)});
    const std::string title = "Plate type";
    std::string question = scope == "project"
        ? "Set the plate type of the project to \"" + plate_type_label(requested) + "\"?\n\nIt applies to every plate without its own type; "
          "the affected plates must be sliced again."
        : (requested == btDefault
               ? "Remove the own plate type of plate " + std::to_string(plate_index + 1) + " (it will follow the project: \"" +
                     plate_type_label(project_before) + "\")?"
               : "Set the plate type of plate " + std::to_string(plate_index + 1) + " to \"" + plate_type_label(requested) +
                     "\"?\n\nThe plate must be sliced again.");
    if (const std::string reason = reason_text(body.value("reason", json())); !reason.empty())
        question += "\n\n" + reason;
    if (!ask_user(title, question, stopping, rows))
        return {{"success", false}, {"cancelled", true}};

    // Stale-context checks after the dialog: the project may have changed meanwhile. The old
    // plate pointer is not dereferenced before the plate is found again by index and identity.
    if (wxGetApp().plater() != plater || !wxGetApp().preset_bundle)
        throw std::runtime_error("MentorSlicer is not ready");
    if (plates.get_plate_count() != plate_count)
        throw std::runtime_error("The plates changed while the dialog was open: nothing was changed");
    plate = plates.get_plate(plate_index);
    if (!plate || plate->id() != plate_id)
        throw std::runtime_error("The plates changed while the dialog was open: nothing was changed");
    if (project_plate_type() != project_before || plate->get_bed_type(false) != override_before)
        throw std::runtime_error("The plate type changed while the dialog was open: nothing was changed");
    if (slicing_busy(*plater))
        throw std::runtime_error("Slicing or export started meanwhile: nothing was changed");

    if (scope == "project") {
        // Same path as the sidebar combo box (Plater::priv::on_select_bed_type): project config,
        // app config, slice state of the plates without override, 3D view; then the combo boxes.
        plater->select_bed_type(plate_type_enum_value(requested));
        plater->on_bed_type_change(requested);
        plater->sidebar().update_filament_panel();
    } else {
        // Same path as the Plate Settings dialog (Plater::open_platesettings_dialog).
        plate->set_bed_type(requested);
        plater->update_project_dirty_from_presets();
        plater->set_plater_dirty(true);
        plater->update();
    }

    const BedType project_after = project_plate_type();
    const BedType override_after = plate->get_bed_type(false);
    const bool verified = scope == "project" ? project_after == requested : override_after == requested;
    if (!verified)
        throw std::runtime_error("The plate type does not appear changed: check in MentorSlicer");
    append_history({{"action", "set_bed_type"}, {"scope", scope}, {"plate_number", plate_index + 1},
                    {"before", plate_type_name(scope == "project" ? project_before : override_before)},
                    {"after", plate_type_name(requested)}, {"reason", sanitized_reason(body.value("reason", json()))}});
    json result = plate_type_state();
    result["scope"] = scope;
    result["plate_number"] = plate_index + 1;
    result["before"] = plate_type_name(scope == "project" ? project_before : override_before);
    result["after"] = plate_type_name(requested);
    result["effective_filament_temps"] = filament_temps_for(filament, plate->get_bed_type(true));
    result["note"] = "Slice the plate again before sending";
    return result;
}

} // namespace

std::vector<AdvisorRoute> plate_routes()
{
    return {{"/v1/bed_type", "GET", true, get_plate_type}, {"/v1/bed_type", "POST", true, set_plate_type}};
}

// Advisory pre-print findings for ASA/ABS on the current plate: glue on smooth PEI for large
// parts, and a cold chamber on an idle printer. Never blocking (warning or info).
// `telemetry_stale_reported`: the caller already emitted printer_data_stale, so chamber_unknown
// is not repeated for the same cause.
std::vector<nlohmann::json> plate_material_findings(const std::string& material, bool asa_abs, BedType bed_type,
                                                    const nlohmann::json& telemetry, bool telemetry_stale_reported)
{
    std::vector<json> findings;
    if (!asa_abs)
        return findings;
    Plater* plater = wxGetApp().plater();
    if (!plater)
        return findings;
    const auto add = [&findings](const char* severity, const std::string& code, const std::string& message, json data) {
        findings.push_back({{"severity", severity}, {"code", code}, {"message", message}, {"data", std::move(data)}});
    };

    // Largest object on the current plate (XY side) and its contact with the bed.
    PartPlate* plate = plater->get_partplate_list().get_curr_plate();
    const Model& model = plater->model();
    double max_side = 0;
    const ModelObject* largest = nullptr;
    size_t largest_instance = 0;
    for (size_t index = 0; index < model.objects.size(); ++index) {
        const ModelObject* object = model.objects[index];
        if (!object)
            continue;
        for (size_t instance = 0; instance < object->instances.size(); ++instance) {
            if (plate && !plate->contain_instance(int(index), int(instance)))
                continue;
            const Vec3d size = object->instance_bounding_box(instance).size();
            const double side = std::max(size.x(), size.y());
            if (side > max_side) {
                max_side = side;
                largest = object;
                largest_instance = instance;
            }
        }
    }
    if (bed_type == btPEI && largest && max_side > 80) {
        // AnalyzeOverhang takes a ModelObject and always uses the transform of its first instance:
        // the contact figures are exact for the largest instance only when it is instance 0
        // (instances share the mesh; they differ only when scaled differently).
        const Bridge::OverhangResult overhang = Bridge::SlicerBridge::AnalyzeOverhang(*largest, 45.0);
        add("warning", "glue_on_smooth_pei_large_part",
            material + " on smooth PEI with a " + std::to_string(int(std::lround(max_side))) +
                " mm part: apply glue stick on the plate before printing. On the K1C a 130 mm ASA part detached at 57% "
                "without glue and printed well with glue (chamber at 33-37 °C).",
            {{"largest_object", sanitized_text(largest->name)}, {"largest_instance", largest_instance}, {"max_xy_side_mm", max_side},
             {"bed_contact_area_mm2", overhang.bottom_contact_area_mm2}, {"bed_contact_ratio", overhang.bottom_contact_ratio},
             {"bed_contact_note", largest_instance == 0 ? json(nullptr) : json("contact figures computed on instance 0 of the object")},
             {"plate_type", plate_type_name(bed_type)}, {"advice", "glue_stick"}});
    }

    // Chamber of the selected printer, only from live telemetry (Device tab open).
    // Age from the monotonic clock: a wall-clock jump must not make old data look fresh.
    const std::string address = target_device_address();
    const long long   age     = address.empty() ? -1 : DeviceTelemetryCache::instance().age_ms(address);
    const bool stale = !telemetry.is_object() || age < 0 || age > 120000;
    if (stale || !telemetry.contains("boxTemp")) {
        // printer_data_stale (preflight) already covers the stale case.
        if (!stale || !telemetry_stale_reported)
            add("info", "chamber_unknown",
                "chamber temperature unknown (" + std::string(stale ? "no recent telemetry" : "no chamber sensor reading") +
                    "): open the Device tab and check the chamber before sending " + material + "; below 30 °C use preheat_chamber",
                {{"reason", !telemetry.is_object() || age < 0 ? "no_telemetry" : (stale ? "stale" : "no_chamber_sensor")}});
        return findings;
    }
    const double chamber = json_number(telemetry["boxTemp"]);
    const int state = telemetry_state(telemetry);
    const bool idle = state == 0 || state == 2 || state == 3 || state == 4;
    if (chamber >= 0 && chamber < 30 && idle) {
        // Same soak figures as cold_room/cool_room in the preflight (PrintAdvisorServer.cpp):
        // below 20 °C at least 30 min, 20-30 °C 15 min. K1C measurements (CLAUDE.md): warm
        // room, bed only: chamber 35-37 °C in 5-6 min; cold room (16 °C): 30 °C after 23 min,
        // 32 °C after 35 min, then flat.
        const int soak_minutes = chamber < 20 ? 30 : 15;
        const std::string preheat = chamber < 20 ? "25-35 min" : "10-15 min";
        add("warning", "chamber_cold_preheat",
            "live chamber reading " + std::to_string(int(std::lround(chamber))) + " °C with the printer idle: before sending " + material +
                " warm the chamber with preheat_chamber (about " + preheat + " on the K1C, lid closed) and use a heat soak of at least " +
                std::to_string(soak_minutes) + " min (heat_soak_time). Estimate from the K1C measurements.",
            {{"chamber_c", chamber}, {"chamber_source", "live chamber sensor"}, {"threshold_c", 30}, {"printer_state", state_name(state)},
             {"suggested_tool", "preheat_chamber"}, {"suggested_preheat", preheat}, {"suggested_soak_minutes", soak_minutes}});
    }
    return findings;
}

} // namespace advisor
} // namespace GUI
} // namespace Slic3r
