#include "PrintAdvisorHooks.hpp"


#include "AdvisorInfoDialog.hpp"
#include "PrintAdvisorInternal.hpp"
#include "PrintAdvisorServer.hpp"
#include "libslic3r/AppConfig.hpp"
#include "slic3r/GUI/GUI_App.hpp"
#include "slic3r/GUI/I18N.hpp"

#include <boost/log/trivial.hpp>
#include <wx/menu.h>

#include <exception>
#include <memory>

namespace Slic3r {
namespace GUI {

namespace {

// The running local API, or empty. Never destroyed, like the former GUI_App member that was
// reset only by advisor_on_shutdown: the server is never torn down by static destructors.
std::unique_ptr<PrintAdvisorServer>& advisor_server()
{
    static auto* server = new std::unique_ptr<PrintAdvisorServer>();
    return *server;
}

void start_server()
{
    AppConfig* app_config = wxGetApp().app_config;
    std::unique_ptr<PrintAdvisorServer>& server = advisor_server();
    if (!app_config || !app_config->get_bool("print_advisor_mcp_enabled") || server)
        return;
    try {
        server = std::make_unique<PrintAdvisorServer>();
        BOOST_LOG_TRIVIAL(info) << "Print Advisor local API started";
        advisor::verdict_on_server_started();
    } catch (const PrintAdvisorOtherInstance& error) {
        // The setting is shared with the other instance (same data folder): keep it, so the other
        // instance still starts its server next time; only this window shows it off (the
        // Print Advisor MCP dialog reads the live state).
        BOOST_LOG_TRIVIAL(warning) << "Print Advisor local API not started: " << error.what();
    } catch (const std::exception& error) {
        BOOST_LOG_TRIVIAL(error) << "Cannot start Print Advisor local API: " << error.what();
        app_config->set_bool("print_advisor_mcp_enabled", false);
        app_config->save();
    }
}

void stop_server()
{
    std::unique_ptr<PrintAdvisorServer>& server = advisor_server();
    if (server) {
        advisor::verdict_on_server_stopped();
        server->stop();
        server.reset();
    }
}

} // namespace

PrintAdvisorServer* advisor_running_server()
{
    return advisor_server().get();
}

void advisor_on_app_init()
{
    if (wxGetApp().app_config && wxGetApp().app_config->get_bool("print_advisor_mcp_enabled"))
        start_server();
}

void advisor_on_shutdown()
{
    stop_server();
}

void advisor_set_enabled(bool enabled)
{
    wxGetApp().app_config->set_bool("print_advisor_mcp_enabled", enabled);
    wxGetApp().app_config->save();
    if (enabled)
        start_server();
    else
        stop_server();
}

wxMenu* advisor_create_ai_menu()
{
    auto* ai_menu = new wxMenu();
    // A plain item: the dialog explains the integration and holds the Enable/Disable switch.
    auto* print_advisor_item = ai_menu->Append(wxID_ANY, _L("Print Advisor MCP") + dots,
                                               _L("What the Print Advisor MCP is, how to install it, and its on/off switch."));
    ai_menu->Bind(wxEVT_MENU, [](wxCommandEvent&) { advisor_show_info_dialog(); }, print_advisor_item->GetId());
    return ai_menu;
}

} // namespace GUI
} // namespace Slic3r
