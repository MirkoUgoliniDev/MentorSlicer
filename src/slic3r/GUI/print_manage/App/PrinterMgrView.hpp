#ifndef slic3r_PrinterMgrView_hpp_
#define slic3r_PrinterMgrView_hpp_


#include "wx/artprov.h"
#include "wx/cmdline.h"
#include "wx/notifmsg.h"
#include "wx/settings.h"
#include <vector>
#include <wx/webview.h>
#include <wx/string.h>
#include <boost/thread.hpp>
#if wxUSE_WEBVIEW_EDGE
#include "wx/msw/webview_edge.h"
#endif

#include "wx/webviewarchivehandler.h"
#include "wx/webviewfshandler.h"
#include "wx/numdlg.h"
#include "wx/infobar.h"
#include "wx/filesys.h"
#include "wx/fs_arc.h"
#include "wx/fs_mem.h"
#include "wx/stdpaths.h"
#include <wx/panel.h>
#include <wx/tbarbase.h>
#include "wx/textctrl.h"
#include <wx/timer.h>
#include "nlohmann/json_fwd.hpp"
#include <slic3r/GUI/print_manage/AppUtils.hpp>
#include "WebSocketProxy.hpp"
#include <future>
#include <wx/timer.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <set>
#include <unordered_map>


namespace Slic3r {
    namespace GUI {

        class PrinterMgrView : public wxPanel {
        public:
            PrinterMgrView(wxWindow* parent);
            virtual ~PrinterMgrView();

            void load_url(const wxString& url);
            void UpdateState();
            void OnClose(wxCloseEvent& evt);
            void OnError(wxWebViewEvent& evt);
            void OnLoaded(wxWebViewEvent& evt);
            
            void reload();
            void RequestDeviceListFromDB();

            void OnScriptMessage(wxWebViewEvent& evt);
            void ExecuteScriptCommand(const std::string& commandInfo, bool async = false);
            void RegisterHandler(const std::string& command, std::function<void(const nlohmann::json&)> handler);
            void UnregisterHandler(const std::string& command);
            virtual bool Show(bool show = true) wxOVERRIDE;
            void run_script(std::string content);
            
            void on_switch_to_device_page();
            void request_refresh_all_device();
            int load_machine_preset_data();
            int getFileListFromLanDevice(const std::string strIp);
            int deleteFileListFromLanDevice(const std::string strIp, const std::string strName);
            int uploadeFileLanDevice(const std::string strIp);
            void uploadDeviceFile(const std::string& address, const std::string& requestId);
            wxString openCAFile();

            bool LoadFile(std::string jPath, std::string & sContent);

            // need to close the video when send page is opened
            void request_close_detail_page();

            void request_reopen_detail_video();

            void update_current_cxy_device_filament(const std::string& mac);

            bool request_print_control(const nlohmann::json& payload);
            // PRINT-ADVISOR BEGIN advisor-hook
            // True once the device page has finished loading (it asked for the machine list).
            bool is_page_loaded() const { return m_browser != nullptr && m_webview_loaded_successfully; }
            // PRINT-ADVISOR END
            bool request_check_upload_file_ready(const std::string& printer_ip,
                                                 const std::string& file_name,
                                                 std::uint64_t file_size,
                                                 int timeout_ms = 900);

        private:
            std::string get_plate_data_on_show();
            std::string get_user_operation_state_file_path() const;
            bool save_user_operation_state(const nlohmann::json& state_data);
            nlohmann::json load_user_operation_state() const;
            void handle_save_user_operation_state(const nlohmann::json& json_data);
            void handle_request_user_operation_state(const nlohmann::json& json_data);
            void handle_get_user_custom_color_list(const nlohmann::json& json_data);
            void handle_set_user_custom_color_list(const nlohmann::json& json_data);
            void startDeviceFileUpload(const std::string& address,
                                       bool secureConnection,
                                       const std::string& requestId);
        private:
            struct DownloadItem {
                std::string name;
                std::string path;
            };
            void down_files(const std::string& address, bool secureConnection, const std::vector<DownloadItem>& download_items, std::string savePath, std::string path_type);
            void scan_device(const std::string& request_id = {});
            void correct_device();

            wxWebView* m_browser;
            // Captured on the UI thread when m_browser is created. Background
            // workers only copy this POD snapshot; the UI thread still resolves
            // and validates the wxWindow before using it.
            std::atomic<wxWindowID> m_browser_handle_id {wxID_NONE};
            std::atomic<std::uintptr_t> m_browser_handle_address {0};
            std::unique_ptr<WebSocketProxy::Manager> m_ws_proxy;
            long m_zoomFactor;
            bool m_plate_data_sent_on_show {false};

            std::unordered_map<std::string, std::function<void(const nlohmann::json&)>> m_commandHandlers;
            boost::thread m_scanPoolThread;
            boost::thread m_deviceScanThread;
            std::atomic<bool> m_scanExit {false};
            #ifdef __WXGTK__
            // When using GTK, there may be a problem of synthetic dirty area failure, so perform a low-frequency refresh
            wxTimer* m_freshTimer;
            #endif
            DM::ThreadController _ctrl;
            std::chrono::steady_clock::time_point lastSendTime;
            std::mutex sendMutex;
            void sendAllProgressWithRateLimit();
            // Upload progress cache: ip -> {progress, speed}
            struct ProgressInfo { float progress = 0.f; double speed = 0.0; };
            std::unordered_map<std::string, ProgressInfo> m_uploadProgressMap;
            std::mutex m_uploadProgressMutex;
            bool m_bHasError = false;
            bool m_webview_loaded_successfully = false;
            struct UploadFileReadyCheckResult {
                bool completed = false;
                bool ready = false;
                std::string message;
            };
            std::atomic<unsigned long long> m_upload_file_ready_seq {0};
            std::mutex m_upload_file_ready_mutex;
            std::condition_variable m_upload_file_ready_cv;
            std::unordered_map<std::string, UploadFileReadyCheckResult> m_upload_file_ready_results;
            // DECLARE_EVENT_TABLE()
        };

    } // GUI
} // Slic3r

#endif /* slic3r_Tab_hpp_ */
