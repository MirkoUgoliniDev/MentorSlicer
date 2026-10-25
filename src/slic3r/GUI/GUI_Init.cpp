#include "libslic3r/DataDirectoryMigration.hpp"
#include "GUI_Init.hpp"

#include "libslic3r/AppConfig.hpp"
#include "libslic3r/libslic3r.h"

#include "slic3r/GUI/GUI.hpp"
#include "slic3r/GUI/GUI_App.hpp"
#include "slic3r/GUI/3DScene.hpp"
#include "slic3r/GUI/InstanceCheck.hpp"
#include "slic3r/GUI/format.hpp"
#include "slic3r/GUI/MainFrame.hpp"
#include "slic3r/GUI/Plater.hpp"
#include "slic3r/GUI/Widgets/Label.hpp"
#include "libslic3r/Utils.hpp"
#include <algorithm>
#include <boost/algorithm/string.hpp> // icontains, for the alpha version folder
#include <boost/log/trivial.hpp>
#include <memory>

// To show a message box if GUI initialization ends up with an exception thrown.
#include <wx/msgdlg.h>
#ifdef _WIN32
#include <wx/msw/wrapwin.h> // MessageBoxW for data-folder errors raised before wx runs (PRINT-ADVISOR F8)
#endif

#include <boost/nowide/iostream.hpp>
#include <boost/nowide/convert.hpp>

#if __APPLE__
    #include <signal.h>
    #include <unistd.h> // getpid, getppid
