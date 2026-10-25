#include "SysRoutes.hpp"
#include "nlohmann/json.hpp"
#include "../AppUtils.hpp"
#include "slic3r/GUI/GUI_App.hpp"
#include <random>

using namespace Slic3r::GUI;

namespace DM{
    namespace {
    // Fork: an id generated once per app session (64 hex digits, the length of the old SHA-256
    // fingerprint of the PC). The Device page uses it as the client id of the camera stream.
    const std::string& session_system_id()
    {
        static const std::string id = [] {
            std::random_device rd;
            static const char digits[] = "0123456789abcdef";
            std::string s;
            for (int i = 0; i < 64; ++i)
                s += digits[rd() & 15];
            return s;
        }();
        return id;
    }
    } // namespace

    SysRoutes::SysRoutes()
    {
        //
        this->Handler({"is_dark_theme"}, [](wxWebView*browse, const std::string& data, const nlohmann::json& json_data, const std::string cmd) {
            wxString lan = wxGetApp().current_language_code_safe();
            nlohmann::json commandJson;
            commandJson["command"] = "is_dark_theme";
            commandJson["data"] = wxGetApp().dark_mode();

            AppUtils::PostMsg(browse, commandJson);
            return true;
        });

        this->Handler({"get_system_id"}, [](wxWebView* browse, const std::string& data, nlohmann::json& json_data, const std::string cmd) {
            nlohmann::json systemId;
            systemId["command"] = "get_system_id";
            systemId["data"]    = session_system_id();
            AppUtils::PostMsg(browse, systemId);
            return true;
        });

        //
        // Fork: no Creality account (F4). The Device page still asks for the user; with an empty
        // userId it clears its cloud state, so answer with empty fields.
        this->Handler({"get_user"}, [](wxWebView* browse, const std::string& data, nlohmann::json& json_data, const std::string cmd) {
            std::string country_code = wxGetApp().app_config->get("region");
            unsigned pid = Slic3r::get_current_pid();

            nlohmann::json top_level_json = {
                {"bLogin", 0},
                {"token", ""},
                {"userId", ""},
                {"region", country_code},
                {"pid", pid}
            };

            nlohmann::json commandJson = {
                {"command", "get_user"},
                {"data", top_level_json}
            };

            AppUtils::PostMsg(browse, commandJson);
            return true;
            });

        //
        this->Handler({ "common_openurl" }, [](wxWebView* browse, const std::string& data, nlohmann::json& json_data, const std::string cmd) {
            wxLaunchDefaultBrowser(json_data["url"].get<std::string>());

            return true;
            });

        this->Handler({ "switch_to_tab" }, [](wxWebView* browse, const std::string& data, nlohmann::json& json_data, const std::string cmd) {
            std::string tabname = json_data["tabName"];
            std::string pageName = json_data["pageName"];
            wxGetApp().switch_to_tab(tabname);
            if (!pageName.empty()) {
                wxGetApp().swith_community_sub_page(pageName);
            }
            return true;
            });
    
        

        this->Handler({ "get_lang" }, [](wxWebView* browse, const std::string& data, nlohmann::json& json_data, const std::string cmd) {
            wxString lan = wxGetApp().current_frontend_language_code();
            nlohmann::json commandJson;
            commandJson["command"] = "get_lang";
            commandJson["data"] = lan.ToStdString();

            AppUtils::PostMsg(browse, commandJson);
            return true;
            });


        this->Handler({ "get_region" }, [](wxWebView* browse, const std::string& data, nlohmann::json& json_data, const std::string cmd) {
            std::string country_code = wxGetApp().app_config->get("region");
            nlohmann::json commandJson;
            commandJson["command"] = "get_region";
            commandJson["data"] = country_code;

            AppUtils::PostMsg(browse, commandJson);
            return true;
            });

    }

}