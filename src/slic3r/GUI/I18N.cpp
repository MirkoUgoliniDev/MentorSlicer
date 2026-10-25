#include "I18N.hpp"
#include "GUI_App.hpp"

namespace Slic3r { namespace GUI { 

namespace I18N {

bool is_chinese()
{
	static std::string cached_lang;
	static bool cached_result = false;
	
	// Get current language setting
	std::string lang = wxGetApp().app_config ? wxGetApp().app_config->get("language") : "";
	
	// Cache the result to avoid repeated string comparisons
	if (lang != cached_lang) {
		cached_lang = lang;
		// Check for Chinese language codes (zh_CN, zh_TW, etc.)
		cached_result = (lang.find("zh_CN") == 0);
	}
	
	return cached_result;
}

} // namespace I18N

wxString L_str(const std::string &str)
{
	//! Explicitly specify that the source string is already in UTF-8 encoding
	return wxGetTranslation(wxString(str.c_str(), wxConvUTF8));
}

} }
