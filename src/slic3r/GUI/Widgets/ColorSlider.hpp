#ifndef slic3r_GUI_ColorSlider_hpp_
#define slic3r_GUI_ColorSlider_hpp_

#include "../wxExtensions.hpp"
#include <wx/wx.h>
#include <wx/control.h>
#include <wx/dcgraph.h>
#include <wx/settings.h>

class ColorSlider : public wxControl
{
public:
    ColorSlider(wxWindow*      parent,
                wxWindowID     id,
                int            value,
                int            minValue,
                int            maxValue,
                const wxPoint& pos   = wxDefaultPosition,
                const wxSize&  size  = wxDefaultSize,
                long           style = wxSL_HORIZONTAL);
    void SetValue(int value);
    void SetRange(int minVal, int maxVal);
    int  GetValue() const { return m_value; }
    int  GetMin() const { return m_min; }
    int  GetMax() const { return m_max; }

private:
    void OnPaint(wxPaintEvent& event);
    void OnEraseBackground(wxEraseEvent& event);
    //void OnScrollUpdate(wxScrollEvent& event);
    void OnTextChanged(wxCommandEvent& event);
    void OnFocusChange(wxFocusEvent& event);
    void OnSysColourChanged(wxSysColourChangedEvent& event);
    void OnLeftDown(wxMouseEvent& event);
    void OnLeftUp(wxMouseEvent& event);
    void OnMotion(wxMouseEvent& event);
    void OnCaptureLost(wxMouseCaptureLostEvent& event);
    void UpdatePaletteFromEnv();
    bool IsDarkAppearance() const;

    void     Draw(wxGCDC& gdc);
    void     SetValueFromXCoord(int x, bool fireEvent);

    wxColour mTrackBg, mThumbFill, mThumbRing;
    int      m_min{0}, m_max{100}, m_value{0};
    bool     m_dragging{false};

    int TrackHeightDIP() const { return FromDIP(6, this); }
    int PaddingDIP() const { return FromDIP(7, this); }

    static const int THUMB_RADIUS = 7;
    wxDECLARE_EVENT_TABLE();
};
#endif // !slic3r_GUI_ColorSlider_hpp_