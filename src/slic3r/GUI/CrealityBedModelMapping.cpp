#include "CrealityBedModelMapping.hpp"

#include "libslic3r/libslic3r.h"
#include "libslic3r/Utils.hpp"
#include "libslic3r/Preset.hpp"

#include <array>

#include <boost/algorithm/string/predicate.hpp>
#include <boost/filesystem/operations.hpp>

namespace Slic3r {
namespace GUI {
namespace CrealityBedModelMapping {
namespace {

bool contains_sparkx(const std::string& value)
{
    return value.find("SPARKX") != std::string::npos || value.find("sparkx") != std::string::npos;
}

static constexpr const char* kBedModelFilenameDefault = "creality_k1_buildplate_model.stl";

static constexpr const char* kBedModelFilenameF022   = "Creality F022_buildplate_model.stl";
static constexpr const char* kBedModelFilenameK2     = "Creality K2_buildplate_model.stl";
static constexpr const char* kBedModelFilenameK2Pro  = "Creality K2 Pro_buildplate_model.stl";
static constexpr const char* kBedModelFilenameK2Plus = "Creality K2 Plus_buildplate_model.stl";

struct BedModelRule
{
    const char* filename = nullptr;
    bool        native_scale = false;
    std::array<const char*, 4> aliases = { nullptr, nullptr, nullptr, nullptr };
};

static constexpr std::array<BedModelRule, 4> kBedModelRules = {{
    {kBedModelFilenameF022, true, {"Creality F022", "Creality_F022", nullptr, nullptr}},
    {kBedModelFilenameK2, true, {"Creality K2", "Creality_K2", nullptr, nullptr}},
    {kBedModelFilenameK2Pro, true, {"Creality K2 Pro", "Creality_K2_Pro", nullptr, nullptr}},
    {kBedModelFilenameK2Plus, true, {"Creality K2 Plus", "Creality_K2_Plus", nullptr, nullptr}},
}};

static bool matches_any_alias(const BedModelRule& rule, const std::string& candidate)
{
    if (candidate.empty())
        return false;
    for (const char* alias : rule.aliases) {
        if (alias != nullptr && candidate == alias)
            return true;
    }
    return false;
}

static const char* bed_model_filename_for_candidates(const std::string& bed_model, const std::string& name, const std::string& id,
                                                     const std::string& model_id)
{
    // Prefer explicit dedicated bed models when available; otherwise keep legacy K1 default.
    if (contains_sparkx(name) || contains_sparkx(id) || contains_sparkx(model_id))
        return kBedModelFilenameF022;

    for (const BedModelRule& rule : kBedModelRules) {
        if (rule.filename == nullptr)
            continue;
        if (!bed_model.empty() && bed_model == rule.filename)
            return rule.filename;
        if (matches_any_alias(rule, name) || matches_any_alias(rule, id) || matches_any_alias(rule, model_id))
            return rule.filename;
    }

    return kBedModelFilenameDefault;
}

std::string bed_model_filename_for_printer_model(const VendorProfile::PrinterModel& pm)
{
    return bed_model_filename_for_candidates(pm.bed_model, pm.name, pm.id, pm.model_id);
}

std::string bed_model_filename_for_printer_model(const std::string& printer_model)
{
    // For project-embedded presets we only have a single string; reuse the same matching rules.
    return bed_model_filename_for_candidates({}, printer_model, printer_model, printer_model);
}

std::string resolve_bed_model_path(const std::string& vendor_id, const std::string& bed_model_filename)
{
    if (vendor_id.empty() || bed_model_filename.empty())
        return {};

    std::string out = Slic3r::data_dir() + "/vendor/" + vendor_id + "/" + bed_model_filename;
    if (!boost::filesystem::exists(boost::filesystem::path(out))) {
        out = Slic3r::resources_dir() + "/profiles/" + vendor_id + "/" + bed_model_filename;
    }
    return out;
}

} // namespace

bool bed_model_has_native_scale(const std::string& model_filename_or_path)
{
    if (model_filename_or_path.empty())
        return false;

    for (const BedModelRule& rule : kBedModelRules) {
        if (rule.native_scale && rule.filename != nullptr && boost::algorithm::iends_with(model_filename_or_path, rule.filename))
            return true;
    }
    return false;
}

std::string bed_model_path_for_preset(const Preset& preset)
{
    std::string vendor_id;
    std::string bed_model_filename;

    const VendorProfile::PrinterModel* pm = PresetUtils::system_printer_model(preset);
    if (pm != nullptr && preset.vendor != nullptr) {
        vendor_id = preset.vendor->id;
        if (!pm->bed_model.empty()) {
            if (vendor_id == "Creality")
                bed_model_filename = bed_model_filename_for_printer_model(*pm);
            else
                bed_model_filename = pm->bed_model;
        }
    }

    // Fallback for project-embedded/custom presets: vendor may be null even though printer_model is Creality related.
    if (bed_model_filename.empty()) {
        auto* printer_model = preset.config.opt<ConfigOptionString>("printer_model");
        if (printer_model != nullptr && !printer_model->value.empty() &&
            (boost::algorithm::starts_with(printer_model->value, "Creality") || contains_sparkx(printer_model->value))) {
            vendor_id          = "Creality";
            bed_model_filename = bed_model_filename_for_printer_model(printer_model->value);
        }
    }

    return resolve_bed_model_path(vendor_id, bed_model_filename);
}

} // namespace CrealityBedModelMapping
} // namespace GUI
} // namespace Slic3r
