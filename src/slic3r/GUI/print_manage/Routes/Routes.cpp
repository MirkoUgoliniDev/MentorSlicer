#include "Routes.hpp"
#include "nlohmann/json.hpp"
#include "../AppUtils.hpp"

namespace DM{
    bool Routes::Invoke(wxWebView* browser, nlohmann::json &j, const std::string& data)
    {
        if (j.contains("command")) {
            std::string command = j["command"].get<std::string>();
            auto it = m_handlers.find(command);
            if (it != m_handlers.end()) {
                return it->second(browser, data, j, command);
            }
        }

        return false;
    }

    void Routes::Handler(const std::vector<std::string>commands, std::function<bool(wxWebView* browse, const std::string&, nlohmann::json&, const std::string)> handler) {
        for(auto cmd : commands){
            m_handlers[cmd] = handler;
        }
    }

    Routes::Routes()
    {
    }
}