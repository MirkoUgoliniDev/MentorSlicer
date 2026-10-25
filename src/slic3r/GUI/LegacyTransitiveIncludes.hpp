#ifndef slic3r_GUI_LegacyTransitiveIncludes_hpp_
#define slic3r_GUI_LegacyTransitiveIncludes_hpp_

// Transitional header (F5, Bambu code removal).
// Until F5 the Bambu bind, device, camera, print-job and network headers reached almost
// every translation unit through GUI_App.hpp. They carried the includes and the global
// using-directives below. This header keeps them so the removal does not break unrelated
// files. Prune it later, one line at a time, with a build after each change.
// The comments deliberately avoid the removed class names, so the F5 grep checklist stays clean.

#include <chrono>
#include <deque>
#include <exception>
#include <functional>
#include <future>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <unordered_set>
#include <vector>

#include <boost/filesystem.hpp>
#include <boost/filesystem/operations.hpp>
#include <boost/filesystem/path.hpp>
#include <boost/format.hpp>
#include <boost/log/trivial.hpp>
#include <boost/nowide/fstream.hpp>
#include <boost/optional.hpp>
#include <boost/thread.hpp>
#include <boost/thread/condition_variable.hpp>

#include <curl/curl.h>
#include "nlohmann/json.hpp"

#include <wx/wx.h>
#include <wx/artprov.h>
#include <wx/bitmap.h>
#include <wx/bmpbuttn.h>
#include <wx/bmpcbox.h>
#include <wx/button.h>
#include <wx/checklst.h>
#include <wx/colour.h>
#include <wx/combo.h>
#include <wx/dataview.h>
#include <wx/dialog.h>
#include <wx/event.h>
#include <wx/font.h>
#include <wx/gauge.h>
#include <wx/gbsizer.h>
#include <wx/gdicmn.h>
#include <wx/hyperlink.h>
#include <wx/icon.h>
#include <wx/image.h>
#include <wx/menu.h>
#include <wx/panel.h>
#include <wx/popupwin.h>
#include <wx/progdlg.h>
#include <wx/scrolwin.h>
#include <wx/settings.h>
#include <wx/simplebook.h>
#include <wx/sizer.h>
#include <wx/spinctrl.h>
#include <wx/splitter.h>
#include <wx/statbmp.h>
#include <wx/statline.h>
#include <wx/stattext.h>
#include <wx/string.h>
#include <wx/textctrl.h>
#include <wx/tglbtn.h>
#include <wx/timer.h>
#include <wx/uri.h> // not media-only: WebGuideDialog.cpp and print_manage/App/PrinterMgrView.cpp use wxURI
#include <wx/version.h>
#include <wx/vlbox.h>
#include <wx/weakref.h>
#include <wx/webrequest.h>
#include <wx/window.h>
#include <wx/xrc/xmlres.h>

#include "libslic3r/Color.hpp"
#include "libslic3r/Preset.hpp"
#include "libslic3r/PrintConfig.hpp"
#include "libslic3r/ProjectTask.hpp"
#include "libslic3r/Semver.hpp"
#include "libslic3r/Utils.hpp"
#include "libslic3r/calib.hpp"

#include "slic3r/GUI/BitmapCache.hpp"
#include "slic3r/GUI/GUI.hpp"
#include "slic3r/GUI/GUI_Utils.hpp"
#include "slic3r/GUI/I18N.hpp"
#include "slic3r/GUI/Jobs/Job.hpp"
#include "slic3r/GUI/Jobs/ProgressIndicator.hpp"
#include "slic3r/GUI/Jobs/Worker.hpp"
#include "slic3r/GUI/Widgets/Button.hpp"
#include "slic3r/GUI/Widgets/Label.hpp"
#include "slic3r/GUI/Widgets/PopupWindow.hpp"
#include "slic3r/GUI/Widgets/ProgressBar.hpp"
#include "slic3r/GUI/Widgets/ProgressDialog.hpp"
#include "slic3r/GUI/Widgets/RadioBox.hpp"
#include "slic3r/GUI/Widgets/RoundedRectangle.hpp"
#include "slic3r/GUI/Widgets/StateColor.hpp"
#include "slic3r/GUI/Widgets/StateHandler.hpp"
#include "slic3r/GUI/Widgets/StaticBox.hpp"
#include "slic3r/GUI/Widgets/StaticLine.hpp"
#include "slic3r/GUI/Widgets/SwitchButton.hpp"
#include "slic3r/GUI/Widgets/TextInput.hpp"
#include "slic3r/GUI/wxExtensions.hpp"

// Global directives that used to come from the removed Bambu JSON-diff and device headers.
using json = nlohmann::json;
using namespace std;
using namespace nlohmann;

#endif // slic3r_GUI_LegacyTransitiveIncludes_hpp_
