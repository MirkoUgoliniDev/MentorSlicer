#ifndef slic3r_AccelerationAndSpeedLimitDialog_hpp_
#define slic3r_AccelerationAndSpeedLimitDialog_hpp_
// The bed shape dialog.
// The dialog opens from Print Settins tab->Bed Shape : Set...

#include "GUI_Utils.hpp"
#include "2DBed.hpp"
#include "I18N.hpp"

#include <libslic3r/BuildVolume.hpp>

#include <wx/dialog.h>
#include <wx/choicebk.h>

#include "Widgets/TextInput.hpp"
#include "Widgets/Label.hpp"
#include "Widgets/CheckBox.hpp"
#include "Widgets/HoverBorderIcon.hpp"

namespace Slic3r {
namespace GUI {

class AccelerationAndSpeedLimitPanel;

class MyTextInput : public TextInput
{
public:
    explicit MyTextInput(wxWindow* parent, wxString text);
    void setTextEditedCb(std::function<void()> funcCb) { m_funcTextEditedCb = funcCb; }

protected:
    void OnEdit() override;
    std::function<void()> m_funcTextEditedCb = nullptr;
};

class WeightLimitItem : public wxPanel
{
public:
    struct LimitData
    {
        std::string value1 = "";
        std::string value2 = "";
        std::string speed1 = "";
        std::string speed2 = "";
        std::string Acc1   = "";
        std::string Acc2   = "";
        std::string Temp1  = "";
        std::string Temp2  = "";
        bool operator==(const LimitData& o) const {
            return value1 == o.value1 && value2 == o.value2 && 
            speed1 == o.speed1 && speed2 == o.speed2  && 
            Acc1 == o.Acc1 && Acc2 == o.Acc2 &&
            Temp1 == o.Temp1 && Temp2 == o.Temp2;
        }
    };

    WeightLimitItem(AccelerationAndSpeedLimitPanel* limitPanel, wxWindow* parent, int idx);
    void updateItemData(const LimitData* limitData, const LimitData* defaultLimitData);
    std::string serialize();
    const LimitData& getEditLimitData();
    void setDelBtnClickedCb(std::function<void(int)> func) { m_funcDelBtnClickedCb = func; }
    void updateIdx(int idx) { m_nIdx = idx; }
    void showSubCtrl();

private:
    void updateWeightLabelColor();
    void updateSpeedLabelColor();
    void updateAccLabelColor();

private:
    AccelerationAndSpeedLimitPanel* m_limitPanel = nullptr;
    wxPanel* m_weightPanel = nullptr;
    Label* m_weightLabel = nullptr;
    MyTextInput* m_weightMin = nullptr;
    wxStaticText* m_lineBreak = nullptr;
    MyTextInput* m_weightMax = nullptr;
    HoverBorderIcon* m_delBtn = nullptr;
    wxPanel* m_speedPanel = nullptr;
    wxStaticText* m_speedLabel  = nullptr;
    MyTextInput* m_speed = nullptr;
    wxPanel* m_AccelerationPanel = nullptr;
    wxStaticText* m_AccelerationLabel = nullptr;
    MyTextInput* m_Acceleration  = nullptr;
    TextInput* m_temp1 = nullptr;
    LimitData m_limitData;
    LimitData m_editLimitData;
    const LimitData* m_defaultLimitData = nullptr;
    int m_nIdx = -1;
    std::function<void(int)> m_funcDelBtnClickedCb = nullptr;
};

class HeightLimitItem : public wxPanel
{
public:
    HeightLimitItem(AccelerationAndSpeedLimitPanel* limitPanel, wxWindow* parent, int idx);
    void updateItemData(const WeightLimitItem::LimitData* limitData, const WeightLimitItem::LimitData* defaultLimitData);
    std::string serialize();
    const WeightLimitItem::LimitData& getEditLimitData();
    void setDelBtnClickedCb(std::function<void(int)> func) { m_funcDelBtnClickedCb = func; }
    void updateIdx(int idx) { m_nIdx = idx; }
    void showSubCtrl();

private:
    void updateHeightLabelColor();
    void updateSpeedLabelColor();
    void updateAccLabelColor();

private:
    AccelerationAndSpeedLimitPanel* m_limitPanel = nullptr;
    wxPanel* m_heightPanel = nullptr;
    wxStaticText* m_heightLabel = nullptr;
    MyTextInput* m_heightMin = nullptr;
    wxStaticText* m_lineBreak = nullptr;
    MyTextInput* m_heightMax = nullptr;
    HoverBorderIcon* m_delBtn = nullptr;
    wxPanel* m_speedPanel = nullptr;
    wxStaticText* m_speedLabel = nullptr;
    MyTextInput* m_speed = nullptr;
    wxPanel* m_AccelerationPanel = nullptr;
    wxStaticText* m_AccelerationLabel = nullptr;
    MyTextInput* m_Acceleration = nullptr;
    TextInput* m_temp1 = nullptr;
    WeightLimitItem::LimitData m_limitData;
    WeightLimitItem::LimitData m_editLimitData;
    const WeightLimitItem::LimitData* m_defaultLimitData = nullptr;
    int m_nIdx = -1;
    std::function<void(int)> m_funcDelBtnClickedCb = nullptr;
};

class AccelerationAndSpeedLimitPanel : public wxPanel
{
public:
    AccelerationAndSpeedLimitPanel(const std::string& type, wxWindow* parent);

    void build_panel(bool bDefaultCheckbox, const ConfigOptionString& defaultData,
        bool bCheckbox, const ConfigOptionString& data);
    bool getCheckbox();
    std::string serialize();
    void checkIsShowResetBtn();

private:
    void create_limit_item(WeightLimitItem::LimitData* limitData = nullptr, const WeightLimitItem::LimitData* defaultLimitData = nullptr);
    void parseLimitStr(std::string str, std::vector<WeightLimitItem::LimitData>& outData);
    void on_reset_btn_clicked(wxEvent&);
    void on_add_btn_clicked(wxEvent&);
    void* getLimitItem(size_t idx);
    void delLimitItem(size_t idx);

private:
    wxScrolledWindow* m_scrolled_window = nullptr;
    wxBoxSizer* m_scrolled_window_sizer = nullptr;
    wxBoxSizer* m_headPanelSizer = nullptr;
    std::string m_type = "";
    ::CheckBox* m_checkbox = nullptr;
    std::vector<WeightLimitItem::LimitData> m_vtLimitData;
    bool m_bDefaultCheckbox = false;
    ConfigOptionString m_defaultData = {};
    std::vector<WeightLimitItem::LimitData> m_vtDefaultLimitData;
    HoverBorderIcon* m_resetBtn = nullptr;
    HoverBorderIcon* m_addBtn = nullptr;

    friend class AccelerationAndSpeedLimitDialog;
};

class AccelerationAndSpeedLimitDialog : public DPIDialog
{
    AccelerationAndSpeedLimitPanel* m_panel;

public:
    AccelerationAndSpeedLimitDialog(const std::string& type, const wxString& title, wxWindow* parent);

    void build_dialog(bool bDefaultCheckbox, const ConfigOptionString& defaultData, bool bCheckbox, const ConfigOptionString& data);
    bool getCheckbox();
    const std::string& getData();

    void on_dpi_changed(const wxRect &suggested_rect) override;

private:
    std::string m_type;
    std::string m_outData;
    bool m_bCheckbox = false;
};

} // GUI
} // Slic3r


#endif  /* slic3r_BedShapeDialog_hpp_ */
