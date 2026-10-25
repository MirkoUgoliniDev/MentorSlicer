#ifndef slic3r_GUI_App_hpp_
#define slic3r_GUI_App_hpp_

#include <wx/app.h>
#include <wx/colour.h>
#include <wx/font.h>
#include <wx/string.h>
#include <wx/snglinst.h>
#include <wx/msgdlg.h>
#include <wx/filename.h>

#include <memory>
#include <string>
#include <atomic>
//#include "ImGuiWrapper.hpp"
#include "ConfigWizard.hpp"
#include "OpenGLManager.hpp"
#include "libslic3r/Preset.hpp"
#include "libslic3r/PresetBundle.hpp"
#include "slic3r/GUI/UserNotification.hpp"
// #include "slic3r/GUI/WebViewDialog.hpp"--
#include "slic3r/GUI/LegacyTransitiveIncludes.hpp" // F5: generic includes that came with the Bambu headers
#include "slic3r/GUI/HttpServer.hpp"
#include "../Utils/PrintHost.hpp"
#include "slic3r/GUI/GLEnums.hpp"
#include "UITour.hpp"
#include <mutex>
#include <stack>

//#define BBL_HAS_FIRST_PAGE          1
#define LOG_FILES_MAX_NUM           30
#define TIMEOUT_CONNECT             15
#define TIMEOUT_RESPONSE            15

#define BE_UNACTED_ON               0x00200001
#define SHOW_BACKGROUND_BITMAP_PIXEL_THRESHOLD 80
#ifndef _MSW_DARK_MODE
    #define _MSW_DARK_MODE            1
#endif // _MSW_DARK_MODE

class wxMenuItem;
class wxMenuBar;
class wxTopLevelWindow;
class wxDataViewCtrl;
class wxBookCtrlBase;
// BBS
class Notebook;
struct wxLanguageInfo;


namespace Slic3r {

class AppConfig;
class PresetBundle;
class PresetUpdater;
class ModelObject;
class Model;

namespace GUI{

class WebViewPanel;
class ImGuiWrapper;
class RemovableDriveManager;
class OtherInstanceMessageHandler;
class MainFrame;
class Sidebar;
class ObjectSettings;
class ObjectList;
class ObjectLayers;
class Plater;
class ParamsPanel;
class NotificationManager;
class Downloader;
struct GUI_InitParams;
class ParamsDialog;
class PrinterDialog;
class PrinterPresetConfig;
class UITour;


enum FileType
{
    FT_STEP,
    FT_STL,
    FT_OBJ,
    FT_AMF,
    FT_3MF,
    FT_GCODE,
    FT_GCODE_3MF,
    FT_MODEL,
    FT_ZIP,
    FT_PROJECT,
    FT_GALLERY,

    FT_INI,
    FT_SVG,

    FT_TEX,

    FT_SL1,

    FT_CXPROJECT,

    FT_ONLY_GCODE,
    FT_MESH_FILE,
    FT_CAD_FILE,
    FT_VIDEO,

    FT_SIZE,
};

extern wxString file_wildcards(FileType file_type, const std::string &custom_extension = std::string{});

enum ConfigMenuIDs {
    //ConfigMenuWizard,
    //ConfigMenuSnapshots,
    //ConfigMenuTakeSnapshot,
    //ConfigMenuUpdate,
    //ConfigMenuDesktopIntegration,
    ConfigMenuPreferences,
    ConfigMenuPrinter,
    //ConfigMenuModeSimple,
    //ConfigMenuModeAdvanced,
    //ConfigMenuLanguage,
    //ConfigMenuFlashFirmware,
    ConfigMenuCnt,
};

enum CameraMenuIDs {
    wxID_CAMERA_PERSPECTIVE,
    wxID_CAMERA_ORTHOGONAL,
    wxID_CAMERA_COUNT,
};


class Tab;
class ConfigWizard;
class GizmoObjectManipulation;

static wxString dots("...", wxConvUTF8);

// Does our wxWidgets version support markup?
#if wxUSE_MARKUP && wxCHECK_VERSION(3, 1, 1)
    #define SUPPORTS_MARKUP
#endif


struct Customize_Config
{
    std::string cur_language = "";
};
class GUI_App : public wxApp
{
public:

