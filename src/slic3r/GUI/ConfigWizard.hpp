#ifndef slic3r_ConfigWizard_hpp_
#define slic3r_ConfigWizard_hpp_

// The old configuration wizard is gone: run_wizard() opens the web guide (GuideFrame).
// Only the enums that describe why and where the guide starts are left here.
// The includes are kept because several files get them through this header.
#include <wx/dialog.h>

#include "GUI_Utils.hpp"

namespace Slic3r {
namespace GUI {

class ConfigWizard
{
public:
    // Why is the Wizard run
    enum RunReason {
        RR_DATA_EMPTY,                  // No or empty datadir
        RR_DATA_LEGACY,                 // Pre-updating datadir
        RR_DATA_INCOMPAT,               // Incompatible datadir - Slic3r downgrade situation
        RR_USER,                        // User requested the Wizard from the menus
    };

    // What page should wizard start on
    enum StartPage {
        SP_WELCOME,
        SP_PRINTERS,
        SP_FILAMENTS,
        SP_MATERIALS,
        SP_CUSTOM,
    };
};

}
}

#endif
