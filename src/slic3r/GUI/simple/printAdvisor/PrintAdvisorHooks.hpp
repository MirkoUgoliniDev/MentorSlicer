#ifndef slic3r_GUI_PrintAdvisorHooks_hpp_
#define slic3r_GUI_PrintAdvisorHooks_hpp_

// Entry points of Print Advisor called from Creality code (GUI_App, MainFrame), each from a
// one-line hook marked PRINT-ADVISOR. GUI thread only.

class wxMenu;

namespace Slic3r {
namespace GUI {

class PrintAdvisorServer;

// The running Print Advisor local API, or null. GUI thread.
PrintAdvisorServer* advisor_running_server();

// MainFrame constructor, after the local HTTP server: starts the Print Advisor local API when
// enabled in AppConfig (print_advisor_mcp_enabled).
void advisor_on_app_init();

// MainFrame::shutdown and GUI_App::OnExit: stops the local API and removes its endpoint
// descriptor. Idempotent.
void advisor_on_shutdown();

// "AI Integration" menu of the editor (before Help): its "Print Advisor MCP..." item opens the
// Print Advisor MCP dialog (AdvisorInfoDialog), which holds the Enable/Disable switch.
wxMenu* advisor_create_ai_menu();

// Saves print_advisor_mcp_enabled and starts or stops the local API accordingly.
void advisor_set_enabled(bool enabled);

} // namespace GUI
} // namespace Slic3r

#endif // slic3r_GUI_PrintAdvisorHooks_hpp_