    //BBS: remove GCodeViewer as seperate APP logic
    enum class EAppMode : unsigned char
    {
        Editor,
        GCodeViewer
    };


private:
    bool            m_initialized { false };
    bool            m_post_initialized { false };
    bool            m_app_conf_exists{ false };
    EAppMode        m_app_mode{ EAppMode::Editor };
    bool            m_is_recreating_gui{ false };
#ifdef __linux__
    bool            m_opengl_initialized{ false };
#endif

    Customize_Config m_customize_config;
//#ifdef _WIN32
    wxColour        m_color_label_modified;
    wxColour        m_color_label_sys;
    wxColour        m_color_label_default;
    wxColour        m_color_window_default;
    wxColour        m_color_highlight_label_default;
    wxColour        m_color_hovered_btn_label;
    wxColour        m_color_default_btn_label;
    wxColour        m_color_highlight_default;
    wxColour        m_color_selected_btn_bg;
    bool            m_force_colors_update { false };
//#endif

public:
    std::chrono::steady_clock::time_point m_perf_slice_start_time;
private:

    wxFont		    m_small_font;
    wxFont		    m_bold_font;
	wxFont			m_normal_font;
	wxFont			m_code_font;
    wxFont		    m_link_font;

    int             m_em_unit; // width of a "m"-symbol in pixels for current system font
                               // Note: for 100% Scale m_em_unit = 10 -> it's a good enough coefficient for a size setting of controls

    std::unique_ptr<wxLocale> 	  m_wxLocale;
    // System language, from locales, owned by wxWidgets.
    const wxLanguageInfo		 *m_language_info_system = nullptr;
    // Best translation language, provided by Windows or OSX, owned by wxWidgets.
    const wxLanguageInfo		 *m_language_info_best   = nullptr;

    OpenGLManager m_opengl_mgr;
    std::unique_ptr<RemovableDriveManager> m_removable_drive_manager;

    std::unique_ptr<ImGuiWrapper> m_imgui;
    std::unique_ptr<PrintHostJobQueue> m_printhost_job_queue;
	std::unique_ptr <OtherInstanceMessageHandler> m_other_instance_message_handler;
    std::unique_ptr <wxSingleInstanceChecker> m_single_instance_checker;
    std::string m_instance_hash_string;
	    size_t m_instance_hash_int;

    std::unique_ptr<Downloader> m_downloader;

    //BBS
    bool m_is_closing {false};


    static std::string version_display;

    bool             m_is_dark_mode{ false };
    bool             m_adding_script_handler { false };
    bool             m_webview_runtime_repair_prompted { false };
    bool             m_side_popup_status{false};
    wxString         m_info_dialog_content;
    HttpServer       m_http_server {LOCALHOST_PORT};
    bool             m_show_gcode_window{true};

    std::string             m_openlink_url = "";

    //when software is launched for the first time (when "AppData\Roaming\Creality" directory first created)
    bool             m_app_first_launch{false};

    //app startup start time

    //app startup end time
    std::chrono::steady_clock::time_point m_app_end_time;

    //app close time

  public:
    void            openDevelopMode(bool open);
    void            initDevelopParams();
    bool            isAlpha();
    bool            isDevelopParams(const std::string key);
	std::string     getDevelopParamsType(const std::string key);
    const std::map<std::string, std::string> getUserKeys();
    bool            OnInit() override;
    int             OnExit() override;
    void            OnUnhandledException()  override;
    bool            initialized() const { return m_initialized; }
    int             get_server_port() { return m_http_server.get_port(); }
    HttpServer*     get_server() { return &m_http_server;}
    std::map<std::string, bool> test_url_state;


    //BBS: remove GCodeViewer as seperate APP logic
    GUI_App();
    //explicit GUI_App(EAppMode mode = EAppMode::Editor);
    ~GUI_App() override;

    // Warn before a mesh-rebuilding operation transfers painted surface data
    // approximately. Returns true only when the user chooses to continue.
    bool confirm_mesh_paint_warning();
    EAppMode get_app_mode() const { return m_app_mode; }
    bool is_editor() const { return m_app_mode == EAppMode::Editor; }
    bool is_gcode_viewer() const { return m_app_mode == EAppMode::GCodeViewer; }
    bool is_recreating_gui() const { return m_is_recreating_gui; }

    
    // SoftFever
    bool show_gcode_window() const { return m_show_gcode_window; }
    void toggle_show_gcode_window();

