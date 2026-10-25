#ifndef slic3r_PrinterPresetConfig_hpp_
#define slic3r_PrinterPresetConfig_hpp_

#include <string>
#include <vector>


#include "LegacyTransitiveIncludes.hpp" // F5: json alias and using-directives that came with the removed JSON-diff header
#include "libslic3r/AppConfig.hpp"

namespace Slic3r {
namespace GUI {

class PrinterPresetConfig
{
public:
    PrinterPresetConfig();
    ~PrinterPresetConfig();

    std::vector<std::string> getFilament(const std::string& printerName, const std::string& nozzle);
    bool getPrinterDefaultMaterials(const std::string& vendor, const std::string& printerName, std::vector<std::string>& vtPrinterDefaultMaterials);
    bool getPrinterDefaultMaterials(const std::string& vendor, const std::string& printerName, std::vector<std::pair<std::string, std::string>>& vtPrinterDefaultMaterials);
    int                      LoadProfile();
    json                     getProfileJson() const { 
        json m_Res           = json::object();
        m_Res["command"]     = "response_userguide_profile";
        m_Res["sequence_id"] = "10001";
        m_Res["response"]    = m_ProfileJson;
        m_Res["MachineJson"] = m_MachineJson;
        m_Res["user_preset"] = json::array(); 

        return m_Res; 
    }

private:
    bool LoadFile(std::string jPath, std::string& sContent);

private:
    json m_ProfileJson;
    json m_MachineJson;
    AppConfig m_appconfig_new;

    std::map<std::string, std::string> m_mapMachineThumbnail;
    bool bbl_bundle_rsrc;
    boost::filesystem::path vendor_dir;
    boost::filesystem::path rsrc_vendor_dir;
    std::string m_Region;
};

}
}

#endif
