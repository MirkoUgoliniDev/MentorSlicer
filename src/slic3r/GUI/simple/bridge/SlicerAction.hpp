#ifndef slic3r_SlicerAction_hpp_
#define slic3r_SlicerAction_hpp_

namespace Slic3r {
namespace GUI {
namespace Bridge {

// ---------------------------------------------------------------------------
// Action IDs accepted by SlicerBridge::Execute (string constants for type-safety)
// ---------------------------------------------------------------------------

namespace ActionID {
    constexpr const char* GET_PRESETS       = "get_presets";
    constexpr const char* GET_EDITED_CONFIG = "get_edited_config";
    constexpr const char* START_SLICE       = "start_slice";
}

} // namespace Bridge
} // namespace GUI
} // namespace Slic3r

#endif // slic3r_SlicerAction_hpp_
