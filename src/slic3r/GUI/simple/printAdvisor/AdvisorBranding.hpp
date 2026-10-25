#ifndef slic3r_GUI_AdvisorBranding_hpp_
#define slic3r_GUI_AdvisorBranding_hpp_

// Look shared by the MCP Advisor dialogs (confirmations, fork update notice): logo and
// colours. Built in every configuration; needs only wxWidgets.

#include <wx/bitmap.h>
#include <wx/colour.h>

namespace Slic3r {
namespace GUI {
namespace advisor_branding {

extern const wxColour green;
extern const wxColour red;

// The MCP Advisor mark: a green rounded square with a white chat bubble and "AI", drawn at
// runtime so it needs no image resource and scales with the display.
wxBitmap logo(int size);

// Dialog palette for the current theme (wxGetApp().dark_mode()).
struct Palette
{
    bool     dark;
    wxColour background;
    wxColour foreground;
    wxColour muted;
    wxColour neutral_button;
};
Palette palette();

} // namespace advisor_branding
} // namespace GUI
} // namespace Slic3r

#endif // slic3r_GUI_AdvisorBranding_hpp_
