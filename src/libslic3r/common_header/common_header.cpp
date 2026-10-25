#include "common_header.h"

#include "../libslic3r/libslic3r_version.h"
namespace Slic3r { 
namespace CxBuildInfo {
 void setDarkMode(bool isDark) {
     if (g_is_dark != isDark) {

     }
     g_is_dark = isDark;
 }

const bool isCusotmized()
    {
#if defined(CUSTOMIZED)
    return true;
#else
    return false;
#endif
}

const std::string getIconName_noTheme() {
#if defined(CUSTOMIZED) && defined(CUSTOM_ICON_NAME)
    return std::string(CUSTOM_ICON_NAME);
#endif //
    return std::string(SLIC3R_APP_NAME);
}
const std::string getIconName()
{
#if defined(CUSTOMIZED) && defined(CUSTOM_ICON_NAME)
    if ( g_is_dark && std::string(CUSTOM_ICON_NAME) == "MorandiPrint")
    {
        return std::string(CUSTOM_ICON_NAME) + "_light";
    }
    return std::string(CUSTOM_ICON_NAME);
#endif //
    return std::string(SLIC3R_APP_NAME);
}

}

} // namespace Slic3r