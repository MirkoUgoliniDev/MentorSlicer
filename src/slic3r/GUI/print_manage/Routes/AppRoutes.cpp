// [FORMATTED BY CLANG-FORMAT 2025-10-24 10:54:26]
#include "AppRoutes.hpp"
#include "nlohmann/json.hpp"
#include "../TypeDefine.hpp"
#include "../AppMgr.hpp"
#include "../AppUtils.hpp"

#include "SysRoutes.hpp"
#include "DeviceMgrRoutes.hpp"

namespace DM {
bool AppRoutes::Invoke(wxWebView* browser, const std::string& data)
{
    try {
        nlohmann::json j = nlohmann::json::parse(data);
        if (j.contains("command")) {
            std::string command = j["command"].get<std::string>();

            bool bSysEvent = false;

            Apps apps;
            AppMgr::Ins().GetNeedToReceiveSysEventApp(command, apps);

            for (auto& app : apps) {
                if (app.browser != browser) {
                    bSysEvent = true;
                    AppUtils::PostMsg(app.browser, j);
                }
            }

            // The handler tables hold no state: build them once.
            static SysRoutes       sys_routes;
            static DeviceMgrRoutes device_mgr_routes;
            if (sys_routes.Invoke(browser, j, data))
                return true;
            if (device_mgr_routes.Invoke(browser, j, data))
                return true;
        }
    } catch (...) {
        // BOOST_LOG_TRIVIAL(error) << "AppRoutes::Invoke error: " << e.what();
    }

    return false;
}

AppRoutes::AppRoutes() {}
} // namespace DM