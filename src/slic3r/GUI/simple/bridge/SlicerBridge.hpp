#ifndef slic3r_SlicerBridge_hpp_
#define slic3r_SlicerBridge_hpp_

#include "SlicerAction.hpp"
#include "nlohmann/json.hpp"

#include <string>

namespace Slic3r { class ModelObject; }

namespace Slic3r {
namespace GUI {
namespace Bridge {

/// Result of lightweight overhang analysis on a model object.
struct OverhangResult {
    double unsupported_area_ratio = 0.0;  ///< 0.0 ~ 1.0  downward-facing area with visible clearance to the bed.
    double downward_area_ratio    = 0.0;  ///< 0.0 ~ 1.0  all downward-facing area above the configured threshold.
    double bottom_contact_ratio   = 0.0;  ///< 0.0 ~ 1.0  surface area near the object's bottom used as bed contact proxy.
    double bottom_contact_area_mm2 = 0.0;
    double max_overhang_angle     = 0.0;  ///< degrees (0 = horizontal bottom, 90 = vertical wall)
};

/// SlicerBridge - executes the few slicer actions the Print Advisor uses
/// (presets, edited config, start slice) and exposes the config schema and
/// overhang helpers.
///
/// Usage (Print Advisor):
///   auto& bridge = SlicerBridge::Instance();
///   nlohmann::json result = bridge.Execute("start_slice", params);
///
class SlicerBridge
{
public:
    static SlicerBridge& Instance();

    // ---- Execution ----

    /// Execute an action by id.  Returns a JSON object with at least
    /// { "success": bool, "action": string, "message": string, ... }
    nlohmann::json Execute(const std::string& action_id,
                           const nlohmann::json& params = nlohmann::json::object());

    // ---- Dev tools ----

    /// Build the user-facing slicer parameter schema array (the "options"
    /// field of the Print Advisor parameter schema).
    /// Pure function over print_config_def — no GUI/runtime state required.
    static nlohmann::json BuildConfigSchemaArray();

    /// Harvest the professional-panel layout into a per-key name map:
    ///   key -> { "group_en", "group_loc", "line_loc" }
    /// where group_* is the optgroup title (English original / UI language) and
    /// line_loc is the (already localized) line label, present only when a line
    /// bundles several options (adds disambiguating context, e.g. Bridge line).
    /// Reads the live Tab layout, so must be called on the GUI thread after the
    /// tabs are built. Used by BuildConfigSchemaArray to compose the exported
    /// schema's disambiguated parameter display names.
    static nlohmann::json BuildPanelNameMap();

    // PRINT-ADVISOR BEGIN advisor-hook (upstream: private, under "// ---- Helpers ----")
    // Mesh-normal overhang estimate, also used by the Print Advisor model analysis.
    static OverhangResult AnalyzeOverhang(const ModelObject& obj, double threshold_deg);
    // PRINT-ADVISOR END

private:
    SlicerBridge() = default;
    ~SlicerBridge() = default;
    SlicerBridge(const SlicerBridge&) = delete;
    SlicerBridge& operator=(const SlicerBridge&) = delete;

    // ---- Action executor implementations ----
    nlohmann::json DoGetPresets(const nlohmann::json& params);
    nlohmann::json DoGetEditedConfig(const nlohmann::json& params);
    nlohmann::json DoStartSlice(const nlohmann::json& params);
};

} // namespace Bridge
} // namespace GUI
} // namespace Slic3r

#endif // slic3r_SlicerBridge_hpp_
