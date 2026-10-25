#include "NumberEntryDialog.hpp"
#include <wx/gdicmn.h>
#include <wx/app.h> // Add this line to include the header file for wxGetApp
#include <wx/wx.h>
#include "slic3r/GUI/GUI_App.hpp"
#include <wx/valnum.h>

std::vector<NumberEntryDialog*> NumberEntryDialog::s_openDialogs;

NumberEntryDialog::NumberEntryDialog(wxWindow* parent, const wxString& title,
                                    const wxString& message, int min, int max, int initial, Purpose purpose)
    : wxDialog(parent, wxID_ANY, title), m_purpose(purpose),m_callback(nullptr) 
{
    s_openDialogs.push_back(this);

    wxBoxSizer* sizer = new wxBoxSizer(wxVERTICAL);
        
    sizer->Add(new wxStaticText(this, wxID_ANY, message), 0, wxALL, 5);
        
    m_spinCtrl = new wxSpinCtrl(this, wxID_ANY, wxString::Format("%d", initial),
                                wxDefaultPosition, wxDefaultSize, 
                                wxSP_ARROW_KEYS, min, max, initial);

    m_spinCtrl->SetValue(1);

    m_spinCtrl->SetValidator(wxIntegerValidator<int>());

    sizer->Add(m_spinCtrl, 0, wxEXPAND | wxALL, 5);
        
    wxSizer* btnSizer = CreateButtonSizer(wxOK | wxCANCEL);
    sizer->Add(btnSizer, 0, wxEXPAND | wxALL, 5);
        
    SetSizerAndFit(sizer);

    m_spinCtrl->Bind(wxEVT_SPINCTRL, &NumberEntryDialog::OnSpinValueChanged, this);
    m_spinCtrl->Bind(wxEVT_TEXT, &NumberEntryDialog::OnTextChanged, this);
}

NumberEntryDialog::~NumberEntryDialog() 
{
    auto it = std::find(s_openDialogs.begin(), s_openDialogs.end(), this);
    if (it != s_openDialogs.end()) {
        s_openDialogs.erase(it);
    }
}


void NumberEntryDialog::SetValueChangedCallback(std::function<void(int)> callback) 
{
    m_callback = callback;
}

void NumberEntryDialog::OnSpinValueChanged(wxSpinEvent& event) 
{
    if (m_callback) {
        m_callback(event.GetValue());
    }
    event.Skip();
}

void NumberEntryDialog::OnTextChanged(wxCommandEvent& event) 
{
    //if(m_spinCtrl) {
    //    m_spinCtrl->SetValue(event.GetInt());
    //}
    //if (m_callback) {
    //    m_callback(m_spinCtrl->GetValue());
    //}

    int value = event.GetInt();

    if(m_callback) {
        m_callback(value);
    }
    event.Skip();
}

bool NumberEntryDialog::IsCloneDialogOpen() 
{
    return std::any_of(
        s_openDialogs.begin(),
        s_openDialogs.end(),
        [](NumberEntryDialog* dlg) {
            return dlg->m_purpose == Purpose::Clone;
        }
    );
}