    bool show_3d_navigator() const { return app_config->get_bool("show_3d_navigator"); }
    void toggle_show_3d_navigator() const { app_config->set_bool("show_3d_navigator", !show_3d_navigator()); }


    bool send_app_message(const std::string& msg,bool bforce = false);
    void reload_homepage();
    //mouse_scheme
    void on_interinstance_message(const std::string& msg);

    // To be called after the GUI is fully built up.
    // Process command line parameters cached in this->init_params,
    // load configs, STLs etc.
    void            post_init();
    void            shutdown();
    // If formatted for github, plaintext with OpenGL extensions enclosed into <details>.
    // Otherwise HTML formatted for the system info dialog.
    static std::string get_gl_info(bool for_github);
    wxGLContext*    init_glcontext(wxGLCanvas& canvas);
    bool            init_opengl();

    void            init_download_path();
    void            swith_community_sub_page(const std::string& pageName);
    void            switch_to_tab(const std::string& tabName);
#if wxUSE_WEBVIEW_EDGE
    void            init_webview_runtime();
    void            reinstall_webview_runtime();
    bool            mark_webview_runtime_repair_prompted();
#endif
    static unsigned get_colour_approx_luma(const wxColour& colour);
    static bool     dark_mode();
    void            init_label_colours();
    void            update_label_colours_from_appconfig();
    void            update_label_colours();
    // update color mode for window
    void            UpdateDarkUI(wxWindow *window, bool highlited = false, bool just_font = false);
    void            UpdateDarkUIWin(wxWindow* win);
    void            Update_dark_mode_flag();
    void            refresh_after_startup();
    // update color mode for whole dialog including all children
    void            UpdateDlgDarkUI(wxDialog* dlg);
    void            UpdateFrameDarkUI(wxFrame* dlg);
    // update color mode for DataViewControl
    void            UpdateDVCDarkUI(wxDataViewCtrl* dvc, bool highlited = false);
    // update color mode for panel including all static texts controls
    void            UpdateAllStaticTextDarkUI(wxWindow* parent);
    void            init_fonts();
	void            update_fonts(const MainFrame *main_frame = nullptr);
    //update side popup status
    bool            get_side_menu_popup_status();
    void            set_side_menu_popup_status(bool status);

    void webGetDevicesInfo(json& result);
        

    const wxColour& get_label_clr_modified(){ return m_color_label_modified; }
    const wxColour& get_label_clr_sys()     { return m_color_label_sys; }
    const wxColour& get_label_clr_default() { return m_color_label_default; }
    const wxColour& get_window_default_clr(){ return m_color_window_default; }

    // BBS
//#ifdef _WIN32
    const wxColour& get_label_highlight_clr()   { return m_color_highlight_label_default; }
    const wxColour& get_highlight_default_clr() { return m_color_highlight_default; }
    const wxColour& get_color_hovered_btn_label() { return m_color_hovered_btn_label; }
    void            force_colors_update();
//#endif

    const wxFont&   small_font()            { return m_small_font; }
    const wxFont&   bold_font()             { return m_bold_font; }
    const wxFont&   normal_font()           { return m_normal_font; }
    const wxFont&   code_font()             { return m_code_font; }
    const wxFont&   link_font()             { return m_link_font; }
    int             em_unit() const         { return m_em_unit; }
    bool            tabs_as_menu() const;
    wxSize          get_min_size() const;
    wxSize          get_min_size_ex(wxWindow* display_win) const;
    float           toolbar_icon_scale(const bool is_limited = false) const;
    void            set_auto_toolbar_icon_scale(float scale) const;
    void            check_printer_presets();

    void            recreate_GUI(const wxString& message);
    void            keyboard_shortcuts();
    void            load_project(wxWindow *parent, wxString& input_file) const;
    void            import_model(wxWindow *parent, wxArrayString& input_files, bool Category_or_not = false) const;
    void            import_zip(wxWindow* parent, wxString& input_file) const;
    void            load_gcode(wxWindow* parent, wxString& input_file) const;


    std::string     handle_web_request(std::string cmd);
    void            request_open_project(std::string project_id);
    void            request_remove_project(std::string project_id);


    //Processing message events and user UI events in the main thread can be used in for code, but it is necessary to primarily determine whether to exit.
    void            process_msg_loop();
    //

