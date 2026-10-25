#include "SendToPrinter.hpp"

#include "slic3r/GUI/I18N.hpp"
#include "slic3r/GUI/wxExtensions.hpp"
#include "slic3r/GUI/GUI_App.hpp"
#include "slic3r/GUI/LinuxDisplayBackend.hpp"
#include "slic3r/GUI/MainFrame.hpp"
#include "libslic3r_version.h"

#include <regex>
#include <string>
#include <set>
#include <wx/sizer.h>
#include <wx/string.h>
#include <wx/toolbar.h>
#include <wx/textdlg.h>
#include <locale>
#include <codecvt>

#include <slic3r/GUI/Widgets/WebView.hpp>
#include <wx/webview.h>
#include "slic3r/GUI/print_manage/RemotePrinterManager.hpp"
#include <boost/beast/core/detail/base64.hpp>

#include <wx/stdpaths.h>
#include "slic3r/GUI/print_manage/utils/cxmdns.h"
#include "slic3r/GUI/print_manage/Utils.hpp"
#include "wx/event.h"
#include "slic3r/GUI/Plater.hpp"
#include "slic3r/GUI/PartPlate.hpp"
#include "slic3r/GUI/PrinterCoverRoute.hpp"
#include "libslic3r/Print.hpp"
#include <wx/variant.h>
#include <wx/datstrm.h>
#include "../data/DataCenter.hpp"
#include "../AppMgr.hpp"
#include "../AppUtils.hpp"
#include "slic3r/GUI/Notebook.hpp"
#include "cereal/external/base64.hpp"
#include "slic3r/GUI/simple/printAdvisor/PrintAdvisorPageBridge.hpp" // PRINT-ADVISOR advisor-hook
#include "libslic3r/Time.hpp"

