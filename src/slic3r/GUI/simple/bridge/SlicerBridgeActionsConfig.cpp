#include "SlicerBridge.hpp"

#include "slic3r/GUI/GUI_App.hpp"
#include "slic3r/GUI/MainFrame.hpp"
#include "slic3r/GUI/Plater.hpp"
#include "slic3r/GUI/PartPlate.hpp"
#include "slic3r/GUI/Tab.hpp"
#include "slic3r/GUI/NotificationManager.hpp"
#include "slic3r/GUI/ParamsPanel.hpp"
#include "libslic3r/PresetBundle.hpp"
#include "libslic3r/PrintConfig.hpp"
#include "libslic3r/Utils.hpp"
#include "slic3r/GUI/format.hpp"

#include <boost/log/trivial.hpp>
#include <algorithm>
#include <cctype>
#include <cmath>
#include <fstream>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <unordered_set>
#include <unordered_map>

using json = nlohmann::json;

namespace Slic3r {
namespace GUI {
namespace Bridge {

json SlicerBridge::DoGetPresets(const json& /*params*/)
{
    auto* bundle = wxGetApp().preset_bundle;
    if (!bundle)
        return {{"success", false}, {"message", "PresetBundle not available"}};

    json result;
    result["success"] = true;
    result["message"] = "OK";

    auto serialize_collection = [](const PresetCollection& col) -> json {
        json arr = json::array();
        for (auto it = col.begin(); it != col.end(); ++it) {
            arr.push_back({
                {"name", it->name},
                {"is_visible", it->is_visible},
                {"is_default", it->is_default},
                {"is_system", it->is_system}
            });
        }
        return arr;
    };

    result["print_presets"]    = serialize_collection(bundle->prints);
    result["current_print"]    = bundle->prints.get_edited_preset().name;
    result["filament_presets"] = serialize_collection(bundle->filaments);
    result["current_filament"] = bundle->filaments.get_edited_preset().name;
    result["printer_presets"]  = serialize_collection(bundle->printers);
    result["current_printer"]  = bundle->printers.get_edited_preset().name;

    return result;
}

// A small summary of the edited profiles. Values come from the selected
// profiles only (never from the overrides of the object selected in the 3D
// view, which analyze_model reports per object); every writable value is in
// editable_profile_values of /v1/context.
json SlicerBridge::DoGetEditedConfig(const json& /*params*/)
{
    auto* bundle = wxGetApp().preset_bundle;
    if (!bundle)
        return {{"success", false}, {"message", "PresetBundle not available"}};

    json result;
    result["success"] = true;
    result["message"] = "OK";

    const auto& print_cfg    = bundle->prints.get_edited_preset().config;
    const auto& filament_cfg = bundle->filaments.get_edited_preset().config;
    const auto& printer_cfg  = bundle->printers.get_edited_preset().config;

    // Vector (per-extruder) options report their first non-nil value.
    auto serialize_current_value = [](const ConfigOption* opt) -> std::string {
        if (!opt)
            return "";
        if (const auto* vector_opt = dynamic_cast<const ConfigOptionVectorBase*>(opt)) {
            const auto values = vector_opt->vserialize();
            for (size_t index = 0; index < values.size(); ++index) {
                if (!vector_opt->is_nil(index) && !values[index].empty())
                    return values[index];
            }
            return "";
        }
        return opt->serialize();
    };

    auto get_filament_opt = [&](const DynamicPrintConfig& cfg, const std::string& key) -> std::string {
        const ConfigOption* opt = cfg.option(key);
        return opt ? opt->serialize() : "";
    };

    // The process settings most often needed for a diagnosis.
    static const char* const print_keys[] = {
        "layer_height", "initial_layer_print_height", "wall_loops", "top_shell_layers", "bottom_shell_layers",
        "sparse_infill_density", "sparse_infill_pattern", "outer_wall_speed", "inner_wall_speed", "sparse_infill_speed",
        "initial_layer_speed", "bridge_speed", "travel_speed", "default_acceleration", "enable_support", "support_type",
        "brim_type", "brim_width", "brim_layers", "brim_object_gap", "skirt_height", "draft_shield", "raft_layers",
        "seam_position", "ironing_type"
    };
    for (const char* key : print_keys) {
        const std::string v = serialize_current_value(print_cfg.option(key));
        if (!v.empty()) result["print"][key] = v;
    }

    const std::vector<std::string> filament_keys = {
        "nozzle_temperature", "nozzle_temperature_initial_layer",
        "bed_temperature", "bed_temperature_initial_layer",
        "filament_flow_ratio", "fan_min_speed", "fan_max_speed"
    };
    for (const auto& k : filament_keys) {
        const std::string v = get_filament_opt(filament_cfg, k);
        if (!v.empty()) result["filament"][k] = v;
    }

    result["printer"]["printer_model"]  = get_filament_opt(printer_cfg, "printer_model");
    result["printer"]["nozzle_diameter"] = get_filament_opt(printer_cfg, "nozzle_diameter");

    return result;
}

json SlicerBridge::BuildPanelNameMap()
{
    // key -> { group_en, group_loc, line_loc }
    //
    // Translation note: optgroup titles are created with the L(...) macro, which
    // is only a *marker* for xgettext and returns the ENGLISH original at runtime;
    // the actual translation happens at display time. So og->title holds English
    // and must be translated here via _u8L. Line labels, by contrast, are built
    // with Line(label) → label(_(label)), i.e. already translated — do not
    // re-translate them.
    json map = json::object();

    auto harvest_tab = [&](Preset::Type type) {
        Tab* tab = wxGetApp().get_tab(type);
        if (!tab) return;
        for (const auto& page : tab->get_pages()) {
            if (!page) continue;
            for (const auto& og : page->m_optgroups) {
                if (!og) continue;
                const std::string group_en  = og->title.utf8_string();
                const std::string group_loc = _u8L(og->title);
                for (const Line& line : og->get_lines()) {
                    if (line.is_separator()) continue;
                    const auto& opts = line.get_options();
                    // A line label only adds meaning when the line bundles
                    // multiple options (otherwise it duplicates the option name).
                    const bool line_adds_context = opts.size() > 1 && !line.label.IsEmpty();
                    for (const auto& opt : opts) {
                        const std::string& k = opt.opt_id;
                        if (k.empty() || map.contains(k)) continue;
                        json entry;
                        entry["group_en"]  = group_en;
                        entry["group_loc"] = group_loc;
                        if (line_adds_context)
                            entry["line_loc"] = line.label.utf8_string();
                        map[k] = std::move(entry);
                    }
                }
            }
        }
    };
    harvest_tab(Preset::TYPE_PRINT);
    harvest_tab(Preset::TYPE_FILAMENT);
    harvest_tab(Preset::TYPE_PRINTER);
    return map;
}

json SlicerBridge::BuildConfigSchemaArray()
{
    auto type_to_string = [](ConfigOptionType t) -> std::string {
        switch (t) {
            case coFloat:
            case coFloats:          return "float";
            case coInt:
            case coInts:            return "int";
            case coBool:
            case coBools:           return "bool";
            case coString:
            case coStrings:         return "string";
            case coPercent:
            case coPercents:        return "percent";
            case coFloatOrPercent:
            case coFloatsOrPercents:return "float_or_percent";
            case coEnum:
            case coEnums:           return "enum";
            case coPoint:
            case coPoints:          return "point";
            default:                return "other";
        }
    };

    // Authoritative parameter classification (bug 17390): the AI assistant may
    // only modify *process* parameters. Filament/printer parameters are exported
    // with param_class so the agent can reject AI edits to them (and steer the
    // user to the professional mode) instead of mis-mapping them onto the closest
    // process key. The classification sets come straight from the preset schema —
    // no hand-maintained tables.
    auto build_key_set = [](const std::vector<std::string>& keys) {
        return std::unordered_set<std::string>(keys.begin(), keys.end());
    };
    const std::unordered_set<std::string> filament_keys = build_key_set(Preset::filament_options());
    const std::unordered_set<std::string> printer_keys  = build_key_set(Preset::printer_options());
    auto classify_param = [&](const std::string& key) -> std::string {
        if (filament_keys.count(key)) return "filament";
        if (printer_keys.count(key))  return "printer";
        return "process";
    };

    // Build an authoritative display-name map from the professional-panel layout.
    //
    // A parameter's user-facing name lives in the GUI layout (Tab → Page →
    // OptionsGroup → Line), NOT in ConfigOptionDef. E.g. bridge_speed's def.label
    // is just "External"; the meaningful name comes from its optgroup ("桥接" /
    // Bridge) and, when a line groups several options, the per-option label.
    //
    // Recover each parameter's real user-facing group/line name (e.g. "线宽",
    // "桥接") from the professional-panel layout — this lives in the GUI layout,
    // not in ConfigOptionDef.
    const json panel_names = BuildPanelNameMap();

    json options_arr = json::array();

    for (const auto& [key, def] : print_config_def.options) {
        // Skip only genuinely non-editable vector/coordinate types (points).
        // NOTE: per-extruder scalars (e.g. pressure_advance, nozzle_temperature,
        // nozzle_diameter) are stored as ConfigOptionFloats/Ints vectors and would
        // be dropped by a blanket !is_scalar() filter — yet the agent must know them
        // to reject AI edits. Keep them; exclude only point/points containers.
        if (def.type == coPoint || def.type == coPoints) continue;
        if (def.label.empty()) continue;
        // Skip SLA/internal params whose label or category is whitespace-only
        {
            auto is_blank = [](const std::string& s) {
                return s.find_first_not_of(" \t\r\n") == std::string::npos;
            };
            if (is_blank(def.label)) continue;
        }
        // Only expose FFF (or technology-agnostic) parameters — skip SLA-only params
        if (def.printer_technology == ptSLA) continue;
        // Skip hidden/expert-only options that are not user-facing
        if (def.mode == comDevelop) continue;

        json item;
        item["key"]      = key;

        // Build a disambiguated "<group> / <leaf>" name from the professional
        // panel — exactly what the user sees and says ("线宽 / 外墙",
        // "桥接 / 外部"). English and localized are built symmetrically so they
        // never mix languages.
        //   panel group + line  → group + line label   (multi-option line)
        //   panel group + def   → group + this option's own label
        //   no panel entry      → category + def label  → def label (fallback)
        const std::string def_leaf_en  =
            !def.full_label.empty() ? def.full_label : def.label;
        const std::string def_leaf_loc = _u8L(def.label.c_str());

        auto compose = [](const std::string& group, const std::string& leaf) -> std::string {
            if (group.empty()) return leaf;
            if (leaf.empty() || leaf == group) return group;
            return group + " / " + leaf;
        };

        // Join non-empty segments with " / ", skipping duplicates/empties.
        auto join_path = [](std::initializer_list<std::string> segs) -> std::string {
            std::string out;
            for (const std::string& s : segs) {
                if (s.empty()) continue;
                if (!out.empty()) {
                    // Avoid immediate duplicate segments (e.g. group == line).
                    const std::string tail = " / " + s;
                    if (out == s || (out.size() >= tail.size() &&
                                     out.compare(out.size() - tail.size(), tail.size(), tail) == 0))
                        continue;
                    out += " / ";
                }
                out += s;
            }
            return out;
        };

        std::string label_en;
        std::string label_loc;
        auto pn_it = panel_names.find(key);
        if (pn_it != panel_names.end() && !pn_it->value("group_loc", std::string()).empty()) {
            const json& pn = *pn_it;
            const std::string group_en  = pn.value("group_en", std::string());
            const std::string group_loc = pn.value("group_loc", std::string());
            const std::string line_loc  = pn.value("line_loc", std::string());
            // Full hierarchy as shown in the professional panel:
            //   optgroup / line / option   e.g. "悬垂速度 / 桥接 / 内部"
            // The line segment only appears for multi-option lines (single-option
            // lines duplicate the option name, so line_loc is empty there) →
            //   "线宽 / 外墙"  (two segments)
            label_loc = join_path({group_loc, line_loc, def_leaf_loc});
            // English has no per-line string, so it degrades to group / option.
            label_en = join_path({group_en, def_leaf_en});
        } else if (!def.category.empty()) {
            // Not on any panel: fall back to category for some grouping context.
            label_en  = compose(def.category, def_leaf_en);
            label_loc = compose(_u8L(def.category.c_str()), def_leaf_loc);
        } else {
            label_en  = def_leaf_en;
            label_loc = def_leaf_loc;
        }

        item["label"] = label_en;
        if (!label_loc.empty() && label_loc != label_en)
            item["label_localized"] = label_loc;

        item["type"]     = type_to_string(def.type);
        item["is_vector"] = !def.is_scalar(); // PRINT-ADVISOR advisor-hook
        item["category"] = def.category.empty() ? "Other" : def.category;
        // process | filament | printer — consumed by the agent's param domain guard.
        item["param_class"] = classify_param(key);
        if (!def.tooltip.empty())
            item["tooltip"] = def.tooltip;
        if (!def.sidetext.empty())
            item["unit"] = def.sidetext;
        if (def.min != INT_MIN)
            item["min"] = def.min;
        if (def.max != INT_MAX)
            item["max"] = def.max;

        // For enums, include protocol values plus English and active-locale labels.
        // Consumers map labels back to values by array index, keeping the schema
        // as the single source of truth for localized enum input.
        if (!def.enum_values.empty()) {
            item["enum_values"] = def.enum_values;
            if (!def.enum_labels.empty()) {
                item["enum_labels"] = def.enum_labels;
                json localized_labels = json::array();
                for (const std::string& label : def.enum_labels)
                    localized_labels.push_back(_u8L(label.c_str()));
                item["enum_labels_localized"] = std::move(localized_labels);
            }
        }

        // Include default value if available.
        //
        // IMPORTANT: enum option types (coEnum / coEnums) must NOT be serialized
        // here. Their serialize() dereferences an internal `keys_map` pointer that
        // is only populated when the option is created via load_option_from_archive
        // — the ConfigOptionDef::default_value objects are constructed without it,
        // so `keys_map` is null and serialize() causes an access violation (a
        // hardware/SEH fault, which try/catch(...) does NOT catch under the default
        // /EHsc model). Instead we recover the default from enum_values using the
        // stored integer index, which lives safely in the ConfigOptionInt(s) base.
        if (def.default_value) {
            const ConfigOptionType t = def.default_value->type();
            if (t == coEnum || t == coEnums) {
                // Recover the enum index without touching the (null) keys_map.
                // coEnum is a scalar (getInt); coEnums is an int vector (get_at).
                int idx = -1;
                try {
                    if (t == coEnum) {
                        idx = def.default_value->getInt();
                    } else {
                        // coEnums derives from ConfigOptionInts (int vector); the
                        // type() check above guarantees the runtime type, so a
                        // static_cast is safe and avoids RTTI/dynamic_cast issues.
                        const auto* ev =
                            static_cast<const ConfigOptionInts*>(def.default_value.get());
                        if (!ev->values.empty())
                            idx = ev->values.front();
                    }
                } catch (...) {
                    idx = -1;
                }
                if (idx >= 0 && idx < static_cast<int>(def.enum_values.size()))
                    item["default"] = def.enum_values[idx];
            } else {
                // Non-enum scalars/vectors serialize safely (nullable vectors emit
                // "nil" tokens rather than crashing).
                item["default"] = def.default_value->serialize();
            }
        }

        options_arr.push_back(std::move(item));
    }

    // Sort for a stable, human-readable layout: group by param_class
    // (process → filament → printer). The output stays a single flat array —
    // consumers only read fields, not order — but the file is far easier to
    // scan/diff with each class in its own contiguous block.
    auto class_rank = [](const std::string& c) -> int {
        if (c == "process")  return 0;
        if (c == "filament") return 1;
        if (c == "printer")  return 2;
        return 3;
    };
    // Within the process block, order categories by the professional-panel tabs:
    // 质量 → 强度 → 速度 → 支撑 → 材料 → 其他. Categories not in this list sort
    // after the named ones (rank 100), then alphabetically. filament/printer
    // blocks fall back to plain alphabetical category order.
    auto category_rank = [](const std::string& cat) -> int {
        if (cat == "Quality")  return 0;
        if (cat == "Strength") return 1;
        if (cat == "Speed")    return 2;
        if (cat == "Support")  return 3;
        if (cat == "Material") return 4;
        if (cat == "Other" || cat == "Others") return 5;
        return 100;
    };
    std::sort(options_arr.begin(), options_arr.end(),
              [&](const json& a, const json& b) {
        const std::string cls_a = a.value("param_class", "process");
        const std::string cls_b = b.value("param_class", "process");
        const int ra = class_rank(cls_a);
        const int rb = class_rank(cls_b);
        if (ra != rb) return ra < rb;

        const std::string ca = a.value("category", "");
        const std::string cb = b.value("category", "");
        // Business-ordered categories for the process block; alphabetical elsewhere.
        if (cls_a == "process") {
            const int cra = category_rank(ca);
            const int crb = category_rank(cb);
            if (cra != crb) return cra < crb;
        }
        if (ca != cb) return ca < cb;
        return a.value("key", "") < b.value("key", "");
    });

    return options_arr;
}

} // namespace Bridge
} // namespace GUI
} // namespace Slic3r

