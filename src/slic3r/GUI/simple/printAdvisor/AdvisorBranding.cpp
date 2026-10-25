#include "AdvisorBranding.hpp"

#include "slic3r/GUI/GUI_App.hpp"

#include <wx/brush.h>
#include <wx/dcmemory.h>
#include <wx/font.h>
#include <wx/pen.h>

#include <algorithm>

namespace Slic3r {
namespace GUI {
namespace advisor_branding {

const wxColour green(0x17, 0xCC, 0x5F);
const wxColour red(0xE0, 0x4F, 0x4F);

wxBitmap logo(int size)
{
    wxBitmap bitmap(size, size, 32);
    bitmap.UseAlpha();
    wxMemoryDC dc(bitmap);
    dc.SetBackground(*wxTRANSPARENT_BRUSH);
    dc.Clear();
    dc.SetPen(*wxTRANSPARENT_PEN);
    dc.SetBrush(wxBrush(green));
    dc.DrawRoundedRectangle(0, 0, size, size, size * 0.22);
    // Chat bubble with a small tail.
    const int margin = size / 6;
    const int bubble_h = size * 0.52;
    dc.SetBrush(*wxWHITE_BRUSH);
    dc.DrawRoundedRectangle(margin, margin, size - 2 * margin, bubble_h, size * 0.12);
    const wxPoint tail[] = {{margin + size / 6, margin + bubble_h - 1},
                            {margin + size / 6, margin + bubble_h + size / 6},
                            {margin + size / 3, margin + bubble_h - 1}};
    dc.DrawPolygon(3, tail);
    wxFont font(wxFontInfo(std::max(6, size / 4)).Bold());
    dc.SetFont(font);
    dc.SetTextForeground(green);
    const wxSize text = dc.GetTextExtent("AI");
    dc.DrawText("AI", (size - text.x) / 2, margin + (bubble_h - text.y) / 2);
    dc.SelectObject(wxNullBitmap);
    return bitmap;
}

Palette palette()
{
    Palette p;
    p.dark = wxGetApp().dark_mode();
    p.background = p.dark ? wxColour(0x2D, 0x2D, 0x31) : *wxWHITE;
    p.foreground = p.dark ? wxColour(0xEE, 0xEE, 0xEE) : wxColour(0x22, 0x22, 0x22);
    p.muted = p.dark ? wxColour(0xA8, 0xA8, 0xA8) : wxColour(0x66, 0x66, 0x66);
    p.neutral_button = p.dark ? wxColour(0x45, 0x45, 0x4A) : wxColour(0xE6, 0xE6, 0xE6);
    return p;
}

} // namespace advisor_branding
} // namespace GUI
} // namespace Slic3r
