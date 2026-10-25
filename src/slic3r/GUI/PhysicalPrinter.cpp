#include "PhysicalPrinter.hpp"

#include "GUI_App.hpp"
#include "slic3r/Utils/PrintHost.hpp"

namespace Slic3r { namespace GUI {

PhysicalPrinter::PhysicalPrinter(const int& hostType,const string& hostUrl,const string& apiKey, const string& caFile, const bool& ignoreCertRevocation) 
{ 
    this->m_hostType = hostType;
    this->m_hostUrl  = hostUrl;
    this->m_apiKey = apiKey; 
    m_config  = &wxGetApp().preset_bundle->printers.get_edited_preset().config;
    if (!m_config)
        return ;
    // Host types come from the device page as integers: ignore values outside the enum.
    if (hostType < 0 || hostType > int(htCrealityPrint))
        return ;
    PrintHostType type = static_cast<PrintHostType>(hostType);
    m_config->set_key_value("host_type", new ConfigOptionEnum<PrintHostType>(type));
    m_config->opt_string("printhost_apikey") = apiKey;
    m_config->opt_string("print_host")       = hostUrl;
    m_config->opt_string("printhost_cafile") = caFile;
    m_config->set_key_value("printhost_ssl_ignore_revoke",new ConfigOptionBool(ignoreCertRevocation));

}

bool PhysicalPrinter::TestConnection(string& info)
{
    if (!this->m_config) return false;
    std::unique_ptr<PrintHost> host(PrintHost::get_print_host(this->m_config));
    if (!host) {
        return false;
    }

    wxString msg;
    bool     result = host->test(msg);
    if (result) 
    {
        info = (host->get_test_ok_msg()).ToUTF8();
    } 
    else 
    {
        info = (host->get_test_failed_msg(msg)).ToUTF8();
    }

    return result;
}




}} // namespace Slic3r::GUI