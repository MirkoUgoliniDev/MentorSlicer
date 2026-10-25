#include "SlicerBridge.hpp"

#include "slic3r/GUI/GUI_App.hpp"
#include "slic3r/GUI/MainFrame.hpp"
#include "slic3r/GUI/Plater.hpp"
#include "slic3r/GUI/PartPlate.hpp"

using json = nlohmann::json;

namespace Slic3r {
namespace GUI {
namespace Bridge {

// Slices the plate shown in MentorSlicer. It never switches the visible plate,
// selects presets or exports G-code: the parameters are ignored. A plate whose slice result is
// still valid is not sliced again (already_sliced); a running slice is reported as such.
json SlicerBridge::DoStartSlice(const json& /*params*/)
{
    auto* mainframe = wxGetApp().mainframe;
    auto* plater = wxGetApp().plater();
    if (!mainframe)
        return {{"success", false}, {"message", "MainFrame not available"}};
    if (!plater)
        return {{"success", false}, {"message", "Plater not available"}};

    PartPlateList& plate_list = plater->get_partplate_list();
    if (plate_list.get_plate_count() <= 0)
        return {{"success", false}, {"message", "No plate available"}};

    const int plate_idx = plate_list.get_curr_plate_index();

    // Apply the latest global and per-object configuration before checking
    // whether the plate may be sliced. MainFrame::slice_plate() checks the
    // existing slice-valid flag before its own update, so without this sync a
    // just-modified object config can be mistaken for an unchanged plate and
    // the slice request silently does nothing.
    plater->apply_background_progress();
    const auto refused = [plate_idx](const char* code, const char* message) {
        return json{{"success", false}, {"code", code}, {"message", message},
                    {"target_plate_index", plate_idx}, {"target_plate_number", plate_idx + 1}};
    };
    // The checks below do not depend on the slice-button mode (slice plate / slice all), which
    // MainFrame::get_enable_slice_status follows.
    PartPlate* plate = plate_list.get_curr_plate();
    if (!plate)
        return refused("NO_PLATE", "No current plate");
    if (plater->only_gcode_mode() || plater->using_exported_file())
        return refused("GCODE_MODE", "MentorSlicer is showing a G-code or exported file: open a project to slice");
    if (plater->is_background_process_slicing())
        return {{"success", true}, {"slicing_in_progress", true}, {"message", "Slicing already in progress"},
                {"target_plate_index", plate_idx}, {"target_plate_number", plate_idx + 1}};
    if (plate->is_slice_result_valid())
        return {{"success", true}, {"already_sliced", true},
                {"message", "The current plate is already sliced with the current settings"},
                {"target_plate_index", plate_idx}, {"target_plate_number", plate_idx + 1}};
    if (plate->empty())
        return refused("NO_OBJECTS", "The current plate is empty: add or move an object onto it");
    if (!plate->has_printable_instances())
        return refused("NO_OBJECTS", "The current plate has no printable object inside the plate area");
    if (!plate->can_slice())
        return refused("SLICE_NOT_STARTED", "Current plate is not ready for slicing after applying the latest configuration");

    mainframe->slice_plate(MainFrame::eSlicePlate);
    return {
        {"success", true},
        {"message", "Slicing started"},
        {"target_plate_index", plate_idx},
        {"target_plate_number", plate_idx + 1}
    };
}

} // namespace Bridge
} // namespace GUI
} // namespace Slic3r
