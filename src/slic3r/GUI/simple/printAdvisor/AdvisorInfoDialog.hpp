#ifndef slic3r_GUI_AdvisorInfoDialog_hpp_
#define slic3r_GUI_AdvisorInfoDialog_hpp_

namespace Slic3r {
namespace GUI {

// Modal "Print Advisor MCP" dialog opened from the AI Integration menu: explains what the
// Print Advisor MCP is, what it allows and how to install the MCP sidecar, shows the live state
// of the local API and holds the Enable/Disable switch (advisor_set_enabled). GUI thread.
void advisor_show_info_dialog();

} // namespace GUI
} // namespace Slic3r

#endif // slic3r_GUI_AdvisorInfoDialog_hpp_