#include <boost/uuid/uuid.hpp>
#include <boost/uuid/uuid_generators.hpp>
#include <boost/uuid/uuid_io.hpp>
#include <nlohmann/json.hpp>
#include <memory>
#include <utility>
#if defined(__linux__) || defined(__LINUX__)
#include "video/WebRTCDecoder.h"
#endif
#include "buildinfo.h"
#if ENABLE_FFMPEG
#include "video/RTSPDecoder.h"
#endif
#include "libslic3r/common_header/common_header.h"
namespace Slic3r {
namespace GUI {
namespace pt = boost::property_tree;

struct SendToPrinterUiGate {
    CxSentToPrinterDialog* dialog = nullptr;
    bool accepting_callbacks = true;
    std::uint64_t generation = 0;
};

namespace {

void append_generated_filament_mapping(nlohmann::json& plate_json, const GCodeProcessorResult* result)
{
    const bool mapping_present = result != nullptr && result->generated_filament_map_present;

    plate_json["filament_map"] = mapping_present ? nlohmann::json(result->generated_filament_map) :
                                                   nlohmann::json::array();
    plate_json["filament_map_mode"] = result != nullptr ? result->generated_filament_map_mode : std::string();
    plate_json["filament_map_present"] = mapping_present;
}

template <typename Action>
void post_to_send_dialog(const std::weak_ptr<SendToPrinterUiGate>& weak_gate,
                         Action&& action,
                         std::uint64_t expected_generation = 0)
{
    wxGetApp().CallAfter([weak_gate,
                          action = std::forward<Action>(action),
                          expected_generation]() mutable {
        const auto gate = weak_gate.lock();
        if (!gate || !gate->accepting_callbacks || gate->dialog == nullptr ||
            (expected_generation != 0 && gate->generation != expected_generation) ||
            gate->dialog->IsBeingDeleted()) {
            return;
        }

        action(*gate->dialog);
    });
}

nlohmann::json gcode_file_size_bytes(const std::string& file_path)
{
    if (file_path.empty())
        return nullptr;

    boost::system::error_code ec;
    const boost::filesystem::path path(file_path);
    if (!boost::filesystem::is_regular_file(path, ec) || ec)
        return nullptr;

    const auto file_size = boost::filesystem::file_size(path, ec);
    if (ec)
        return nullptr;

    return static_cast<std::uint64_t>(file_size);
}

} // namespace


CxSentToPrinterDialog::CxSentToPrinterDialog(Plater *plater,
                                             SendType sendtype,std::string mapString)
    : DPIDialog(wxGetApp().mainframe,
                wxID_ANY,
                _L("Send to Lan Printer"),
                wxDefaultPosition,
                wxDefaultSize,
                // Linux 下不需要最大/最小化按钮：移除可调尺寸边框
                // 其他平台保持原行为（可调整大小）
                #if defined(__linux__) || defined(__LINUX__) || defined(__WXGTK__)
                wxCAPTION | wxCLOSE_BOX
                #else
                wxCAPTION | wxCLOSE_BOX | wxRESIZE_BORDER
                #endif
                ), m_sendtype(sendtype),m_mapString(mapString)
    , m_plater(plater)
{
    m_ui_gate = std::make_shared<SendToPrinterUiGate>();
    m_ui_gate->dialog = this;

    // 双保险：即使窗口管理器默认添加，也在 Linux 下移除最小/最大化按钮
    #if defined(__linux__) || defined(__LINUX__) || defined(__WXGTK__)
    SetWindowStyleFlag(GetWindowStyleFlag() & ~(wxMINIMIZE_BOX | wxMAXIMIZE_BOX));
    #endif
    #ifdef __WINDOWS__
    SetDoubleBuffered(true);
#endif //__WINDOWS__

    wxGetApp().UpdateDlgDarkUI(this);

    wxSize minSize = wxSize(FromDIP(1170), FromDIP(500)); // 设置最小尺寸
    wxSize initialSize = wxSize(FromDIP(1170), FromDIP(650));

    SetMinSize(minSize);
    SetSize(initialSize);
    // 将窗体移动到屏幕顶部
    Bind(wxEVT_SHOW, [this](wxShowEvent& event) {
        if (event.IsShown()) {
#if defined(__WXGTK__)
            // Wayland top-level positions do not include reliable decoration offsets.
            // Applying the X11 adjustment would shrink the dialog by the title-bar height.
            if (is_running_on_wayland()) {
                wxSize newSize = wxSize(FromDIP(1170), FromDIP(700));
                SetSize(newSize);
                event.Skip();
                return;
            }
#endif

            wxPoint position = GetPosition();
            if (position.y < 0 && abs(position.y)<20)
            {
                wxSize newSize = wxSize(FromDIP(1170), FromDIP(650) + position.y);
                SetSize(newSize);
                SetPosition(wxPoint(position.x, GetParent()->GetPosition().y));
            }
                
        }
        event.Skip();
    });

    // icon
    std::string icon_path = (boost::format("%1%/images/%2%.ico") % resources_dir() % Slic3r::CxBuildInfo::getIconName()).str();
    SetIcon(wxIcon(encode_path(icon_path.c_str()), wxBITMAP_TYPE_ICO));

    wxBoxSizer* topsizer = new wxBoxSizer(wxVERTICAL);
    topsizer->SetMinSize(FromDIP(1170), FromDIP(600));

    // Create the webview
    m_browser = WebView::CreateWebView(this, "");
    if (m_browser == nullptr) {
        wxLogError("Could not init m_browser");
        return;
    }
#if defined(__linux__)
    WebView::ConfigureHardwareAccelerationForMjpeg(m_browser);
#endif

    bind_events();

    SetSizer(topsizer);

    topsizer->Add(m_browser, 1, wxEXPAND | wxALL, 0);
    std::string version = std::string(CREALITYPRINT_VERSION);
    std::string os      = wxGetOsDescription().ToStdString();
    std::string type    = std::string(PROJECT_VERSION_EXTRA);
    int port = wxGetApp().get_server_port();
    wxString url = wxString::Format("%s/web/sendToPrinterPage/index.html?version=%s&port=%d&sendtype=%d&os=%s&type=%s",
                                    from_u8(resources_dir()), version, port, (int) m_sendtype, os, type);
    url.Replace(wxT("\\"), wxT("/"));
    url.Replace(wxT("#"), wxT("%23"));
    wxURI uri(url);
    wxString encodedUrl = uri.BuildURI();
    encodedUrl = wxT("file://")+encodedUrl;
    this->load_url(encodedUrl);
    m_browser->EnableAccessToDevTools();

    if (m_plater)
    {
        update_send_page_content();
    }

    wxGetApp().mainframe->get_printer_mgr_view()->RequestDeviceListFromDB();

    CenterOnParent();
    DM::AppMgr::Ins().Register(m_browser, "SentToPrinter");
}

CxSentToPrinterDialog::~CxSentToPrinterDialog() 
{
    invalidate_ui_gate();
    DM::AppMgr::Ins().UnRegister(m_browser);
    restore_extruder_colors();
    UnregisterHandler("register_complete");
    UnregisterHandler("request_onlygcode_plate_data_on_show");
    UnregisterHandler("send_gcode");
    UnregisterHandler("send_3mf");
    UnregisterHandler("cancel_send");
    UnregisterHandler("send_start_print_cmd");
    UnregisterHandler("request_update_plate_thumbnail");
    UnregisterHandler("forward_device_detail");
    UnregisterHandler("refresh_all_device");
    UnregisterHandler("get_threeMF");
    UnregisterHandler("get_machine_list");
    UnregisterHandler("save_user_operation_state");
    UnregisterHandler("request_user_operation_state");

}
void CxSentToPrinterDialog::OnCloseWindow(wxCloseEvent& event)
{
    if (m_close_allowed || m_uploadingIp.IsEmpty()) {
        // need to reopen the detail-page Video when close the send page
        wxGetApp().mainframe->get_printer_mgr_view()->request_reopen_detail_video();
        m_close_allowed = true;
        event.Skip();
        return;
    }

    if (!event.CanVeto()) {
        m_close_allowed = true;
        const std::string task_id = m_upload_task_id;
        const wxString uploading_ip = m_uploadingIp;
        auto& upload_manager = RemotePrint::RemotePrinterManager::getInstance();
        if (!task_id.empty())
            upload_manager.cancelUploadTask(task_id);
        else
            upload_manager.cancelUpload(uploading_ip.ToStdString());
        event.Skip();
        return;
    }

    event.Veto();
    if (m_close_pending)
        return;

    m_close_pending = true;
    m_send_was_canceled = true;

    auto& upload_manager = RemotePrint::RemotePrinterManager::getInstance();
    if (!m_upload_task_id.empty())
        upload_manager.cancelUploadTask(m_upload_task_id);
    else
        upload_manager.cancelUpload(m_uploadingIp.ToStdString());
}

void CxSentToPrinterDialog::close_after_upload_stopped(bool invalidate_cancelled_generation)
{
    m_uploadingIp.clear();
    m_upload_task_id.clear();
    if (invalidate_cancelled_generation && m_ui_gate)
        ++m_ui_gate->generation;
    if (!m_close_pending || IsBeingDeleted())
        return;

    // The upload future has completed, so protocol objects and their source-file
    // handles have been released. A second close event may now leave the modal dialog.
    m_close_allowed = true;
    Close(false);
}
void CxSentToPrinterDialog::bind_events()
{
    Bind(wxEVT_CLOSE_WINDOW, &CxSentToPrinterDialog::OnCloseWindow, this);
    if(m_browser)
    {
        m_browser->Bind(wxEVT_WEBVIEW_ERROR, &CxSentToPrinterDialog::OnError, this);
        Bind(wxEVT_WEBVIEW_SCRIPT_MESSAGE_RECEIVED, &CxSentToPrinterDialog::OnScriptMessage, this, m_browser->GetId());
    }

    RegisterHandler("register_complete", [this](const nlohmann::json& json_data) {
        this->handle_register_complete(json_data);
    });

    RegisterHandler("request_onlygcode_plate_data_on_show", [this](const nlohmann::json& json_data) {
        wxString strJS = wxString::Format("window.handleStudioCmd('%s');", from_u8(get_onlygcode_plate_data_on_show()));
        run_script(strJS.ToStdString());
    });

    RegisterHandler("send_gcode", [this](const nlohmann::json& json_data) {
        this->handle_send_gcode(json_data);
    });

    RegisterHandler("send_3mf", [this](const nlohmann::json& json_data) {
        this->handle_send_3mf(json_data);
    });

    RegisterHandler("cancel_send", [this](const nlohmann::json& json_data) { 
        this->handle_cancel_send(json_data); 
    });

    RegisterHandler("send_start_print_cmd", [this](const nlohmann::json& json_data) {
        this->handle_send_start_print_cmd(json_data);
    });

    RegisterHandler("start_heartbeat_cmd", [this](const nlohmann::json& json_data) {
        this->handle_start_heartbeat_cmd(json_data);
    });
    RegisterHandler("stop_heartbeat_cmd", [this](const nlohmann::json& json_data) {
        this->handle_stop_heartbeat_cmd(json_data);
    });
    RegisterHandler("set_video_elapse_cmd", [this](const nlohmann::json& json_data) {
        this->handle_set_video_elapse_cmd(json_data);
    });

    RegisterHandler("set_error_cmd", [this](const nlohmann::json& json_data) { this->handle_set_error_cmd(json_data); });

    RegisterHandler("save_user_operation_state", [this](const nlohmann::json& json_data) {
        this->handle_save_user_operation_state(json_data);
    });

    RegisterHandler("request_user_operation_state", [this](const nlohmann::json& json_data) {
        this->handle_request_user_operation_state(json_data);
    });

    RegisterHandler("request_update_plate_thumbnail", [this](const nlohmann::json& json_data) {
        this->handle_request_update_plate_thumbnail(json_data);
    });

    RegisterHandler("refresh_all_device", [](const nlohmann::json&) {
        if (wxGetApp().mainframe) {
            wxGetApp().mainframe->refresh_device_page();
        }
    });

    RegisterHandler("get_threeMF", [this](const nlohmann::json& json_data) {
        wxString project_name = Slic3r::GUI::wxGetApp().plater()->get_project_name();

        boost::property_tree::wptree req;
        req.put(L"command", L"get_threeMF");

        boost::property_tree::wptree item;
        item.put(L"project_name", project_name);

        req.put_child(L"data", item);

        std::wostringstream oss;
        pt::write_json(oss, req, false);
        WebView::RunScript(m_browser, wxString::Format("window.handleStudioCmd(%s)", oss.str()));
        });

    RegisterHandler("get_machine_list", [this](const nlohmann::json& json_data) {
        load_machine_preset_data();
    });
}

void CxSentToPrinterDialog::RegisterHandler(const std::string& command, std::function<void(const nlohmann::json&)> handler)
{
    m_commandHandlers[command] = handler;
}
void CxSentToPrinterDialog::UnregisterHandler(const std::string& command)
{
    m_commandHandlers.erase(command);
}

void CxSentToPrinterDialog::OnScriptMessage(wxWebViewEvent& evt)
{
    try {
        wxString strInput = evt.GetString();
        BOOST_LOG_TRIVIAL(trace) << "DeviceDialog::OnScriptMessage;OnRecv:" << strInput.c_str();
        json     j = json::parse(strInput);
        wxString strCmd = j["command"].get<std::string>();
        BOOST_LOG_TRIVIAL(trace) << "DeviceDialog::OnScriptMessage;Command:" << strCmd;
        if(strCmd == "forward_device_detail"){
            wxPostEvent(this, wxCloseEvent(wxEVT_CLOSE_WINDOW));
        }else if(strCmd == "switch_webrtc_source")
        {
            bool isOrderPrinter = j["isOrderPrinter"].get<bool>();
            if(isOrderPrinter)
            {
                // For order printer, we use the webrtc local url
                #if ENABLE_FFMPEG
                RTSPDecoder::GetInstance()->stopPlay(); 
                #endif
            }
            else
            {
                #if defined(__linux__) || defined(__LINUX__)
                    WebRTCDecoder::GetInstance()->stopPlay();
                #endif
            }

        } else if (strCmd == "common_openurl") {
            // SysRoutes (AppMgr::Invoke below) opens the URL
            wxPostEvent(this, wxCloseEvent(wxEVT_CLOSE_WINDOW));
        }else if (strCmd == "common_open_wiki_url") {
            wxLaunchDefaultBrowser(j["url"].get<std::string>());
        }
        
        if (DM::AppMgr::Ins().Invoke(m_browser, evt.GetString().ToUTF8().data()))
        {
            return;
        }

        

        

        if (m_commandHandlers.find(strCmd.ToStdString()) != m_commandHandlers.end()) {
            m_commandHandlers[strCmd.ToStdString()](j);
        } else {
            BOOST_LOG_TRIVIAL(trace) << "CxSentToPrinterDialog::OnScriptMessage;Unknown Command:" << strCmd;
        }

    } catch (std::exception &e) {
       // wxMessageBox(e.what(), "json Exception", MB_OK);
        BOOST_LOG_TRIVIAL(trace) << "DeviceDialog::OnScriptMessage;Error:" << e.what();
    }
    

}

/**
 *  Handle the "send_start_print_cmd" command from the sendPage browser, 
 *  and then send the request to the printerMgrView
 */
void CxSentToPrinterDialog::handle_send_start_print_cmd(const nlohmann::json& json_data)
{
    // create command to send to the webview
    nlohmann::json commandJson;
    commandJson["command"] = "send_print_cmd";
    commandJson["data"]    = json_data["data"].dump(-1, ' ', true);

    wxGetApp().mainframe->get_printer_mgr_view()->ExecuteScriptCommand(RemotePrint::Utils::url_encode(commandJson.dump(-1, ' ', true)));
}
void CxSentToPrinterDialog::handle_set_error_cmd(const nlohmann::json& json_data)
{
    // create command to send to the webview
    nlohmann::json commandJson;
    commandJson["command"] = "set_error_cmd";
    commandJson["data"]    = json_data["data"].dump();

    wxGetApp().mainframe->get_printer_mgr_view()->ExecuteScriptCommand(RemotePrint::Utils::url_encode(commandJson.dump()));
}



void CxSentToPrinterDialog::post_notify_event(const std::vector<int>& plate_extruders, const std::vector<std::string>& extruder_match_colors, bool bUpdateSelf)
{
    wxVariantList str_variantList;
    for (const auto& str : extruder_match_colors) {
        str_variantList.Append(new wxVariant(str));
    }
    wxVariant str_variant(str_variantList);

    wxVariantList int_variantList;
    for (const auto& i : plate_extruders) {
        int_variantList.Append(new wxVariant(i));
    }
    wxVariant int_variant(int_variantList);

    wxVariantList variantList;
    variantList.Append(&str_variant);
    variantList.Append(&int_variant);

    wxCommandEvent notifyEvent(EVT_NOTIFY_PLATE_THUMBNAIL_UPDATE);
    notifyEvent.SetClientData(new wxVariant(variantList));

    //wxPostEvent(wxGetApp().plater(), notifyEvent);
    wxGetApp().plater()->update_plate_thumbnail(notifyEvent);//Optimize this code in the future

    if(bUpdateSelf)
        update_plate_preview_img_on_send_page();
}

void CxSentToPrinterDialog::handle_request_update_plate_thumbnail(const nlohmann::json& json_data)
{
    std::vector<int> plate_extruders;
    std::vector<std::string> extruder_match_colors;

    if (m_plater->model().objects.size() == 0)
        return;

    m_request_color_match_plateIndex = json_data["plateIndex"];

    // 遍历 matchInfo 数组，解析 extruderId 和 matchColor
    for (const auto& matchInfo : json_data["matchInfo"])
    {
        int extruderId = matchInfo["extruderId"];
        std::string matchColor = matchInfo["matchColor"];

        plate_extruders.emplace_back(extruderId);
        extruder_match_colors.emplace_back(matchColor);
    }


    post_notify_event(plate_extruders, extruder_match_colors);
}

void CxSentToPrinterDialog::update_plate_preview_img_on_send_page()
{
    std::string str_val = get_updated_plate_preview_img(m_request_color_match_plateIndex);
    if(str_val.empty())
        return;

    wxString strJS = wxString::Format("window.handleStudioCmd('%s');", str_val);

    run_script(strJS.ToStdString());
}

void CxSentToPrinterDialog::handle_send_3mf(const nlohmann::json& json_data)
{
    if (m_close_pending)
        return;

    if(!m_plater)
        return;

    int      plateIndex = json_data["printPlateIndex"];  // which plate to print
    wxString ipAddress  = json_data["ipAddress"].get<std::string>();
    std::string upload3mfName = json_data["upload3mfName"];
    bool secureConnection = json_data.value("secureConnection", false);

    RemotePrint::RemotePrinterManager::getInstance().setSecureConnectionMap(ipAddress.ToStdString(), secureConnection);

    std::string tmp_3mf_path = "";
    if (m_plater->only_gcode_mode())
    {
        tmp_3mf_path = m_plater->get_last_loaded_3mf().string();
    } 
    else 
    {
        boost::filesystem::path temp_path(m_plater->model().get_backup_path("Metadata"));
        temp_path /= (boost::format(".%1%.%2%_upload.3mf") % get_current_pid() % plateIndex).str();

        tmp_3mf_path = temp_path.string();

        // 不传 plate_idx（用默认值 -1 = PLATE_CURRENT_IDX），让 store_to_3mf_structure
        // 把所有盘的 gcode 都打进 3mf 包（与"导出所有切片文件"行为一致）。
        // export_3mf 内部会为每个盘共享同一时间戳生成 task_id 并写入对应 gcode。
        int result = m_plater->export_3mf(tmp_3mf_path, SaveStrategy::UploadToPrinter);
        if (result < 0) {
            BOOST_LOG_TRIVIAL(warning) << "handle_send_3mf: export_3mf failed";
            return;
        }
    }

    if (tmp_3mf_path.empty())
        return;

    if (!m_ui_gate)
        return;
    const std::uint64_t upload_generation = ++m_ui_gate->generation;
    const std::weak_ptr<SendToPrinterUiGate> weak_ui_gate = m_ui_gate;

    m_uploadingIp = ipAddress;
    m_upload_task_id = RemotePrint::RemotePrinterManager::getInstance().pushUploadTasks(
        ipAddress.ToStdString(), upload3mfName, tmp_3mf_path,
        [weak_ui_gate, upload_generation](std::string ip, float progress,double speed) {
            nlohmann::json top_level_json;
            top_level_json["printer_ip"] = ip;
            top_level_json["progress"]   = progress;
            top_level_json["speed"]   = speed;
            std::string json_str = top_level_json.dump();

            // create command to send to the webview
            nlohmann::json commandJson;
            commandJson["command"] = "display_upload_progress";
            commandJson["data"]    = RemotePrint::Utils::url_encode(json_str);

            wxString strJS = wxString::Format("window.handleStudioCmd('%s');", RemotePrint::Utils::url_encode(commandJson.dump(-1, ' ', true)));

            post_to_send_dialog(weak_ui_gate,
                [strJS](CxSentToPrinterDialog& dialog) {
                    dialog.run_script_if_ready(strJS);
                },
                upload_generation);
        },
        [weak_ui_gate, upload_generation](std::string ip, int statusCode) {
            nlohmann::json top_level_json;
            top_level_json["printer_ip"]  = ip;
            top_level_json["status_code"] = statusCode;

            std::string json_str = top_level_json.dump();

            // create command to send to the webview
            nlohmann::json commandJson;
            commandJson["command"] = "notify_upload_status";
            commandJson["data"]    = RemotePrint::Utils::url_encode(json_str);

            wxString strJS = wxString::Format("window.handleStudioCmd('%s');", RemotePrint::Utils::url_encode(commandJson.dump(-1, ' ', true)));
            
            post_to_send_dialog(weak_ui_gate,
                [strJS, statusCode](CxSentToPrinterDialog& dialog) {
                    dialog.run_script_if_ready(strJS);
                    dialog.close_after_upload_stopped(statusCode == 601);
                },
                upload_generation);
        }, [weak_ui_gate, upload_generation](std::string ip, std::string body) {
            nlohmann::json commandJson;
            commandJson["command"] = "notify_send_complete";
            wxString strJS = wxString::Format("window.handleStudioCmd('%s');", RemotePrint::Utils::url_encode(commandJson.dump(-1, ' ', true)));

            post_to_send_dialog(weak_ui_gate, [strJS](CxSentToPrinterDialog& dialog) {
                try
                    {
                        if (dialog.m_browser != nullptr && !dialog.m_browser->IsBeingDeleted() && !dialog.m_browser->IsBusy()) {
                            dialog.run_script(strJS.ToStdString());
                        }
                    }
                    catch (...)
                    {
                    }
                },
                upload_generation);
        });
}
void CxSentToPrinterDialog::handle_cancel_send(const nlohmann::json& json_data) {
    int      plateIndex = json_data["plateIndex"];
    wxString ipAddress  = json_data["ipAddress"].get<std::string>();
    m_send_was_canceled = true;
    auto& upload_manager = RemotePrint::RemotePrinterManager::getInstance();
    if (!m_upload_task_id.empty()) {
        BOOST_LOG_TRIVIAL(warning) << "[UploadCancel] cancel task " << m_upload_task_id;
        upload_manager.cancelUploadTask(m_upload_task_id);
    } else {
        BOOST_LOG_TRIVIAL(warning) << "[UploadCancel] cancel arrived before task creation, address="
                                   << ipAddress.ToStdString();
        upload_manager.cancelUpload(ipAddress.ToStdString());
    }
}
void CxSentToPrinterDialog::handle_start_heartbeat_cmd(const nlohmann::json& json_data) {
    // create command to send to the webview
    nlohmann::json commandJson;
    commandJson["command"] = "start_heartbeat_cmd";
    commandJson["data"]    = json_data["data"].dump(-1, ' ', true);

    wxGetApp().mainframe->get_printer_mgr_view()->ExecuteScriptCommand(RemotePrint::Utils::url_encode(commandJson.dump(-1, ' ', true)));
}
void CxSentToPrinterDialog::handle_stop_heartbeat_cmd(const nlohmann::json& json_data) {
     // create command to send to the webview
    nlohmann::json commandJson;
    commandJson["command"] = "stop_heartbeat_cmd";
    commandJson["data"]    = json_data["data"].dump(-1, ' ', true);

    wxGetApp().mainframe->get_printer_mgr_view()->ExecuteScriptCommand(RemotePrint::Utils::url_encode(commandJson.dump(-1, ' ', true)));
}

void CxSentToPrinterDialog::handle_set_video_elapse_cmd(const nlohmann::json& json_data) {
    // create command to send to the webview
    nlohmann::json commandJson;
    commandJson["command"] = "set_video_elapse_cmd";
    commandJson["data"]    = json_data["data"].dump(-1, ' ', true);

    wxGetApp().mainframe->get_printer_mgr_view()->ExecuteScriptCommand(RemotePrint::Utils::url_encode(commandJson.dump(-1, ' ', true)));
}

void CxSentToPrinterDialog::handle_register_complete(const nlohmann::json& json_data)
{
    if (m_plater) {
        update_send_page_content();
    }
}


void CxSentToPrinterDialog::handle_send_gcode(const nlohmann::json& json_data) 
{
    if (m_close_pending)
        return;

    int      plateIndex = json_data["plateIndex"];
    wxString ipAddress  = json_data["ipAddress"].get<std::string>();
    std::string uploadName = json_data["uploadName"];  // convert from wxString to std::string would cause exception
    bool oldPrinter = json_data["oldPrinter"];
    int  moonrakerPort = json_data["moonrakerPort"];
    bool secureConnection = json_data.value("secureConnection", false);
    RemotePrint::RemotePrinterManager::getInstance().setSecureConnectionMap(ipAddress.ToStdString(), secureConnection);
    std::string uploadTaskId;
    if (json_data.contains("uploadTaskId") && json_data["uploadTaskId"].is_string())
        uploadTaskId = json_data["uploadTaskId"].get<std::string>();
    const int device_type = json_data.value("deviceType", -1);
    const bool is_wan_old_printer = oldPrinter && device_type == 1;
    const bool is_wan_upload = is_wan_old_printer ||
                               (!oldPrinter && ipAddress.ToStdString().find('.') == std::string::npos);

    if (oldPrinter && !is_wan_old_printer)
    {
        std::string strIpAddr = ipAddress.ToStdString();
        RemotePrint::RemotePrinterManager::getInstance().setOldPrinterMap(strIpAddr);
    }

    if (moonrakerPort > 0)
    {
        std::string strIpAddr = ipAddress.ToStdString();
        if (strIpAddr.find('(') != std::string::npos)
        {
           RemotePrint::RemotePrinterManager::getInstance().setKlipperPrinterMap(strIpAddr, moonrakerPort);
        }
    }

    PartPlate* plate = wxGetApp().plater()->get_partplate_list().get_plate(plateIndex);
    if (plate) {

        if (!m_ui_gate)
            return;
        const std::uint64_t upload_generation = ++m_ui_gate->generation;
        const std::weak_ptr<SendToPrinterUiGate> weak_ui_gate = m_ui_gate;

        {
            // Upstream sends only to devices known to the data center (the check sat in its analytics block).
            auto device = DM::DataCenter::Ins().get_printer_data(ipAddress.ToStdString());
            if (!device.valid || device.mac.empty()) {
                BOOST_LOG_TRIVIAL(warning) << __FUNCTION__ << ": Invalid device data for IP=" << ipAddress.ToStdString();
                return;
            }
        }

        std::string gcodeFilePath;
        if (m_plater->only_gcode_mode()) {

            GCodeProcessorResult* plate_gcode_result = plate->get_slice_result();
            if (plate_gcode_result)
            {
                gcodeFilePath = plate_gcode_result->filename;
            }

            if (gcodeFilePath.empty())
                return;

        }
        else{
            gcodeFilePath = _L(plate->get_tmp_gcode_path()).ToUTF8();
        }
        
        // Release GCodeViewer's mapped_file_source (same as export_3mf flow)
        m_plater->get_preview_canvas3D()->get_gcode_viewer().release_gcode_file_mapping();
        
        m_uploadingIp = ipAddress;
        m_upload_task_id = RemotePrint::RemotePrinterManager::getInstance().pushUploadTasks(
            ipAddress.ToStdString(), uploadName, gcodeFilePath,
            [weak_ui_gate, upload_generation, uploadTaskId, is_wan_upload](std::string ip, float progress,double speed) {

                nlohmann::json top_level_json;
                top_level_json["printer_ip"] = ip;
                top_level_json["progress"]  = progress;
                top_level_json["speed"]  = round(speed);
                if (is_wan_upload && !uploadTaskId.empty())
                    top_level_json["uploadTaskId"] = uploadTaskId;

                std::string json_str = top_level_json.dump();

                // create command to send to the webview
                nlohmann::json commandJson;
                commandJson["command"] = "display_upload_progress";
                commandJson["data"]    = RemotePrint::Utils::url_encode(json_str);

                wxString strJS = wxString::Format("window.handleStudioCmd('%s');", RemotePrint::Utils::url_encode(commandJson.dump(-1, ' ', true)));

                post_to_send_dialog(weak_ui_gate,
                    [strJS](CxSentToPrinterDialog& dialog) {
                        dialog.run_script_if_ready(strJS);
                    },
                    upload_generation);
            },
            [weak_ui_gate, upload_generation, uploadTaskId, is_wan_upload](std::string ip, int statusCode) {
                nlohmann::json top_level_json;
                top_level_json["status_code"]  = statusCode;
                if (is_wan_upload && !uploadTaskId.empty())
                    top_level_json["uploadTaskId"] = uploadTaskId;
                std::string json_str = top_level_json.dump();
                
                nlohmann::json commandJson;
                commandJson["command"] = "notify_upload_status";
                commandJson["data"] = RemotePrint::Utils::url_encode(json_str);
                wxString strJS = wxString::Format("window.handleStudioCmd('%s');", RemotePrint::Utils::url_encode(commandJson.dump(-1, ' ', true)));
                
                post_to_send_dialog(weak_ui_gate,
                    [strJS, statusCode](CxSentToPrinterDialog& dialog) {
                        dialog.run_script_if_ready(strJS);
                        dialog.close_after_upload_stopped(statusCode == 601);
                    },
                    upload_generation);
            },
            [weak_ui_gate, upload_generation, uploadTaskId](std::string ip, std::string body){
                int deviceType = 0;//local device
                int statusCode = 1;
                json jBody = json::parse(body);
                if (jBody.contains("code") && jBody["code"].is_number_integer()) {
                    statusCode = jBody["code"];
                }

                nlohmann::json top_level_json;
                top_level_json["status_code"] = statusCode;
                top_level_json["id"] = "";
                top_level_json["name"] = "";
                top_level_json["type"] = "";
                top_level_json["filekey"] = "";
                if(jBody.contains("result") && jBody["result"].contains("list") &&jBody["result"]["list"].size()>=0){
                    deviceType = 1;//CX device
                    if(jBody["result"]["list"][0].contains("id"))top_level_json["id"]=jBody["result"]["list"][0]["id"];
                    if(jBody["result"]["list"][0].contains("name"))top_level_json["name"]=jBody["result"]["list"][0]["name"];
                    if(jBody["result"]["list"][0].contains("type"))top_level_json["type"]=jBody["result"]["list"][0]["type"];
                    if(jBody["result"]["list"][0].contains("filekey"))top_level_json["filekey"]=jBody["result"]["list"][0]["filekey"];
                    if (!uploadTaskId.empty())
                        top_level_json["uploadTaskId"] = uploadTaskId;
                }
                

                std::string json_str;
                if(1 == deviceType)
                { json_str = top_level_json.dump(-1, ' ', true);}

                nlohmann::json commandJson;
                commandJson["command"] = "notify_send_complete";
                commandJson["data"] = RemotePrint::Utils::url_encode(json_str);

                wxString strJS = wxString::Format("window.handleStudioCmd('%s');", RemotePrint::Utils::url_encode(commandJson.dump(-1, ' ', true)));

                post_to_send_dialog(weak_ui_gate, [strJS](CxSentToPrinterDialog& dialog) {
                    try
                    {
                        if (dialog.m_browser != nullptr && !dialog.m_browser->IsBeingDeleted() && !dialog.m_browser->IsBusy()) {
                            dialog.run_script(strJS.ToStdString());
                        }
                    }
                    catch (...)
                    {
                    }
                },
                upload_generation);
            });
    }
}

void CxSentToPrinterDialog::load_url(const wxString& url)
{
    if (m_browser == nullptr)
        return;
    m_browser->LoadURL(url);
    //m_browser->SetFocus();
    UpdateState();
}

void CxSentToPrinterDialog::restore_extruder_colors()
{
    if (m_plater->model().objects.size() == 0)
        return;
    if(m_backup_extruder_colors.size() == 0)
        return;
    
    std::vector<int> plate_extruders;
    for(int i = 0; i < m_backup_extruder_colors.size(); i++)
    {
        plate_extruders.emplace_back(i+1);
    }
    
    post_notify_event(plate_extruders, m_backup_extruder_colors, false);
}

bool CxSentToPrinterDialog::Show(bool show)
{
    return wxDialog::Show(show);
}

std::string CxSentToPrinterDialog::get_updated_plate_preview_img(int plateIndex)
{
    if (wxGetApp().plater()->only_gcode_mode())
        return "";

    PartPlate* plate = wxGetApp().plater()->get_partplate_list().get_plate(plateIndex);
    if (!plate)
        return "";

    if (plate && plate->thumbnail_data.is_valid()) 
    {
        wxImage image(plate->thumbnail_data.width, plate->thumbnail_data.height);
        image.InitAlpha();
        for (unsigned int r = 0; r < plate->thumbnail_data.height; ++r) {
            unsigned int rr = (plate->thumbnail_data.height - 1 - r) * plate->thumbnail_data.width;
            for (unsigned int c = 0; c < plate->thumbnail_data.width; ++c) {
                unsigned char* px = (unsigned char*) plate->thumbnail_data.pixels.data() + 4 * (rr + c);
                image.SetRGB((int) c, (int) r, px[0], px[1], px[2]);
                image.SetAlpha((int) c, (int) r, px[3]);
            }
        }

        wxImage resized_image = image.Rescale(256, 256, wxIMAGE_QUALITY_HIGH);

        wxMemoryOutputStream mem_stream;
        if (!resized_image.SaveFile(mem_stream, wxBITMAP_TYPE_PNG)) {
            wxLogError("Failed to save image to memory stream.");
        }

        auto size = mem_stream.GetSize();
        auto imgdata = new unsigned char[size];
        mem_stream.CopyTo(imgdata, size);

        std::size_t encoded_size = boost::beast::detail::base64::encoded_size(size);
        std::string img_base64_data(encoded_size, '\0');
        boost::beast::detail::base64::encode(&img_base64_data[0], imgdata, size);

        nlohmann::json top_level_json;
        top_level_json["image"]       = "data:image/png;base64," + std::move(img_base64_data);
        top_level_json["plate_index"] = plateIndex;

        std::string json_str = top_level_json.dump();

        // create command to send to the webview
        nlohmann::json commandJson;
        commandJson["command"] = "update_plate_preview_img";
        commandJson["data"]    = RemotePrint::Utils::url_encode(json_str);

        std::string commandStr = commandJson.dump();

        return RemotePrint::Utils::url_encode(commandStr);
    }

    return "";
}
std::string CxSentToPrinterDialog::get_onlygcode_plate_data_on_show()
{

    nlohmann::json           colors_json     = nlohmann::json::array();
    nlohmann::json filament_types_json = nlohmann::json::array();
    nlohmann::json json_array = nlohmann::json::array();
    nlohmann::json json_data;

    std::string printer_model = "";
    std::string printer_name = "";
    float       nozzle_diameter = 0.0f;

    m_backup_extruder_colors.clear();
    std::string project_name = wxGetApp().plater()->get_preview_only_filename();
    std::regex  pattern(R"(.*\.(gcode))", std::regex::icase);
    m_is_only_gcode_mode = std::regex_match(project_name, pattern);

    for (int i = 0; i < wxGetApp().plater()->get_partplate_list().get_plate_count(); i++) 
    {
        PartPlate* plate = wxGetApp().plater()->get_partplate_list().get_plate(i);
        if (!plate) 
        {
            continue;
        }

        GCodeProcessorResult* current_result = plate->get_slice_result();
        
        if (!current_result->filename.empty() && current_result->image_data.size() > 0) 
        {
            printer_model = current_result->printer_model;
            printer_name = current_result->printer_settings_id;
            nozzle_diameter = current_result->nozzle_diameter;
            int color_index = 0;
            for (auto color : current_result->creality_default_extruder_colors) {
                if(colors_json.size()<=color_index)
                {
                    colors_json.push_back(color);
                    m_backup_extruder_colors.push_back(color);
                }
                else
                {
                    if(color!="")
                    {
                        colors_json[color_index] = color;
                        m_backup_extruder_colors[color_index] = color;
                    }
                        
                }
                    
                color_index++;
            }
            // filament_types_json
            if(filament_types_json.size() ==0)
            {
                for (auto filament_type : current_result->creality_extruder_types) {
                    filament_types_json.push_back(filament_type);
                }
            }
            int            size    = 0;
            unsigned char* imgdata = nullptr;
            ThumbnailData  data;
            int            maxIndex = current_result->image_data.size() - 1;
            if (maxIndex >= 0) {
                data.width  = current_result->image_data[maxIndex].first[0];
                data.height = current_result->image_data[maxIndex].first[1];
                data.pixels = current_result->image_data[maxIndex].second;
            }

            size    = data.pixels.size();
            imgdata = new unsigned char[size];
            memcpy(imgdata, data.pixels.data(), size);
            std::size_t encoded_size = boost::beast::detail::base64::encoded_size(size);
            std::string img_base64_data(encoded_size, '\0');
            boost::beast::detail::base64::encode(&img_base64_data[0], imgdata, size);

            json_data["image"]       = "data:image/png;base64," + std::move(img_base64_data);
            json_data["plate_index"] = plate->get_index();

            boost::filesystem::path gcode_path(current_result->filename);
            json_data["upload_gcode__name"] = gcode_path.filename().string();
            json_data["gcode_size"] = gcode_file_size_bytes(current_result->filename);

            nlohmann::json extruders_json = nlohmann::json::array();
            int            extruder_index = 1;
            for (const auto& extruder : current_result->creality_default_extruder_colors) {
                if (!extruder.empty()) {
                    extruders_json.push_back(extruder_index);
                }
                extruder_index++;
            }
            json_data["plate_extruders"] = extruders_json;

            wxString total_weight_str;
            wxString print_time_str;
            get_gcode_display_info(total_weight_str, print_time_str, m_plater->get_partplate_list().get_curr_plate());
            json_data["total_weight"] = total_weight_str.ToStdString();
            json_data["print_time"]   = print_time_str.ToStdString();

            nlohmann::json   filamentJsonArray = nlohmann::json::array();
            std::vector<int> extruders_vec = extruders_json.get<std::vector<int>>();
            get_filament_length_info(extruders_vec, plate, filamentJsonArray);
            json_data["filament_length"] = filamentJsonArray;

            get_gcode_temperature_info(plate,json_data);

            get_nozzle_info(extruders_vec, plate, json_data);

            append_generated_filament_mapping(json_data, current_result);

            json_array.push_back(json_data);

        }else if (!current_result->filename.empty()){
            boost::filesystem::path gcode_path(current_result->filename);
            json_data["upload_gcode__name"] = gcode_path.filename().string();
            json_data["gcode_size"] = gcode_file_size_bytes(current_result->filename);

            if (plate && plate->thumbnail_data.is_valid()) {
                wxImage image(plate->thumbnail_data.width, plate->thumbnail_data.height);
                image.InitAlpha();
                for (unsigned int r = 0; r < plate->thumbnail_data.height; ++r) {
                    unsigned int rr = (plate->thumbnail_data.height - 1 - r) * plate->thumbnail_data.width;
                    for (unsigned int c = 0; c < plate->thumbnail_data.width; ++c) {
                        unsigned char* px = (unsigned char*) plate->thumbnail_data.pixels.data() + 4 * (rr + c);
                        image.SetRGB((int) c, (int) r, px[0], px[1], px[2]);
                        image.SetAlpha((int) c, (int) r, px[3]);
                    }
                }
                wxImage resized_image = image.Rescale(256, 256, wxIMAGE_QUALITY_HIGH);

                wxMemoryOutputStream mem_stream;
                if (!resized_image.SaveFile(mem_stream, wxBITMAP_TYPE_PNG)) {
                    wxLogError("Failed to save image to memory stream.");
                }

                auto size = mem_stream.GetSize();
                auto imgdata = new unsigned char[size];
                mem_stream.CopyTo(imgdata, size);

                std::size_t encoded_size = boost::beast::detail::base64::encoded_size(size);
                std::string img_base64_data(encoded_size, '\0');
                boost::beast::detail::base64::encode(&img_base64_data[0], imgdata, size);

                json_data["image"]       = "data:image/png;base64," + std::move(img_base64_data);
            }
           
            json_data["plate_index"] = plate->get_index();
            json_data["plate_extruders"] = nlohmann::json::array();
            json_data["total_weight"] = "";
            json_data["print_time"]   = "";
            append_generated_filament_mapping(json_data, current_result);
            json_array.push_back(json_data);
        }
    }

    nlohmann::json top_level_json;
    top_level_json["extruder_colors"] = std::move(colors_json);
    top_level_json["filament_types"]    = std::move(filament_types_json);
    top_level_json["plates"]          = std::move(json_array);
    top_level_json["is_only_gcode_mode"] = m_is_only_gcode_mode;

    int cur_plate_index                   = m_plater->get_partplate_list().get_curr_plate_index();
    top_level_json["current_plate_index"] = cur_plate_index;
    if(printer_name.empty())
    {
        if (printer_model.empty())
        {
            printer_name = _L("Unknown model").ToUTF8().data();
        }
        else
        {
            printer_name = (boost::format("%s %.1f nozzle") % printer_model % nozzle_diameter).str();
        }
    }
    
    //std::string presetname = wxGetApp().preset_bundle->prints.get_selected_preset_name();
    top_level_json["printer_model"] = printer_model;
    top_level_json["preset_name"] = printer_name;
    advisor_append_send_page_data(top_level_json); // PRINT-ADVISOR advisor-hook: chamber temperatures for the cold-chamber notice

    std::string json_str         = top_level_json.dump(-1, ' ', true);

    // create command to send to the webview
    nlohmann::json commandJson;
    commandJson["command"] = "update_plate_data";
    commandJson["data"]    = RemotePrint::Utils::url_encode(json_str);

    std::string commandStr = commandJson.dump(-1, ' ', true);

    return RemotePrint::Utils::url_encode(commandStr);
}

void CxSentToPrinterDialog::replaceIllegalChars(std::string& str)
{
    const std::unordered_set<char> illegalChars = { '/', '\\', ':', '*', '?', '"', '<', '>', '|' };
    for (char& ch : str)
    {
        if (illegalChars.find(ch) != illegalChars.end())
        {
            ch = '_';
        }
    }
}

std::string CxSentToPrinterDialog::get_plate_data_on_show()
{
    m_backup_extruder_colors.clear();

    // Mixed-filament changes may invalidate the cached plate thumbnail after
    // slicing. Rebuild missing thumbnails before filtering plates below;
    // otherwise a printable plate is omitted and the frontend receives
    // "plates": [].
    if (!m_plater->only_gcode_mode())
        m_plater->update_all_plate_thumbnails(false);

    nlohmann::json json_array = nlohmann::json::array();

    std::vector<std::string> filament_presets = wxGetApp().preset_bundle->filament_presets;
    const size_t physical_filament_count = filament_presets.size();

    m_backup_extruder_colors = Slic3r::GUI::wxGetApp().plater()->get_extruder_colors_from_plater_config();
    nlohmann::json colors_json = nlohmann::json::array();
    for (size_t i = 0; i < m_backup_extruder_colors.size() && i < physical_filament_count; ++i) {
        colors_json.push_back(m_backup_extruder_colors[i]);
    }

    std::vector<std::string> filament_types;
    nlohmann::json filament_types_json = nlohmann::json::array();
    nlohmann::json filament_maps_json = nlohmann::json::array();
    std::vector<std::string> filament_maps;
    boost::split(filament_maps, m_mapString, boost::is_any_of(";"));
    for (size_t i = 0; i < filament_maps.size() && i < physical_filament_count; ++i) {
        filament_maps_json.emplace_back(filament_maps[i]);
    }

    for (const auto& preset_name : filament_presets) {
        std::string     filament_type;
        Slic3r::Preset* preset = wxGetApp().preset_bundle->filaments.find_preset(preset_name);
        if (preset) {
            preset->get_filament_type(filament_type);
            filament_types_json.push_back(filament_type);
            filament_types.emplace_back(filament_type);
        }
    }

    for (int i = 0; i < wxGetApp().plater()->get_partplate_list().get_plate_count(); i++) {
        PartPlate* plate = wxGetApp().plater()->get_partplate_list().get_plate(i);
        if(!plate) {
            continue;
        }

        if ( ( plate->is_slice_result_ready_for_print() || m_plater->only_gcode_mode() ) &&  plate->thumbnail_data.is_valid() ) 
        {
            int size;
            unsigned char* imgdata;
            if (!m_plater->only_gcode_mode()) {
                ThumbnailData& thumbnail = plate->thumbnail_data;
                wxImage image(thumbnail.width, thumbnail.height);
                image.InitAlpha();
                for (unsigned int r = 0; r < thumbnail.height; ++r) {
                    unsigned int rr = (thumbnail.height - 1 - r) * thumbnail.width;
                    for (unsigned int c = 0; c < thumbnail.width; ++c) {
                        unsigned char* px = (unsigned char*) thumbnail.pixels.data() + 4 * (rr + c);
                        image.SetRGB((int) c, (int) r, px[0], px[1], px[2]);
                        image.SetAlpha((int) c, (int) r, px[3]);
                    }
                }
                wxImage resized_image = image.Rescale(256, 256, wxIMAGE_QUALITY_HIGH);

                wxMemoryOutputStream mem_stream;
                if (!resized_image.SaveFile(mem_stream, wxBITMAP_TYPE_PNG)) {
                    wxLogError("Failed to save image to memory stream.");
                }
                size = mem_stream.GetSize();
                // wxMemoryBuffer buffer(size);
                imgdata = new unsigned char[size];
                mem_stream.CopyTo(imgdata, size);
            }
            else
            {   // picture from gcode
                ThumbnailData data = m_plater->get_gcode_thumbnail();
                size               = data.pixels.size();
                imgdata            = new unsigned char[size];
                memcpy(imgdata, data.pixels.data(), size);
            }

            std::size_t encoded_size = boost::beast::detail::base64::encoded_size(size);
            std::string img_base64_data(encoded_size, '\0');
            boost::beast::detail::base64::encode(&img_base64_data[0], imgdata, size);

            std::string default_gcode_name = "";

            std::vector<int> plate_extruders = plate->get_used_extruders();

            if (m_plater->only_gcode_mode()) {
                wxString   last_loaded_gcode = m_plater->get_last_loaded_gcode();
                wxFileName fileName(last_loaded_gcode);
                default_gcode_name = std::string(fileName.GetName().ToUTF8().data());
                plate_extruders = m_plater->get_gcode_extruders_in_only_gcode_mode();
            }
            else {
                // {m_pathname=L"Cone_PLA_10m45s.gcode" }
                ModelObjectPtrs plate_objects = plate->get_objects_on_this_plate();
                std::string     obj0_name     = ""; // the first object's name
                if (plate_objects.size() > 0 && nullptr != plate_objects[0]) {
                    obj0_name = plate_objects[0]->name;
                }

                auto                                  plate_print_statistics = plate->get_slice_result()->print_statistics;
                const PrintEstimatedStatistics::Mode& plate_time_mode =
                    plate_print_statistics.modes[static_cast<size_t>(PrintEstimatedStatistics::ETimeMode::Normal)];

                if (wxGetApp().plater()->has_illegal_filename_characters(obj0_name))
                {
                    replaceIllegalChars(obj0_name);
                }
                
                if (plate_extruders.size() > 0) {
                    const int extruder_index = plate_extruders[0] - 1;
                    const std::string filament_type =
                        extruder_index >= 0 && extruder_index < static_cast<int>(filament_types.size()) ?
                            filament_types[extruder_index] : "PLA";
                    default_gcode_name = obj0_name + "_" + filament_type + "_" +
                                         get_bbl_time_dhms(plate_time_mode.model_time_s());
                } else {
                    default_gcode_name = "plate" + std::to_string(i + 1);
                }
            }

            nlohmann::json json_data;
            json_data["image"]              = "data:image/png;base64," + std::move(img_base64_data);
            json_data["plate_index"]        = plate->get_index();
            json_data["upload_gcode__name"] = default_gcode_name;
            json_data["gcode_size"]         = gcode_file_size_bytes(plate->get_tmp_gcode_path());

            nlohmann::json extruders_json = nlohmann::json::array();
            for (const auto& extruder : plate_extruders) {
                extruders_json.push_back(extruder);
            }
            json_data["plate_extruders"] = extruders_json;

            wxString total_weight_str;
            wxString print_time_str;
            get_gcode_display_info(total_weight_str, print_time_str, plate);
            json_data["total_weight"] = total_weight_str.ToStdString();
            json_data["print_time"] = print_time_str.ToStdString();
            m_is_only_gcode_mode = false;

            nlohmann::json filamentJsonArray = nlohmann::json::array();
            get_filament_length_info(plate_extruders,plate,filamentJsonArray);
            json_data["filament_length"] = filamentJsonArray;

			get_temperature_info(plate_extruders,plate,json_data);     

            get_nozzle_info(plate_extruders, plate, json_data);

            append_generated_filament_mapping(json_data, plate->get_slice_result());

            json_array.push_back(json_data);
        }
    }

    nlohmann::json top_level_json;
    top_level_json["extruder_colors"] = std::move(colors_json);
    top_level_json["filament_types"]    = std::move(filament_types_json);
    top_level_json["filament_maps"]    = std::move(filament_maps_json);
    top_level_json["plates"]          = std::move(json_array);
    top_level_json["is_only_gcode_mode"] = m_is_only_gcode_mode;


    bool is_all_plates = wxGetApp().plater()->get_preview_canvas3D()->is_all_plates_selected();
    int cur_plate_index = m_plater->get_partplate_list().get_curr_plate_index();
    top_level_json["current_plate_index"] = cur_plate_index;
    
    std::string printer_name = Slic3r::GUI::wxGetApp().preset_bundle->printers.get_selected_preset_name();
    Preset&  presetSelect = Slic3r::GUI::wxGetApp().preset_bundle->printers.get_selected_preset();
    ConfigOptionString* printer_model = presetSelect.config.option<ConfigOptionString>("printer_model");
    if (printer_model) {
        top_level_json["printer_model"] = printer_model->value;
    }else{
        top_level_json["printer_model"] = printer_name;
    }

    top_level_json["preset_name"] = printer_name;
    top_level_json["slice_type"]  = m_plater->isSliceAll() ? 2 : 1; // 1 - 切单盘 2 - 切所有盘
    advisor_append_send_page_data(top_level_json); // PRINT-ADVISOR advisor-hook: chamber temperatures for the cold-chamber notice

    std::string json_str         = top_level_json.dump(-1, ' ', true);

    // create command to send to the webview
    nlohmann::json commandJson;
    commandJson["command"] = "update_plate_data";
    commandJson["data"]    = RemotePrint::Utils::url_encode(json_str);


    std::string commandStr = commandJson.dump(-1, ' ', true);

    return RemotePrint::Utils::url_encode(commandStr);

}

void CxSentToPrinterDialog::update_send_page_content()
{
    if(m_plater->only_gcode_mode())
    {
        wxString strJS = wxString::Format("window.handleStudioCmd('%s');", from_u8(get_onlygcode_plate_data_on_show()));
        run_script(strJS.ToStdString());
    }else{
        wxString strJS = wxString::Format("window.handleStudioCmd('%s');", from_u8(get_plate_data_on_show()));
        run_script(strJS.ToStdString());
    }
}

void CxSentToPrinterDialog::run_script(std::string content)
{
    if (m_browser == nullptr || m_browser->IsBeingDeleted())
        return;

    WebView::RunScript(m_browser, from_u8(content));
}

void CxSentToPrinterDialog::run_script_if_ready(const wxString& script)
{
    if (m_browser == nullptr || m_browser->IsBeingDeleted() || m_browser->IsBusy())
        return;

    run_script(script.ToStdString());
}

void CxSentToPrinterDialog::invalidate_ui_gate()
{
    if (!m_ui_gate)
        return;

    m_ui_gate->accepting_callbacks = false;
    m_ui_gate->dialog = nullptr;
    m_ui_gate.reset();
}
void CxSentToPrinterDialog::reload()
{
    m_browser->Reload();
}
/**
 * Method that retrieves the current state from the web control and updates the
 * GUI the reflect this current state.
 */
void CxSentToPrinterDialog::UpdateState() {
  // SetTitle(m_browser->GetCurrentTitle());

}

void CxSentToPrinterDialog::OnClose(wxCloseEvent& evt)
{
    this->Hide();
}

void CxSentToPrinterDialog::OnError(wxWebViewEvent &evt)
{
    auto e = "unknown error";
    switch (evt.GetInt()) {
      case wxWEBVIEW_NAV_ERR_CONNECTION:
        e = "wxWEBVIEW_NAV_ERR_CONNECTION";
        break;
      case wxWEBVIEW_NAV_ERR_CERTIFICATE:
        e = "wxWEBVIEW_NAV_ERR_CERTIFICATE";
        break;
      case wxWEBVIEW_NAV_ERR_AUTH:
        e = "wxWEBVIEW_NAV_ERR_AUTH";
        break;
      case wxWEBVIEW_NAV_ERR_SECURITY:
        e = "wxWEBVIEW_NAV_ERR_SECURITY";
        break;
      case wxWEBVIEW_NAV_ERR_NOT_FOUND:
        e = "wxWEBVIEW_NAV_ERR_NOT_FOUND";
        break;
      case wxWEBVIEW_NAV_ERR_REQUEST:
        e = "wxWEBVIEW_NAV_ERR_REQUEST";
        break;
      case wxWEBVIEW_NAV_ERR_USER_CANCELLED:
        e = "wxWEBVIEW_NAV_ERR_USER_CANCELLED";
        break;
      case wxWEBVIEW_NAV_ERR_OTHER:
        e = "wxWEBVIEW_NAV_ERR_OTHER";
        break;
      }
    BOOST_LOG_TRIVIAL(info) << __FUNCTION__<< boost::format(": error loading page %1% %2% %3% %4%") %evt.GetURL() %evt.GetTarget() %e %evt.GetString();
}

void CxSentToPrinterDialog::on_dpi_changed(const wxRect& suggested_rect)
{
    
}

double CxSentToPrinterDialog::get_gcode_total_weight()
{
    double total_weight = 0.0;
    GCodeProcessorResult* current_result = m_plater->get_partplate_list().get_current_slice_result();
    if (current_result) {
        auto&  ps         = current_result->print_statistics;
        for (auto volume : ps.total_volumes_per_extruder) {
            size_t extruder_id = volume.first;
            double density = 1.24;
            if(extruder_id<current_result->filament_densities.size())
                density     = current_result->filament_densities.at(extruder_id);
            double weight      = volume.second * density * 0.001;
            total_weight += weight;
        }
    }

    return total_weight;
}

void CxSentToPrinterDialog::get_gcode_display_info(wxString& total_weight_str, wxString& print_time, Slic3r::GUI::PartPlate* plate)
{
    if(!m_plater || !plate) return;

	Slic3r::Print* print = nullptr;
	plate->get_print((Slic3r::PrintBase **)&print, nullptr, nullptr);
    if(!print) return;

    // basic info
    auto       aprint_stats = print->print_statistics();

    if(m_plater->only_gcode_mode())
    {
        GCodeProcessorResult* gcode_process_result = m_plater->get_partplate_list().get_current_slice_result();
        if (gcode_process_result) {
            const auto& mode = gcode_process_result->print_statistics.modes[0];
            print_time = wxString::Format("%s", short_time(get_time_dhms(mode.model_time_s())));
        }
    }
    else {
        if (plate->get_slice_result()) { 
            const auto& mode = plate->get_slice_result()->print_statistics.modes[0];
            // PRINT-ADVISOR BEGIN heat-soak (upstream: print_time from model_time_s() only)
            // model_time_s() leaves out the preparation stage, where the heat soak dwell is.
            // Same rule as the G-code: the longest soak among the filaments the plate uses
            // (the initial extruder included), clamped to 0-60 min; not the first filament slot.
            float soak_s = 0.f;
            const auto& soak = print->config().heat_soak_time;
            int soak_minutes = 0;
            for (unsigned int id : print->extruders())
                if (id < soak.values.size())
                    soak_minutes = std::max(soak_minutes, soak.get_at(id));
            soak_s = float(std::clamp(soak_minutes, 0, 60)) * 60.f;
            print_time = wxString::Format("%s", short_time(get_time_dhms(mode.model_time_s() + soak_s))); 
            // PRINT-ADVISOR END
        }
    }

    double total_weight = aprint_stats.total_weight;
    if(m_plater->only_gcode_mode())
    {
        total_weight = get_gcode_total_weight();
    }

    char weight[64];
    if (wxGetApp().app_config->get("use_inches") == "1") {
        ::sprintf(weight, "  %.2f oz", total_weight*0.035274);
    }else{
        ::sprintf(weight, "  %.2f g", total_weight);
    }

    total_weight_str = wxString(weight);

}

void CxSentToPrinterDialog::get_filament_length_info(std::vector<int> plate_extruders, Slic3r::GUI::PartPlate* plate, nlohmann::json& jsonArray)
{
    if(!plate) 
        return;
	Slic3r::Print* print = nullptr;
	plate->get_print((Slic3r::PrintBase **)&print, nullptr, nullptr);
    if(!print) return;

    GCodeProcessorResult* gcode_process_result = plate->get_slice_result();
    if (!gcode_process_result) 
    {
        return ;
    } 
    PrintEstimatedStatistics ps = gcode_process_result->print_statistics;

    for (size_t i = 0; i < plate_extruders.size(); i++) 
    {
        int idx = plate_extruders[i] - 1;
        if (idx < 0) 
            continue;

        std::vector<float> filaments  = gcode_process_result->filament_diameters;
        std::map<size_t, double> total_volumes = ps.total_volumes_per_extruder;
        if (idx >= filaments.size()|| total_volumes.find(idx) == total_volumes.end()) 
        {
            continue;
        }

        double diameter = filaments[idx];
        double totalVolume = total_volumes[idx];
        double s = PI * sqr(0.5 * diameter);

        double length = totalVolume / s;

        json jsonObj;
        jsonObj["length"] = length;
        jsonObj["extruderIndex"] = idx+1;
        jsonArray.push_back(jsonObj);
    }

    return ;
}

void CxSentToPrinterDialog::get_nozzle_info(std::vector<int> plate_extruders, Slic3r::GUI::PartPlate* plate, nlohmann::json& json_data)
{
    if (!plate)
        return;

    GCodeProcessorResult* gcode_process_result = plate->get_slice_result();
    if (!gcode_process_result)
        return;

    const std::vector<float>& nozzle_diameters          = gcode_process_result->nozzle_diameters;
    const std::vector<float>& filament_nozzle_diameters  = gcode_process_result->filament_nozzle_diameters;
    const std::vector<int>&   filament_map                = gcode_process_result->generated_filament_map;

    // Backward compatibility: only emit the multi-nozzle fields when the machine actually
    // has more than one physical nozzle. Single-nozzle payload stays byte-for-byte identical.
    if (nozzle_diameters.size() <= 1)
        return;

    // Whole-machine nozzle capability list: [{ id: 1-based, diameter }, ...]
    nlohmann::json nozzles_json = nlohmann::json::array();
    for (size_t n = 0; n < nozzle_diameters.size(); ++n) {
        nlohmann::json item;
        item["id"]       = static_cast<int>(n) + 1; // 1-based physical nozzle id
        item["diameter"] = nozzle_diameters[n];
        nozzles_json.push_back(item);
    }
    json_data["nozzles"] = std::move(nozzles_json);

    // Per-filament(extruder) nozzle id and diameter, only for extruders used by this plate.
    nlohmann::json filament_nozzles_json = nlohmann::json::array();
    // Set of physical nozzle ids actually used by this plate.
    std::set<int> plate_nozzle_id_set;
    for (int extruder_id : plate_extruders) {
        const int idx = extruder_id - 1; // filament/extruder index, 0-based
        if (idx < 0)
            continue;

        int nozzle_id = 0; // 1-based physical nozzle
        if (idx < static_cast<int>(filament_map.size()) && filament_map[idx] > 0)
            nozzle_id = filament_map[idx];
        else
            nozzle_id = 1; // legacy fallback: Tn -> nozzle n, default first nozzle

        float diameter = 0.0f;
        if (idx < static_cast<int>(filament_nozzle_diameters.size()))
            diameter = filament_nozzle_diameters[idx];
        else {
            const int nidx = (nozzle_id > 0 && nozzle_id - 1 < static_cast<int>(nozzle_diameters.size())) ? nozzle_id - 1 : 0;
            diameter = nozzle_diameters[nidx];
        }

        nlohmann::json item;
        item["extruderIndex"] = extruder_id;   // 1-based, aligns with plate_extruders / filament_length
        item["nozzleId"]      = nozzle_id;      // 1-based physical nozzle
        item["nozzleDiameter"] = diameter;
        filament_nozzles_json.push_back(item);

        if (nozzle_id > 0)
            plate_nozzle_id_set.insert(nozzle_id);
    }
    json_data["filament_nozzles"] = std::move(filament_nozzles_json);

    // Physical nozzle ids used by this plate.
    nlohmann::json plate_nozzle_ids_json = nlohmann::json::array();
    for (int id : plate_nozzle_id_set)
        plate_nozzle_ids_json.push_back(id);
    json_data["plate_nozzle_ids"] = std::move(plate_nozzle_ids_json);
}

void CxSentToPrinterDialog::get_gcode_temperature_info(Slic3r::GUI::PartPlate* plate,  nlohmann::json& json_data)
{
    if(!plate) 
        return;
	Slic3r::Print* print = nullptr;
	plate->get_print((Slic3r::PrintBase **)&print, nullptr, nullptr);
    if(!print) return;

    GCodeProcessorResult* gcode_process_result = plate->get_slice_result();
    if (!gcode_process_result) 
    {
        return;
    }

    float nozzleDiameter = gcode_process_result->nozzle_diameter;
    int maxBedTemp = gcode_process_result->max_printer_bed_temp;
    int maxNozzleTem = gcode_process_result->max_printer_nozzle_temp;

    json_data["max_nozzle_temperature"] = maxNozzleTem;
    json_data["max_bed_temperature"] = maxBedTemp;
    json_data["nozzle_diameter"] = nozzleDiameter; 

    return ;
}

void CxSentToPrinterDialog::get_temperature_info(std::vector<int> plate_extruders, Slic3r::GUI::PartPlate* plate,  nlohmann::json& json_data)
{
    if(!plate) 
        return;
	Slic3r::Print* print = nullptr;
	plate->get_print((Slic3r::PrintBase **)&print, nullptr, nullptr);
    if(!print) return;

    const PrintConfig& config = print->config();
    const ConfigOption* bed_type_opt = config.option("curr_bed_type");
    if (!bed_type_opt)
        return ;
    int curr_bed_type = bed_type_opt->getInt();
    const ConfigOptionInts* bed_temp_1st_layer_opt = config.option<ConfigOptionInts>(get_bed_temp_1st_layer_key((BedType)curr_bed_type));
    const ConfigOptionInts* bed_temp_other_layer = config.option<ConfigOptionInts>(get_bed_temp_key((BedType)curr_bed_type));
    if(!bed_temp_1st_layer_opt || !bed_temp_other_layer)
    {
        return ;
    }

    int maxBedTemFirstLayer = 0;
    for (auto extruderID : plate_extruders) {
        int bedTemp = bed_temp_1st_layer_opt->get_at(extruderID - 1);
        if (bedTemp > maxBedTemFirstLayer)
            maxBedTemFirstLayer = bedTemp;
    }

    int maxBedTemOtherLayer = 0;
    for (auto extruderID : plate_extruders) {
        int bedTemp = bed_temp_other_layer->get_at(extruderID - 1);
        if (bedTemp > maxBedTemOtherLayer)
            maxBedTemOtherLayer = bedTemp;
    }
    int maxBedTemp = maxBedTemFirstLayer > maxBedTemOtherLayer ? maxBedTemFirstLayer : maxBedTemOtherLayer;


    GCodeProcessorResult* gcode_process_result = plate->get_slice_result();
    if (!gcode_process_result) 
    {
        return ;
    } 
    PrintEstimatedStatistics ps = gcode_process_result->print_statistics;

    int maxNozzleTem = 0;
    for (size_t i = 0; i < plate_extruders.size(); i++) 
    {
        int idx = plate_extruders[i] - 1;
        if (idx < 0) 
            continue;

        int nozzleTemFirstLayer = print->config().nozzle_temperature_initial_layer.get_at(idx);
        int nozzleTemp = print->config().nozzle_temperature.get_at(idx);
        nozzleTemFirstLayer = nozzleTemFirstLayer > nozzleTemp ? nozzleTemFirstLayer : nozzleTemp ;
        maxNozzleTem = maxNozzleTem > nozzleTemFirstLayer ? maxNozzleTem : nozzleTemFirstLayer ;
    }
    float nozzleDiameter = gcode_process_result->nozzle_diameter;
    json_data["max_nozzle_temperature"] = maxNozzleTem;
    json_data["max_bed_temperature"] = maxBedTemp;
    json_data["nozzle_diameter"] = nozzleDiameter; 

    return ;
}

std::string CxSentToPrinterDialog::get_user_operation_state_file_path() const
{
    return (fs::path(data_dir()) / "cache" / "send_to_printer" / "user_operation_state.json").make_preferred().string();
}

bool CxSentToPrinterDialog::save_user_operation_state(const nlohmann::json& state_data)
{
    try {
        const auto path = fs::path(get_user_operation_state_file_path());
        const auto dir  = path.parent_path();
        if (!dir.empty() && !fs::exists(dir))
            fs::create_directories(dir);

        boost::nowide::ofstream out(path.string(), std::ios::out | std::ios::trunc);
        if (!out.is_open()) {
            BOOST_LOG_TRIVIAL(error) << __FUNCTION__ << ": failed to open file: " << path.string();
            return false;
        }

        out << state_data.dump(-1, ' ', true);
        return true;
    } catch (const std::exception& e) {
        BOOST_LOG_TRIVIAL(error) << __FUNCTION__ << ": failed to save state, error=" << e.what();
        return false;
    }
}

nlohmann::json CxSentToPrinterDialog::load_user_operation_state() const
{
    nlohmann::json default_state = {
        {"print_calibration", 1},
        {"customColorList", nlohmann::json::array()}
    };

    try {
        const auto path = fs::path(get_user_operation_state_file_path());
        if (!fs::exists(path))
            return default_state;

        boost::nowide::ifstream in(path.string());
        if (!in.is_open()) {
            BOOST_LOG_TRIVIAL(error) << __FUNCTION__ << ": failed to open file: " << path.string();
            return default_state;
        }

        std::stringstream buffer;
        buffer << in.rdbuf();
        if (buffer.str().empty())
            return default_state;

        auto parsed = nlohmann::json::parse(buffer.str(), nullptr, false);
        if (parsed.is_discarded() || !parsed.is_object()) {
            BOOST_LOG_TRIVIAL(error) << __FUNCTION__ << ": invalid state file: " << path.string();
            return default_state;
        }

        int print_calibration = 0;
        if (parsed.contains("print_calibration")) {
            try {
                if (parsed["print_calibration"].is_boolean())
                    print_calibration = parsed["print_calibration"].get<bool>() ? 1 : 0;
                else if (parsed["print_calibration"].is_number_integer())
                    print_calibration = parsed["print_calibration"].get<int>();
            } catch (...) {
                print_calibration = 0;
            }
        }

        parsed["print_calibration"] = (print_calibration == 1) ? 1 : 0;

        nlohmann::json custom_color_list = nlohmann::json::array();
        try {
            if (parsed.contains("customColorList") && parsed["customColorList"].is_array()) {
                const auto is_valid_slot_value = [](const nlohmann::json& value) {
                    return value.is_string() || value.is_number_integer() || value.is_number_unsigned();
                };
                for (const auto& item : parsed["customColorList"]) {
                    if (!item.is_object())
                        continue;
                    if (!item.contains("address") || !item["address"].is_string())
                        continue;
                    if (!item.contains("boxType") || !is_valid_slot_value(item["boxType"]))
                        continue;
                    if (!item.contains("boxId") || !is_valid_slot_value(item["boxId"]))
                        continue;
                    if (!item.contains("materialId") || !is_valid_slot_value(item["materialId"]))
                        continue;
                    if (!item.contains("customColor") || !item["customColor"].is_string())
                        continue;

                    const auto address      = item["address"].get<std::string>();
                    const auto custom_color = item["customColor"].get<std::string>();
                    if (address.empty() || custom_color.empty())
                        continue;

                    custom_color_list.push_back({
                        {"address", address},
                        {"boxType", item["boxType"]},
                        {"boxId", item["boxId"]},
                        {"materialId", item["materialId"]},
                        {"customColor", custom_color}
                    });
                }
            }
        } catch (...) {
            custom_color_list = nlohmann::json::array();
        }

        parsed["customColorList"] = custom_color_list;
        return parsed;
    } catch (const std::exception& e) {
        BOOST_LOG_TRIVIAL(error) << __FUNCTION__ << ": failed to load state, error=" << e.what();
        return default_state;
    }
}

void CxSentToPrinterDialog::handle_save_user_operation_state(const nlohmann::json& json_data)
{
    const nlohmann::json payload = json_data.contains("data") ? json_data["data"] : json_data;

    int print_calibration = 0;
    try {
        if (payload.is_object() && payload.contains("print_calibration")) {
            if (payload["print_calibration"].is_boolean())
                print_calibration = payload["print_calibration"].get<bool>() ? 1 : 0;
            else if (payload["print_calibration"].is_number_integer())
                print_calibration = payload["print_calibration"].get<int>();
        } else if (payload.is_number_integer()) {
            print_calibration = payload.get<int>();
        }
    } catch (...) {
        print_calibration = 0;
    }

    nlohmann::json state_data = load_user_operation_state();
    if (!state_data.is_object())
        state_data = nlohmann::json::object();
    state_data["print_calibration"] = (print_calibration == 1) ? 1 : 0;

    const bool ok = save_user_operation_state(state_data);

    nlohmann::json commandJson;
    commandJson["command"] = "save_user_operation_state";
    commandJson["data"]    = {
        {"success", ok},
        {"print_calibration", state_data["print_calibration"]}
    };

    wxString strJS = wxString::Format("window.handleStudioCmd('%s');", RemotePrint::Utils::url_encode(commandJson.dump(-1, ' ', true)));
    run_script(strJS.ToStdString());
}

void CxSentToPrinterDialog::handle_request_user_operation_state(const nlohmann::json& json_data)
{
    (void)json_data;

    nlohmann::json commandJson;
    commandJson["command"] = "request_user_operation_state";
    commandJson["data"]    = load_user_operation_state();

    wxString strJS = wxString::Format("window.handleStudioCmd('%s');", RemotePrint::Utils::url_encode(commandJson.dump(-1, ' ', true)));
    run_script(strJS.ToStdString());
}

bool CxSentToPrinterDialog::LoadFile(std::string jPath, std::string &sContent)
{
    try {
        boost::nowide::ifstream t(jPath);
        std::stringstream buffer;
        buffer << t.rdbuf();
        sContent=buffer.str();
        BOOST_LOG_TRIVIAL(trace) << __FUNCTION__ << boost::format(", load %1% into buffer")% jPath;
    }
    catch (std::exception &e)
    {
        BOOST_LOG_TRIVIAL(error) << __FUNCTION__ << ",  got exception: "<<e.what();
        return false;
    }

    return true;
}

int CxSentToPrinterDialog::load_machine_preset_data()
{
    // Fork: the printer catalogue is read only from the bundled resources, never downloaded.
    const std::string vendor_preset_path = (boost::filesystem::path(Slic3r::resources_dir()) / PRESET_PROFILES_DIR /
                                            "Creality" / "machineList.json").make_preferred().string();
    if (!boost::filesystem::exists(vendor_preset_path)) {
        BOOST_LOG_TRIVIAL(error) << __FUNCTION__ << ": missing " << vendor_preset_path;
        return -1;
    }
    try {
        std::string contents;
        LoadFile(vendor_preset_path, contents);
        json jLocal = json::parse(contents);
        localize_printer_list_images(jLocal["printerList"],
                                     wxGetApp().get_server()->is_started() ? wxGetApp().get_server_port() : 0);
        nlohmann::json commandJson;
        commandJson["command"] = "get_machine_list";
        commandJson["data"]    = jLocal["printerList"];
        wxString strJS = wxString::Format("window.handleStudioCmd('%s');", RemotePrint::Utils::url_encode(commandJson.dump(-1, ' ', true)));
        run_script(strJS.ToStdString());
    } catch (const std::exception& e) {
        BOOST_LOG_TRIVIAL(error) << __FUNCTION__ << ": parse " << vendor_preset_path << " failed: " << e.what();
        return -1;
    }
    return 0;
}

} // namespace GUI
} // namespace Slic3r