    static std::string format_display_version();
    void            show_dialog(wxString msg);
    void            push_notification(wxString msg, wxString title = wxEmptyString, UserNotificationStyle style = UserNotificationStyle::UNS_NORMAL);
    void            save_user_default_filaments(AppConfig *new_app_config);
    void            start_http_server();

	
	void			check_user_lite_mode_dlg();
	

    void            persist_window_geometry(wxTopLevelWindow *window, bool default_maximized = false);
    void            update_ui_from_settings();

    bool            load_language(wxString language, bool initial);

    Tab*            get_tab(Preset::Type type);
    Tab*            get_plate_tab();
    Tab*            get_model_tab(bool part = false);
    Tab*            get_layer_tab();
    ConfigOptionMode get_mode();
    std::string     get_mode_str();
    void            save_mode(const /*ConfigOptionMode*/int mode, bool need_save = true) ;
    void            update_develop_state() ;
    void             update_mode(const int mode = -1    );
    void            update_internal_development();

    // BBS
    bool            has_unsaved_preset_changes() const;
    bool            has_current_preset_changes() const;
    void            update_saved_preset_from_current_preset();
    std::vector<std::pair<unsigned int, std::string>> get_selected_presets() const;
    bool            check_and_save_current_preset_changes(const wxString& caption, const wxString& header, bool remember_choice = true, bool use_dont_save_insted_of_discard = false);
    void            apply_keeped_preset_modifications();
    bool            check_and_keep_current_preset_changes(const wxString& caption, const wxString& header, int action_buttons, bool* postponed_apply_of_keeped_changes = nullptr);
    bool            can_load_project();
    bool            check_print_host_queue();
    bool            checked_tab(Tab* tab);
    //BBS: add preset combox re-active logic
    void            load_current_presets(bool active_preset_combox = false, bool check_printer_presets = true);

    wxString        current_language_code() const { return m_wxLocale->GetCanonicalName(); }
	// Translate the language code to a code, for which Prusa Research maintains translations. Defaults to "en_US".
    wxString 		current_language_code_safe() const;
    // Translate the active wxWidgets locale to the locale keys registered by the embedded Creality web apps.
    wxString        current_frontend_language_code() const;
    bool            is_localized() const { return m_wxLocale->GetLocale() != "English"; }
    void            open_preferences(size_t open_on_tab = 0, const std::string& highlight_option = std::string());
    void         open_config_relate(size_t open_on_tab = 0, const std::string& highlight_option = std::string());
    virtual bool OnExceptionInMainLoop() override;
    // Calls wxLaunchDefaultBrowser if user confirms in dialog.
    bool            open_browser_with_warning_dialog(const wxString& url, int flags = 0);
#ifdef __APPLE__
    void            OSXStoreOpenFiles(const wxArrayString &files);
    // wxWidgets override to get an event on open files.
    void            MacOpenFiles(const wxArrayString &fileNames) override;
    void            MacOpenURL(const wxString& url) override;
#endif /* __APPLE */

    Sidebar&             sidebar();
    GizmoObjectManipulation*  obj_manipul();
    ObjectSettings*      obj_settings();
    ObjectList*          obj_list();
    ObjectLayers*        obj_layers();
    Plater*              plater();
    const Plater*        plater() const;
    ParamsPanel*         params_panel();
    ParamsDialog*        params_dialog();
    Model&      		 model();
    NotificationManager * notification_manager();
    Downloader*          downloader();

    std::map<std::string, std::string> m_DevelopParamslist;


    void            run_script(wxString js);
    bool            is_adding_script_handler() { return m_adding_script_handler; }
    void            set_adding_script_handler(bool status) { m_adding_script_handler = status; }

    std::string     url_encode(std::string value);
    std::string     url_decode(std::string value);

    void            SaveProfile(json profileJson);
    bool            apply_config(AppConfig*           app_config,
                                 AppConfig*           app_config_new,
                                 PresetBundle*        preset_bundle,
                                 const PresetUpdater* updater,
                                 bool&                apply_keeped_changes);
    // Parameters extracted from the command line to be passed to GUI after initialization.
    GUI_InitParams* init_params { nullptr };

    AppConfig*      app_config{ nullptr };
    AppConfig*           m_appconfig_new{nullptr};
    std::atomic<bool>    m_device_add_in_progress{false};
    PresetBundle*   preset_bundle{ nullptr };
    PresetUpdater*  preset_updater{ nullptr };
    MainFrame*      mainframe{ nullptr };
    Plater*         plater_{ nullptr };
    PrinterPresetConfig* printerPresetConfig{ nullptr };

