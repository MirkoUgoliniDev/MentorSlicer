#ifndef NUMBERENTRYDIALOG_HPP
#define NUMBERENTRYDIALOG_HPP

#include <wx/wx.h>
#include <functional>

class NumberEntryDialog : public wxDialog {
public:

    enum class Purpose {
        Clone,    // Clone 
        General   // Other
    };

    NumberEntryDialog(wxWindow* parent, const wxString& title, 
                     const wxString& message, int min, int max, int initial, Purpose purpose = Purpose::General);

    ~NumberEntryDialog();


    void SetValueChangedCallback(std::function<void(int)> callback);

    int GetValue() const { return m_spinCtrl->GetValue(); }

    static bool IsCloneDialogOpen();

private:
    Purpose m_purpose;
    static std::vector<NumberEntryDialog*> s_openDialogs;
    wxSpinCtrl* m_spinCtrl{ nullptr };
    std::function<void(int)> m_callback;
    void OnSpinValueChanged(wxSpinEvent& event);
    void OnTextChanged(wxCommandEvent& event);
};

#endif // NUMBERENTRYDIALOG_HPP