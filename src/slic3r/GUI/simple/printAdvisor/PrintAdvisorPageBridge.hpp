#ifndef slic3r_GUI_PrintAdvisorPageBridge_hpp_
#define slic3r_GUI_PrintAdvisorPageBridge_hpp_

// Print Advisor bridge to the device manager page (PrinterMgrView). Built in every
// configuration, like the monitor that uses it; the only Creality hook is one line in
// PrinterMgrView::OnScriptMessage (marked PRINT-ADVISOR).

#include "nlohmann/json_fwd.hpp"

namespace Slic3r {
namespace GUI {

// GUI thread only. True when the device page of the main frame exists and has finished loading.
bool advisor_device_page_loaded();
// GUI thread only. Sends msg (it must contain "command") to window.handleStudioCmd of the device
// page, with the same envelope as PrinterMgrView::request_print_control. No-op when the page is
// not loaded.
void advisor_push_device_page(const nlohmann::json& msg);
// Reply of the page to "advisor_snapshot" (command "advisor_snapshot_result"). Defined in
// simple/printAdvisor/PrintAdvisorCamera.cpp; any thread. Unknown
// request ids are ignored.
void advisor_deliver_snapshot_result(nlohmann::json&& result);
// "advisor_preheat" message of the chamber-card button (web patch 17): starts or ends a chamber
// preheat through the bed, with the same confirmation and safety rules as the MCP tool. Defined
// in PrintAdvisorServer.cpp. GUI thread.
void advisor_preheat_from_page(const nlohmann::json& msg);

// Hook of CxSentToPrinterDialog (SendToPrinter.cpp): adds to the "update_plate_data" payload of
// the send dialog the current chamber temperature of every connected LAN printer,
// "advisor_chamber_temps": { "<host>": <boxTemp> }, from DeviceTelemetryCache. Only values
// reported on the current connection are included. The page (web patch 10) shows a notice
// when the plate uses ASA/ABS and the selected printer's chamber is cold. Any thread.
void advisor_append_send_page_data(nlohmann::json& top_level_json);

// Hook of PrinterMgrView::OnScriptMessage, called first with the parsed page message. Returns
// true when the message belongs to Print Advisor and was consumed (it may have been moved from):
// the caller then returns without the generic routing and the trace log, because a camera frame
// carries a large base64 image.
bool advisor_handle_page_message(nlohmann::json& incoming);

} // namespace GUI
} // namespace Slic3r

#endif // slic3r_GUI_PrintAdvisorPageBridge_hpp_
