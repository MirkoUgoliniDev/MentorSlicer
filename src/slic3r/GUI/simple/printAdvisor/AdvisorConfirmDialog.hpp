#ifndef slic3r_GUI_AdvisorConfirmDialog_hpp_
#define slic3r_GUI_AdvisorConfirmDialog_hpp_

#include <string>
#include <vector>

namespace Slic3r {
namespace GUI {

// One row of the change table shown in a confirmation: parameter, value before, value after.
struct AdvisorChangeRow
{
    std::string parameter;
    std::string before;
    std::string after;
};

// Modal confirmation for an action requested through the MCP Advisor (Print Advisor). It is
// branded with the MCP Advisor logo and title, so it is not mistaken for a native Creality
// popup. Cancel is the default button. Returns true only when the user confirms.
bool advisor_confirm(const std::string& action, const std::string& message,
                     const std::vector<AdvisorChangeRow>& changes = {}, bool destructive = false);

} // namespace GUI
} // namespace Slic3r

#endif // slic3r_GUI_AdvisorConfirmDialog_hpp_
