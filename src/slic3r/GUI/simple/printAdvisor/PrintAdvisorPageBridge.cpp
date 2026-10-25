#include "PrintAdvisorPageBridge.hpp"
#include "PrintAdvisorInternal.hpp"

#include "slic3r/GUI/GUI_App.hpp"
#include "slic3r/GUI/MainFrame.hpp"
#include "slic3r/GUI/print_manage/App/DeviceTelemetryCache.h"
#include "slic3r/GUI/print_manage/App/PrinterMgrView.hpp"
#include "slic3r/GUI/print_manage/Utils.hpp"

#include <boost/log/trivial.hpp>
#include <nlohmann/json.hpp>
#include <wx/app.h>

#include <cmath>
#include <exception>
#include <string>
#include <utility>

namespace Slic3r {
namespace GUI {

namespace {

// Oldest telemetry used for the cold-chamber notice of the send page.
constexpr long long ADVISOR_CHAMBER_MAX_AGE_MS = 2 * 60 * 1000;

// Device page view of the main frame, or null. Print Advisor calls this from CallAfter
// handlers that may run while the application closes: GUI_App::mainframe is not reset when
// the frame is destroyed, so the pointer is dereferenced only while the frame is still a
// registered top-level window that is not being deleted.
PrinterMgrView* advisor_device_view()
{
    if (!wxTheApp)
        return nullptr;
    MainFrame* mainframe = wxGetApp().mainframe;
    if (!mainframe || !wxTopLevelWindows.Find(mainframe) || mainframe->IsBeingDeleted())
        return nullptr;
    PrinterMgrView* view = mainframe->get_printer_mgr_view();
    if (!view || view->IsBeingDeleted())
        return nullptr;
    return view;
}

} // namespace

bool advisor_device_page_loaded()
{
    PrinterMgrView* view = advisor_device_view();
    return view != nullptr && view->is_page_loaded();
}

void advisor_push_device_page(const nlohmann::json& msg)
{
    PrinterMgrView* view = advisor_device_view();
    if (!view || !view->is_page_loaded())
        return;
    try {
        auto json_str = RemotePrint::Utils::url_encode(msg.dump(-1, ' ', true));
        view->ExecuteScriptCommand(json_str, true);
    }
    catch (const std::exception& e) {
        BOOST_LOG_TRIVIAL(error) << "advisor_push_device_page failed: " << e.what();
    }
}

void advisor_append_send_page_data(nlohmann::json& top_level_json)
{
    nlohmann::json temps = nlohmann::json::object();
    try {
        const nlohmann::json devices = DeviceTelemetryCache::instance().snapshot();
        for (auto it = devices.begin(); it != devices.end(); ++it) {
            const nlohmann::json& device = it.value();
            if (!device.is_object() || !device.contains("boxTemp") || !device["boxTemp"].is_number())
                continue;
            // Same rule as PrintAdvisorMonitor::usable(): connected, and not a value kept from
            // a previous connection.
            auto connected = device.find("connected");
            if (connected == device.end() || !connected->is_boolean() || !connected->get<bool>())
                continue;
            bool stale = false;
            auto stale_keys = device.find("stale_keys");
            if (stale_keys != device.end() && stale_keys->is_array())
                for (const nlohmann::json& item : *stale_keys)
                    if (item.is_string() && item.get_ref<const std::string&>() == "boxTemp")
                        stale = true;
            if (stale)
                continue;
            // A connection can stay open while the printer stops reporting: only a recent
            // reading counts (steady clock).
            const long long age = DeviceTelemetryCache::instance().age_ms(it.key());
            if (age < 0 || age > ADVISOR_CHAMBER_MAX_AGE_MS)
                continue;
            const double value = device["boxTemp"].get<double>();
            if (std::isfinite(value))
                temps[it.key()] = value;
        }
    }
    catch (const std::exception& e) {
        BOOST_LOG_TRIVIAL(error) << "advisor_append_send_page_data failed: " << e.what();
    }
    top_level_json["advisor_chamber_temps"] = std::move(temps);
}

bool advisor_handle_page_message(nlohmann::json& incoming)
{
    // Print Advisor camera frame.
    if (incoming.value("command", std::string()) == "advisor_snapshot_result") {
        advisor_deliver_snapshot_result(std::move(incoming));
        return true;
    }
    // "Preriscalda" / "Stop" button of the chamber card.
    if (incoming.value("command", std::string()) == "advisor_preheat") {
        advisor_preheat_from_page(incoming);
        return true;
    }
    // Answer of the page to request_print_control (pause/resume/stop, bed setpoint).
    if (incoming.value("command", std::string()) == "print_control_result") {
        advisor_on_print_control_result(incoming);
        return true;
    }
    return false;
}

} // namespace GUI
} // namespace Slic3r