	PresetUpdater*  get_preset_updater() { return preset_updater; }

    Notebook*       tab_panel() const ;
    int             extruders_edited_cnt() const;

    // BBS
    int             filaments_cnt() const;
    PrintSequence   global_print_sequence() const;
    std::vector<Tab *>      tabs_list;
    std::vector<Tab *>      model_tabs_list;
    Tab*                    plate_tab;

	RemovableDriveManager* removable_drive_manager() { return m_removable_drive_manager.get(); }
	OtherInstanceMessageHandler* other_instance_message_handler() { return m_other_instance_message_handler.get(); }
    wxSingleInstanceChecker* single_instance_checker() {return m_single_instance_checker.get();}

	void        init_single_instance_checker(const std::string &name, const std::string &path);
	void        set_instance_hash (const size_t hash) { m_instance_hash_int = hash; m_instance_hash_string = std::to_string(hash); }
    std::string get_instance_hash_string ()           { return m_instance_hash_string; }
	size_t      get_instance_hash_int ()              { return m_instance_hash_int; }

    ImGuiWrapper* imgui() { return m_imgui.get(); }

    PrintHostJobQueue& printhost_job_queue() { return *m_printhost_job_queue.get(); }

    bool            may_switch_to_SLA_preset(const wxString& caption);
    bool            run_wizard(ConfigWizard::RunReason reason, ConfigWizard::StartPage start_page = ConfigWizard::SP_WELCOME);

    OpenGLManager& get_opengl_manager() { return m_opengl_mgr; }
    GLShaderProgram* get_shader(const std::string& shader_name) { return m_opengl_mgr.get_shader(shader_name); }
    GLShaderProgram* get_current_shader() { return m_opengl_mgr.get_current_shader(); }
	void bind_shader(const GLShaderProgram* p_shader);
    void unbind_shader();

    bool is_gl_version_greater_or_equal_to(unsigned int major, unsigned int minor) const { return m_opengl_mgr.get_gl_info().is_version_greater_or_equal_to(major, minor); }
    bool is_glsl_version_greater_or_equal_to(unsigned int major, unsigned int minor) const { return m_opengl_mgr.get_gl_info().is_glsl_version_greater_or_equal_to(major, minor); }

    // extend is stl/3mf/gcode/step etc 
    void            associate_files(std::wstring extend);
    void            disassociate_files(std::wstring extend);
    void            associate_url(std::wstring url_prefix);

    // URL download - PrusaSlicer gets system call to open prusaslicer:// URL which should contain address of download
    void            start_download(std::string url);
    int             load_machine_preset_data();
    bool            LoadFile(std::string jPath, std::string& sContent);


    std::string get_client_id();

    void startTour(int startIndex = 0);
    void startTour_Apple();

    void set_picking_effect(EPickingEffect effect);
    EPickingEffect get_picking_effect() const;

    void set_picking_color(const ColorRGB& color);
    const ColorRGB& get_picking_color() const;

private:
    UITour* m_UITour = nullptr;
    bool            on_init_inner();
    void            on_init_custom_config();
    void            init_app_config();
    void            copy_older_config();
    void            window_pos_save(wxTopLevelWindow* window, const std::string &name);
    bool            window_pos_restore(wxTopLevelWindow* window, const std::string &name, bool default_maximized = false);
    void            window_pos_sanitize(wxTopLevelWindow* window);
    void            window_pos_center(wxTopLevelWindow *window);



    bool                    m_init_app_config_from_older { false };
    bool                    m_datadir_redefined { false };
    std::string             m_older_data_dir_path;
    bool                    m_config_corrupted { false };
    std::string             m_open_method;
    bool                    need_exit_{false};

    
    std::mutex                                                                         download_mtx_;
    std::list<std::tuple<wxString, wxString, wxString, wxString, wxString>> download_tasks_;


    EPickingEffect          m_picking_effect{ EPickingEffect::Silhouette };
    ColorRGB                m_picking_color{ 1.0f, 1.0f, 1.0f };


};

DECLARE_APP(GUI_App)



bool is_support_filament(int extruder_id, bool strict_check = true);
bool is_soluble_filament(int extruder_id);
// check if the filament for model is in the list
bool has_filaments(const std::vector<string>& model_filaments);
} // namespace GUI
} // Slic3r

#endif // slic3r_GUI_App_hpp_
