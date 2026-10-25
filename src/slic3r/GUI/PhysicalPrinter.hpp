#ifndef slic3r_PhysicalPrinter_hpp_
#define slic3r_PhysicalPrinter_hpp_

#include "libslic3r/Preset.hpp"
using namespace std;

namespace Slic3r { 
namespace GUI {

class PhysicalPrinter
{

public:
    PhysicalPrinter(const int& hostType,const string& hostUrl,const string& apiKey, const string& caFile, const bool& ignoreCertRevocation);
    ~PhysicalPrinter() {};

    bool TestConnection(string& info);

private:
    int m_hostType;
    string m_hostUrl;
    string m_apiKey;
    DynamicPrintConfig* m_config{nullptr};
       
};

} 
} 


#endif