#endif // __APPLE__
namespace Slic3r {
namespace GUI {

int GUI_Run(int argc, char **argv)
{
    DynamicPrintAndCommandLineConfig config;
    DynamicPrintConfig extra_config;
    std::vector<std::string> input_files;
    t_config_option_keys option_order;

    if (!config.read_cli(argc, argv, &input_files, &option_order)) {
        boost::nowide::cerr << "GUI parameter parsing failed" << std::endl;
        return 1;
    }

    const std::map<std::string, std::string> validity = config.validate(true);
    for (const t_optiondef_map *options : {
             &cli_actions_config_def.options,
             &cli_misc_config_def.options}) {
        for (const t_optiondef_map::value_type &definition : *options)
            config.option(definition.first, true);
    }

    set_data_dir(config.opt_string("datadir"));
    if (!validity.empty()) {
        boost::nowide::cerr << "GUI command-line parameter error:" << std::endl;
        for (const auto &item : validity)
            boost::nowide::cerr << item.first << ": " << item.second << std::endl;
        return 1;
    }

    extra_config.apply(config, true);
    extra_config.normalize_fdm();

    GUI_InitParams params;
    params.argc = argc;
    params.argv = argv;
    params.load_configs =
        config.option<ConfigOptionStrings>("load_settings", true)->values;
    params.extra_config = std::move(extra_config);
    params.input_files = std::move(input_files);
    return GUI_Run(params);
}
int GUI_Run(GUI_InitParams &params)
{
    ::Label::initSysFont();

    params.input_gcode = !params.input_files.empty() &&
        std::all_of(
            params.input_files.begin(),
            params.input_files.end(),
            [](const std::string &filename) { return is_gcode_file(filename); });
    BOOST_LOG_TRIVIAL(info)
        << "GUI input mode=" << (params.input_gcode ? "G-code viewer" : "editor")
        << ", input_files=" << params.input_files.size();

#if __APPLE__
    // On OSX, we use boost::process::spawn() to launch new instances of PrusaSlicer from another PrusaSlicer.
    // boost::process::spawn() sets SIGCHLD to SIGIGN for the child process, thus if a child PrusaSlicer spawns another
    // subprocess and the subrocess dies, the child PrusaSlicer will not receive information on end of subprocess
    // (posix waitpid() call will always fail).
    // https://jmmv.dev/2008/10/boostprocess-and-sigchld.html
    // The child instance of PrusaSlicer has to reset SIGCHLD to its default, so that posix waitpid() and similar continue to work.
    // See GH issue #5507
    signal(SIGCHLD, SIG_DFL);
#endif // __APPLE__

    //BBS: remove the try-catch and let exception goto above
    try {
        //GUI::GUI_App* gui = new GUI::GUI_App(params.start_as_gcodeviewer ? GUI::GUI_App::EAppMode::GCodeViewer : GUI::GUI_App::EAppMode::Editor);
        GUI::GUI_App* gui = new GUI::GUI_App();
        //if (gui->get_app_mode() != GUI::GUI_App::EAppMode::GCodeViewer) {
            // G-code viewer is currently not performing instance check, a new G-code viewer is started every time.
            bool gui_single_instance_setting = gui->app_config->get("app", "single_instance") == "true";
            BOOST_LOG_TRIVIAL(warning) << "GUI_Run: single_instance setting=" << (gui_single_instance_setting ? "true" : "false");
            if (Slic3r::instance_check(params.argc, params.argv, gui_single_instance_setting)) {
                BOOST_LOG_TRIVIAL(error) << "GUI_Run: instance_check returned true (another instance found). Exiting this instance.";
                //TODO: do we have delete gui and other stuff?
                return -1;
            } else {
                BOOST_LOG_TRIVIAL(warning) << "GUI_Run: instance_check returned false (no other instance).";
            }
        //}

//      gui->autosave = m_config.opt_string("autosave");
        GUI::GUI_App::SetInstance(gui);
        gui->init_params = &params;
        
        if (params.argc > 1) {
            // STUDIO-273 wxWidgets report error when opening some files with specific names
            // wxWidgets does not handle parameters, so intercept parameters here, only keep the app name
            // Log incoming args before trimming them
            {
                std::stringstream ss;
                ss << "GUI_Run: argc=" << params.argc << ", argv=[";
                for (int i = 0; i < params.argc; ++i) {
                    ss << (i == 0 ? "" : ", ") << (params.argv && params.argv[i] ? params.argv[i] : "<null>");
                }
                ss << "]";
                BOOST_LOG_TRIVIAL(warning) << ss.str();
            }
            BOOST_LOG_TRIVIAL(warning) << "GUI_Run: truncating argv to keep only argv[0] before wxEntry() (this may drop parameters)";
            int                 argc = 1;
            std::vector<char *> argv;
            argv.push_back(params.argv[0]);
            return wxEntry(argc, argv.data());
        } else {
            BOOST_LOG_TRIVIAL(warning) << "GUI_Run: argc <= 1, passing through original argv to wxEntry()";
            return wxEntry(params.argc, params.argv);
        }
    } catch (const Slic3r::DataMigration::ImportError &ex) {
        // PRINT-ADVISOR F8: the one-time import of the Creality Print folder failed. The log is not
        // configured yet and wx is not running: plain Win32 message box. The text is complete.
        BOOST_LOG_TRIVIAL(error) << ex.what();
#ifdef _WIN32
        ::MessageBoxW(nullptr, boost::nowide::widen(ex.what()).c_str(), L"MentorSlicer", MB_OK | MB_ICONSTOP);
#endif
        return 1;
    } catch (const Slic3r::DataMigration::Error &ex) {
        // Migration is automatic and non-interactive. Details are in the parent migration log.
        BOOST_LOG_TRIVIAL(error) << ex.what();
#ifdef _WIN32
        // PRINT-ADVISOR F8: say why the app does not start instead of exiting silently.
        const std::string message = std::string("MentorSlicer non può preparare la cartella dati:\n\n") + ex.what();
        ::MessageBoxW(nullptr, boost::nowide::widen(message).c_str(), L"MentorSlicer", MB_OK | MB_ICONSTOP);
#endif
        return 1;
    } catch (const Slic3r::Exception &ex) {
        BOOST_LOG_TRIVIAL(error) << ex.what() << std::endl;
        wxMessageBox(boost::nowide::widen(ex.what()), _L("MentorSlicer GUI initialization failed"), wxICON_STOP);
    } catch (const std::exception &ex) {
        BOOST_LOG_TRIVIAL(error) << ex.what() << std::endl;
        wxMessageBox(format_wxstr(_L("Fatal error, exception catched: %1%"), ex.what()), _L("MentorSlicer GUI initialization failed"), wxICON_STOP);
    }
    // error
    return 1;
}
}
}
