#ifndef RemotePrint_Routes_hpp_
#define RemotePrint_Routes_hpp_
#include "nlohmann/json.hpp"
#include <string>

class wxWebView;
namespace DM{

    class Routes
    {
    public:
        Routes();
        bool Invoke(wxWebView* browse, nlohmann::json &j, const std::string& data);

    protected:
        std::unordered_map< std::string, std::function<bool(wxWebView*, const std::string&, nlohmann::json&, const std::string)>> m_handlers;
        void Handler(const std::vector<std::string>commands, std::function<bool(wxWebView*, const std::string&, nlohmann::json&, const std::string)> handler);
    };
}
#endif /* RemotePrint_DeviceDB_hpp_ */