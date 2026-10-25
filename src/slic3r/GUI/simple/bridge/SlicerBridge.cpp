#include "SlicerBridge.hpp"

#include <boost/log/trivial.hpp>

using json = nlohmann::json;

namespace Slic3r {
namespace GUI {
namespace Bridge {

// ===========================================================================
// Singleton
// ===========================================================================

SlicerBridge& SlicerBridge::Instance()
{
    static SlicerBridge inst;
    return inst;
}

// ===========================================================================
// Execution dispatcher
// ===========================================================================

json SlicerBridge::Execute(const std::string& action_id, const json& params)
{
    json (SlicerBridge::*executor)(const json&) = nullptr;
    if (action_id == ActionID::GET_PRESETS)
        executor = &SlicerBridge::DoGetPresets;
    else if (action_id == ActionID::GET_EDITED_CONFIG)
        executor = &SlicerBridge::DoGetEditedConfig;
    else if (action_id == ActionID::START_SLICE)
        executor = &SlicerBridge::DoStartSlice;
    if (executor == nullptr) {
        BOOST_LOG_TRIVIAL(warning) << "[SlicerBridge] Unknown action: " << action_id;
        return {{"success", false}, {"action", action_id},
                {"message", "Unknown action: " + action_id}};
    }

    try {
        BOOST_LOG_TRIVIAL(debug) << "[SlicerBridge] Execute: " << action_id;
        json result = (this->*executor)(params);
        result["action"] = action_id;
        return result;
    } catch (const std::exception& e) {
        BOOST_LOG_TRIVIAL(error) << "[SlicerBridge] Execute " << action_id
                                 << " error: " << e.what();
        return {{"success", false}, {"action", action_id}, {"message", e.what()}};
    }
}


} // namespace Bridge
} // namespace GUI
} // namespace Slic3r


