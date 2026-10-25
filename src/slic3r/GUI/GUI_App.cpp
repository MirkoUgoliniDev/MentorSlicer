#include "libslic3r/DataDirectoryMigration.hpp"
#include "libslic3r/DeviceAddUtils.hpp"
#include "DataDirectoryVersion.hpp"
#include "boost/filesystem/operations.hpp"
#include "libslic3r/Technologies.hpp"
#include "GUI_App.hpp"
#include "MentorSlicerVersion.hpp"
#include "BaseColorSpectraCache.hpp"
#include "OfficialMaterialColorCache.hpp"
#include "slic3r/Utils/Http.hpp"
#include <nlohmann/json.hpp>
#include "PrinterCoverRoute.hpp"
#include "GUI_Init.hpp"
#include "LinuxDisplayBackend.hpp"
#include "GUI_ObjectList.hpp"
#include "GUI_Factories.hpp"
#include "format.hpp"
#include "libslic3r_version.h"
#include "Downloader.hpp"
#include <string>
#include <cstring>
#include <wx/colour.h>
#include <wx/event.h>
#include <wx/bitmap.h>
#include <wx/string.h>
#include <wx/dialog.h>
#include "slic3r/GUI/print_manage/utils/cxmdns.h"
#include "slic3r/GUI/print_manage/Utils.hpp"
#include "slic3r/Utils/ColorSpaceConvert.hpp"
#include "ImGuiWrapper.hpp"
#include "slic3r/GUI/WebViewDialog.hpp"
#include "slic3r/GUI/Widgets/WebView.hpp"
#include "slic3r/GUI/simple/printAdvisor/PrintAdvisorHooks.hpp" // PRINT-ADVISOR advisor-hook
#include "slic3r/GUI/print_manage/utils/cxmdns.h"
#include "slic3r/GUI/print_manage/Utils.hpp"
#include "Widgets/HoverBorderIcon.hpp"
#include "Widgets/Label.hpp"
#include "slic3r/GUI/MsgDialog.hpp"
// Localization headers: include libslic3r version first so everything in this file
// uses the slic3r/GUI version (the macros will take precedence over the functions).
// Also, there is a check that the former is not included from slic3r module.
// This is the only place where we want to allow that, so define an override macro.
#define SLIC3R_ALLOW_LIBSLIC3R_I18N_IN_SLIC3R
#include "libslic3r/I18N.hpp"
#undef SLIC3R_ALLOW_LIBSLIC3R_I18N_IN_SLIC3R
#include "slic3r/GUI/I18N.hpp"

#include <algorithm>
#include <iterator>
#include <exception>
#include <stdexcept>
#include <cstdlib>
#include <cctype>
#include <regex>
#include <thread>
#include <string_view>
#include <fstream>
#include <chrono>
#include <iomanip>
#include <boost/filesystem.hpp>
#include <boost/algorithm/string/predicate.hpp>
#include <boost/algorithm/string.hpp>
#include <boost/format.hpp>
#include <boost/lexical_cast.hpp>
#include <boost/log/core.hpp>
#include <boost/log/trivial.hpp>
#include <boost/nowide/convert.hpp>
#include <boost/property_tree/ptree.hpp>
#include <boost/property_tree/json_parser.hpp>
#include <boost/uuid/uuid.hpp>
#include <boost/uuid/uuid_generators.hpp>
#include <boost/uuid/uuid_io.hpp>
#include <boost/uuid/detail/sha1.hpp>
#ifdef _WIN32
#include <boost/algorithm/hex.hpp>
#include <boost/uuid/detail/md5.hpp>
#endif

#ifndef _WIN32
#include <sys/statvfs.h>
#endif

#include "slic3r/GUI/MsgDialog.hpp"

#include <wx/stdpaths.h>
#include <wx/imagpng.h>
#include <wx/display.h>
#include <wx/menu.h>
#include <wx/menuitem.h>
#include <wx/filedlg.h>
#include <wx/progdlg.h>
#include <wx/dir.h>
#include <wx/wupdlock.h>
#include <wx/filefn.h>
#include <wx/sysopt.h>
#include <wx/process.h>
#include <wx/richmsgdlg.h>
#include <wx/hyperlink.h>
#include <wx/log.h>
#include <wx/intl.h>
#include <wx/tokenzr.h>
#include <wx/dialog.h>
#include <wx/textctrl.h>
#include <wx/splash.h>
#include <wx/fontutil.h>
#include <wx/glcanvas.h>
#include <wx/dcgraph.h>
#include <wx/evtloop.h>

#include "libslic3r/Utils.hpp"
#include "libslic3r/Model.hpp"
#include "libslic3r/ModelVolume.hpp"
#include "libslic3r/PresetBundle.hpp"
#include "libslic3r/Thread.hpp"
#include "libslic3r/miniz_extension.hpp"
#include "libslic3r/Color.hpp"
#include "PresetComboBoxes.hpp"
#include <wx/animate.h>
#include <wx/dynarray.h>
#include <wx/grid.h>
#include "GUI.hpp"
#include "GUI_Utils.hpp"
#include "3DScene.hpp"
#include "MainFrame.hpp"
#include "Plater.hpp"
#include "GLCanvas3D.hpp"

#include "../Utils/PresetUpdater.hpp"
#include "../Utils/PrintHost.hpp"
#include "../Utils/Process.hpp"
#include "../Utils/MacDarkMode.hpp"
#include "../Utils/Http.hpp"
#include "../Utils/UndoRedo.hpp"
#include "../Utils/ProfileFamilyLoader.hpp"
#include "../Utils/mouse_scheme.hpp"
#include "Preferences.hpp"
#include "ConfigRelateDialog.hpp"
#include "Tab.hpp"
#include "Mouse3DController.hpp"
#include "RemovableDriveManager.hpp"
#include "InstanceCheck.hpp"
#include "NotificationManager.hpp"
#include "UnsavedChangesDialog.hpp"
#include "SavePresetDialog.hpp"
#include "PrintHostDialogs.hpp"
#include "DesktopIntegrationDialog.hpp"
#include "ParamsDialog.hpp"

#include "KBShortcutsDialog.hpp"

#include "BitmapCache.hpp"
#include "Notebook.hpp"
#include "Widgets/Label.hpp"
#include "Widgets/ProgressDialog.hpp"
#include "Widgets/TabCtrl.hpp"

//BBS: DailyTip and UserGuide Dialog
#include "WebGuideDialog.hpp"
#include "EnableLiteModeDialog.hpp"
#include "HintNotification.hpp"
#include <boost/url.hpp>
#include <boost/bind.hpp>
#include <boost/thread.hpp>
#include <boost/regex.hpp>
#ifdef _WIN32
#include <boost/interprocess/sync/file_lock.hpp>
#endif
#include "libslic3r/common_header/common_header.h"
#include "buildinfo.h"
#ifdef __APPLE__
#include "libslic3r/MacUtils.hpp"
#endif

#ifdef __WXMSW__
#include <dbt.h>
#include <shlobj.h>

#ifdef __WINDOWS__
#ifdef _MSW_DARK_MODE
#include "dark_mode.hpp"
#include "wx/headerctrl.h"
#include "wx/msw/headerctrl.h"
#endif // _MSW_DARK_MODE
#endif // __WINDOWS__

#endif
#ifdef _WIN32
#include <boost/dll/runtime_symbol_info.hpp>
#endif

#ifdef WIN32
#include <iphlpapi.h>
#endif

// Needed for forcing menu icons back under gtk2 and gtk3
#if defined(__WXGTK20__) || defined(__WXGTK3__)
    #include <gtk/gtk.h>
    #include <gio/gio.h>
#endif
#include "print_manage/AppMgr.hpp"
#include "PrinterPresetConfig.hpp"

using namespace std::literals;
namespace pt = boost::property_tree;

namespace Slic3r {
namespace GUI {

class MainFrame;

static std::atomic<bool> g_app_exiting { false };

using PresetNameReplacements = std::vector<std::pair<std::string, std::string>>;

static bool replace_legacy_preset_name(std::string& value, const PresetNameReplacements& replacements)
{
    bool changed = false;
    for (const auto& [old_name, new_name] : replacements) {
        if (old_name.empty() || old_name == new_name)
            continue;

        size_t pos = 0;
        while ((pos = value.find(old_name, pos)) != std::string::npos) {
            value.replace(pos, old_name.size(), new_name);
            pos += new_name.size();
            changed = true;
        }
    }
    return changed;
}

static bool migrate_json_preset_names(nlohmann::json& value, const PresetNameReplacements& replacements)
{
    bool changed = false;
    if (value.is_string()) {
        std::string migrated = value.get<std::string>();
        if (replace_legacy_preset_name(migrated, replacements)) {
            value = std::move(migrated);
            changed = true;
        }
    } else if (value.is_array()) {
        for (auto& item : value)
            changed = migrate_json_preset_names(item, replacements) || changed;
    } else if (value.is_object()) {
        for (auto& item : value)
            changed = migrate_json_preset_names(item, replacements) || changed;

        std::vector<std::pair<std::string, std::string>> renamed_keys;
        for (auto it = value.begin(); it != value.end(); ++it) {
            std::string migrated_key = it.key();
            if (replace_legacy_preset_name(migrated_key, replacements))
                renamed_keys.emplace_back(it.key(), std::move(migrated_key));
        }
        for (const auto& [old_key, new_key] : renamed_keys) {
            // Keep an already migrated value when old and new data coexist.
            if (!value.contains(new_key))
                value[new_key] = std::move(value[old_key]);
            value.erase(old_key);
            changed = true;
        }
    }
    return changed;
}

#ifdef _WIN32
static std::string preset_migration_md5_line(const std::string_view data)
{
    using boost::uuids::detail::md5;
    md5 md5_hash;
    md5::digest_type md5_digest {};
    std::string md5_digest_str;
    md5_hash.process_bytes(data.data(), data.size());
    md5_hash.get_digest(md5_digest);
    boost::algorithm::hex(md5_digest, md5_digest + std::size(md5_digest), std::back_inserter(md5_digest_str));
    return "# MD5 checksum " + md5_digest_str + "\n";
}
#endif

static bool write_migrated_json(const fs::path& path, const nlohmann::json& value, bool app_config)
{
    const std::string serialized = value.dump(4);
    fs::path tmp_path = path;
    tmp_path += (boost::format(".%1%.preset-name-migration.tmp") % get_current_pid()).str();

    boost::nowide::ofstream output(tmp_path.string(), std::ios::out | std::ios::trunc);
    if (!output.is_open()) {
        BOOST_LOG_TRIVIAL(error) << "Failed to open preset-name migration output: " << tmp_path.string();
        return false;
    }
    output << serialized << '\n';
#ifdef _WIN32
    if (app_config)
        output << preset_migration_md5_line(serialized);
#else
    (void) app_config;
#endif
    output.close();
    if (!output) {
        BOOST_LOG_TRIVIAL(error) << "Failed to write preset-name migration output: " << tmp_path.string();
        boost::system::error_code remove_error;
        fs::remove(tmp_path, remove_error);
        return false;
    }

    if (app_config) {
        std::string copy_error;
        if (copy_file(tmp_path.string(), path.string() + ".bak", copy_error, false) != SUCCESS)
            BOOST_LOG_TRIVIAL(error) << "Failed to update the migrated application config backup: " << copy_error;
    }

    const std::error_code rename_error = rename_file(tmp_path.string(), path.string());
    if (rename_error) {
        BOOST_LOG_TRIVIAL(error) << "Failed to replace migrated JSON " << path.string() << ": " << rename_error.message();
        boost::system::error_code remove_error;
        fs::remove(tmp_path, remove_error);
        return false;
    }
    return true;
}

static bool migrate_json_file(const fs::path& path, const PresetNameReplacements& replacements,
                              bool app_config = false, bool* completed = nullptr)
{
    if (completed != nullptr)
        *completed = false;
    try {
        boost::nowide::ifstream input(path.string());
        if (!input.is_open())
            return false;

        std::stringstream buffer;
        buffer << input.rdbuf();
        std::string contents = buffer.str();
        if (app_config) {
            const size_t json_end = contents.find_last_of('}');
            if (json_end == std::string::npos)
                return false;
            contents.resize(json_end + 1);
        }

        nlohmann::json value = nlohmann::json::parse(contents);
        if (!migrate_json_preset_names(value, replacements)) {
            if (completed != nullptr)
                *completed = true;
            return false;
        }
        const bool written = write_migrated_json(path, value, app_config);
        if (completed != nullptr)
            *completed = written;
        return written;
    } catch (const std::exception& error) {
        BOOST_LOG_TRIVIAL(error) << "Failed to migrate preset names in " << path.string() << ": " << error.what();
        return false;
    }
}

static PresetNameReplacements legacy_multi_extruder_preset_names()
{
    PresetNameReplacements replacements;
    const auto collect_replacements = [&replacements](const fs::path& profile_root) {
        const fs::path manifest = profile_root / "Creality.json";
        try {
            boost::nowide::ifstream input(manifest.string());
            if (!input.is_open())
                return;

            const nlohmann::json vendor = nlohmann::json::parse(input);
            if (!vendor.contains("machine_list") || !vendor["machine_list"].is_array())
                return;

            for (const auto& entry : vendor["machine_list"]) {
                const std::string sub_path = entry.value("sub_path", std::string());
                if (sub_path.empty())
                    continue;

                const fs::path machine_path = profile_root / "Creality" / sub_path;
                boost::nowide::ifstream machine_input(machine_path.string());
                if (!machine_input.is_open())
                    continue;

                const nlohmann::json machine = nlohmann::json::parse(machine_input);
                const std::string current_name = machine.value("name", entry.value("name", std::string()));
                const std::string model = machine.value("printer_model", std::string());
                const auto nozzle_it = machine.find("nozzle_diameter");
                if (machine.value("type", std::string()) != "machine" || current_name.empty() ||
                    model == "Creality Sermoon D3 Pro" || boost::ends_with(current_name, " nozzle") ||
                    nozzle_it == machine.end() || !nozzle_it->is_array() || nozzle_it->size() <= 1)
                    continue;

                std::string variant = machine.value("printer_variant", std::string());
                if (variant.empty() && !nozzle_it->empty() && nozzle_it->front().is_string())
                    variant = nozzle_it->front().get<std::string>();
                if (variant.empty())
                    continue;

                replacements.emplace_back(current_name + " " + variant + " nozzle", current_name);
            }
        } catch (const std::exception& error) {
            BOOST_LOG_TRIVIAL(error) << "Failed to collect legacy multi-extruder preset names from " << manifest.string()
                                     << ": " << error.what();
        }
    };

    // A printer installed by an online parameter update may exist only in the
    // writable system bundle. Inspect it as well as the bundled resources.
    collect_replacements(fs::path(resources_dir()).append("profiles"));
    collect_replacements(fs::path(data_dir()).append(PRESET_SYSTEM_DIR));

    std::sort(replacements.begin(), replacements.end(), [](const auto& lhs, const auto& rhs) {
        return lhs.first.size() > rhs.first.size();
    });
    replacements.erase(std::unique(replacements.begin(), replacements.end()), replacements.end());
    return replacements;
}

static bool migrate_loaded_app_config_preset_names(AppConfig& config,
                                                   const PresetNameReplacements& replacements)
{
    bool changed = false;
    for (const std::string& section_name : {AppConfig::SECTION_FILAMENTS, std::string("presets")}) {
        if (!config.has_section(section_name))
            continue;

        const std::map<std::string, std::string>& old_section = config.get_section(section_name);
        std::map<std::string, std::string> new_section;
        for (const auto& [old_key, old_value] : old_section) {
            std::string new_key = old_key;
            std::string new_value = old_value;
            changed = replace_legacy_preset_name(new_key, replacements) || changed;
            changed = replace_legacy_preset_name(new_value, replacements) || changed;

            // Prefer a value that was already stored under the current name.
            if (new_section.find(new_key) == new_section.end() || new_key == old_key)
                new_section[new_key] = std::move(new_value);
        }
        if (new_section != old_section)
            config.set_section(section_name, new_section);
    }
    return changed;
}

static bool migrate_user_preset_names(const fs::path& user_root, const PresetNameReplacements& replacements)
{
    if (replacements.empty() || !fs::exists(user_root))
        return true;

    std::vector<fs::path> files;
    bool completed = true;
    try {
        for (fs::recursive_directory_iterator it(user_root), end; it != end; ++it)
            if (fs::is_regular_file(it->path()))
                files.emplace_back(it->path());
    } catch (const fs::filesystem_error& error) {
        BOOST_LOG_TRIVIAL(error) << "Failed to scan user presets for name migration: " << error.what();
        completed = false;
    }

    size_t migrated_json_count = 0;
    for (const fs::path& path : files) {
        if (!boost::iequals(path.extension().string(), ".json"))
            continue;
        bool file_completed = false;
        if (migrate_json_file(path, replacements, false, &file_completed))
            ++migrated_json_count;
        completed = file_completed && completed;
    }

    size_t renamed_file_count = 0;
    for (const fs::path& path : files) {
        const std::string extension = path.extension().string();
        if (!boost::iequals(extension, ".json") && !boost::iequals(extension, ".info"))
            continue;

        std::string migrated_filename = path.filename().string();
        if (!replace_legacy_preset_name(migrated_filename, replacements))
            continue;

        const fs::path destination = path.parent_path() / migrated_filename;
        try {
            if (fs::exists(destination)) {
                BOOST_LOG_TRIVIAL(warning) << "Skipped preset-name file migration because the target already exists: "
                                           << destination.string();
                continue;
            }
            fs::rename(path, destination);
            ++renamed_file_count;
        } catch (const fs::filesystem_error& error) {
            BOOST_LOG_TRIVIAL(error) << "Failed to rename user preset " << path.string() << ": " << error.what();
            completed = false;
        }
    }

    if (migrated_json_count != 0 || renamed_file_count != 0)
        BOOST_LOG_TRIVIAL(info) << "Migrated legacy multi-extruder user presets: JSON files=" << migrated_json_count
                                << ", renamed files=" << renamed_file_count;
    return completed;
}

static PresetNameReplacements migrate_legacy_multi_extruder_preset_names(AppConfig& config, bool& completed)
{
    completed = true;
    const PresetNameReplacements replacements = legacy_multi_extruder_preset_names();
    if (replacements.empty())
        return replacements;

    const fs::path config_path(config.config_path());
    if (fs::exists(config_path)) {
        bool config_completed = false;
        if (migrate_json_file(config_path, replacements, true, &config_completed))
            BOOST_LOG_TRIVIAL(info) << "Migrated legacy multi-extruder preset names in " << config_path.string();
        completed = config_completed && completed;
    }

    completed = migrate_user_preset_names(fs::path(data_dir()).append(PRESET_USER_DIR), replacements) && completed;
    return replacements;
}

static fs::path legacy_multi_extruder_preset_name_migration_marker()
{
    // Bump the suffix when another one-time preset-name migration is added.
    return fs::path(data_dir()).append("preset_name_migration_v2.done");
}

static bool mark_legacy_multi_extruder_preset_name_migration_complete(const fs::path& marker)
{
    fs::path tmp_marker = marker;
    tmp_marker += (boost::format(".%1%.tmp") % get_current_pid()).str();
    boost::nowide::ofstream output(tmp_marker.string(), std::ios::out | std::ios::trunc);
    if (!output.is_open())
        return false;
    output << "legacy_multi_extruder_preset_names_v2\n";
    output.close();
    if (!output) {
        boost::system::error_code remove_error;
        fs::remove(tmp_marker, remove_error);
        return false;
    }

    const std::error_code rename_error = rename_file(tmp_marker.string(), marker.string());
    if (rename_error) {
        boost::system::error_code remove_error;
        fs::remove(tmp_marker, remove_error);
        BOOST_LOG_TRIVIAL(error) << "Failed to write preset-name migration marker " << marker.string()
                                 << ": " << rename_error.message();
        return false;
    }
    return true;
}


void start_ping_test()
{
    return;
    wxArrayString output;
    wxExecute("ping www.amazon.com", output, wxEXEC_NODISABLE);

    wxString output_i;
    std::string output_temp;

    for (int i = 0; i < output.size(); i++) {
        output_i = output[i].To8BitData();
        output_temp = output_i.ToStdString(wxConvUTF8);
        BOOST_LOG_TRIVIAL(info) << "ping amazon:" << output_temp;

    }
    wxExecute("ping www.apple.com", output, wxEXEC_NODISABLE);
    for (int i = 0; i < output.size(); i++) {
        output_i = output[i].To8BitData();
        output_temp = output_i.ToStdString(wxConvUTF8);
        BOOST_LOG_TRIVIAL(info) << "ping www.apple.com:" << output_temp;
    }
    //Get GateWay IP
    wxExecute("ping 192.168.0.1", output, wxEXEC_NODISABLE);
    for (int i = 0; i < output.size(); i++) {
        output_i = output[i].To8BitData();
        output_temp = output_i.ToStdString(wxConvUTF8);
        BOOST_LOG_TRIVIAL(info) << "ping 192.168.0.1:" << output_temp;
    }
}
#if __linux__
// 检测是否在Flatpak环境中运行
inline bool isRunningInFlatpak() {
    const char* flatpakInfo = std::getenv("FLATPAK_ID");
    return flatpakInfo != nullptr;
}

static void ensure_flatpak_user_launcher()
{
    static const std::string app_id = "io.github.crealityofficial.CrealityPrint";
    static const std::string app_name = "CrealityPrint";

    const char *flatpak_id = std::getenv("FLATPAK_ID");
    if (flatpak_id == nullptr || app_id != flatpak_id)
        return;

    const char *home = std::getenv("HOME");
    if (home == nullptr || home[0] == '\0') {
        BOOST_LOG_TRIVIAL(warning) << "Flatpak launcher repair skipped: HOME is empty";
        return;
    }

    try {
        const boost::filesystem::path data_home = boost::filesystem::path(home) / ".local" / "share";
        const boost::filesystem::path applications_dir = data_home / "applications";
        const boost::filesystem::path icons_dir = data_home / "icons";
        boost::system::error_code ec;

        boost::filesystem::create_directories(applications_dir, ec);
        if (ec) {
            BOOST_LOG_TRIVIAL(warning) << "Flatpak launcher repair failed to create applications directory: " << ec.message();
            return;
        }

        const boost::filesystem::path desktop_path = applications_dir / (app_id + ".desktop");
        std::ofstream desktop_file(desktop_path.string(), std::ios::out | std::ios::trunc);
        if (!desktop_file) {
            BOOST_LOG_TRIVIAL(warning) << "Flatpak launcher repair failed to open desktop file: " << desktop_path.string();
            return;
        }

        desktop_file
            << "[Desktop Entry]\n"
            << "Name=" << app_name << "\n"
            << "GenericName=3D Printing Software\n"
            << "Icon=" << app_id << "\n"
            << "Exec=/usr/bin/flatpak run --branch=master --arch=x86_64 --command=entrypoint --file-forwarding "
            << app_id << " @@u %U @@\n"
            << "Terminal=false\n"
            << "Type=Application\n"
            << "MimeType=model/stl;model/3mf;application/vnd.ms-3mfdocument;application/prs.wavefront-obj;application/x-amf;x-scheme-handler/crealityprintlink;\n"
            << "Categories=Graphics;Utility;3DGraphics;Engineering;\n"
            << "Keywords=3D;Printing;Slicer;slice;3D;printer;convert;gcode;stl;obj;amf;SLA\n"
            << "StartupNotify=false\n"
            << "StartupWMClass=" << app_name << "\n"
            << "X-Flatpak=" << app_id << "\n";
        desktop_file.close();

        boost::filesystem::permissions(desktop_path,
            boost::filesystem::owner_read | boost::filesystem::owner_write |
            boost::filesystem::group_read | boost::filesystem::others_read, ec);
        ec.clear();

        const boost::filesystem::path src_icons_root("/app/share/icons");
        if (boost::filesystem::exists(src_icons_root)) {
            boost::filesystem::create_directories(icons_dir, ec);
            if (ec) {
                BOOST_LOG_TRIVIAL(warning) << "Flatpak launcher repair failed to create icons directory: " << ec.message();
                ec.clear();
            } else {
                size_t copied_icons = 0;
                for (boost::filesystem::recursive_directory_iterator it(src_icons_root), end; it != end; ++it) {
                    if (!boost::filesystem::is_regular_file(it->path()))
                        continue;

                    const std::string filename = it->path().filename().string();
                    if (!boost::starts_with(filename, app_id + ".") && !boost::starts_with(filename, app_name + "."))
                        continue;

                    const std::string src_root = src_icons_root.string();
                    const std::string src_path = it->path().string();
                    const std::string rel_path = src_path.size() > src_root.size() + 1 ? src_path.substr(src_root.size() + 1) : filename;
                    const boost::filesystem::path dst_path = icons_dir / rel_path;
                    boost::filesystem::create_directories(dst_path.parent_path(), ec);
                    if (ec) {
                        BOOST_LOG_TRIVIAL(warning) << "Flatpak launcher repair failed to create icon directory: " << ec.message();
                        ec.clear();
                        continue;
                    }

                    std::string error_message;
                    if (Slic3r::copy_file(src_path, dst_path.string(), error_message, false) == Slic3r::SUCCESS)
                        ++copied_icons;
                    else
                        BOOST_LOG_TRIVIAL(warning) << "Flatpak launcher repair failed to copy icon " << src_path << ": " << error_message;
                }
                BOOST_LOG_TRIVIAL(info) << "Flatpak launcher repair copied " << copied_icons << " icons";
            }
        } else {
            BOOST_LOG_TRIVIAL(warning) << "Flatpak launcher repair icon source does not exist: " << src_icons_root.string();
        }

        BOOST_LOG_TRIVIAL(info) << "Flatpak launcher repair wrote " << desktop_path.string();
    } catch (const std::exception &e) {
        BOOST_LOG_TRIVIAL(warning) << "Flatpak launcher repair failed: " << e.what();
    }
}
#endif
static std::string convert_studio_language_to_api(std::string lang_code)
{
    boost::replace_all(lang_code, "_", "-");
    return lang_code;

    /*if (lang_code == "zh_CN")
        return "zh-hans";
    else if (lang_code == "zh_TW")
        return "zh-hant";
    else
        return "en";*/
}

#ifdef _WIN32
bool is_associate_files(std::wstring extend)
{
    wchar_t app_path[MAX_PATH];
    ::GetModuleFileNameW(nullptr, app_path, sizeof(app_path));

    std::wstring prog_id             = L" Orca.Slicer.1";
    std::wstring reg_base            = L"Software\\Classes";
    std::wstring reg_extension       = reg_base + L"\\." + extend;

    wchar_t szValueCurrent[1000];
    DWORD   dwType;
    DWORD   dwSize = sizeof(szValueCurrent);

    int iRC = ::RegGetValueW(HKEY_CURRENT_USER, reg_extension.c_str(), nullptr, RRF_RT_ANY, &dwType, szValueCurrent, &dwSize);

    bool bDidntExist = iRC == ERROR_FILE_NOT_FOUND;

    if (!bDidntExist && ::wcscmp(szValueCurrent, prog_id.c_str()) == 0)
        return true;

    return false;
}
#endif

#ifdef __WXGTK__
static bool gui_app_is_wayland_session()
{
    const char* gdk_backend     = ::getenv("GDK_BACKEND");
    const char* wayland_display = ::getenv("WAYLAND_DISPLAY");
    const char* session_type    = ::getenv("XDG_SESSION_TYPE");

    return (gdk_backend != nullptr && ::strstr(gdk_backend, "wayland") != nullptr) ||
           ((gdk_backend == nullptr || *gdk_backend == '\0') &&
            ((wayland_display && *wayland_display) ||
             (session_type && ::strcmp(session_type, "wayland") == 0)));
}
#endif

class SplashScreen : public wxSplashScreen
{
public:
    SplashScreen(const wxBitmap& bitmap, long splashStyle, int milliseconds, wxPoint pos = wxDefaultPosition)
        : wxSplashScreen(bitmap, splashStyle, milliseconds, static_cast<wxWindow*>(wxGetApp().mainframe), wxID_ANY, pos, wxDefaultSize,
#ifdef __APPLE__
            wxBORDER_NONE | wxFRAME_NO_TASKBAR | wxSTAY_ON_TOP
#else
            wxBORDER_NONE | wxFRAME_NO_TASKBAR
#endif // !__APPLE__
        )
    {
        int init_dpi = get_dpi_for_window(this);
        if (pos != wxDefaultPosition)
            this->SetPosition(pos);
        else
            this->CenterOnScreen();
        int new_dpi = get_dpi_for_window(this);

        m_scale = (float)(new_dpi) / (float)(init_dpi);

        m_main_bitmap = bitmap;

        scale_bitmap(m_main_bitmap, m_scale);

        // init constant texts and scale fonts
        m_constant_text.init(Label::Body_16);

		// ORCA scale all fonts with monitor scale
        scale_font(m_constant_text.version_font,	m_scale * 2);
        scale_font(m_constant_text.based_on_font,	m_scale * 1.5f);
        scale_font(m_constant_text.credits_font,	m_scale * 2);

        // this font will be used for the action string
        m_action_font = m_constant_text.credits_font;

        // draw logo and constant info text
        Decorate(m_main_bitmap);
        set_bitmap(m_main_bitmap);
        wxGetApp().UpdateFrameDarkUI(this);
    }

    void SetText(const wxString& text)
    {
        set_bitmap(m_main_bitmap);
        if (!text.empty()) {
            wxBitmap bitmap(m_main_bitmap);

            wxMemoryDC memDC;
            memDC.SelectObject(bitmap);
            memDC.SetFont(m_action_font);
            memDC.SetTextForeground(StateColor::darkModeColorFor(wxColour(144, 144, 144)));
            int width = bitmap.GetWidth();
            //int text_height = memDC.GetTextExtent(text).GetHeight();
            //int text_width = memDC.GetTextExtent(text).GetWidth();
            //wxRect text_rect(wxPoint(0, m_action_line_y_position), wxPoint(width, m_action_line_y_position + text_height));
            //memDC.DrawLabel(text, text_rect, wxALIGN_CENTER);

            memDC.SelectObject(wxNullBitmap);
            set_bitmap(bitmap);
#ifdef __WXOSX__
            // without this code splash screen wouldn't be updated under OSX
            wxYield();
#endif
        }
    }
    // Draw centred text, wrapped by words inside rect; explicit '\n' starts a new line
    void DrawCenteredWrappedText(wxGraphicsContext* gc, const wxString& text, const wxRect& rect, const wxFont& font, const wxColour& color)
    {
        gc->SetFont(font, color);

        std::vector<wxString> lines;
        wxArrayString paragraphs = wxSplit(text, '\n', '\0');
        for (const wxString& paragraph : paragraphs) {
            wxArrayString words = wxSplit(paragraph, ' ', '\0');
            wxString currentLine;
            for (const wxString& word : words) {
                wxString candidate = currentLine.empty() ? word : currentLine + " " + word;
                wxDouble w, h;
                gc->GetTextExtent(candidate, &w, &h);
                if (w > rect.width && !currentLine.empty()) {
                    lines.push_back(currentLine);
                    currentLine = word;
                } else {
                    currentLine = candidate;
                }
            }
            lines.push_back(currentLine); // an empty paragraph keeps its blank line
        }

        wxDouble lineWidth, lineHeight;
        gc->GetTextExtent("Ag", &lineWidth, &lineHeight);
        const wxDouble lineStep = lineHeight + 2;

        wxDouble y = rect.y;
        for (const wxString& line : lines) {
            gc->GetTextExtent(line, &lineWidth, &lineHeight);
            wxDouble x = rect.x + (rect.width - lineWidth) / 2;
            gc->DrawText(line, x, y);
            y += lineStep;
        }
    }
    void Decorate(wxBitmap& bmp)
    {
        if (!bmp.IsOk())
            return;

		bool is_dark = wxGetApp().app_config->get("dark_color_mode") == "1";

        // use a memory DC to draw directly onto the bitmap
        wxMemoryDC memDc(bmp);
        
        int width = bmp.GetWidth();
		int height = bmp.GetHeight();

        // Logo. Load the PNG as an image and force an alpha channel: wxWidgets drops the alpha of a
        // fully opaque PNG, and a bitmap without alpha blitted onto the transparent base bitmap
        // leaves the destination alpha at 0 on Windows (the splash came out blank).
        wxImage logo_image;
        if (logo_image.LoadFile(from_u8(Slic3r::var("splash_logo.png")), wxBITMAP_TYPE_PNG) && logo_image.IsOk()) {
            logo_image.Rescale(width, height, wxIMAGE_QUALITY_BILINEAR);
            if (!logo_image.HasAlpha())
                logo_image.InitAlpha();
            memDc.DrawBitmap(wxBitmap(logo_image), 0, 0, true);
        }

        // Create a graphics context from the memory DC
        wxGraphicsContext* gc = wxGraphicsContext::Create(memDc);
        if(!gc)
            return;

        gc->SetAntialiasMode(wxANTIALIAS_DEFAULT);

        // const wxColor  font_color(184, 188, 187);
        const wxColor  font_color(100, 100, 100);

        // Fork tagline (replaces the Creality "Official brief introduction")
        wxString official_brief_introduction = m_loading_info + "\n\n" + _L("Your 3D printing mentor: profiles, diagnostics and printer control, with every change confirmed by you.");

        // The MentorSlicer splash has a 430 px wide left panel: centre the text under the logo
        wxRect rect(FromDIP(40), height * 0.50, FromDIP(350), FromDIP(100));
        const double splash_scale = std::max(1.0, double(width) / 860.0);
        auto scale_splash_font = [splash_scale](wxFont font) {
            const int point_size = std::max(1, int(font.GetPointSize() * splash_scale + 0.5));
            font.SetPointSize(point_size);
            return font;
        };
        wxFont font = scale_splash_font(wxFont(11, wxFONTFAMILY_SWISS, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL));
        wxColour color(0, 0, 0); 

        DrawCenteredWrappedText(gc, official_brief_introduction, rect, font, font_color);

        // Version
         //gc->SetFont(m_constant_text.version_font, font_color);
        wxString version_text = _L("Version:") + m_constant_text.version;
        wxFont version_font = scale_splash_font(Label::Body_13);
        gc->SetFont(version_font, font_color);
        wxDouble text_width, text_height;
        gc->GetTextExtent(version_text, &text_width, &text_height);
        wxRect version_rect(FromDIP(40), height * 0.90, FromDIP(350), text_height);
        DrawCenteredWrappedText(gc, version_text, version_rect, version_font, font_color);
        // Clean up the graphics context
        delete gc;

        // Dynamic Text
        // m_action_line_y_position = int(height * 0.83);

		// // Based on Text
        // memDc.SetFont(m_constant_text.based_on_font);
        // auto bs_version = wxString::Format("Based on OrcaaSlicer").ToStdString();
        // wxSize based_on_ext = memDc.GetTextExtent(bs_version);
        // wxRect based_on_rect(
		// 	wxPoint(0, height - based_on_ext.GetHeight() * 2),
        //     wxPoint(width, height - based_on_ext.GetHeight())
		// );
        // memDc.DrawLabel(bs_version, based_on_rect, wxALIGN_CENTER);
    }

    static wxBitmap MakeBitmap()
    {
        int width = FromDIP(860, nullptr);
        int height = FromDIP(480, nullptr);

        wxImage image(width, height, true);
        image.InitAlpha();
        unsigned char* alpha = image.GetAlpha();
        memset(alpha, 0, width * height);

        wxBitmap new_bmp(image);

        wxMemoryDC memDC;
        memDC.SelectObject(new_bmp);
        // memDC.SetBrush(StateColor::darkModeColorFor(*wxWHITE));
        memDC.SetBrush(*wxTRANSPARENT_BRUSH);
        memDC.SetPen(*wxTRANSPARENT_PEN);
        memDC.DrawRectangle(-1, -1, width + 2, height + 2);
        memDC.DrawBitmap(new_bmp, 0, 0, true);

        memDC.SelectObject(wxNullBitmap);
        return new_bmp;
    }

    void set_bitmap(wxBitmap& bmp)
    {
        m_window->SetBitmap(bmp);
        m_window->Refresh();
        m_window->Update();
    }

    void scale_bitmap(wxBitmap& bmp, float scale)
    {
        if (scale == 1.0)
            return;

        wxImage image = bmp.ConvertToImage();
        if (!image.IsOk() || image.GetWidth() == 0 || image.GetHeight() == 0)
            return;

        int width   = int(scale * image.GetWidth());
        int height  = int(scale * image.GetHeight());
        image.Rescale(width, height, wxIMAGE_QUALITY_BILINEAR);

        bmp = wxBitmap(std::move(image));
    }

    void scale_font(wxFont& font, float scale)
    {
#ifdef __WXMSW__
        // Workaround for the font scaling in respect to the current active display,
        // not for the primary display, as it's implemented in Font.cpp
        // See https://github.com/wxWidgets/wxWidgets/blob/master/src/msw/font.cpp
        // void wxNativeFontInfo::SetFractionalPointSize(float pointSizeNew)
        wxNativeFontInfo nfi= *font.GetNativeFontInfo();
        float pointSizeNew  = scale * font.GetPointSize();
        nfi.lf.lfHeight     = nfi.GetLogFontHeightAtPPI(pointSizeNew, get_dpi_for_window(this));
        nfi.pointSize       = pointSizeNew;
        font = wxFont(nfi);
#else
        font.Scale(scale);
#endif //__WXMSW__
    }


private:
    wxStaticText* m_staticText_slicer_name;
    wxStaticText* m_staticText_slicer_version;
    wxStaticBitmap* m_bitmap;
    wxStaticText* m_staticText_loading;
    wxString        m_loading_info = _L("Loading configuration") + dots;
    wxBitmap    m_main_bitmap;
    wxFont      m_action_font;
    float       m_scale {1.0};

    struct ConstantText
    {
        wxString title;
        wxString version;
        wxString credits;

        wxFont   title_font;
        wxFont   version_font;
        wxFont   credits_font;
        wxFont   based_on_font;

        void init(wxFont init_font)
        {
            // title
            //title = wxGetApp().is_editor() ? SLIC3R_APP_FULL_NAME : GCODEVIEWER_APP_NAME;

            // dynamically get the version to display
            version = MENTORSLICER_VERSION; // F8: display version (internal one: format_display_version)

            // credits infornation
            credits = "";

            //title_font    = Label::Head_16;
            version_font  = Label::Body_13;
            based_on_font = Label::Body_8;
            credits_font  = Label::Body_8;
        }
    }
    m_constant_text;
};

#ifdef __linux__
bool static check_old_linux_datadir(const wxString& app_name) {
    // If we are on Linux and the datadir does not exist yet, look into the old
    // location where the datadir was before version 2.3. If we find it there,
    // tell the user that he might wanna migrate to the new location.
    // (https://github.com/prusa3d/PrusaSlicer/issues/2911)
    // To be precise, the datadir should exist, it is created when single instance
    // lock happens. Instead of checking for existence, check the contents.

    namespace fs = boost::filesystem;

    std::string new_path = Slic3r::data_dir();

    wxString dir;
    if (! wxGetEnv(wxS("XDG_CONFIG_HOME"), &dir) || dir.empty() )
        dir = wxFileName::GetHomeDir() + wxS("/.config");
    std::string default_path = (dir + "/" + app_name).ToUTF8().data();

    if (new_path != default_path) {
        // This happens when the user specifies a custom --datadir.
        // Do not show anything in that case.
        return true;
    }

    fs::path data_dir = fs::path(new_path);
    if (! fs::is_directory(data_dir))
        return true; // This should not happen.

    int file_count = std::distance(fs::directory_iterator(data_dir), fs::directory_iterator());

    if (file_count <= 1) { // just cache dir with an instance lock
        // BBS
    } else {
        // If the new directory exists, be silent. The user likely already saw the message.
    }
    return true;
}
#endif

struct FileWildcards {
    std::string_view              title;
    std::vector<std::string_view> file_extensions;
};

static const FileWildcards file_wildcards_by_type[FT_SIZE] = {
    /* FT_STEP */    { "STEP files"sv,      { ".stp"sv, ".step"sv } },
    /* FT_STL */     { "STL files"sv,       { ".stl"sv } },
    /* FT_OBJ */     { "OBJ files"sv,       { ".obj"sv } },
    /* FT_AMF */     { "AMF files"sv,       { ".amf"sv, ".zip.amf"sv, ".xml"sv } },
    /* FT_3MF */     { "3MF files"sv,       { ".3mf"sv } },
    /* FT_GCODE */   { "G-code files"sv,    { ".gcode"sv, ".3mf"sv } },
    /* FT_GCODE_3MF */   { "Gcode 3MF files"sv,    { ".gcode.3mf"sv } },
#ifdef __APPLE__
    /* FT_MODEL */
    {"All"sv, {".3mf"sv, ".stl"sv, ".oltp"sv, ".stp"sv, ".step"sv, ".svg"sv, ".amf"sv, ".obj"sv, ".gltf"sv, ".glb"sv, ".fbx"sv, ".usd"sv, ".usda"sv, ".usdc"sv, ".usdz"sv, ".abc"sv, ".ply"sv,".dae"sv,".3ds"sv,".off"sv}},
#else
    /* FT_MODEL */
    {"All"sv, {".3mf"sv, ".stl"sv, ".oltp"sv, ".stp"sv, ".step"sv, ".svg"sv, ".amf"sv, ".obj"sv, ".gltf"sv, ".glb"sv, ".fbx"sv, ".dae"sv, ".3ds"sv, ".ply"sv, ".off"sv}},
#endif
    /* FT_ZIP */     { "ZIP files"sv,       { ".zip"sv } },
    /* FT_PROJECT */ { "Project files"sv,   { ".3mf"sv, ".cxprj"sv } },
    /* FT_GALLERY */ { "Known files"sv,     { ".stl"sv, ".obj"sv } },

    /* FT_INI */     { "INI files"sv,       { ".ini"sv } },
    /* FT_SVG */     { "SVG files"sv,       { ".svg"sv } },
    /* FT_TEX */     { "Texture"sv,         { ".png"sv, ".svg"sv } },
    /* FT_SL1 */     { "Masked SLA files"sv, { ".sl1"sv, ".sl1s"sv } },
    /* FT_CXPRJ */   { "Cxprj files"sv,     { ".cxprj"sv } },
    /* FT_ONLY_GCODE */   { "G-code files"sv,    { ".gcode"sv } },
    /* FT_MESH_FILE */ { "Mesh files"sv,   { ".stl"sv, ".obj"sv, ".dae"sv, ".3ds"sv, ".off"sv, ".ply"sv, ".3mf"sv } },
    /* FT_CAD_FILE */     { "CAD files"sv, { ".stp"sv, ".step"sv } },
};

// This function produces a Win32 file dialog file template mask to be consumed by wxWidgets on all platforms.
// The function accepts a custom extension parameter. If the parameter is provided, the custom extension
// will be added as a fist to the list. This is important for a "file save" dialog on OSX, which strips
// an extension from the provided initial file name and substitutes it with the default extension (the first one in the template).
wxString file_wildcards(FileType file_type, const std::string &custom_extension)
{
    const FileWildcards& data = file_wildcards_by_type[file_type];
    std::string title;
    std::string mask;
    std::string custom_ext_lower;

    if (! custom_extension.empty()) {
        // Generate an extension into the title mask and into the list of extensions.
        custom_ext_lower = boost::to_lower_copy(custom_extension);
        const std::string custom_ext_upper = boost::to_upper_copy(custom_extension);
        if (custom_ext_lower == custom_extension) {
            // Add a lower case version.
            title = std::string("*") + custom_ext_lower;
            mask = title;
            // Add an upper case version.
            mask  += ";*";
            mask  += custom_ext_upper;
        } else if (custom_ext_upper == custom_extension) {
            // Add an upper case version.
            title = std::string("*") + custom_ext_upper;
            mask = title;
            // Add a lower case version.
            mask += ";*";
            mask += custom_ext_lower;
        } else {
            // Add the mixed case version only.
            title = std::string("*") + custom_extension;
            mask = title;
        }
    }

    for (const std::string_view &ext : data.file_extensions)
        // Only add an extension if it was not added first as the custom extension.
        if (ext != custom_ext_lower) {
            if (title.empty()) {
                title = "*";
                title += ext;
                mask  = title;
            } else {
                title += ", *";
                title += ext;
                mask  += ";*";
                mask  += ext;
            }
            mask += ";*";
            mask += boost::to_upper_copy(std::string(ext));
        }
    return GUI::format_wxstr("%s (%s)|%s", data.title, title, mask);
}

static std::string libslic3r_translate_callback(const char *s) { return wxGetTranslation(wxString(s, wxConvUTF8)).utf8_str().data(); }

#ifdef WIN32
#if !wxVERSION_EQUAL_OR_GREATER_THAN(3,1,3)
static void register_win32_dpi_event()
{
    enum { WM_DPICHANGED_ = 0x02e0 };

    wxWindow::MSWRegisterMessageHandler(WM_DPICHANGED_, [](wxWindow *win, WXUINT nMsg, WXWPARAM wParam, WXLPARAM lParam) {
        const int dpi = wParam & 0xffff;
        const auto rect = reinterpret_cast<PRECT>(lParam);
        const wxRect wxrect(wxPoint(rect->top, rect->left), wxPoint(rect->bottom, rect->right));

        DpiChangedEvent evt(EVT_DPI_CHANGED_SLICER, dpi, wxrect);
        win->GetEventHandler()->AddPendingEvent(evt);

        return true;
    });
}
#endif // !wxVERSION_EQUAL_OR_GREATER_THAN

static GUID GUID_DEVINTERFACE_HID = { 0x4D1E55B2, 0xF16F, 0x11CF, 0x88, 0xCB, 0x00, 0x11, 0x11, 0x00, 0x00, 0x30 };

static void register_win32_device_notification_event()
{
    wxWindow::MSWRegisterMessageHandler(WM_DEVICECHANGE, [](wxWindow *win, WXUINT /* nMsg */, WXWPARAM wParam, WXLPARAM lParam) {
        // Some messages are sent to top level windows by default, some messages are sent to only registered windows, and we explictely register on MainFrame only.
        auto main_frame = dynamic_cast<MainFrame*>(win);
        auto plater = (main_frame == nullptr) ? nullptr : main_frame->plater();
        if (plater == nullptr)
            // Maybe some other top level window like a dialog or maybe a pop-up menu?
            return true;
		PDEV_BROADCAST_HDR lpdb = (PDEV_BROADCAST_HDR)lParam;
        switch (wParam) {
        case DBT_DEVICEARRIVAL:
			if (lpdb->dbch_devicetype == DBT_DEVTYP_VOLUME)
		        plater->GetEventHandler()->AddPendingEvent(VolumeAttachedEvent(EVT_VOLUME_ATTACHED));
			else if (lpdb->dbch_devicetype == DBT_DEVTYP_DEVICEINTERFACE) {
				PDEV_BROADCAST_DEVICEINTERFACE lpdbi = (PDEV_BROADCAST_DEVICEINTERFACE)lpdb;
//				if (lpdbi->dbcc_classguid == GUID_DEVINTERFACE_VOLUME) {
//					printf("DBT_DEVICEARRIVAL %d - Media has arrived: %ws\n", msg_count, lpdbi->dbcc_name);
				if (lpdbi->dbcc_classguid == GUID_DEVINTERFACE_HID)
			        plater->GetEventHandler()->AddPendingEvent(HIDDeviceAttachedEvent(EVT_HID_DEVICE_ATTACHED, boost::nowide::narrow(lpdbi->dbcc_name)));
			}
            break;
		case DBT_DEVICEREMOVECOMPLETE:
			if (lpdb->dbch_devicetype == DBT_DEVTYP_VOLUME)
                plater->GetEventHandler()->AddPendingEvent(VolumeDetachedEvent(EVT_VOLUME_DETACHED));
			else if (lpdb->dbch_devicetype == DBT_DEVTYP_DEVICEINTERFACE) {
				PDEV_BROADCAST_DEVICEINTERFACE lpdbi = (PDEV_BROADCAST_DEVICEINTERFACE)lpdb;
//				if (lpdbi->dbcc_classguid == GUID_DEVINTERFACE_VOLUME)
//					printf("DBT_DEVICEARRIVAL %d - Media was removed: %ws\n", msg_count, lpdbi->dbcc_name);
				if (lpdbi->dbcc_classguid == GUID_DEVINTERFACE_HID)
        			plater->GetEventHandler()->AddPendingEvent(HIDDeviceDetachedEvent(EVT_HID_DEVICE_DETACHED, boost::nowide::narrow(lpdbi->dbcc_name)));
			}
			break;
        default:
            break;
        }
        return true;
    });

    wxWindow::MSWRegisterMessageHandler(MainFrame::WM_USER_MEDIACHANGED, [](wxWindow *win, WXUINT /* nMsg */, WXWPARAM wParam, WXLPARAM lParam) {
        // Some messages are sent to top level windows by default, some messages are sent to only registered windows, and we explictely register on MainFrame only.
        auto main_frame = dynamic_cast<MainFrame*>(win);
        auto plater = (main_frame == nullptr) ? nullptr : main_frame->plater();
        if (plater == nullptr)
            // Maybe some other top level window like a dialog or maybe a pop-up menu?
            return true;
        wchar_t sPath[MAX_PATH];
        if (lParam == SHCNE_MEDIAINSERTED || lParam == SHCNE_MEDIAREMOVED) {
            struct _ITEMIDLIST* pidl = *reinterpret_cast<struct _ITEMIDLIST**>(wParam);
            if (! SHGetPathFromIDList(pidl, sPath)) {
                BOOST_LOG_TRIVIAL(error) << "MediaInserted: SHGetPathFromIDList failed";
                return false;
            }
        }
        switch (lParam) {
        case SHCNE_MEDIAINSERTED:
        {
            //printf("SHCNE_MEDIAINSERTED %S\n", sPath);
            plater->GetEventHandler()->AddPendingEvent(VolumeAttachedEvent(EVT_VOLUME_ATTACHED));
            break;
        }
        case SHCNE_MEDIAREMOVED:
        {
            //printf("SHCNE_MEDIAREMOVED %S\n", sPath);
            plater->GetEventHandler()->AddPendingEvent(VolumeDetachedEvent(EVT_VOLUME_DETACHED));
            break;
        }
	    default:
//          printf("Unknown\n");
            break;
	    }
        return true;
    });

    wxWindow::MSWRegisterMessageHandler(WM_INPUT, [](wxWindow *win, WXUINT /* nMsg */, WXWPARAM wParam, WXLPARAM lParam) {
        auto main_frame = dynamic_cast<MainFrame*>(Slic3r::GUI::find_toplevel_parent(win));
        auto plater = (main_frame == nullptr) ? nullptr : main_frame->plater();
//        if (wParam == RIM_INPUTSINK && plater != nullptr && main_frame->IsActive()) {
        if (wParam == RIM_INPUT && plater != nullptr && main_frame->IsActive()) {
        RAWINPUT raw;
			UINT rawSize = sizeof(RAWINPUT);
			::GetRawInputData((HRAWINPUT)lParam, RID_INPUT, &raw, &rawSize, sizeof(RAWINPUTHEADER));
			if (raw.header.dwType == RIM_TYPEHID && plater->get_mouse3d_controller().handle_raw_input_win32(raw.data.hid.bRawData, raw.data.hid.dwSizeHid))
				return true;
		}
        return false;
    });

	wxWindow::MSWRegisterMessageHandler(WM_COPYDATA, [](wxWindow* win, WXUINT /* nMsg */, WXWPARAM wParam, WXLPARAM lParam) {
		COPYDATASTRUCT* copy_data_structure = { 0 };
		copy_data_structure = (COPYDATASTRUCT*)lParam;
		if (copy_data_structure->dwData == 1) {
			LPCWSTR arguments = (LPCWSTR)copy_data_structure->lpData;
			Slic3r::GUI::wxGetApp().other_instance_message_handler()->handle_message(boost::nowide::narrow(arguments));
		}
		return true;
		});
}
#endif // WIN32

static void generic_exception_handle()
{
    // Note: Some wxWidgets APIs use wxLogError() to report errors, eg. wxImage
    // - see https://docs.wxwidgets.org/3.1/classwx_image.html#aa249e657259fe6518d68a5208b9043d0
    //
    // wxLogError typically goes around exception handling and display an error dialog some time
    // after an error is logged even if exception handling and OnExceptionInMainLoop() take place.
    // This is why we use wxLogError() here as well instead of a custom dialog, because it accumulates
    // errors if multiple have been collected and displays just one error message for all of them.
    // Otherwise we would get multiple error messages for one missing png, for example.
    //
    // If a custom error message window (or some other solution) were to be used, it would be necessary
    // to turn off wxLogError() usage in wx APIs, most notably in wxImage
    // - see https://docs.wxwidgets.org/trunk/classwx_image.html#aa32e5d3507cc0f8c3330135bc0befc6a
    try {
        throw;
    } catch (const std::bad_alloc& ex) {
        // bad_alloc in main thread is most likely fatal. Report immediately to the user (wxLogError would be delayed)
        // and terminate the app so it is at least certain to happen now.
        BOOST_LOG_TRIVIAL(error) << boost::format("std::bad_alloc exception: %1%") % ex.what();
        flush_logs();
        wxString errmsg = wxString::Format(_L("MentorSlicer will terminate because of running out of memory."
                                              "It may be a bug. It will be appreciated if you report the issue to our team."));
        wxMessageBox(errmsg + "\n\n" + wxString(ex.what()), _L("Fatal error"), wxOK | wxICON_ERROR);

        std::terminate();
        //throw;
     } catch (const boost::io::bad_format_string& ex) {
     	BOOST_LOG_TRIVIAL(error) << boost::format("Uncaught exception: %1%") % ex.what();
        flush_logs();
        wxString errmsg = _L("MentorSlicer will terminate because of a localization error. "
                             "It will be appreciated if you report the specific scenario this issue happened.");
        wxMessageBox(errmsg + "\n\n" + wxString(ex.what()), _L("Critical error"), wxOK | wxICON_ERROR);
        std::terminate();
        //throw;
    } catch (const std::exception& ex) {
        wxLogError(format_wxstr(_L("MentorSlicer got an unhandled exception: %1%"), ex.what()));
        BOOST_LOG_TRIVIAL(error) << boost::format("Uncaught exception: %1%") % ex.what();
        flush_logs();
        throw;
    }
}

void GUI_App::toggle_show_gcode_window()
{
    m_show_gcode_window = !m_show_gcode_window;
    app_config->set_bool("show_gcode_window", m_show_gcode_window);
}
#ifdef _WIN32
static std::list<HWND> l_creality_print_hwnds;
static BOOL CALLBACK EnumWindowsProc(_In_ HWND   hwnd, _In_ LPARAM lParam)
{
		//checks for other instances of prusaslicer, if found brings it to front and return false to stop enumeration and quit this instance
		//search is done by classname(wxWindowNR is wxwidgets thing, so probably not unique) and name in window upper panel
		//other option would be do a mutex and check for its existence
		//BOOST_LOG_TRIVIAL(error) << "ewp: version: " << l_version_wstring;
		TCHAR 		 wndText[1000];
		TCHAR 		 className[1000];
		int          err;
		err = GetClassName(hwnd, className, 1000);
		if (err == 0)
			return true;
		err = GetWindowText(hwnd, wndText, 1000);
		if (err == 0)
			return true;
		std::wstring classNameString(className);
		std::wstring wndTextString(wndText);
		// PRINT-ADVISOR F8: matches the " - MentorSlicer" title set by Plater (and InstanceCheck.cpp).
		if (wndTextString.find(L"MentorSlicer") != std::wstring::npos && classNameString == L"wxWindowNR") {
			//check if other instances has same instance hash
			//if not it is not same version(binary) as this version 
			HANDLE   handle = GetProp(hwnd, L"Instance_Hash_Minor");
			uint64_t other_instance_hash = PtrToUint(handle);
			uint64_t other_instance_hash_major;
			uint64_t my_instance_hash = GUI::wxGetApp().get_instance_hash_int();
			handle = GetProp(hwnd, L"Instance_Hash_Major");
			other_instance_hash_major = PtrToUint(handle);
			other_instance_hash_major = other_instance_hash_major << 32;
			other_instance_hash += other_instance_hash_major;
            //All MentorSlicer instances send
			//if(my_instance_hash == other_instance_hash) 
			{
				BOOST_LOG_TRIVIAL(debug) << "win enum - found correct instance";
				l_creality_print_hwnds.push_back(hwnd);
				return true;
			}
			BOOST_LOG_TRIVIAL(debug) << "win enum - found wrong instance";
		}
		return true;
	}
#endif
void  GUI_App::reload_homepage()
{
    if (mainframe)
    {
        if(mainframe->m_webview)
        {
            mainframe->m_webview->Reload();
        }
    }

}

bool GUI_App::send_app_message(const std::string& msg,bool bforce)
{
    bool isActive = mainframe->IsActive();
    BOOST_LOG_TRIVIAL(error) << "send message to other instance: " << isActive;
    if(!isActive && !bforce)
    {
        //only send message when the mainframe is active
        return false;
    }   
#ifdef __APPLE__
    std::cout << "Request received: ";
    send_message_mac(msg,get_instance_hash_string());
    return true;
#endif     
#ifdef _WIN32
    l_creality_print_hwnds.clear();
    EnumWindows(EnumWindowsProc, 0);
    if (!l_creality_print_hwnds.empty()) {
        std::wstring wstr = boost::nowide::widen(msg);
        std::unique_ptr<LPWSTR> command_line_args = std::make_unique<LPWSTR>(const_cast<LPWSTR>(wstr.c_str()));
        /*LPWSTR command_line_args = new wchar_t[wstr.size() + 1];
        copy(wstr.begin(), wstr.end(), command_line_args);
        command_line_args[wstr.size()] = 0;*/

        //Create a COPYDATASTRUCT to send the information
        //cbData represents the size of the information we want to send.
        //lpData represents the information we want to send.
        //dwData is an ID defined by us(this is a type of ID different than WM_COPYDATA).
        COPYDATASTRUCT data_to_send = { 0 };
        data_to_send.dwData = 1;
        data_to_send.cbData = sizeof(TCHAR) * (wcslen(*command_line_args.get()) + 1);
        data_to_send.lpData = *command_line_args.get();
        for(auto hwnd : l_creality_print_hwnds)
        {
            if(hwnd != mainframe->GetHWND())
            {
                SendMessage(hwnd, WM_COPYDATA, 0, (LPARAM)&data_to_send);
            }
            
        }
        
        return true;  
    }
#endif
    return false;
}

// token_expired token过期
// login 登录
void GUI_App::swith_community_sub_page(const std::string& pageName)
{
    json m_Res       = json::object();
    m_Res["command"] = "switch_sub_page";
    m_Res["page_name"]     = pageName;
    wxString strJS   = wxString::Format("window.handleStudioCmd(%s)", m_Res.dump(-1, ' ', false, json::error_handler_t::ignore));
    GUI::wxGetApp().run_script(strJS);
}

//  参数值：
//  tpHome - 社区页面
void GUI_App::switch_to_tab(const std::string& tabName)
{
    if (mainframe == nullptr)
        return;

    // Only the home page can still be reached this way: the Online Models tab was removed.
    if (tabName == "tpHome") {
        mainframe->select_tab(MainFrame::tpHome);
    }
}

void GUI_App::post_init()
{
    assert(initialized());
    if (! this->initialized())
        throw Slic3r::RuntimeError("Calling post_init() while not yet initialized");

    refresh_after_startup();

    m_open_method = "double_click";
    bool switch_to_3d = false;
    if (!this->init_params->input_files.empty()) {
        bool onlyDefault = preset_bundle->printers.only_default_printers();
        if (onlyDefault) return;

        BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << boost::format(", init with input files, size %1%, input_gcode %2%")
            %this->init_params->input_files.size() %this->init_params->input_gcode;
        const auto first_url = this->init_params->input_files.front();
        if (this->init_params->input_files.size() == 1 && is_supported_open_protocol(first_url)) {
            switch_to_3d = true;
            std::cout << "first_url: " << first_url << std::endl;
            BOOST_LOG_TRIVIAL(warning) << __FUNCTION__ << first_url << "\n";
            start_download(first_url);
            m_openlink_url = first_url;
            m_open_method = "url";
            
            
            
        } else {
            switch_to_3d = true;
            if (this->init_params->input_gcode) {
                mainframe->select_tab(size_t(MainFrame::tp3DEditor));
                plater_->select_view_3D("3D");
                this->plater()->load_gcode(from_u8(this->init_params->input_files.front()));
                this->plater()->update();
                m_open_method = "gcode";
            } else {
                mainframe->select_tab(size_t(MainFrame::tp3DEditor));
                plater_->select_view_3D("3D");
                wxArrayString input_files;
                for (auto& file : this->init_params->input_files) {
                    input_files.push_back(wxString::FromUTF8(file));
                }


                this->plater()->set_project_filename(_L("Untitled"));
                this->plater()->load_files(input_files);
                try {
                    if (!input_files.empty()) {
                        std::string           file_path = input_files.front().ToStdString();
                        std::filesystem::path path(file_path);
                        //m_open_method = "file_" + path.extension().string();
                    }
                } catch (...) {
                    BOOST_LOG_TRIVIAL(error) << __FUNCTION__ << ", file path exception!";
                    m_open_method = "file";
                }
            }
        }
    }

//#if BBL_HAS_FIRST_PAGE
    bool slow_bootup = false;
    if (app_config->get("slow_bootup") == "true") {
        slow_bootup = true;
        BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << ", slow bootup, won't render gl here.";
    }
    if (!switch_to_3d) {
        BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << ", begin load_gl_resources";
#ifndef __linux__
        mainframe->Freeze();
#endif
        plater_->canvas3D()->enable_render(false);
        mainframe->select_tab(size_t(MainFrame::tp3DEditor));
        plater_->select_view_3D("3D");
        //BBS init the opengl resource here
#ifdef __linux__
        const bool glcontext_ready = is_running_on_wayland()
            ? plater_->canvas3D()->make_current_for_postinit()
            : (plater_->canvas3D()->get_wxglcanvas()->IsShownOnScreen() &&
               plater_->canvas3D()->make_current_for_postinit());
#else
        const bool glcontext_ready = plater_->canvas3D()->get_wxglcanvas()->IsShownOnScreen() &&
            plater_->canvas3D()->make_current_for_postinit();
#endif
        if (!glcontext_ready) {
            BOOST_LOG_TRIVIAL(warning) << __FUNCTION__ << ": glcontext not ready, postpone init";
            plater_->canvas3D()->enable_render(true);
            plater_->canvas3D()->set_as_dirty();
#ifdef __linux__
            // Wayland/EGL may not have committed the GL surface yet; ask the
            // idle loop to retry post_init when the canvas is actually mapped.
            // Without this, GL function pointers stay null and the first
            // Preview focus can crash in Camera::apply_viewport.
            m_post_initialized = false;
            return;
#endif
        } else {
            Size canvas_size = plater_->canvas3D()->get_canvas_size();
            wxGetApp().imgui()->set_display_size(static_cast<float>(canvas_size.get_width()), static_cast<float>(canvas_size.get_height()));
            BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << ", start to init opengl";
            wxGetApp().init_opengl();
            if (!is_gl_version_greater_or_equal_to(2, 0)) {
                return;
            }

            BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << ", finished init opengl";
            plater_->canvas3D()->init();

            BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << ", finished init canvas3D";
            wxGetApp().imgui()->new_frame();

            BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << ", finished init imgui frame";
            plater_->canvas3D()->enable_render(true);

            if (!slow_bootup) {
                BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << ", start to render a first frame for test";
                plater_->canvas3D()->render(false);
                BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << ", finished rendering a first frame for test";
            }
        }
        if (app_config->get("default_page") == "1")
        {
            // First launch: keep the home page in front so the home-page SPA
            // (get_is_first_install -> deviceAddEnd=0) renders the onboarding
            // guide (machine -> LAN connect -> new user tour). With default_page
            // set to "1" the app would otherwise open the 3D editor and the
            // guide would never show on a clean install.
#if defined(__WXOSX__) && defined(CREALITYPRINT_APP_STORE)
            // First-launch: keep the home page in front so the home-page SPA
            // renders the onboarding guide. Other platforms keep the original
            // default page.
            if (m_app_first_launch) {
                mainframe->select_tab(size_t(MainFrame::tpHome));
                mainframe->m_topbar->SetSelection(size_t(MainFrame::tpHome));
            } else
#endif
            {
                mainframe->select_tab(size_t(MainFrame::tp3DEditor));
                mainframe->m_topbar->SetSelection(size_t(MainFrame::tp3DEditor));
            }
        }
        else if (is_editor())
        {
            mainframe->select_tab(size_t(MainFrame::tpHome));
            mainframe->m_topbar->SetSelection(size_t(MainFrame::tpHome));
         }
#ifndef __linux__
        mainframe->Thaw();
#endif
        plater_->trigger_restore_project(1);
        BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << ", end load_gl_resources";
    }
//#endif

    //BBS: remove GCodeViewer as seperate APP logic
    /*if (this->init_params->start_as_gcodeviewer) {
        if (! this->init_params->input_files.empty())
            this->plater()->load_gcode(wxString::FromUTF8(this->init_params->input_files[0].c_str()));
    }
    else
    {
        if (! this->init_params->preset_substitutions.empty())
            show_substitutions_info(this->init_params->preset_substitutions);

#if 0
        // Load the cummulative config over the currently active profiles.
        //FIXME if multiple configs are loaded, only the last one will have an effect.
        // We need to decide what to do about loading of separate presets (just print preset, just filament preset etc).
        // As of now only the full configs are supported here.
        if (!m_print_config.empty())
            this->gui->mainframe->load_config(m_print_config);
#endif
        if (! this->init_params->load_configs.empty())
            // Load the last config to give it a name at the UI. The name of the preset may be later
            // changed by loading an AMF or 3MF.
            //FIXME this is not strictly correct, as one may pass a print/filament/printer profile here instead of a full config.
            this->mainframe->load_config_file(this->init_params->load_configs.back());
        // If loading a 3MF file, the config is loaded from the last one.
        if (!this->init_params->input_files.empty()) {
            const std::vector<size_t> res = this->plater()->load_files(this->init_params->input_files);
            if (!res.empty() && this->init_params->input_files.size() == 1) {
                // Update application titlebar when opening a project file
                const std::string& filename = this->init_params->input_files.front();
                //BBS: remove amf logic as project
                if (boost::algorithm::iends_with(filename, ".3mf"))
                    this->plater()->set_project_filename(filename);
            }
        }
        if (! this->init_params->extra_config.empty())
            this->mainframe->load_config(this->init_params->extra_config);
    }*/

    // BBS: to be checked
#if 1
    // show "Did you know" notification
    if (app_config->get("show_hints") == "true" && !is_gcode_viewer()) {
        plater_->get_notification_manager()->push_hint_notification(false);
    }
#endif

    m_show_gcode_window = app_config->get_bool("show_gcode_window");

    // remove old log files over LOG_FILES_MAX_NUM
    std::string log_addr = data_dir();
    if (!log_addr.empty()) {
        auto log_folder = boost::filesystem::path(log_addr) / "log";
        if (boost::filesystem::exists(log_folder)) {
           std::vector<std::pair<time_t, std::string>> files_vec;
           for (auto& it : boost::filesystem::directory_iterator(log_folder)) {
               auto temp_path = it.path();
               try {
                   std::time_t lw_t = boost::filesystem::last_write_time(temp_path) ;
                   files_vec.push_back({ lw_t, temp_path.filename().string() });
               } catch (const std::exception &ex) {
               }
           }
           std::sort(files_vec.begin(), files_vec.end(), [](
               std::pair<time_t, std::string> &a, std::pair<time_t, std::string> &b) {
               return a.first > b.first;
           });

           while (files_vec.size() > LOG_FILES_MAX_NUM) {
               auto full_path = log_folder / boost::filesystem::path(files_vec[files_vec.size() - 1].second);
               BOOST_LOG_TRIVIAL(info) << "delete log file over " << LOG_FILES_MAX_NUM << ", filename: "<< files_vec[files_vec.size() - 1].second;
               try {
                   boost::filesystem::remove(full_path);
               }
               catch (const std::exception& ex) {
                   BOOST_LOG_TRIVIAL(error) << "failed to delete log file: "<< files_vec[files_vec.size() - 1].second << ". Error: " << ex.what();
               }
               files_vec.pop_back();
           }
        }
    }
    BOOST_LOG_TRIVIAL(info) << "finished post_init";
    
//BBS: remove the single instance currently
#ifdef _WIN32
    // Sets window property to mainframe so other instances can indentify it.
    OtherInstanceMessageHandler::init_windows_properties(mainframe, m_instance_hash_int);
#endif //WIN32
}

wxDEFINE_EVENT(EVT_SHOW_DIALOG, wxCommandEvent);


IMPLEMENT_APP(GUI_App)

//BBS: remove GCodeViewer as seperate APP logic
//GUI_App::GUI_App(EAppMode mode)
GUI_App::GUI_App()
    : wxApp()
    //, m_app_mode(mode)
    , m_app_mode(EAppMode::Editor)
    , m_em_unit(10)
    , m_imgui(new ImGuiWrapper())
	, m_removable_drive_manager(std::make_unique<RemovableDriveManager>())
    , m_downloader(std::make_unique<Downloader>())
	, m_other_instance_message_handler(std::make_unique<OtherInstanceMessageHandler>())
{
	//app config initializes early becasuse it is used in instance checking in CrealityPrint.cpp
    this->init_app_config();
    this->init_download_path();
#if wxUSE_WEBVIEW_EDGE
    this->init_webview_runtime();
#endif
    initDevelopParams();
#ifdef __APPLE__
    Slic3r::GUI::register_receive_mac([](const std::string& m) { wxGetApp().on_interinstance_message(m); });
#endif
}

void GUI_App::shutdown()
{
    BOOST_LOG_TRIVIAL(info) << "GUI_App::shutdown enter";

	if (m_removable_drive_manager) {
		removable_drive_manager()->shutdown();
	}

    if (m_is_recreating_gui) return;
    g_app_exiting = true;
    m_is_closing = true;
    BOOST_LOG_TRIVIAL(info) << "GUI_App::shutdown exit";
}

GUI_App::~GUI_App()
{
    BOOST_LOG_TRIVIAL(info) << __FUNCTION__<< boost::format(": enter");
    if (app_config != nullptr) {
        BOOST_LOG_TRIVIAL(info) << __FUNCTION__<< boost::format(": destroy app_config");
        delete app_config;
    }

    if (preset_bundle != nullptr) {
        BOOST_LOG_TRIVIAL(info) << __FUNCTION__<< boost::format(": destroy preset_bundle");
        delete preset_bundle;
    }

    if (preset_updater != nullptr) {
        BOOST_LOG_TRIVIAL(info) << __FUNCTION__<< boost::format(": destroy preset updater");
        delete preset_updater;
    }

    if (printerPresetConfig != nullptr) {
        BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << boost::format(": destroy printerPresetConfig");
        delete printerPresetConfig;
    }

    BOOST_LOG_TRIVIAL(info) << __FUNCTION__<< boost::format(": exit");
}

// If formatted for github, plaintext with OpenGL extensions enclosed into <details>.
// Otherwise HTML formatted for the system info dialog.
std::string GUI_App::get_gl_info(bool for_github)
{
    return OpenGLManager::get_gl_info().to_string(for_github);
}

wxGLContext* GUI_App::init_glcontext(wxGLCanvas& canvas)
{
    return m_opengl_mgr.init_glcontext(canvas);
}

bool GUI_App::init_opengl()
{
#ifdef __linux__
    bool status = m_opengl_mgr.init_gl();
    if (status)
        m_opengl_initialized = true;
    return status;
#else
    return m_opengl_mgr.init_gl();
#endif
}

// gets path to PrusaSlicer.ini, returns semver from first line comment
static boost::optional<Semver> parse_semver_from_ini(std::string path)
{
    std::ifstream stream(path);
    std::stringstream buffer;
    buffer << stream.rdbuf();
    std::string body = buffer.str();
    size_t start = body.find("CrealityPrint ");
    if (start == std::string::npos) {
        start = body.find("CrealityPrint ");
        if (start == std::string::npos)
            return boost::none;
    }
    body = body.substr(start + 12);
    size_t end = body.find_first_of(" \n");
    if (end < body.size())
        body.resize(end);
    return Semver::parse(body);
}

#ifdef _WIN32
// PRINT-ADVISOR F8: true when `path` (UTF-8) lies inside the Creality Print data folder the
// MentorSlicer one was imported from (case-insensitive, normalized).
static bool inside_legacy_creality_data_dir(const std::string& path)
{
    const auto normal = [](const std::filesystem::path& p) {
        return wxString(p.lexically_normal().make_preferred().wstring()).Lower();
    };
    const wxString legacy = normal(std::filesystem::path(wxStandardPaths::Get().GetUserConfigDir().ToStdWstring()) /
                                   std::filesystem::u8path(DataMigration::LEGACY_CREALITY_DATA_RELATIVE));
    const wxString candidate = normal(std::filesystem::u8path(path));
    return candidate == legacy || candidate.StartsWith(legacy + "\\");
}
#endif

void GUI_App::init_download_path()
{
    std::string down_path = app_config->get("download_path");

    if (down_path.empty()) {
        boost::filesystem::path user_down_path =  boost::filesystem::path(data_dir()) / "cloud_download_data";//wxStandardPaths::Get().GetUserDir(wxStandardPaths::Dir_Downloads).ToUTF8().data();
        app_config->set("download_path", user_down_path.string());
    }
    else {
        fs::path dp(down_path);
#ifdef _WIN32
        // PRINT-ADVISOR F8: the imported config still points into the Creality Print folder.
        const bool legacy_path = !m_datadir_redefined && inside_legacy_creality_data_dir(down_path);
#else
        const bool legacy_path = false;
#endif
        if (legacy_path || !fs::exists(dp)) {

            boost::filesystem::path user_down_path =  boost::filesystem::path(data_dir()) / "cloud_download_data";
            if (!fs::exists(user_down_path))
                fs::create_directories(user_down_path);
            app_config->set("download_path", user_down_path.string());
        }
    }
}

#if wxUSE_WEBVIEW_EDGE
void GUI_App::init_webview_runtime()
{
#ifdef __WIN32__
    const bool single_process = app_config->get_bool("webview_single_process");
    WebView::SetForceSingleProcess(single_process);
    BOOST_LOG_TRIVIAL(info) << "[WebViewRuntime] config webview_single_process=" << (single_process ? "true" : "false");
#endif
    // Check WebView Runtime
    // The fork no longer ships the WebView2 installer: only tell the user where
    // to get it, never launch anything.
    if (!WebView::CheckWebViewRuntime()) {
        wxMessageBox(_L("MentorSlicer requires the Microsoft WebView2 Runtime to operate certain features.\nInstall it from https://developer.microsoft.com/microsoft-edge/webview2/ and restart MentorSlicer."),
                     _L("WebView2 Runtime"), wxOK | wxICON_WARNING);
    }
}
bool GUI_App::mark_webview_runtime_repair_prompted()
{
    if (m_webview_runtime_repair_prompted) {
        BOOST_LOG_TRIVIAL(info) << "[WebViewRuntime] Repair prompt already shown in this session, skip duplicate prompt.";
        return false;
    }
    m_webview_runtime_repair_prompted = true;
    return true;
}
void GUI_App::reinstall_webview_runtime()
{
    // Check WebView Runtime
    int nRet = wxMessageBox(
        _L("MentorSlicer requires the Microsoft WebView2 Runtime,But it has been found that the abnormal operation of Microsoft WebView2 may be related to the installation of Microsoft Edge. \nClick the Yes to reinstall Microsoft Edge"),
        _L("WebView2 Runtime"), wxYES_NO);
    if (nRet != wxYES)
        return;

    // Fork: the automatic Edge/WebView2 reinstall is disabled, so the user has to reinstall it by hand.
    wxMessageBox(_L("Failed to reinstall the Microsoft WebView2 Runtime. Please reinstall it manually."),
                 _L("WebView2 Runtime"), wxOK | wxICON_WARNING);
    if (mainframe)
        mainframe->Close(true);
    else
        ExitMainLoop();
}
#endif

bool GUI_App::confirm_mesh_paint_warning()
{
    MessageDialog dlg(nullptr,
        _L("This operation rebuilds the model's mesh. Painted color, supports, seam and "
           "fuzzy-skin will be transferred to the new mesh by a best-effort approximation, "
           "so the result may be slightly off and, in rare cases, some painting may be lost.\n\n"
           "Do you want to continue?"),
        _L("Painting may change"),
        wxICON_WARNING | wxYES_NO | wxNO_DEFAULT);
    return dlg.ShowModal() == wxID_YES;
}

void GUI_App::init_app_config()
{
	// Profiles for the alpha are stored into the PrusaSlicer-alpha directory to not mix with the current release.
    SetAppName(SLIC3R_APP_KEY);
//	SetAppName(SLIC3R_APP_KEY "-alpha");
//  SetAppName(SLIC3R_APP_KEY "-beta");
//	SetAppDisplayName(SLIC3R_APP_NAME);

	// Set the Slic3r data directory at the Slic3r XS module.
	// Unix: ~/ .Slic3r
	// Windows : "C:\Users\username\AppData\Roaming\Slic3r" or "C:\Documents and Settings\username\Application Data\Slic3r"
	// Mac : "~/Library/Application Support/Slic3r"
    if (data_dir().empty()) {
        // PRINT-ADVISOR F8: the data folder is the wx user data dir itself (%APPDATA%\MentorSlicer,
        // AppName = SLIC3R_APP_KEY), without the "<folder>\<version>" suffix of an explicit --datadir.
#ifndef __linux__
        const std::string root = wxStandardPaths::Get().GetUserDataDir().ToUTF8().data();
        m_app_first_launch = !root.empty() && !fs::exists(root);
        set_data_dir(root, true);
#else
        wxString root;
        if (!wxGetEnv(wxS("XDG_CONFIG_HOME"), &root) || root.empty())
            root = wxFileName::GetHomeDir() + wxS("/.config");
        set_data_dir((root + "/" + GetAppName()).ToUTF8().data(), true);
#endif
        const auto target = std::filesystem::u8path(Slic3r::data_dir());
#ifdef _WIN32
        // PRINT-ADVISOR F8: one-time copy of the Creality Print data folder; the source is never
        // modified. Throws DataMigration::ImportError (shown by GUI_Run) and never falls through to
        // initialize() after a failure.
        DataMigration::ImportOptions legacy_import;
        legacy_import.source = std::filesystem::path(wxStandardPaths::Get().GetUserConfigDir().ToStdWstring()) /
                        std::filesystem::u8path(DataMigration::LEGACY_CREALITY_DATA_RELATIVE);
        legacy_import.target = target;
        legacy_import.config_name = SLIC3R_APP_KEY ".conf";
        legacy_import.application_version = CREALITYPRINT_VERSION;
        if (DataMigration::import_legacy_directory(legacy_import).imported)
            m_app_first_launch = false;
#endif
        // Fresh install only: an imported folder already holds Creality's committed manifest, and
        // initialize() would otherwise create its lock and diagnostics next to the target on every start.
        if (!fs::exists(fs::path(data_dir()) / "migration" / "manifest.json") &&
            !fs::exists(fs::path(data_dir()) / "migration" / DataMigration::IMPORT_MARKER)) {
            DataMigration::Options migration;
            migration.target = target;
            migration.resources = std::filesystem::u8path(resources_dir());
            migration.data_version = CREALITYPRINT_DATA_VERSION;
            migration.application_version = CREALITYPRINT_VERSION;
            migration.alpha = boost::algorithm::icontains(std::string(PROJECT_VERSION_EXTRA), "alpha");
            migration.target_name = SLIC3R_APP_KEY;
            migration.config_name = SLIC3R_APP_KEY ".conf";
            migration.validate_preset = DataMigration::validate_preset_readonly;
            try {
                DataMigration::initialize(migration);
            } catch (const std::exception& error) {
                // Include filesystem/permission failures in the non-interactive migration error path.
                throw DataMigration::Error(error.what());
            }
        }
        fs::create_directories(fs::path(Slic3r::data_dir()) / "log");
        chdir(encode_path((Slic3r::data_dir() + "/log").c_str()).c_str());
    } else {
        // Explicit --datadir keeps its existing behavior and never imports another installation.
        m_datadir_redefined = true;
    }

    // start log here
    std::time_t       t        = std::time(0);
    std::tm *         now_time = std::localtime(&t);
    std::stringstream buf;
    buf << std::put_time(now_time, "debug_%a_%b_%d_%H_%M_%S_");
    buf << get_current_pid() << ".log";
    std::string log_filename = buf.str();

    set_log_path_and_level(log_filename, 3);

    //BBS: remove GCodeViewer as seperate APP logic
    if (!app_config)
        app_config = new AppConfig();
    // app_config = new AppConfig(is_editor() ? AppConfig::EAppMode::Editor : AppConfig::EAppMode::GCodeViewer);

    // Multi-extruder printer presets no longer include the nozzle suffix (except D3 Pro).
    // Migrate the persisted config and user presets before they are loaded.
    PresetNameReplacements migrated_preset_names;
    const fs::path preset_name_migration_marker =
        legacy_multi_extruder_preset_name_migration_marker();
    if (!DataMigration::preserves_user_presets(std::filesystem::u8path(data_dir())) &&
        !fs::exists(preset_name_migration_marker)) {
        bool migration_completed = false;
        migrated_preset_names = migrate_legacy_multi_extruder_preset_names(*app_config, migration_completed);
        if (migration_completed &&
            mark_legacy_multi_extruder_preset_name_migration_complete(preset_name_migration_marker))
            BOOST_LOG_TRIVIAL(info) << "Completed one-time multi-extruder preset-name migration";
        else if (!migration_completed)
            BOOST_LOG_TRIVIAL(warning) << "Preset-name migration was incomplete and will be retried next launch";
    }
    
    m_config_corrupted = false;
	// load settings
	m_app_conf_exists = app_config->exists();
    if (m_app_conf_exists) {
        std::string error = app_config->load();
        if (!error.empty()) {
            BOOST_LOG_TRIVIAL(warning) << "Skipping loaded preset-name migration because application config failed to load";
        } else if (migrate_loaded_app_config_preset_names(*app_config, migrated_preset_names)) {
            BOOST_LOG_TRIVIAL(info) << "Migrated legacy multi-extruder preset names in loaded application config";
        }
        if (!error.empty()) {
            // Orca: if the config file is corrupted, we will show a error dialog and create a default config file.
            m_config_corrupted = true;

        }
    }
    else {
#ifdef _WIN32
        // update associate files from registry information
        if (is_associate_files(L"3mf")) {
            app_config->set("associate_3mf", "true");
        }
        if (is_associate_files(L"stl")) {
            app_config->set("associate_stl", "true");
        }
        if (is_associate_files(L"step") && is_associate_files(L"stp")) {
            app_config->set("associate_step", "true");
        }
#endif // _WIN32
    }
    set_logging_level(Slic3r::level_string_to_boost(app_config->get("log_severity_level")));

    if (printerPresetConfig == nullptr) {
        printerPresetConfig = new PrinterPresetConfig();
    }
}

// returns true if found newer version and user agreed to use it
void GUI_App::copy_older_config()
{
    preset_bundle->copy_files(m_older_data_dir_path);
}
std::string GUI_App::get_client_id()
{
    // Fork: no machine identifier is handed to the web pages.
    return {};
}

void GUI_App::startTour(int startIndex)
{
    m_UITour = new UITour(this->mainframe);
    wxRect size = this->mainframe->GetRect();

    if (startIndex == 0)
    {
        // step0
        wxRect printerBtn = sidebar().obj_list()->printComboRect();
        m_UITour->AddStep(0, printerBtn, _L("Here you can select and add your printer presets"), "", "userGuide_step1", "", wxRIGHT);
    }

    //step1
    wxRect wifiBtn = sidebar().obj_list()->wifiBtn();
    m_UITour->AddStep(1 - startIndex, wifiBtn, _L("Click 【"), _L("】 to select Creality printer matching the chosen preset"),
       "userGuide_step2", "wifi", wxRIGHT);

    //step2
    Plater*     plater = wxGetApp().plater();
    GLCanvas3D* canvas = plater->get_current_canvas3D();
    Vec2f       pos    = canvas->getToolBar().get_item_position_from_top_left(*canvas, "add");
    
    wxRect rect;
    rect.x      = pos.x();
    #ifdef __APPLE__
   float scale = 1;
    rect.x = pos.x() ;
    rect.y      = pos.y() + 37;
    #else
    float scale = mainframe->scale_factor();
    rect.y      = pos.y() + 37 * scale;
    #endif
    rect.SetWidth(40 * scale);
    rect.SetHeight(40 * scale);
    m_UITour->AddStep(2 - startIndex, rect, _L("Here to import model files"), "", "userGuide_step3", "", wxRIGHT);

    // step3
    HoverBorderIcon* flushBtn                     = sidebar().autoMap_button();
    wxRect           step3Rect = flushBtn->GetRect();
    auto p_rect = this->mainframe->ClientToScreen(wxPoint(0, 0));
    wxPoint          screenPos = flushBtn->GetScreenPosition() - p_rect;
    wxRect           resStep3Rect                 = wxRect(screenPos.x, screenPos.y, step3Rect.width + 150 * scale, step3Rect.height);
    
    m_UITour->AddStep(3 - startIndex, resStep3Rect, _L("Click the Mapping button【"),
                                   _L("】to map filament colors and types(If the selected device supports multi-color printing)"), "userGuide_step4",
                               "auto_mapping_dark", wxLEFT);

    // step4
    wxRect setp4Rect = canvas->getSlicerBtnRec();
    m_UITour->AddStep(4 - startIndex, setp4Rect, _L("Click 【Slice plate】 to generate the G-code file for printing"), "",
                      "userGuide_step5", "",
                               wxUP);

    // step5
    wxRect setp5Rect = canvas->getSenderBtnRec();
    m_UITour->AddStep(5 - startIndex, setp5Rect, _L("Click 【Send print】, send the file to the selected device and start printing"), "",
                               "userGuide_step6", "", wxUP);

    m_UITour->Start();

#ifndef __APPLE__
    this->mainframe->set_content_visible(false);
#endif // !1
}

void GUI_App::startTour_Apple() 
{
    wxGetApp().app_config->set("is_first_install", "1");
}

void GUI_App::set_picking_effect(EPickingEffect effect)
{
    if (m_picking_effect != effect)
    {
        std::string str_picking_effect{};
        switch (effect)
        {
        case EPickingEffect::Disabled:
            str_picking_effect = "Disabled";
            break;
        case EPickingEffect::StencilOutline:
            str_picking_effect = "StencilOutline";
            break;
        case EPickingEffect::Silhouette:
            str_picking_effect = "Silhouette";
            break;
        }
        BOOST_LOG_TRIVIAL(info) << "Switched picking effect to: " << str_picking_effect;
        m_picking_effect = effect;
    }
}

EPickingEffect GUI_App::get_picking_effect() const
{
    return m_picking_effect;
}

void GUI_App::set_picking_color(const ColorRGB& color)
{
    m_picking_color = color;
}

const ColorRGB& GUI_App::get_picking_color() const
{
    return m_picking_color;
}

void GUI_App::openDevelopMode(bool open)
{    
    if(isAlpha())
        return;

    app_config->set("role_type", "1");
    update_develop_state();
}

void GUI_App::initDevelopParams()
{
    app_config->set("is_factory_mode", "false");
    app_config->save();
    boost::filesystem::path path_resources = resources_dir();
    boost::filesystem::path creality_file = boost::filesystem::path(path_resources.string()).append("profiles").append("CrealityUserMode.json");
    if (boost::filesystem::exists(creality_file))
    {
        try {
            boost::nowide::ifstream t(creality_file.string());
            std::stringstream buffer;
            buffer << t.rdbuf();
            json jLocal = json::parse(buffer.str());
            auto paramsList = jLocal["params_list"];
            for (const auto& item : paramsList)
            {
                std::string paramsKey = item["key"];
                std::string paramType = item["type"];
                m_DevelopParamslist.insert(std::make_pair(paramsKey, paramType));
            }
        }
        catch (std::exception e) {

        }

    }
}

bool GUI_App::isAlpha()
{
    std::string version = std::string(PROJECT_VERSION_EXTRA);
    bool        is_alpha = boost::algorithm::icontains(version, "alpha");
    return is_alpha;
}

bool GUI_App::isDevelopParams(const std::string key)
{
    std::string processed_key = key;
    size_t pos = processed_key.find("#0");
    if (pos != std::string::npos) {
        processed_key.erase(pos, 2);
    }

    pos = processed_key.find("#1");
    if (pos != std::string::npos) {
        processed_key.erase(pos, 2);
    }

    auto it = wxGetApp().m_DevelopParamslist.find(processed_key);
    if (it != wxGetApp().m_DevelopParamslist.end())
    {
        return true;
    }
	return false;
}

std::string GUI_App::getDevelopParamsType(const std::string key)
{
    std::string processed_key = key;
    size_t pos = processed_key.find("#0");
    if (pos != std::string::npos) {
        processed_key.erase(pos, 2);
    }

    pos = processed_key.find("#1");
    if (pos != std::string::npos) {
        processed_key.erase(pos, 2);
    }

    auto it = wxGetApp().m_DevelopParamslist.find(processed_key);
    if (it != wxGetApp().m_DevelopParamslist.end())
    {
        return it->second;
    }
    return "";
}

const std::map<std::string, std::string> GUI_App::getUserKeys()
{
    return m_DevelopParamslist;
}

void GUI_App::init_single_instance_checker(const std::string &name, const std::string &path)
{
    BOOST_LOG_TRIVIAL(debug) << "init wx instance checker " << name << " "<< path;
    m_single_instance_checker = std::make_unique<wxSingleInstanceChecker>(boost::nowide::widen(name), boost::nowide::widen(path));
}

bool GUI_App::OnInit()
{
    ::Label::initSysFont();
    system_memory_stats(__FUNCTION__);
    try {
        #ifdef _WIN32
        boost::filesystem::path data_dir = boost::filesystem::path(Slic3r::data_dir()).append("applock.data");
        fs::ofstream file(data_dir);
        file.close();
        boost::interprocess::file_lock fl(from_u8(data_dir.string().c_str()));
        fl.lock();
        #endif
        bool res = on_init_inner();
        //test code
         //int* p = nullptr;
         //*p = 0;
         #ifdef _WIN32
         fl.unlock();
         #endif
        return res;
    } catch (const std::exception &err) {
        BOOST_LOG_TRIVIAL(error) << __FUNCTION__<<" got a generic exception, reason = " << err.what();
        generic_exception_handle();
        return false;
    }
}

int GUI_App::OnExit()
{
    advisor_on_shutdown(); // PRINT-ADVISOR advisor-hook
    g_app_exiting = true;
    BOOST_LOG_TRIVIAL(warning) << __FUNCTION__ << " start";

#if defined(__WIN32__) && wxUSE_WEBVIEW_EDGE
    // CallOnExit() has already destroyed pending windows and their WebView
    // controls. It is now safe to release the shared WebView2 environment,
    // wait for the PIDs captured by BeginShutdown(), and clean the profile.
    WebView::FinalizeShutdown();
#endif
    int result = wxApp::OnExit();

    BOOST_LOG_TRIVIAL(warning) << __FUNCTION__ << " end";
    boost::log::core::get()->flush();
    return result;
}

void GUI_App::OnUnhandledException()
{

    wxApp::OnUnhandledException();
}
class wxBoostLog : public wxLog
{
    void DoLogText(const wxString &msg) override {

        BOOST_LOG_TRIVIAL(warning) << msg.ToUTF8().data();
    }
    ~wxBoostLog() override
    {
        // This is a hack. Prevent thread logs from going to wxGuiLog on app quit.
        auto t = wxLog::SetActiveTarget(this);
        wxLog::FlushActive();
        wxLog::SetActiveTarget(t);
    }
};

bool GUI_App::on_init_inner()
{
    wxLog::SetActiveTarget(new wxBoostLog());
    BaseColorSpectraCache::initialize();
    OfficialMaterialColors::initialize_from_resources();
#ifdef __linux__
    ensure_flatpak_user_launcher();
#endif
    wxLog::SetLogLevel(wxLOG_Message);

    // Set initialization of image handlers before any UI actions - See GH issue #7469
    wxInitAllImageHandlers();

#ifdef NDEBUG
    wxImage::SetDefaultLoadFlags(0); // ignore waring in release build
#endif

#if defined(_WIN32) && ! defined(_WIN64)
    // BBS: remove 32bit build prompt
    // Win32 32bit build.
#endif // _WIN64

    // Forcing back menu icons under gtk2 and gtk3. Solution is based on:
    // https://docs.gtk.org/gtk3/class.Settings.html
    // see also https://docs.wxwidgets.org/3.0/classwx_menu_item.html#a2b5d6bcb820b992b1e4709facbf6d4fb
    // TODO: Find workaround for GTK4
#if defined(__WXGTK20__) || defined(__WXGTK3__)
    g_object_set (gtk_settings_get_default (), "gtk-menu-images", TRUE, NULL);
#endif

#if defined(__WXGTK20__) || defined(__WXGTK3__)
    // Suppress harmless GTK critical warnings from GTK3/wxWidgets interactions.
    // These include allocation/events on unrealized widgets and style-context
    // operations while custom controls are still being constructed.
    g_log_set_handler("Gtk", G_LOG_LEVEL_CRITICAL,
        [](const gchar *log_domain, GLogLevelFlags log_level, const gchar *message, gpointer user_data) {
            if (message && (strstr(message, "gtk_widget_set_allocation") ||
                            strstr(message, "WIDGET_REALIZED_FOR_EVENT") ||
                            strstr(message, "gtk_widget_get_style_context") ||
                            strstr(message, "gtk_style_context_add_provider")))
                return;
            g_log_default_handler(log_domain, log_level, message, user_data);
        }, nullptr);
#endif

    wxGetApp().Bind(wxEVT_QUERY_END_SESSION, [this](auto & e) {
        BOOST_LOG_TRIVIAL(info) << __FUNCTION__<< "received wxEVT_QUERY_END_SESSION";
        if (mainframe) {
            wxCloseEvent e2(wxEVT_CLOSE_WINDOW);
            e2.SetCanVeto(true);
            mainframe->GetEventHandler()->ProcessEvent(e2);
            if (e2.GetVeto()) {
                e.Veto();
                return;
            }
        }
        for (auto d : dialogStack)
            d->EndModal(wxID_ABORT);
    });

    // Verify resources path
    const wxString resources_dir = from_u8(Slic3r::resources_dir());
    wxCHECK_MSG(wxDirExists(resources_dir), false,
        wxString::Format("Resources path does not exist or is not a directory: %s", resources_dir));

#ifdef __linux__
    if (! check_old_linux_datadir(GetAppName())) {
        std::cerr << "Quitting, user chose to move their data to new location." << std::endl;
        return false;
    }
#endif

    BOOST_LOG_TRIVIAL(info) << boost::format("gui mode, Current CrealityPrint Version %1%")%CREALITYPRINT_VERSION;
    // Enable this to get the default Win32 COMCTRL32 behavior of static boxes.
//    wxSystemOptions::SetOption("msw.staticbox.optimized-paint", 0);
    // Enable this to disable Windows Vista themes for all wxNotebooks. The themes seem to lead to terrible
    // performance when working on high resolution multi-display setups.
//    wxSystemOptions::SetOption("msw.notebook.themed-background", 0);

//     Slic3r::debugf "wxWidgets version %s, Wx version %s\n", wxVERSION_STRING, wxVERSION;
    if (is_editor()) {
        std::string msg = Slic3r::Http::tls_global_init();
        std::string ssl_cert_store = app_config->get("tls_accepted_cert_store_location");
        bool ssl_accept = app_config->get("tls_cert_store_accepted") == "yes" && ssl_cert_store == Slic3r::Http::tls_system_cert_store();

        if (!msg.empty() && !ssl_accept) {
            RichMessageDialog
                dlg(nullptr,
                    wxString::Format(_L("%s\nDo you want to continue?"), msg),
                    SLIC3R_APP_FULL_NAME, wxICON_QUESTION | wxYES_NO);
            dlg.ShowCheckBox(_L("Remember my choice"));
            if (dlg.ShowModal() != wxID_YES) return false;

            app_config->set("tls_cert_store_accepted",
                dlg.IsCheckBoxChecked() ? "yes" : "no");
            app_config->set("tls_accepted_cert_store_location",
                dlg.IsCheckBoxChecked() ? Slic3r::Http::tls_system_cert_store() : "");
        }
    }
    on_init_custom_config();
    // !!! Initialization of UI settings as a language, application color mode, fonts... have to be done before first UI action.
    // Like here, before the show InfoDialog in check_older_app_config()

    bool is_first_run = !boost::filesystem::exists(data_dir() + "/" SLIC3R_APP_KEY ".conf") || 
                        app_config->get("language").empty();
#ifdef __APPLE__
    if (is_first_run) {
        // 首次运行时使用系统语言
        std::string system_lang = get_system_language();
        app_config->set("language", system_lang);
        app_config->save();
    }
#endif

    // If load_language() fails, the application closes.
    load_language(wxString(), true);

#ifdef _MSW_DARK_MODE

#ifndef __WINDOWS__
    #if __linux__
        if(!isRunningInFlatpak())
        {
            wxSystemAppearance app = wxSystemSettings::GetAppearance();
            GUI::wxGetApp().app_config->set("dark_color_mode", app.IsDark() ? "1" : "0");
            GUI::wxGetApp().app_config->save();
        }
    #else
        wxSystemAppearance app = wxSystemSettings::GetAppearance();
        GUI::wxGetApp().app_config->set("dark_color_mode", app.IsDark() ? "1" : "0");
        GUI::wxGetApp().app_config->save();
    #endif
#endif // __APPLE__


    bool init_dark_color_mode = dark_mode();
    bool init_sys_menu_enabled = app_config->get("sys_menu_enabled") == "1";
#ifdef __WINDOWS__
     NppDarkMode::InitDarkMode(init_dark_color_mode, init_sys_menu_enabled);
#endif // __WINDOWS__

#endif
    // initialize label colors and fonts
    init_label_colours();
    init_fonts();
    wxGetApp().Update_dark_mode_flag();

#ifdef _MSW_DARK_MODE
    // app_config can be updated in check_older_app_config(), so check if dark_color_mode and sys_menu_enabled was changed
    if (bool new_dark_color_mode = dark_mode();
        init_dark_color_mode != new_dark_color_mode) {

#ifdef __WINDOWS__
        NppDarkMode::SetDarkMode(new_dark_color_mode);
#endif // __WINDOWS__

        init_label_colours();
        //update_label_colours_from_appconfig();
    }
    if (bool new_sys_menu_enabled = app_config->get("sys_menu_enabled") == "1";
        init_sys_menu_enabled != new_sys_menu_enabled)
#ifdef __WINDOWS__
        NppDarkMode::SetSystemMenuForApp(new_sys_menu_enabled);
#endif
#endif

    if(app_config->get("version") != SLIC3R_VERSION) {
        app_config->set("version", SLIC3R_VERSION);
    }

    SplashScreen * scrn = nullptr;
    if (app_config->get("show_splash_screen") == "true") {
        // make a bitmap with dark grey banner on the left side
        //BBS make BBL splash screen bitmap
        wxBitmap bmp = SplashScreen::MakeBitmap();
        // Detect position (display) to show the splash screen
        // Now this position is equal to the mainframe position
        wxPoint splashscreen_pos = wxDefaultPosition;
        long splash_style = wxSPLASH_TIMEOUT | wxSPLASH_CENTRE_ON_SCREEN;
#if defined(__WXGTK__)
        const bool wayland_session = gui_app_is_wayland_session();
#else
        const bool wayland_session = false;
#endif
        if (!wayland_session && app_config->has("window_mainframe")) {
            auto metrics = WindowMetrics::deserialize(app_config->get("window_mainframe"));
            if (metrics) {
                wxPoint mainframe_pos = metrics->get_rect().GetPosition();
                int display_index = wxDisplay::GetFromPoint(mainframe_pos);
                if (display_index != wxNOT_FOUND) {
                    wxRect display_rect = wxDisplay(display_index).GetClientArea();
                    splashscreen_pos = wxPoint(display_rect.x + (display_rect.width - bmp.GetWidth()) / 2,
                                               display_rect.y + (display_rect.height - bmp.GetHeight()) / 2);
                    splash_style = wxSPLASH_TIMEOUT | wxSPLASH_NO_CENTRE;
                }
            }
        }


        BOOST_LOG_TRIVIAL(info) << "begin to show the splash screen...";
        //BBS use BBL splashScreen
        scrn = new SplashScreen(bmp, splash_style, 1500, splashscreen_pos);
       
        //CusDialog* wDialog = new CusDialog(this->mainframe, wxID_ANY, "Login");
        
        scrn->Bind(wxEVT_CLOSE_WINDOW, [this](wxCloseEvent& evt) {
            int a = 0;
          /*  wxRect size = this->mainframe->GetRect();
            wDialog->SetSize(size.width, size.height);
            wDialog->Show();*/
            // 创建UI Tour
            //startTour();
            evt.Skip();
        });   
        wxYield();

        scrn->SetText(_L("Loading configuration") + dots);
    }

    BOOST_LOG_TRIVIAL(info) << "loading systen presets...";
    
    preset_bundle = new PresetBundle();

    // just checking for existence of Slic3r::data_dir is not enough : it may be an empty directory
    // supplied as argument to --datadir; in that case we should still run the wizard
    preset_bundle->setup_directories();


    if (m_init_app_config_from_older)
        copy_older_config();

    if (is_editor()) {
//#ifdef __WXMSW__
        //if (app_config->get("associate_3mf") == "true")
        //    associate_files(L"3mf");
        //if (app_config->get("associate_stl") == "true")
        //    associate_files(L"stl");
        //if (app_config->get("associate_step") == "true") {
        //    associate_files(L"step");
        //    associate_files(L"stp");
        //}
        //associate_url(L"MentorSlicer");

        //if (app_config->get("associate_gcode") == "true")
        //    associate_files(L"gcode");
//#endif // __WXMSW__

        preset_updater = new PresetUpdater();

        // start load profile family after preset updater finished
        ProfileFamilyLoader::init();

        Bind(EVT_SHOW_DIALOG, [this](const wxCommandEvent& evt) {
                    wxString   msg = evt.GetString();
                    InfoDialog dlg(this->mainframe, _L("Info"), msg);
            dlg.Bind(wxEVT_DESTROY, [this](auto& e) {
                m_info_dialog_content = wxEmptyString;
            });
                    dlg.ShowModal();
        });
    }
    else {
#ifdef __WXMSW__
        if (app_config->get("associate_gcode") == "true")
            associate_files(L"gcode");
#endif // __WXMSW__
    }

    // Suppress the '- default -' presets.
    preset_bundle->set_default_suppressed(true);

    // No account agent: user presets always live in the default folder.
    BOOST_LOG_TRIVIAL(info) << "preset_folder: set to empty";
    app_config->set("preset_folder", "");
    preset_bundle->update_user_presets_directory(DEFAULT_USER_FOLDER_NAME);

    // BBS if load user preset failed
    //if (loaded_preset_result != 0) {
        try {
            // Enable all substitutions (in both user and system profiles), but log the substitutions in user profiles only.
            // If there are substitutions in system profiles, then a "reconfigure" event shall be triggered, which will force
            // installation of a compatible system preset, thus nullifying the system preset substitutions.
            init_params->preset_substitutions = preset_bundle->load_presets(*app_config, ForwardCompatibilitySubstitutionRule::EnableSystemSilent);
        }catch(ConfigurationError &ex)
        {
            int ret = wxMessageBox(_L("Detected corruption in the MentorSlicer configuration file, attempted to rebuild, \nplease manually restart the software."), _L("Error"), wxOK | wxICON_ERROR);
            if(ret == wxOK) {
                boost::filesystem::path dir = (boost::filesystem::path(data_dir()) / PRESET_SYSTEM_DIR).make_preferred();
                if (boost::filesystem::exists(dir)) {
                    boost::filesystem::remove_all(dir);
                }
                wxExit();
                return false;
            }
           
        }
        catch (const std::exception& ex) {
            show_error(nullptr, ex.what());
        }
    //}

#ifdef WIN32
#if !wxVERSION_EQUAL_OR_GREATER_THAN(3,1,3)
    register_win32_dpi_event();
#endif // !wxVERSION_EQUAL_OR_GREATER_THAN
    register_win32_device_notification_event();
#endif // WIN32
    
    // Let the libslic3r know the callback, which will translate messages on demand.
    Slic3r::I18N::set_translate_callback(libslic3r_translate_callback);

    BOOST_LOG_TRIVIAL(info) << "create the main window";
  
    mainframe = new MainFrame();
    //UITour::Instance();
    // hide settings tabs after first Layout
    if (this->init_params->input_files.empty()) {
        mainframe->select_tab(size_t(MainFrame::tpHome));
    }else{
        mainframe->select_tab(size_t(MainFrame::tp3DEditor));
    }

    sidebar().obj_list()->init();
    //sidebar().aux_list()->init_auxiliary();

//     update_mode(); // !!! do that later
    SetTopWindow(mainframe);

    plater_->init_notification_manager();

    m_printhost_job_queue.reset(new PrintHostJobQueue(mainframe->printhost_queue_dlg()));

    if (is_gcode_viewer()) {
        mainframe->update_layout();
        if (plater_ != nullptr)
            // ensure the selected technology is ptFFF
            plater_->set_printer_technology(ptFFF);
    }
    else
        load_current_presets();

    if (plater_ != nullptr) {
        plater_->reset_project_dirty_initial_presets();
        plater_->update_project_dirty_from_presets();
    }

    // BBS:
#ifdef __WINDOWS__
    mainframe->topbar()->SaveNormalRect();
#endif
    mainframe->Show(true);
#ifdef __WXOSX__
    // App Store review (Guideline 4): activate on launch so the App menu and
    // Dock icon appear; without this the window stays inactive on fresh installs.
    Slic3r::activate_app();
    mainframe->Raise();
#endif
#ifdef __WXMSW__
    // Show() invalidates the custom topbar, but startup continues synchronously
    // before the event loop can paint it. Render the entire child tree now so
    // DWM never presents the toolbar's unpainted background.
    ::RedrawWindow(mainframe->GetHandle(), nullptr, nullptr,
        RDW_INVALIDATE | RDW_ALLCHILDREN | RDW_UPDATENOW);
#endif
    BOOST_LOG_TRIVIAL(info) << "main frame firstly shown";

//#if BBL_HAS_FIRST_PAGE
    //BBS: set tp3DEditor firstly
    /*plater_->canvas3D()->enable_render(false);
    mainframe->select_tab(size_t(MainFrame::tp3DEditor));
    scrn->SetText(_L("Loading Opengl resourses..."));
    plater_->select_view_3D("3D");
    //BBS init the opengl resource here
    Size canvas_size = plater_->canvas3D()->get_canvas_size();
    wxGetApp().imgui()->set_display_size(static_cast<float>(canvas_size.get_width()), static_cast<float>(canvas_size.get_height()));
    wxGetApp().init_opengl();
    plater_->canvas3D()->init();
    wxGetApp().imgui()->new_frame();
    plater_->canvas3D()->enable_render(true);
    plater_->canvas3D()->render();
    if (is_editor())
        mainframe->select_tab(size_t(0));*/
//#else
    //plater_->trigger_restore_project(1);
//#endif

    obj_list()->set_min_height();

    int mode = wxGetApp().app_config->get("role_type") != "0";
    update_mode(mode); // update view mode after fix of the object_list size

#ifdef __APPLE__
   other_instance_message_handler()->bring_instance_forward();
#endif //__APPLE__


    Bind(wxEVT_IDLE, [this](wxIdleEvent& event)
    {
        if (! plater_)
            return;

        // BBS
        //this->obj_manipul()->update_if_dirty();

        //use m_post_initialized instead
        //static bool update_gui_after_init = true;

        // An ugly solution to GH #5537 in which GUI_App::init_opengl (normally called from events wxEVT_PAINT
        // and wxEVT_SET_FOCUS before GUI_App::post_init is called) wasn't called before GUI_App::post_init and OpenGL wasn't initialized.
//#ifdef __linux__
//        if (!m_post_initialized && m_opengl_initialized) {
//#else
        if (!m_post_initialized && !m_adding_script_handler) {
//#endif
            m_post_initialized = true;
#ifdef WIN32
            this->mainframe->register_win32_callbacks();
#endif
            this->post_init();

            m_app_end_time = std::chrono::steady_clock::now();

            
        }
        
        if (m_post_initialized && app_config->dirty()) {
            save_user_default_filaments(app_config);
            app_config->save();
        }

    });

    m_initialized = true;

    flush_logs();

    BOOST_LOG_TRIVIAL(info) << "finished the gui app init";
    
    if (m_config_corrupted) {
        m_config_corrupted = false;
        show_error(nullptr,
                   _u8L(
                       "The MentorSlicer configuration file may be corrupted and cannot be parsed.\nMentorSlicer has attempted to recreate the "
                       "configuration file.\nPlease note, application settings will be lost, but printer profiles will not be affected."));
    }

    return true;
}
void  GUI_App::on_init_custom_config()
{
    auto config_file = fs::path(resources_dir() + "/data/customized_config.json");
    json j;
    if (fs::exists(config_file))
    {
        try{
            boost::nowide::ifstream ifs(config_file.string());
            ifs >> j;
        }catch (const std::exception& e) {
            BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << boost::format(",  parse json failed, %1%.") % e.what();
        }
    }else{
        return;
    }
    m_customize_config.cur_language = j["language"];
    std::string language = app_config->get("language");
    if (language.empty() && !m_customize_config.cur_language.empty())
    {
        app_config->set("language", m_customize_config.cur_language);
    }
}
unsigned GUI_App::get_colour_approx_luma(const wxColour &colour)
{
    double r = colour.Red();
    double g = colour.Green();
    double b = colour.Blue();

    return std::round(std::sqrt(
        r * r * .241 +
        g * g * .691 +
        b * b * .068
        ));
}

bool GUI_App::dark_mode()
{
#ifdef SUPPORT_DARK_MODE
    #if __APPLE__
        // The check for dark mode returns false positive on 10.12 and 10.13,
        // which allowed setting dark menu bar and dock area, which is
        // is detected as dark mode. We must run on at least 10.14 where the
        // proper dark mode was first introduced.
        return wxPlatformInfo::Get().CheckOSVersion(10, 14) && mac_dark_mode();
    #elif __linux__
        // return false;
        if(isRunningInFlatpak())
        {
           const unsigned luma = get_colour_approx_luma(wxSystemSettings::GetColour(wxSYS_COLOUR_ACTIVECAPTION));
           return luma < 128;
        }else{
            return wxSystemSettings::GetAppearance().IsDark();
        }
        
        // return wxGetApp().app_config->get("dark_color_mode") == "1" ? true : false;
    #else 
        return wxGetApp().app_config->get("dark_color_mode") == "1" ? true : check_dark_mode();
        //const unsigned luma = get_colour_approx_luma(wxSystemSettings::GetColour(wxSYS_COLOUR_WINDOW));
        //return luma < 128;
    #endif
#else
    //BBS disable DarkUI mode
    return false;
#endif
}

void GUI_App::init_label_colours()
{
    bool is_dark_mode = dark_mode();
    m_color_label_modified = is_dark_mode ? wxColour("#EBC406") : wxColour("#EBC406");
    m_color_label_sys      = is_dark_mode ? wxColour("#B2B3B5") : wxColour("#363636");

#if defined(_WIN32) || defined(__linux__) || defined(__APPLE__)
    m_color_label_default           = is_dark_mode ? wxColour(250, 250, 250) : m_color_label_sys; // wxSystemSettings::GetColour(wxSYS_COLOUR_WINDOWTEXT);
    m_color_highlight_label_default = is_dark_mode ? wxColour(230, 230, 230): wxSystemSettings::GetColour(/*wxSYS_COLOUR_HIGHLIGHTTEXT*/wxSYS_COLOUR_WINDOWTEXT);
    m_color_highlight_default       = is_dark_mode ? wxColour(78, 78, 78)   : wxSystemSettings::GetColour(wxSYS_COLOUR_3DLIGHT);
    m_color_hovered_btn_label       = is_dark_mode ? wxColour(255, 255, 254) : wxColour(0,0,0);
    m_color_default_btn_label       = is_dark_mode ? wxColour(255, 255, 254): wxColour(0,0,0);
    m_color_selected_btn_bg         = is_dark_mode ? wxColour(84, 84, 91)   : wxColour(206, 206, 206);
#else
    m_color_label_default = wxSystemSettings::GetColour(wxSYS_COLOUR_WINDOWTEXT);
#endif
    m_color_window_default          = is_dark_mode ? wxColour(43, 43, 43)   : wxSystemSettings::GetColour(wxSYS_COLOUR_WINDOW);
    StateColor::SetDarkMode(is_dark_mode);
}

void GUI_App::update_label_colours_from_appconfig()
{
    if (app_config->has("label_clr_sys")) {
        auto str = app_config->get("label_clr_sys");
        if (str != "")
            m_color_label_sys = wxColour(str);
    }

    if (app_config->has("label_clr_modified")) {
        auto str = app_config->get("label_clr_modified");
        if (str != "")
            m_color_label_modified = wxColour(str);
    }
}

void GUI_App::update_label_colours()
{
    for (Tab* tab : tabs_list)
        tab->update_label_colours();
}

#ifdef _WIN32
static bool is_focused(HWND hWnd)
{
    HWND hFocusedWnd = ::GetFocus();
    return hFocusedWnd && hWnd == hFocusedWnd;
}

static bool is_default(wxWindow* win)
{
    wxTopLevelWindow* tlw = find_toplevel_parent(win);
    if (!tlw)
        return false;

    return win == tlw->GetDefaultItem();
}
#endif

void GUI_App::UpdateDarkUI(wxWindow* window, bool highlited/* = false*/, bool just_font/* = false*/)
{
    if (m_is_dark_mode != dark_mode() )
        m_is_dark_mode = dark_mode();   

    if (wxButton *btn = dynamic_cast<wxButton*>(window)) {
        if (btn->GetWindowStyleFlag() & wxBU_AUTODRAW)
            return;
        else {
#ifdef _WIN32
            if (btn->GetId() == wxID_OK || btn->GetId() == wxID_CANCEL) {
                bool is_focused_button = false;
                bool is_default_button = false;

                if (!(btn->GetWindowStyle() & wxNO_BORDER)) {
                    btn->SetWindowStyle(btn->GetWindowStyle() | wxNO_BORDER);
                    highlited = true;
                }

                auto mark_button = [this, btn, highlited](const bool mark) {
                    btn->SetBackgroundColour(mark ? m_color_selected_btn_bg : highlited ? m_color_highlight_default : m_color_window_default);
                    btn->SetForegroundColour(mark ? m_color_hovered_btn_label :m_color_default_btn_label);
                    btn->Refresh();
                    btn->Update();
                };

                // hovering
                btn->Bind(wxEVT_ENTER_WINDOW, [mark_button](wxMouseEvent& event) { mark_button(true); event.Skip(); });
                btn->Bind(wxEVT_LEAVE_WINDOW, [mark_button, btn](wxMouseEvent& event) { mark_button(is_focused(btn->GetHWND())); event.Skip(); });
                // focusing
                btn->Bind(wxEVT_SET_FOCUS, [mark_button](wxFocusEvent& event) { mark_button(true); event.Skip(); });
                btn->Bind(wxEVT_KILL_FOCUS, [mark_button](wxFocusEvent& event) { mark_button(false); event.Skip(); });

                is_focused_button = is_focused(btn->GetHWND());
                is_default_button = is_default(btn);
                mark_button(is_focused_button);
            }
#endif
        }
    }

    if (Button* btn = dynamic_cast<Button*>(window)) {
        if (btn->GetWindowStyleFlag() & wxBU_AUTODRAW)
            return;
    }

    if (m_is_dark_mode) {

        auto orig_col = window->GetBackgroundColour();
        auto bg_col = StateColor::darkModeColorFor(orig_col);
        // there are cases where the background color of an item is bright, specifically:
        // * the background color of a button: #009688  -- 73
        if (bg_col != orig_col) {
            window->SetBackgroundColour(bg_col);
        }

        orig_col = window->GetForegroundColour();
        auto fg_col = StateColor::darkModeColorFor(orig_col);
        auto fg_l = StateColor::GetLightness(fg_col);

        auto color_difference = StateColor::GetColorDifference(bg_col, fg_col);

        // fallback and sanity check with LAB
        // color difference of less than 2 or 3 is not normally visible, and even less than 30-40 doesn't stand out
        if (color_difference < 10) {
            fg_col = StateColor::SetLightness(fg_col, 90);
        }
        // some of the stock colors have a lightness of ~49
        if (fg_l < 45) {
            fg_col = StateColor::SetLightness(fg_col, 70);
        }
        // at this point it shouldn't be possible that fg_col is the same as bg_col, but let's be safe
        if (fg_col == bg_col) {
            fg_col = StateColor::SetLightness(fg_col, 70);
        }

        window->SetForegroundColour(fg_col);
    }
    else {
        auto original_col = window->GetBackgroundColour();
        auto bg_col = StateColor::lightModeColorFor(original_col);

        if (bg_col != original_col) {
            window->SetBackgroundColour(bg_col);
        }

        original_col = window->GetForegroundColour();
        auto fg_col = StateColor::lightModeColorFor(original_col);

        if (fg_col != original_col) {
            window->SetForegroundColour(fg_col);
        }
    }
}


// recursive function for scaling fonts for all controls in Window
static void update_dark_children_ui(wxWindow* window, bool just_buttons_update = false)
{
    /*bool is_btn = dynamic_cast<wxButton*>(window) != nullptr;
    is_btn = false;*/
    if (!window) return;

    wxGetApp().UpdateDarkUI(window);

    auto children = window->GetChildren();
    for (auto child : children) {
        update_dark_children_ui(child);
    }
}

// Note: Don't use this function for Dialog contains ScalableButtons
void GUI_App::UpdateDarkUIWin(wxWindow* win)
{
    update_dark_children_ui(win);
}

void GUI_App::Update_dark_mode_flag()
{
    m_is_dark_mode = dark_mode();
    BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << boost::format(": switch the current dark mode status to %1% ")%m_is_dark_mode;
}

void GUI_App::UpdateDlgDarkUI(wxDialog* dlg)
{
#ifdef __WINDOWS__
    NppDarkMode::SetDarkExplorerTheme(dlg->GetHWND());
    NppDarkMode::SetDarkTitleBar(dlg->GetHWND());
#endif
    update_dark_children_ui(dlg);
}

void GUI_App::UpdateFrameDarkUI(wxFrame* dlg)
{
#ifdef __WINDOWS__
    NppDarkMode::SetDarkExplorerTheme(dlg->GetHWND());
    NppDarkMode::SetDarkTitleBar(dlg->GetHWND());
#endif
    update_dark_children_ui(dlg);
}

void GUI_App::UpdateDVCDarkUI(wxDataViewCtrl* dvc, bool highlited/* = false*/)
{
#ifdef __WINDOWS__
    if (!dvc)
        return;

    UpdateDarkUI(dvc, highlited ? dark_mode() : false);
#ifdef _MSW_DARK_MODE
    HWND hwnd;
    if (!dvc->HasFlag(wxDV_NO_HEADER)) {
        auto header = dvc->GenericGetHeader();
        if (header) {
            hwnd = (HWND)header->GetHandle();
            if (hwnd != NULL) {
                hwnd = GetWindow(hwnd, GW_CHILD);
                if (hwnd != NULL)
                    NppDarkMode::SetDarkListViewHeader(hwnd);
            }

            wxItemAttr attr;
            attr.SetFont(::Label::Body_13);
            dvc->SetHeaderAttr(attr);
        }
    }
#endif //_MSW_DARK_MODE
   /* if (dvc->HasFlag(wxDV_ROW_LINES))
        dvc->SetAlternateRowColour(m_color_highlight_default);*/
    if (dvc->GetBorder() != wxBORDER_SIMPLE)
        dvc->SetWindowStyle(dvc->GetWindowStyle() | wxBORDER_SIMPLE);
#endif
}

void GUI_App::UpdateAllStaticTextDarkUI(wxWindow* parent)
{
#ifdef __WINDOWS__
    wxGetApp().UpdateDarkUI(parent);

    auto children = parent->GetChildren();
    for (auto child : children) {
        if (dynamic_cast<wxStaticText*>(child))
            child->SetForegroundColour(m_color_label_default);
    }
#endif
}

void GUI_App::init_fonts()
{
    // BBS: modify font
    m_small_font = Label::Body_10;
    m_bold_font = Label::Body_10.Bold();
    m_normal_font = Label::Body_10;

#ifdef __WXMAC__
    m_small_font.SetPointSize(11);
    m_bold_font.SetPointSize(13);
#endif /*__WXMAC__*/

    // wxSYS_OEM_FIXED_FONT and wxSYS_ANSI_FIXED_FONT use the same as
    // DEFAULT in wxGtk. Use the TELETYPE family as a work-around
    m_code_font = wxFont(wxFontInfo().Family(wxFONTFAMILY_TELETYPE));
    m_code_font.SetPointSize(m_small_font.GetPointSize());
}

void GUI_App::update_fonts(const MainFrame *main_frame)
{
    /* Only normal and bold fonts are used for an application rescale,
     * because of under MSW small and normal fonts are the same.
     * To avoid same rescaling twice, just fill this values
     * from rescaled MainFrame
     */
	if (main_frame == nullptr)
		main_frame = this->mainframe;
    m_normal_font   = Label::Body_14; // BBS: larger font size
    m_small_font    = m_normal_font;
    m_bold_font     = m_normal_font.Bold();
    m_link_font     = m_bold_font.Underlined();
    m_em_unit       = main_frame->em_unit();
    m_code_font.SetPointSize(m_small_font.GetPointSize());
}

bool GUI_App::get_side_menu_popup_status()
{
    return m_side_popup_status;
}

void GUI_App::set_side_menu_popup_status(bool status)
{
    m_side_popup_status = status;
}

void GUI_App::webGetDevicesInfo(json& result)
{
    printerPresetConfig->LoadProfile();
    result = printerPresetConfig->getProfileJson();

    //struct PrinterInfo
    //{
    //    std::string name;
    //    std::string seriesNameList;
    //};

    //boost::filesystem::path vendor_dir = (boost::filesystem::path(Slic3r::data_dir()) / PRESET_SYSTEM_DIR).make_preferred();
    //boost::filesystem::path rsrc_vendor_dir = (boost::filesystem::path(resources_dir()) / "profiles").make_preferred();

    //json machineJson                   = json::parse("{}");
    //machineJson["machine"]              = json::array();
    //boost::filesystem::path machinepath = vendor_dir;
    //if (!boost::filesystem::exists((vendor_dir / "Creality" / "machineList").replace_extension(".json"))) {
    //    machinepath = rsrc_vendor_dir;
    //}

    //std::string strVendor = "machineList";
    //std::string             strFilePath = (machinepath / "Creality" / "machineList.json").string();
    //boost::filesystem::path file_path(strFilePath);
    //boost::filesystem::path vendor_dir1 = boost::filesystem::absolute(file_path.parent_path() / strVendor).make_preferred();
    //BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << boost::format(",  vendor path %1%.") % vendor_dir1.string();
    //try {
    //    std::string contents;
    //    boost::nowide::ifstream t(strFilePath);
    //    std::stringstream       buffer;
    //    buffer << t.rdbuf();
    //    contents = buffer.str();
    //    BOOST_LOG_TRIVIAL(trace) << __FUNCTION__ << boost::format(", load %1% into buffer") % strFilePath;

    //    json jLocal = json::parse(contents);
    //    json pmodels = jLocal["printerList"];
    //    json series  = jLocal["series"];

    //    std::vector<PrinterInfo> printers;

    //    for (const auto& item : series) {
    //        int         id   = item["id"];
    //        std::string name = item["name"];

    //        if (name.empty())
    //            continue;

    //        PrinterInfo printerInfo;
    //        printerInfo.name = name;

    //        std::map<std::string, std::string> mapMachineThumbnail;

    //        for (const auto& printer : pmodels) {
    //            int seriesId = printer["seriesId"];
    //            if (seriesId == id) {
    //                std::string str1 = printer["name"];
    //                if (str1.find("Creality") == std::string::npos) {
    //                    str1 = "Creality " + str1;
    //                }
    //                std::string str2 = printer["printerIntName"];
    //                printerInfo.seriesNameList += (str1 + ";" + str2 + ";");
    //                std::string printerName = printer["name"];
    //                if (printerName.find("Creality") == std::string::npos) {
    //                    printerName = "Creality " + printerName;
    //                }
    //                mapMachineThumbnail[printerName] = printer["thumbnail"];
    //            }
    //        }

    //        printers.push_back(printerInfo);
    //    }

    //    auto toLowerAndContains = [](const std::string& str, const std::string& key) {
    //        std::string lowerStr = str;
    //        std::string lowerKey = key;

    //        std::transform(lowerStr.begin(), lowerStr.end(), lowerStr.begin(), ::tolower);
    //        std::transform(lowerKey.begin(), lowerKey.end(), lowerKey.begin(), ::tolower);

    //        return lowerStr.find(lowerKey) != std::string::npos;
    //    };

    //    std::sort(printers.begin(), printers.end(), [toLowerAndContains](const PrinterInfo& pi, const PrinterInfo& pi1) {
    //            static const std::vector<std::string> order = {"flagship", "ender", "cr", "halot"};

    //            auto getPriority = [toLowerAndContains](const std::string& name) {
    //                for (size_t i = 0; i < order.size(); ++i) {
    //                    if (toLowerAndContains(name, order[i])) {
    //                        return i;
    //                    }
    //                }
    //                return order.size(); // Lowest priority if no keyword matches
    //            };

    //            return getPriority(pi.name) < getPriority(pi1.name);
    //    });

    //    for (const auto& info : printers) {
    //        json childList = json::object();

    //        std::string name  = info.name;
    //        wxString    sName = _L(name);
    //        childList["name"] = sName.utf8_str();

    //        childList["printers"] = info.seriesNameList;
    //        machineJson["machine"].push_back(childList);
    //    }

    //    // wxString strJS = wxString::Format("handleStudioCmd(%s)", m_MachineJson.dump(-1, ' ', true));
    //    result = machineJson;
    //} catch (nlohmann::detail::parse_error& err) {
    //    BOOST_LOG_TRIVIAL(error) << __FUNCTION__ << ": parse " << strFilePath
    //                             << " got a nlohmann::detail::parse_error, reason = " << err.what();
    //    return;
    //} catch (std::exception& e) {
    //    BOOST_LOG_TRIVIAL(error) << __FUNCTION__ << ": parse " << strFilePath << " got exception: " << e.what();
    //    return;
    //}

    //return;
}

bool GUI_App::tabs_as_menu() const
{
    return false;
}

wxSize GUI_App::get_min_size() const
{
    return wxSize(120*m_em_unit, 49 * m_em_unit);
}

wxSize GUI_App::get_min_size_ex(wxWindow* display_win) const
{
    // be careful when setting "105 * m_em_unit", in some screen size when toggle the maximize button, could possibly hide the maximize and close button
    wxSize min_size(106 * m_em_unit, 60 * m_em_unit);

    //const wxDisplay display = wxDisplay(display_win);
    //wxRect display_rect = display.GetGeometry();  // for example: 1920 * 1080
    //display_rect.width *= 0.75;
    //display_rect.height *= 0.75;

    //if (min_size.x > display_rect.GetWidth())
    //    min_size.x = display_rect.GetWidth();
    //if (min_size.y > display_rect.GetHeight())
    //    min_size.y = display_rect.GetHeight();

    return min_size;
}

float GUI_App::toolbar_icon_scale(const bool is_limited/* = false*/) const
{
#ifdef __APPLE__
    const float icon_sc = 1.0f; // for Retina display will be used its own scale
#else
    const float icon_sc = m_em_unit * 0.1f;
#endif // __APPLE__

    // fixed icon size
    return icon_sc;

    const std::string& auto_val = app_config->get("toolkit_size");

    if (auto_val.empty())
        return icon_sc;

    int int_val =  100;
    // correct value in respect to toolkit_size
    int_val = std::min(atoi(auto_val.c_str()), int_val);

    if (is_limited && int_val < 50)
        int_val = 50;

    return 0.01f * int_val * icon_sc;
}

void GUI_App::set_auto_toolbar_icon_scale(float scale) const
{
#ifdef __APPLE__
    const float icon_sc = 1.0f; // for Retina display will be used its own scale
#else
    const float icon_sc = m_em_unit * 0.1f;
#endif // __APPLE__

    long int_val = std::min(int(std::lround(scale / icon_sc * 100)), 100);
    std::string val = std::to_string(int_val);

    app_config->set("toolkit_size", val);
}

// check user printer_presets for the containing information about "Print Host upload"
void GUI_App::check_printer_presets()
{
//BBS
#if 0
    std::vector<std::string> preset_names = PhysicalPrinter::presets_with_print_host_information(preset_bundle->printers);
    if (preset_names.empty())
        return;

    // BBS: remove "print host upload" message dialog
    preset_bundle->physical_printers.load_printers_from_presets(preset_bundle->printers);
#endif
}

void switch_window_pools();
void release_window_pools();

void GUI_App::recreate_GUI(const wxString &msg_name)
{
    BOOST_LOG_TRIVIAL(warning) << __FUNCTION__ << " start";
    m_is_recreating_gui = true;

    mainframe->shutdown();
    ProgressDialog dlg(msg_name, msg_name, 100, nullptr, wxPD_AUTO_HIDE);
    dlg.Pulse();
    dlg.Update(10, _L("Rebuild") + dots);

    MainFrame *old_main_frame = mainframe;
    struct ClientData : wxClientData
    {
        ~ClientData() { release_window_pools(); }
    };
    old_main_frame->SetClientObject(new ClientData);

    switch_window_pools();
    old_main_frame->destroy_webviews_for_recreate();
    mainframe = new MainFrame();
    if (is_editor())
        // hide settings tabs after first Layout
        mainframe->select_tab(size_t(MainFrame::tp3DEditor));
    // Propagate model objects to object list.
    sidebar().obj_list()->init();
    //sidebar().aux_list()->init_auxiliary();
    SetTopWindow(mainframe);

    dlg.Update(30, _L("Rebuild") + dots);
    old_main_frame->Destroy();

    dlg.Update(80, _L("Loading current presets") + dots);
    m_printhost_job_queue.reset(new PrintHostJobQueue(mainframe->printhost_queue_dlg()));
    load_current_presets();
    mainframe->Show(true);
#ifdef __WXOSX__
    // App Store review (Guideline 4): activate on launch so the App menu and
    // Dock icon appear; without this the window stays inactive on fresh installs.
    Slic3r::activate_app();
    mainframe->Raise();
#endif
#ifdef __WXMSW__
    // Show() invalidates the custom topbar, but startup continues synchronously
    // before the event loop can paint it. Render the entire child tree now so
    // DWM never presents the toolbar's unpainted background.
    ::RedrawWindow(mainframe->GetHandle(), nullptr, nullptr,
        RDW_INVALIDATE | RDW_ALLCHILDREN | RDW_UPDATENOW);
#endif

    dlg.Update(90, _L("Loading a mode view") + dots);

    obj_list()->set_min_height();
    update_mode();

    //BBS: trigger restore project logic here, and skip confirm
    plater_->trigger_restore_project(1);

    // #ys_FIXME_delete_after_testing  Do we still need this  ?
//     CallAfter([]() {
//         // Run the config wizard, don't offer the "reset user profile" checkbox.
//         config_wizard_startup(true);
//     });


    m_is_recreating_gui = false;

    BOOST_LOG_TRIVIAL(warning) << __FUNCTION__ << " end";
    boost::log::core::get()->flush();
}

void GUI_App::keyboard_shortcuts()
{
    KBShortcutsDialog dlg;
    dlg.ShowModal();
}




// static method accepting a wxWindow object as first parameter
void fatal_error(wxWindow* parent)
{
    show_error(parent, "");
    //     exit 1; // #ys_FIXME
}

#ifdef __WINDOWS__
#ifdef _MSW_DARK_MODE
static void update_scrolls(wxWindow* window)
{
    wxWindowList::compatibility_iterator node = window->GetChildren().GetFirst();
    while (node)
    {
        wxWindow* win = node->GetData();
        if (dynamic_cast<wxScrollHelper*>(win) ||
            dynamic_cast<wxTreeCtrl*>(win) ||
            dynamic_cast<wxTextCtrl*>(win))
            NppDarkMode::SetDarkExplorerTheme(win->GetHWND());

        update_scrolls(win);
        node = node->GetNext();
    }
}
#endif //_MSW_DARK_MODE
#endif //__WINDOWS__

void GUI_App::force_colors_update()
{
#ifdef _MSW_DARK_MODE
#ifdef __WINDOWS__
    NppDarkMode::SetDarkMode(dark_mode());
#if !wxCHECK_VERSION(3, 3, 0)
    if (WXHWND wxHWND = wxToolTip::GetToolTipCtrl())
        NppDarkMode::SetDarkExplorerTheme((HWND)wxHWND);
#endif
    NppDarkMode::SetDarkTitleBar(mainframe->GetHWND());


    //NppDarkMode::SetDarkExplorerTheme((HWND)mainframe->m_settings_dialog.GetHWND());
    //NppDarkMode::SetDarkTitleBar(mainframe->m_settings_dialog.GetHWND());

#endif // __WINDOWS__
#endif //_MSW_DARK_MODE
    m_force_colors_update = true;
}

// Called after the Preferences dialog is closed and the program settings are saved.
// Update the UI based on the current preferences.
void GUI_App::update_ui_from_settings()
{
    update_label_colours();
    // Upadte UI colors before Update UI from settings
    if (m_force_colors_update) {
        m_force_colors_update = false;
        //UpdateDlgDarkUI(&mainframe->m_settings_dialog);
        //mainframe->m_settings_dialog.Refresh();
        //mainframe->m_settings_dialog.Update();

        if (mainframe) {
#ifdef __WINDOWS__
            mainframe->force_color_changed();
            update_scrolls(mainframe);
            update_scrolls(&mainframe->m_settings_dialog);
#endif //_MSW_DARK_MODE
            update_dark_children_ui(mainframe);
        }
    }

    if (mainframe) {mainframe->update_ui_from_settings();}
}

void GUI_App::persist_window_geometry(wxTopLevelWindow *window, bool default_maximized)
{
    const std::string name = into_u8(window->GetName());

    window->Bind(wxEVT_CLOSE_WINDOW, [=](wxCloseEvent &event) {
        BOOST_LOG_TRIVIAL(info) << __FUNCTION__<< ": received wxEVT_CLOSE_WINDOW, trigger save for window_mainframe";
        window_pos_save(window, "mainframe");
        event.Skip();
    });

    if (window_pos_restore(window, "mainframe", default_maximized)) {
        on_window_geometry(window, [=]() {
            window_pos_sanitize(window);
        });
    } else {
        on_window_geometry(window, [=]() {
            window_pos_center(window);
        });
    }
}

void GUI_App::load_project(wxWindow *parent, wxString& input_file) const
{
    input_file.Clear();
    wxFileDialog dialog(parent ? parent : GetTopWindow(),
        _L("Choose one file (3mf):"),
        app_config->get_last_dir(), "",
        file_wildcards(FT_PROJECT), wxFD_OPEN | wxFD_FILE_MUST_EXIST);

    if (dialog.ShowModal() == wxID_OK) {
        input_file = dialog.GetPath();
    }
}

void GUI_App::import_model(wxWindow *parent, wxArrayString& input_files, bool Category_or_not) const
{
    wxString wildcard;
    wildcard = file_wildcards(FT_MODEL);
    if (Category_or_not)
    {
        wildcard = GUI::format_wxstr("%s|%s|%s", file_wildcards(FT_MODEL), file_wildcards(FT_MESH_FILE),file_wildcards(FT_CAD_FILE));
    }

    input_files.Clear();
    wxFileDialog dialog(parent ? parent : GetTopWindow(),
#ifdef __APPLE__
        _L("Choose one or more files (3mf/step/stl/svg/obj/amf/usd*/abc/ply):"),
#else
        _L("Choose one or more files (3mf/step/stl/svg/obj/amf):"),
#endif
        from_u8(app_config->get_last_dir()), "",
        wildcard, wxFD_OPEN | wxFD_MULTIPLE | wxFD_FILE_MUST_EXIST);

    if (dialog.ShowModal() == wxID_OK)
        dialog.GetPaths(input_files);
}

void GUI_App::import_zip(wxWindow* parent, wxString& input_file) const
{
    wxFileDialog dialog(parent ? parent : GetTopWindow(),
                        _L("Choose ZIP file") + ":",
                        from_u8(app_config->get_last_dir()), "",
                        file_wildcards(FT_ZIP), wxFD_OPEN | wxFD_FILE_MUST_EXIST);

    if (dialog.ShowModal() == wxID_OK)
        input_file = dialog.GetPath();
}

void GUI_App::load_gcode(wxWindow* parent, wxString& input_file) const
{
    input_file.Clear();
    wxFileDialog dialog(parent ? parent : GetTopWindow(),
        _L("Choose one file (gcode/3mf):"),
        app_config->get_last_dir(), "",
        file_wildcards(FT_GCODE), wxFD_OPEN | wxFD_FILE_MUST_EXIST);

    if (dialog.ShowModal() == wxID_OK)
        input_file = dialog.GetPath();
}

struct DeviceAddPreparation
{
    json selected_printers;
    json profile_json;
    bool valid{false};
};

static std::vector<std::string> split_device_add_values(const std::string& values)
{
    std::vector<std::string> result;
    boost::split(result, values, boost::is_any_of(";"), boost::token_compress_on);
    result.erase(std::remove_if(result.begin(), result.end(), [](const std::string& value) { return value.empty(); }), result.end());
    return result;
}

static void apply_device_add_config_snapshot(json& profile_json,
                                             const AppConfig::VendorMap& enabled_vendors,
                                             const std::map<std::string, std::string>& enabled_filaments)
{
    for (auto& model : profile_json["model"]) {
        if (!model.is_object())
            continue;

        const std::string model_name  = model.value("model", std::string());
        const std::string vendor_name = model.value("vendor", std::string());
        std::string selected;
        auto vendor_it = enabled_vendors.find(vendor_name);
        if (vendor_it != enabled_vendors.end()) {
            auto model_it = vendor_it->second.find(model_name);
            if (model_it != vendor_it->second.end()) {
                for (const std::string& nozzle : split_device_add_values(model.value("nozzle_diameter", std::string()))) {
                    if (model_it->second.find(nozzle) == model_it->second.end())
                        continue;
                    if (!selected.empty())
                        selected += ";";
                    selected += nozzle;
                }
            }
        }
        model["nozzle_selected"] = selected;
    }

    if (profile_json["model"].size() == 1)
        profile_json["model"][0]["nozzle_selected"] = profile_json["model"][0]["nozzle_diameter"];

    for (auto it = profile_json["filament"].begin(); it != profile_json["filament"].end(); ++it) {
        if (enabled_filaments.find(it.key()) != enabled_filaments.end())
            it.value()["selected"] = 1;
    }
}

static DeviceAddPreparation prepare_device_add(json selected_printers,
                                               json profile_json,
                                               const AppConfig::VendorMap& enabled_vendors,
                                               const std::map<std::string, std::string>& enabled_filaments)
{
    DeviceAddPreparation result;
    apply_device_add_config_snapshot(profile_json, enabled_vendors, enabled_filaments);

    auto load_catalog = [](const fs::path& path) {
        json catalog;
        try {
            if (fs::exists(path)) {
                boost::nowide::ifstream input(path.string());
                input >> catalog;
            }
        } catch (const std::exception& e) {
            BOOST_LOG_TRIVIAL(warning) << "Cannot read printer catalog " << path.string() << ": " << e.what();
        }
        return catalog;
    };
    // Fork: the data-dir catalogue is no longer downloaded; only the bundled one is used.
    const json system_catalog;
    const json bundled_catalog = load_catalog(fs::path(resources_dir()) / "profiles" / "Creality" / "machineList.json");
    DeviceAddUtils::resolve_printer_selections(selected_printers, profile_json["model"], system_catalog, bundled_catalog);

    struct SelectedNozzle
    {
        size_t index;
        std::string model;
        std::vector<std::string> diameters;
        std::vector<bool> selected;
    };

    std::vector<SelectedNozzle> selected_nozzles;
    for (size_t index = 0; index < profile_json["model"].size(); ++index) {
        json& model = profile_json["model"][index];
        model["nozzle_selected"] = "";
        for (auto it = selected_printers.begin(); it != selected_printers.end(); ++it) {
            const json& selected = it.value();
            if (!selected.is_object() || model.value("model", std::string()) != selected.value("model", std::string()) ||
                model.value("vendor", std::string()) != selected.value("vendor", std::string()))
                continue;

            const std::string selected_value = selected.contains("nozzle_diameter") ?
                                                   DeviceAddUtils::normalize_nozzles(selected["nozzle_diameter"]) :
                                                   std::string();
            model["nozzle_selected"] = selected_value;
            SelectedNozzle nozzle;
            nozzle.index     = index;
            nozzle.model     = model.value("model", std::string());
            nozzle.diameters = split_device_add_values(model.value("nozzle_diameter", std::string()));
            const auto selected_values = split_device_add_values(selected_value);
            nozzle.selected.reserve(nozzle.diameters.size());
            for (const std::string& diameter : nozzle.diameters) {
                nozzle.selected.push_back(std::find(selected_values.begin(), selected_values.end(), diameter) != selected_values.end());
            }
            selected_nozzles.emplace_back(std::move(nozzle));
            break;
        }
    }

    if (selected_nozzles.empty())
        return result;

    std::set<std::string> selected_filaments;
    for (const SelectedNozzle& nozzle : selected_nozzles) {
        json& model = profile_json["model"][nozzle.index];
        if (!model["materials"].is_string())
            continue;

        for (const std::string& material : split_device_add_values(model["materials"].get<std::string>())) {
            bool found = false;
            if (profile_json["filament"].contains(material)) {
                for (size_t i = 0; i < nozzle.diameters.size(); ++i) {
                    if (!nozzle.selected[i])
                        continue;
                    const std::string compatible_model = nozzle.model + "++" + nozzle.diameters[i];
                    if (profile_json["filament"][material].value("models", std::string()).find(compatible_model) != std::string::npos) {
                        selected_filaments.emplace(material);
                        found = true;
                    }
                }
            }
            if (found || material.find('@') != std::string::npos)
                continue;
            for (size_t i = 0; i < nozzle.diameters.size(); ++i) {
                if (!nozzle.selected[i])
                    continue;
                std::string printer_material = material + " @" + nozzle.model;
                if (profile_json["filament"].contains(printer_material)) {
                    selected_filaments.emplace(printer_material);
                } else {
                    printer_material += " " + nozzle.diameters[i] + " nozzle";
                    if (profile_json["filament"].contains(printer_material))
                        selected_filaments.emplace(printer_material);
                }
            }
        }
    }

    for (const std::string& material : selected_filaments) {
        if (profile_json["filament"].contains(material))
            profile_json["filament"][material]["selected"] = 1;
    }

    result.selected_printers = std::move(selected_printers);
    result.profile_json      = std::move(profile_json);
    result.valid             = true;
    return result;
}

std::string GUI_App::handle_web_request(std::string cmd)
{

    try {
        //BBS use nlohmann json format
        std::stringstream ss(cmd), oss;
        pt::ptree root, response;
        pt::read_json(ss, root);
        if (root.empty())
            return "";

        boost::optional<std::string> sequence_id = root.get_optional<std::string>("sequence_id");
        boost::optional<std::string> command = root.get_optional<std::string>("command");

        wxString strInput  = cmd;

        if (command.has_value()) {
            std::string command_str = command.value();
            if (command_str.compare("homepage_newproject") == 0) {
                this->request_open_project("<new>");
            }
            else if (command_str.compare("homepage_openproject") == 0) {
                this->request_open_project({});
            }
            else if (command_str.compare("get_recent_projects") == 0) {
                if (mainframe) {
                    if (mainframe->m_webview) {
                        mainframe->m_webview->SendRecentList(INT_MAX);
                    }
                }
            }
            else if (command_str.compare("homepage_open_recentfile") == 0) {
                if (root.get_child_optional("data") != boost::none) {
                    pt::ptree data_node = root.get_child("data");
                    boost::optional<std::string> path = data_node.get_optional<std::string>("path");
                    if (path.has_value()) {
                        this->request_open_project(path.value());
                    }
                }
            }
            else if (command_str.compare("homepage_delete_recentfile") == 0) {
                if (root.get_child_optional("data") != boost::none) {
                    pt::ptree                    data_node = root.get_child("data");
                    boost::optional<std::string> path      = data_node.get_optional<std::string>("path");
                    if (path.has_value()) {
                        this->request_remove_project(path.value());
                    }
                }
            }
            else if (command_str.compare("homepage_delete_all_recentfile") == 0) {
                this->request_remove_project("");
            }
            else if (command_str.compare("homepage_explore_recentfile") == 0) {
                if (root.get_child_optional("data") != boost::none) {
                    pt::ptree                    data_node = root.get_child("data");
                    boost::optional<std::string> path      = data_node.get_optional<std::string>("path");
                    if (path.has_value())
                    {
                        boost::filesystem::path NowFile(path.value());

                        std::string FilePath = NowFile.make_preferred().string();
                        desktop_open_any_folder(FilePath);
                    }
                }
            }
            else if(command_str.compare("get_client_id") == 0){
                    json tmp_res = json::object();
                    tmp_res["command"] = "get_client_id";
                    tmp_res["client_id"] = wxGetApp().get_client_id();

                    wxString tmp_str = wxString::Format("window.handleStudioCmd(%s)", tmp_res.dump(-1, ' ', true));
                    GUI::wxGetApp().run_script(tmp_str);
            }
            else if (command_str.compare("get_preset_params") == 0) {
                json j;
                j["sequence_id"] = "";
                j["command"]     = "get_preset_params";

                auto cache_file = fs::path(data_dir()).append("system").append("Creality").append("profile_version.json");
                if (!fs::exists(cache_file)) {
                    auto src = fs::path(resources_dir()).append("profiles").append("Creality").append("profile_version.json");
                    fs::copy_file(src, cache_file);
                }
                json cache_json;
                if (fs::exists(cache_file)) {
                    boost::nowide::ifstream ifs(cache_file.string());
                    ifs >> cache_json;
                }
                j["data"] = cache_json["Creality"];
                std::stringstream ss;
                ss << j << std::endl;
                return ss.str();
            } else if (command_str.compare("get_user_preset_status") == 0) {
                // Fork: cloud preset sync was removed (F4); the Home page always sees it off.
                json j;
                j["sequence_id"] = "";
                j["command"]     = "get_user_preset_status";
                j["data"]        = json{{"status", false}};
                std::stringstream ss;
                ss << j << std::endl;
                return ss.str();
            } else if (command_str.compare("refresh_all_device") == 0) {
                CallAfter([this] { 
                if (wxGetApp().mainframe->get_printer_mgr_view()) {
                    wxGetApp().mainframe->get_printer_mgr_view()->request_refresh_all_device();
                }
                });
            } else if (command_str.compare("update_devices_list") == 0) {
                boost::thread _thread = Slic3r::create_thread([this] {
                    std::vector<std::string> prefix;
                    prefix.push_back("CXSWBox");
                    prefix.push_back("creality");
                    prefix.push_back("Creality");
                    std::vector<std::string> vtIp, vtBoxIp;
                    auto                     vtDevice = cxnet::syncDiscoveryService(prefix);
                    for (auto& item : vtDevice) {
                        std::string answer = item.answer;
                        if (answer.substr(0, 8) == "_CXSWBox") {
                            vtBoxIp.push_back(item.machineIp);
                        } else {
                            vtIp.push_back(item.machineIp);
                        }
                    }

                    nlohmann::json dataJson;
                    dataJson["servers"] = vtIp;
                    dataJson["boxs"]    = vtBoxIp;

                    nlohmann::json commandJson;
                    commandJson["command"] = "update_devices_list";
                    commandJson["data"]    = dataJson;

                    wxString strJS = wxString::Format("window.handleStudioCmd('%s');", commandJson.dump());
                    wxGetApp().CallAfter([this, strJS] { run_script(strJS.ToStdString()); });
                });
            } else if (command_str.compare("get_is_first_install") == 0) {
                std::string    res = app_config->get("is_first_install");
                nlohmann::json dataJson;
                nlohmann::json commandJson;
                bool           onlyDefault = preset_bundle->printers.only_default_printers();
                if (res == "1") {
                    dataJson["deviceAddEnd"] = "1";
                    commandJson["command"]   = "get_is_first_install";
                    commandJson["data"]      = dataJson;
                } else {
                    if (onlyDefault) {
                        dataJson["deviceAddEnd"] = "0";
                        commandJson["command"]   = "get_is_first_install";
                        dataJson["guideType"]    = "begin";
                        dataJson["guideType"] = "new";
                        commandJson["data"]      = dataJson;


                        if (!m_appconfig_new)
                            m_appconfig_new = new AppConfig();
                        #ifdef WIN32
                           wxGetApp().mainframe->topbar()->DisableGuideModeItems();
                        #endif
                        #ifdef __APPLE__
                           wxGetApp().mainframe->topbar()->DisableGuideModeItemsMac();
                           Slic3r::macos_set_menu_bar_hidden(true);
                        #endif
                           wxGetApp().mainframe->select_tab(size_t(MainFrame::tpHome));
                           wxGetApp().mainframe->m_topbar->SetSelection(size_t(MainFrame::tpHome));
                        #ifdef WIN32
                            ShowWindow(wxGetApp().mainframe->GetHWND(), SW_SHOWMAXIMIZED);
                        #else
                            wxDisplay Screen;
                            wxRect ClientRect = Screen.GetClientArea();
                            wxGetApp().mainframe->SetSize(ClientRect);
                        #endif
                           
                       } else
                       {
                           dataJson["deviceAddEnd"] = "1";
                           commandJson["command"]   = "get_is_first_install";
                           commandJson["data"]      = dataJson;

                           //m_UITour->deleteStep(0);
                          
                           #ifdef WIN32
                           mainframe->topbar()->EnableGuideModeItems();
                           #endif
                           #ifdef __APPLE__
                           mainframe->topbar()->EnableGuideModeItemsMac();
                           Slic3r::macos_set_menu_bar_hidden(false);
                           #endif
                           //mainframe->select_tab(size_t(1));
                           //mainframe->m_topbar->SetSelection(size_t(MainFrame::tpPreview));
                       }
                   }
                   wxString strJS = wxString::Format("window.handleStudioCmd('%s');", commandJson.dump());
                   wxGetApp().CallAfter([this, strJS] { run_script(strJS.ToStdString()); });
            } else if (command_str.compare("set_deviceAdd_end") == 0) {
                json printersData = json::parse(cmd);
                std::string request_sequence = printersData.value("sequence_id", std::string());
                bool        background_prepared = printersData.value("_background_prepared", false);

                auto send_result = [this, request_sequence](int result, bool finish_task = true) {
                    if (finish_task)
                        m_device_add_in_progress = false;

                    nlohmann::json commandJson;
                    commandJson["command"]         = "set_deviceAdd_end";
                    commandJson["sequence_id"]     = request_sequence;
                    commandJson["data"]["result"] = result;
                    wxString strJS = wxString::Format("handleStudioCmd(%s)", commandJson.dump(-1, ' ', true));
                    wxGetApp().CallAfter([this, strJS, result] {
                        run_script(strJS.ToStdString());
                        if (result)
                            reload_homepage();
                    });
                };

                if (!background_prepared) {
                    if (m_device_add_in_progress.exchange(true)) {
                        send_result(0, false);
                        return "";
                    }

                    if (!m_appconfig_new)
                        m_appconfig_new = new AppConfig();

                    const auto enabled_vendors = app_config->vendors();
                    const auto enabled_filaments = app_config->has_section(AppConfig::SECTION_FILAMENTS) ?
                                                       app_config->get_section(AppConfig::SECTION_FILAMENTS) :
                                                       std::map<std::string, std::string>();
                    json selected_printers = printersData["selected_printers"];

                    boost::thread prepare_thread = Slic3r::create_thread(
                        [this, selected_printers = std::move(selected_printers), enabled_vendors, enabled_filaments,
                         request_sequence, send_result]() mutable {
                            try {
                                ProfileFamilyLoader* loader = ProfileFamilyLoader::get_instance();
                                loader->wait_until_loaded();
                                json profile_json;
                                json machine_json;
                                bool load_custom_from_bundle = false;
                                loader->get_result(profile_json, machine_json, load_custom_from_bundle);

                                DeviceAddPreparation prepared = prepare_device_add(std::move(selected_printers), std::move(profile_json),
                                                                                  enabled_vendors, enabled_filaments);
                                if (!prepared.valid) {
                                    send_result(0);
                                    return;
                                }

                                json deferred_command;
                                deferred_command["command"]               = "set_deviceAdd_end";
                                deferred_command["sequence_id"]           = request_sequence;
                                deferred_command["selected_printers"]     = std::move(prepared.selected_printers);
                                deferred_command["_prepared_profile"]     = std::move(prepared.profile_json);
                                deferred_command["_background_prepared"]  = true;
                                const std::string deferred_payload = deferred_command.dump();
                                wxGetApp().CallAfter([this, deferred_payload] { handle_web_request(deferred_payload); });
                            } catch (const std::exception& e) {
                                BOOST_LOG_TRIVIAL(error) << "Failed to prepare printer configuration: " << e.what();
                                send_result(0);
                            } catch (...) {
                                BOOST_LOG_TRIVIAL(error) << "Failed to prepare printer configuration";
                                send_result(0);
                            }
                        });
                    prepare_thread.detach();
                    return "";
                }

                if (!m_device_add_in_progress) {
                    send_result(0, false);
                    return "";
                }

                try {
                json selectedPrintersJson = printersData["selected_printers"];

                if (!background_prepared) {
                for (auto it = selectedPrintersJson.begin(); it != selectedPrintersJson.end(); ++it) {
                    json &OneSelect = it.value();
                    if(OneSelect.contains("vendor"))
                    {
                        auto vendor = OneSelect["vendor"];
                        //通过内部型号添加机器
                        if(vendor == "")
                        {
                            OneSelect["vendor"] = "Creality";
                            //获取机器内部型号
                            auto model = OneSelect["model"].get<std::string>();
                            //K1 and K1 Max内部型号需要转换
                            if(model=="K1 Max")
                            {
                                model = "CR-K1 Max";
                            }
                            if(model=="K1")
                            {
                                model = "CR-K1";
                            }
                            fs::path printer_list_file = fs::path(resources_dir()).append("profiles").append("Creality").append("machineList.json").string();
                            json printer_json;
                            if (fs::exists(printer_list_file)) {
                                boost::nowide::ifstream ifs(printer_list_file.string());
                                ifs >> printer_json;
                            }
                            if(printer_json.contains("printerList"))
                            {
                                json pmodels = printer_json["printerList"];
                                for (const auto& printer : pmodels) {
                                auto printerIntName = printer["printerIntName"].get<std::string>();
                                    if(printerIntName == model)
                                    {
                                        std::string nozzle_diameter = printer["nozzleDiameter"][0].get<std::string>();
                                        std::string name = printer["name"].get<std::string>();
                                        if(name.find("Creality")==std::string::npos&&name.find("SPARKX")==std::string::npos)
                                        {
                                            name = "Creality " + name;
                                        }
                                        OneSelect["model"] = name;
                                        if(nozzle_diameter=="0.4")
                                        {
                                            OneSelect["nozzle_diameter"] = nozzle_diameter;
                                        }else{
                                            if(OneSelect["nozzle_diameter"].size()==0)
                                            {
                                                OneSelect["nozzle_diameter"] = nozzle_diameter;
                                            }
                                        }
                                        
                                    }
                                }
                            }
                        }
                    }
                }
                }
                
                //std::string dump = selectedPrintersJson.dump();
                //std::cout << "dump: " << dump;
                
                struct NozzleSelected
                {
                    int                      index;
                    bool                     modify;
                    std::string              model;
                    int                      selected_size;
                    std::vector<std::string> diameters;
                    std::vector<int>         selected;
                };
                auto Loc_GetMaterials = [](const std::string& materials) {
                    std::vector<std::string> results;
                    int                      idx = 0;
                    while (idx != std::string::npos && idx < materials.size()) {
                        int index = materials.find(';', idx);
                        if (idx < index) {
                            std::string nozzle = materials.substr(idx, index - idx);
                            results.emplace_back(nozzle);
                            idx = index + 1;
                        } else if (index == std::string::npos) {
                            std::string nozzle = materials.substr(idx);
                            results.emplace_back(nozzle);
                            idx = index;
                        } else {
                            idx = std::string::npos;
                        }
                    }
                    return results;
                };
                json m_ProfileJson = std::move(printersData["_prepared_profile"]);

                if (!background_prepared) {
                std::vector<NozzleSelected>        nozzle_selected;
                std::set<std::string>              selected_filaments;
                std::set<std::string>              un_selected_filaments;
                json                               MSelected = selectedPrintersJson;
                std::map<std::string, std::string> OldMSelectedMap;
                int                                nModel = m_ProfileJson["model"].size();
                for (int m = 0; m < nModel; m++) {
                    json TmpModel = m_ProfileJson["model"][m];
                    if (!TmpModel["nozzle_selected"].get<std::string>().empty())
                        OldMSelectedMap.emplace(TmpModel["model"].get<std::string>(), TmpModel["nozzle_selected"].get<std::string>());
                    m_ProfileJson["model"][m]["nozzle_selected"] = "";

                    for (auto it = MSelected.begin(); it != MSelected.end(); ++it) {
                        json OneSelect = it.value();

                        std::string s1 = TmpModel["model"].get<std::string>();
                        std::string s2 = OneSelect["model"].get<std::string>();
                        if (s1.compare(s2) == 0) {
                            m_ProfileJson["model"][m]["nozzle_selected"] = OneSelect["nozzle_diameter"];
                            NozzleSelected nozzle;
                            nozzle.model          = TmpModel["model"].get<std::string>();
                            nozzle.index          = m;
                            auto select_diameters = Loc_GetMaterials(OneSelect["nozzle_diameter"]);
                            nozzle.selected_size  = Loc_GetMaterials(OneSelect["nozzle_diameter"]).size();
                            nozzle.diameters      = Loc_GetMaterials(TmpModel["nozzle_diameter"]);
                            nozzle.selected.resize(nozzle.diameters.size());
                            for (int i = 0; i < nozzle.diameters.size(); ++i) {
                                auto it = std::find(select_diameters.begin(), select_diameters.end(), nozzle.diameters[i]);
                                if (it != select_diameters.end())
                                    nozzle.selected[i] = 1;
                                else
                                    nozzle.selected[i] = 0;
                            }
                            nozzle_selected.emplace_back(nozzle);
                            break;
                        }
                    }
                }
                if(nozzle_selected.size()==0)
                {
                    send_result(0);
                    return "";
                }
                for (int i = 0; i < nozzle_selected.size(); ++i) {
                    const NozzleSelected& nozzle = nozzle_selected[i];

                    if (m_ProfileJson["model"][nozzle.index]["materials"].is_string()) {
                        std::vector<std::string> materials = Loc_GetMaterials(m_ProfileJson["model"][nozzle.index]["materials"]);
                        if (nozzle.selected_size > 0) {
                            for (auto& material : materials) {
                                bool bFindDefault = false;
                                if (m_ProfileJson["filament"].contains(material)) {
                                    // 判断耗材是否支持当前的喷嘴
                                    for (int i = 0; i < nozzle.diameters.size(); ++i) {
                                        if (nozzle.selected[i] > 0) {
                                            std::string new_models = nozzle.model + "++" + nozzle.diameters[i];
                                            if (m_ProfileJson["filament"][material]["models"].get<std::string>().find(new_models) !=
                                                std::string::npos) {
                                                bFindDefault = true;
                                                selected_filaments.emplace(material);
                                            }
                                        }
                                    }
                                }
                                if (!bFindDefault) {
                                    size_t pos = material.find("@");
                                    if (pos == std::string::npos) {
                                        for (int i = 0; i < nozzle.diameters.size(); ++i) {
                                            if (nozzle.selected[i] > 0) {
                                                std::string new_material = material + " @" + nozzle.model;
                                                if (m_ProfileJson["filament"].contains(new_material)) {
                                                    selected_filaments.emplace(new_material);
                                                } else {
                                                    new_material += " " + nozzle.diameters[i] + " nozzle";
                                                    if (m_ProfileJson["filament"].contains(new_material))
                                                        selected_filaments.emplace(new_material);
                                                }
                                            }
                                        }
                                    }
                                };
                            }
                        } else {
                            for (auto& material : materials) {
                                if (m_ProfileJson["filament"].contains(material))
                                    un_selected_filaments.emplace(material);
                                else {
                                    size_t pos = material.find("@");
                                    if (pos == std::string::npos) {
                                        for (int i = 0; i < nozzle.diameters.size(); ++i) {
                                            if (nozzle.selected[i] > 0) {
                                                std::string new_material = material + " @" + nozzle.model;
                                                if (m_ProfileJson["filament"].contains(new_material)) {
                                                    un_selected_filaments.emplace(new_material);
                                                } else {
                                                    new_material += " " + nozzle.diameters[i] + " nozzle";
                                                    if (m_ProfileJson["filament"].contains(new_material))
                                                        un_selected_filaments.emplace(new_material);
                                                }
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
                std::vector<std::string> diff_material;
                std::set_difference(un_selected_filaments.begin(), un_selected_filaments.end(), selected_filaments.begin(),
                                    selected_filaments.end(), std::inserter(diff_material, diff_material.begin()));

                // set default material
                for (auto& material : selected_filaments) {
                    if (m_ProfileJson["filament"].contains(material)) {
                        m_ProfileJson["filament"][material]["selected"] = 1;
                    }
                }
                // remove un-used material
                for (auto& material : diff_material) {
                    if (m_ProfileJson["filament"].contains(material)) {
                        m_ProfileJson["filament"][material]["selected"] = 0;
                    }
                }
                }

                // 处理打印机用户预设
                wxGetApp().app_config->set_userPresets("", true);
                PresetCollection* presetColl = &wxGetApp().preset_bundle->printers;
                if (presetColl) {
                    for (int i = 0; i < presetColl->get_presets().size(); i++) {
                        Preset& preset = presetColl->preset(i, true); // true 返回  false 当前的copy
                        if ((preset.is_visible) && (preset.is_user())) {
                            preset.m_is_user_printer_hidden = true;
                            string strModelName             = preset.name;

                            json useSelected = selectedPrintersJson.value("user_data", json::array());
                            for (auto it = useSelected.begin(); it != useSelected.end(); ++it) {
                                json        useInfo    = it.value();
                                std::string sFrontName = useInfo["name"].get<std::string>();
                                if (strModelName == sFrontName && (!strModelName.empty() && !sFrontName.empty())) {
                                    preset.m_is_user_printer_hidden = false;
                                    wxGetApp().app_config->set_userPresets(strModelName);
                                }
                            }
                        }
                    }

                    const Preset& presetSelect = presetColl->get_selected_preset();
                    if (false == presetSelect.m_is_user_printer_hidden) {
                        size_t idxCurrent = presetColl->get_selected_idx();
                        size_t idx_new    = idxCurrent + 1;
                        if (idx_new < presetColl->size()) {
                            // for (; idx_new < presetColl->size() && !presetColl->preset(idx_new,true).is_visible; ++idx_new);
                            while (idx_new < presetColl->size() && (!presetColl->preset(idx_new, true).is_visible ||
                                                                    !presetColl->preset(idx_new, true).m_is_user_printer_hidden)) {
                                ++idx_new;
                            }
                        }
                        if (idx_new == presetColl->size()) {
                            // for (idx_new = idxCurrent - 1; idx_new > 0 && !presetColl->preset(idx_new,true).is_visible; --idx_new);
                            idx_new = idxCurrent - 1;
                            while (idx_new > 0 && (!presetColl->preset(idx_new, true).is_visible ||
                                                   !presetColl->preset(idx_new, true).m_is_user_printer_hidden)) {
                                --idx_new;
                            }
                        }
                        // presetColl->select_preset(idx_new);

                        Preset& selPreset = presetColl->preset(idx_new);
                        string  strName   = selPreset.name;
                        wxGetApp().get_tab(Preset::TYPE_PRINTER)->select_preset(strName);
                    }
                }

                SaveProfile(m_ProfileJson);
                bool apply_keeped_changes = false;
                if (!apply_config(app_config, m_appconfig_new, preset_bundle, preset_updater, apply_keeped_changes)) {
                    send_result(0);
                    return "";
                }

                if (apply_keeped_changes)
                    apply_keeped_preset_modifications();

                app_config->set_legacy_datadir(false);
                update_mode();

                load_current_presets();
                delete m_appconfig_new;
                m_appconfig_new = nullptr;

                app_config->set("is_first_install", "1");
                mainframe->select_tab(size_t(MainFrame::tp3DEditor));
                mainframe->m_topbar->SetSelection(size_t(MainFrame::tp3DEditor));
                CallAfter([this] {
                    std::shared_ptr<wxTimer> tour_timer = std::make_shared<wxTimer>();
                    tour_timer->Bind(wxEVT_TIMER, [this,tour_timer](wxTimerEvent&) {
                        tour_timer->Stop();
#ifdef __APPLE__
                wxPlatformInfo platformInfo;
                int major = platformInfo.GetOSMajorVersion();
                if(major>=15)
                {
                    startTour();
                }else{
                    startTour_Apple();
                }
                mainframe->topbar()->EnableGuideModeItemsMac();
                Slic3r::macos_set_menu_bar_hidden(false);
#elif defined(__linux__) || defined(__LINUX__)
                startTour_Apple();
#else
                startTour();
                mainframe->topbar()->EnableGuideModeItems();
#endif // 

                        
                    });
                    tour_timer->StartOnce(1000);
                });
                send_result(1);
                } catch (const std::exception& e) {
                    BOOST_LOG_TRIVIAL(error) << "Failed to commit printer configuration: " << e.what();
                    send_result(0);
                } catch (...) {
                    BOOST_LOG_TRIVIAL(error) << "Failed to commit printer configuration";
                    send_result(0);
                }

            } else if (command_str.compare("finish_bind_device_guide") == 0) {
                auto send_result = [this](int result) {
                    nlohmann::json commandJson;
                    commandJson["command"] = "finish_bind_device_guide";
                    commandJson["data"]["result"] = result;

                    wxString strJS = wxString::Format("handleStudioCmd(%s)", commandJson.dump(-1, ' ', true));
                    wxGetApp().CallAfter([this, strJS, result] {
                        run_script(strJS.ToStdString());
                        if (result) {
                            this->reload_homepage();
                        }
                    });
                };

                app_config->set("is_first_install", "1");
                mainframe->select_tab(size_t(MainFrame::tp3DEditor));
                mainframe->m_topbar->SetSelection(size_t(MainFrame::tp3DEditor));
                send_result(1);
                CallAfter([this] {
                    std::shared_ptr<wxTimer> tour_timer = std::make_shared<wxTimer>();
                    tour_timer->Bind(wxEVT_TIMER, [this, tour_timer](wxTimerEvent&) {
                        tour_timer->Stop();
#ifdef __APPLE__
                        wxPlatformInfo platformInfo;
                        int major = platformInfo.GetOSMajorVersion();
                        if (major >= 15) {
                            startTour();
                        } else {
                            startTour_Apple();
                        }
                        mainframe->topbar()->EnableGuideModeItemsMac();
                        Slic3r::macos_set_menu_bar_hidden(false);
#elif defined(__linux__) || defined(__LINUX__)
                        startTour_Apple();
#else
                        startTour();
                        mainframe->topbar()->EnableGuideModeItems();
#endif
                    });
                    tour_timer->StartOnce(1000);
                });
            } else if (command_str.compare("get_devices_info") == 0) {
                json res;
                webGetDevicesInfo(res);

                nlohmann::json commandJson;
                commandJson["command"] = "get_devices_info_result";
                commandJson["data"]    = res;

                wxString strJS = wxString::Format("handleStudioCmd(%s)", commandJson.dump(-1, ' ', true));
                wxGetApp().CallAfter([this, strJS] { run_script(strJS.ToStdString()); });
            } else if (command_str.compare("get_machine_list") == 0) {
                load_machine_preset_data();
            } else if (command_str.compare("set_user_login_area") == 0) {
                json        regionData = json::parse(strInput);
                std::string region = regionData["region"];
                AppConfig* config = GUI::wxGetApp().app_config;
                config->set("region", region);
            }
        }
    }
    catch (...) {
        BOOST_LOG_TRIVIAL(trace) << "parse json cmd failed " << cmd;
        return "";
    }
    return "";
}

void GUI_App::request_open_project(std::string project_id)
{
    if (plater()->is_background_process_slicing()) {
        Slic3r::GUI::show_info(nullptr, _L("new or open project file is not allowed during the slicing process!"), _L("Open Project"));
        return;
    }

    if (project_id == "<new>")
        plater()->new_project();
    else if (project_id.empty())
        plater()->load_project();
    else if (std::find_if_not(project_id.begin(), project_id.end(),
        [](char c) { return std::isdigit(c); }) == project_id.end())
        ;
    else if (boost::algorithm::starts_with(project_id, "http"))
        ;
    else
        CallAfter([this, project_id] { 
        mainframe->open_recent_project(-1, wxString::FromUTF8(project_id)); 
            });
}

void GUI_App::request_remove_project(std::string project_id)
{
    mainframe->remove_recent_project(-1, wxString::FromUTF8(project_id));
}

void GUI_App::process_msg_loop()
{
    if (wxEventLoopBase::GetActive())
        wxEventLoopBase::GetActive()->YieldFor(wxEVT_CATEGORY_ALL);
}

void GUI_App::check_user_lite_mode_dlg() 
{
    std::string key = "gcode_preview_lite_mode";
    if (get_app_config()->get(key).empty())	
	{
        EnableLiteModeDialog dlg(this->mainframe, wxID_ANY, _L("Slicing Preview"));
        wxGetApp().UpdateDlgDarkUI(&dlg);
        bool enable = (dlg.ShowModal() == wxID_YES);
        get_app_config()->set(key, enable ? "true" : "false");
	}
}

std::string GUI_App::version_display = "";
std::string GUI_App::format_display_version()
{
    if (!version_display.empty()) return version_display;

    version_display = CREALITYPRINT_VERSION;
    return version_display;
}

void GUI_App::show_dialog(wxString msg)
{
    if (m_info_dialog_content.empty()) {
        wxCommandEvent* evt = new wxCommandEvent(EVT_SHOW_DIALOG);
        evt->SetString(msg);
        GUI::wxGetApp().QueueEvent(evt);
        m_info_dialog_content = msg;
    }
}

void  GUI_App::push_notification(wxString msg, wxString title, UserNotificationStyle style)
{
    if (style == UserNotificationStyle::UNS_NORMAL) {
        if (m_info_dialog_content.empty()) {
            wxCommandEvent* evt = new wxCommandEvent(EVT_SHOW_DIALOG);
            evt->SetString(msg);
            GUI::wxGetApp().QueueEvent(evt);
            m_info_dialog_content = msg;
        }
    }
    else if (style == UserNotificationStyle::UNS_WARNING_CONFIRM) {
        GUI::wxGetApp().CallAfter([msg, title] {
            GUI::MessageDialog msg_dlg(nullptr, msg, title, wxICON_WARNING | wxOK);
            msg_dlg.ShowModal();
        });
    }
}

void GUI_App::save_user_default_filaments(AppConfig *new_app_config)
{
    std::set<std::string> selected_models;
    for (const auto& vendor : new_app_config->vendors()) {
        for (const auto& model : vendor.second) {
            if (!model.second.empty())
                selected_models.emplace(model.first);
        }
    }

    auto add_filament_to_sessiton = [this,new_app_config](std::string machine_name,std::string default_materials){
        std::string section_name = AppConfig::SECTION_FILAMENTS;
            if (new_app_config->has_section(section_name)) {
            const std::map<std::string, std::string> &installed = new_app_config->get_section(section_name);
            auto has = [&installed](const std::string &name) {
                auto it = installed.find(name);
                return it != installed.end() && ! it->second.empty();
            };
            std::vector<std::string> materials;
            boost::algorithm::split(materials, default_materials, boost::is_any_of(";"));
            bool bInstalled = false;
            for (const auto& str : materials) {
                std::string basename =  str + " @"+machine_name;
                    bool is_visible = has(basename);
                    if (is_visible) {
                        bInstalled = true;
                    }
            }
            if(bInstalled)
                return;
            for (const auto& str : materials) {
                std::string basename =  str + " @"+machine_name;
                        new_app_config->set(section_name, basename, "true");
            }
             //app_config->save();
             
        }
    };
    fs::path creality_file = fs::path(resources_dir()).append("profiles").append("Creality.json");
    if(fs::exists(creality_file))
    {
        try{
        boost::nowide::ifstream t(creality_file.string());
        std::stringstream buffer;
        buffer << t.rdbuf();
        json jLocal = json::parse(buffer.str());
        auto machine_list = jLocal["machine_model_list"];
        for (const auto& item : machine_list)
        {
            std::string name = item["name"];
            if (!selected_models.empty() && selected_models.find(name) == selected_models.end())
                continue;

            std::string sub_path = item["sub_path"];
            fs::path machine_file = fs::path(resources_dir()).append("profiles").append("Creality").append(sub_path);
            boost::nowide::ifstream m(machine_file.string());
            std::stringstream buffer2;
            buffer2 << m.rdbuf();
            json j = json::parse(buffer2.str());
            std::string nozzle_diameter = j["nozzle_diameter"];
            std::vector<std::string> diameters;
            boost::algorithm::split(diameters, nozzle_diameter, boost::is_any_of(";"));
            auto default_materials = j["default_materials"];
            for (const auto& diameter : diameters) {
                std::string machine_name = (boost::format( "%1% %2% nozzle") % name % diameter).str();
                add_filament_to_sessiton(machine_name,default_materials);
            }
        
        }
        
        
    }catch(std::exception e){

        }
   
    }

}

void GUI_App::start_http_server()
{
    if (!m_http_server.is_started())
        m_http_server.start();
}
#ifdef __linux__
static const wxLanguageInfo* linux_get_existing_locale_language(const wxLanguageInfo* language,
                                                                const wxLanguageInfo* system_language)
{
    constexpr size_t max_len = 50;
    char path[max_len] = "";
    std::vector<std::string> locales;
    const std::string lang_prefix = into_u8(language->CanonicalName.BeforeFirst('_'));

    // Call locale -a so we can parse the output to get the list of available locales
    // We expect lines such as "en_US.utf8". Pick ones starting with the language code
    // we are switching to. Lines with different formatting will be removed later.
    FILE* fp = popen("locale -a", "r");
    if (fp != NULL) {
        while (fgets(path, max_len, fp) != NULL) {
            std::string line(path);
            line = line.substr(0, line.find('\n'));
            if (boost::starts_with(line, lang_prefix))
                locales.push_back(line);
        }
        pclose(fp);
    }

    // locales now contain all candidates for this language.
    // Sort them so ones containing anything about UTF-8 are at the end.
    std::sort(locales.begin(), locales.end(), [](const std::string& a, const std::string& b)
    {
        auto has_utf8 = [](const std::string & s) {
            auto S = boost::to_upper_copy(s);
            return S.find("UTF8") != std::string::npos || S.find("UTF-8") != std::string::npos;
        };
        return ! has_utf8(a) && has_utf8(b);
    });

    // Remove the suffix behind a dot, if there is one.
    for (std::string& s : locales)
        s = s.substr(0, s.find("."));

    // We just hope that dear Linux "locale -a" returns country codes
    // in ISO 3166-1 alpha-2 code (two letter) format.
    // https://en.wikipedia.org/wiki/List_of_ISO_3166_country_codes
    // To be sure, remove anything not looking as expected
    // (any number of lowercase letters, underscore, two uppercase letters).
    locales.erase(std::remove_if(locales.begin(),
                                 locales.end(),
                                 [](const std::string& s) {
                                     return ! std::regex_match(s,
                                         std::regex("^[a-z]+_[A-Z]{2}$"));
                                 }),
                   locales.end());

    // Is there a candidate matching a country code of a system language? Move it to the end,
    // while maintaining the order of matches, so that the best match ends up at the very end.
    std::string temp_local = into_u8(system_language->CanonicalName.AfterFirst('_'));
    if (temp_local.size() >= 2) {
        temp_local = temp_local.substr(0, 2);
    }
    std::string system_country = "_" + temp_local;
    int cnt = locales.size();
    for (int i=0; i<cnt; ++i)
        if (locales[i].find(system_country) != std::string::npos) {
            locales.emplace_back(std::move(locales[i]));
            locales[i].clear();
        }

    // Now try them one by one.
    for (auto it = locales.rbegin(); it != locales.rend(); ++ it)
        if (! it->empty()) {
            const std::string &locale = *it;
            const wxLanguageInfo* lang = wxLocale::FindLanguageInfo(from_u8(locale));
            if (wxLocale::IsAvailable(lang->Language))
                return lang;
        }
    return language;
}

static bool check_has_C_language_set() 
{
    setenv("LANG", "C", 1);
    char* current_locale = setlocale(LC_ALL, nullptr);
    if (current_locale != nullptr) {
        printf("Current locale: %s\n", current_locale);
        std::string lang = current_locale;
        return lang == "C" || lang == "C.UTF-8";
    }
    return false;
}
#endif

void GUI_App::bind_shader(const GLShaderProgram* p_shader)
{
    m_opengl_mgr.bind_shader(p_shader);
}

void GUI_App::unbind_shader()
{
    m_opengl_mgr.unbind_shader();
}

// Load gettext translation files and activate them at the start of the application,
// based on the "language" key stored in the application config.
bool GUI_App::load_language(wxString language, bool initial)
{
    BOOST_LOG_TRIVIAL(info) << boost::format("%1%: language %2%, initial: %3%") %__FUNCTION__ %language %initial;
    
    if (initial) {
    	// There is a static list of lookup path prefixes in wxWidgets. Add ours.
	    wxFileTranslationsLoader::AddCatalogLookupPathPrefix(from_u8(localization_dir()));
    	// Get the active language from PrusaSlicer.ini, or empty string if the key does not exist.
        language = app_config->get("language");
        if (! language.empty())
        	BOOST_LOG_TRIVIAL(info) << boost::format("language provided by CrealityPrint.conf: %1%") % language;
        else {
            // Get the system language.
            const wxLanguage lang_system = wxLanguage(wxLocale::GetSystemLanguage());
            if (lang_system != wxLANGUAGE_UNKNOWN) {
                m_language_info_system = wxLocale::GetLanguageInfo(lang_system);
#ifdef __WXMSW__
                WCHAR wszLanguagesBuffer[LOCALE_NAME_MAX_LENGTH];
                ::LCIDToLocaleName(LOCALE_USER_DEFAULT, wszLanguagesBuffer, LOCALE_NAME_MAX_LENGTH, 0);
                wxString lang(wszLanguagesBuffer);
                lang.Replace('-', '_');
                if (auto info = wxLocale::FindLanguageInfo(lang))
                    m_language_info_system = info;
#endif
                BOOST_LOG_TRIVIAL(info) << boost::format("System language detected (user locales and such): %1%") % m_language_info_system->CanonicalName.ToUTF8().data();
                // BBS set language to app config
                app_config->set("language", m_language_info_system->CanonicalName.ToUTF8().data());
            } else {
                {
                    // Allocating a temporary locale will switch the default wxTranslations to its internal wxTranslations instance.
                    wxLocale temp_locale;
                    temp_locale.Init();
                    // Set the current translation's language to default, otherwise GetBestTranslation() may not work (see the wxWidgets source code).
                    wxTranslations::Get()->SetLanguage(wxLANGUAGE_DEFAULT);
                    // Let the wxFileTranslationsLoader enumerate all translation dictionaries for PrusaSlicer
                    // and try to match them with the system specific "preferred languages".
                    // There seems to be a support for that on Windows and OSX, while on Linuxes the code just returns wxLocale::GetSystemLanguage().
                    // The last parameter gets added to the list of detected dictionaries. This is a workaround
                    // for not having the English dictionary. Let's hope wxWidgets of various versions process this call the same way.
                    wxString best_language = wxTranslations::Get()->GetBestTranslation(SLIC3R_APP_KEY, wxLANGUAGE_ENGLISH);
                    if (!best_language.IsEmpty()) {
                        m_language_info_best = wxLocale::FindLanguageInfo(best_language);
                        BOOST_LOG_TRIVIAL(info) << boost::format("Best translation language detected (may be different from user locales): %1%") %
                                                        m_language_info_best->CanonicalName.ToUTF8().data();
                        app_config->set("language", m_language_info_best->CanonicalName.ToUTF8().data());
                    }
#ifdef __linux__
                    wxString lc_all;
                    if (wxGetEnv("LC_ALL", &lc_all) && !lc_all.IsEmpty()) {
                        // Best language returned by wxWidgets on Linux apparently does not respect LC_ALL.
                        // Disregard the "best" suggestion in case LC_ALL is provided.
                        m_language_info_best = nullptr;
                    }
#endif
                }
            }
        }
    }

	const wxLanguageInfo *language_info = language.empty() ? nullptr : wxLocale::FindLanguageInfo(language);
	if (! language.empty() && (language_info == nullptr || language_info->CanonicalName.empty())) {
		// Fix for wxWidgets issue, where the FindLanguageInfo() returns locales with undefined ANSII code (wxLANGUAGE_KONKANI or wxLANGUAGE_MANIPURI).
		language_info = nullptr;
    	BOOST_LOG_TRIVIAL(error) << boost::format("Language code \"%1%\" is not supported") % language.ToUTF8().data();
	}

	if (language_info != nullptr && language_info->LayoutDirection == wxLayout_RightToLeft) {
    	BOOST_LOG_TRIVIAL(trace) << boost::format("The following language code requires right to left layout, which is not supported by MentorSlicer: %1%") % language_info->CanonicalName.ToUTF8().data();
		language_info = nullptr;
	}

    if (language_info == nullptr) {
        // PrusaSlicer does not support the Right to Left languages yet.
        if (m_language_info_system != nullptr && m_language_info_system->LayoutDirection != wxLayout_RightToLeft)
            language_info = m_language_info_system;
        if (m_language_info_best != nullptr && m_language_info_best->LayoutDirection != wxLayout_RightToLeft)
        	language_info = m_language_info_best;
	    if (language_info == nullptr)
			language_info = wxLocale::GetLanguageInfo(wxLANGUAGE_ENGLISH_US);
    }

	BOOST_LOG_TRIVIAL(trace) << boost::format("Switching wxLocales to %1%") % language_info->CanonicalName.ToUTF8().data();

    // Select language for locales. This language may be different from the language of the dictionary.
    //if (language_info == m_language_info_best || language_info == m_language_info_system) {
    //    // The current language matches user's default profile exactly. That's great.
    //} else if (m_language_info_best != nullptr && language_info->CanonicalName.BeforeFirst('_') == m_language_info_best->CanonicalName.BeforeFirst('_')) {
    //    // Use whatever the operating system recommends, if it the language code of the dictionary matches the recommended language.
    //    // This allows a Swiss guy to use a German dictionary without forcing him to German locales.
    //    language_info = m_language_info_best;
    //} else if (m_language_info_system != nullptr && language_info->CanonicalName.BeforeFirst('_') == m_language_info_system->CanonicalName.BeforeFirst('_'))
    //    language_info = m_language_info_system;

    // Alternate language code.
    wxLanguage language_dict = wxLanguage(language_info->Language);
    if (language_info->CanonicalName.BeforeFirst('_') == "sk") {
    	// Slovaks understand Czech well. Give them the Czech translation.
    	language_dict = wxLANGUAGE_CZECH;
		BOOST_LOG_TRIVIAL(info) << "Using Czech dictionaries for Slovak language";
    }

#ifdef __linux__
    // If we can't find this locale , try to use different one for the language
    // instead of just reporting that it is impossible to switch.
    if (! wxLocale::IsAvailable(language_info->Language) && m_language_info_system) {
        std::string original_lang = into_u8(language_info->CanonicalName);
        language_info = linux_get_existing_locale_language(language_info, m_language_info_system);
        BOOST_LOG_TRIVIAL(info) << boost::format("Can't switch language to %1% (missing locales). Using %2% instead.")
                                    % original_lang % language_info->CanonicalName.ToUTF8().data();
    }
#endif

    if (! wxLocale::IsAvailable(language_info->Language)&&initial) {
        language_info = wxLocale::GetLanguageInfo(wxLANGUAGE_ENGLISH_UK);
        app_config->set("language", language_info->CanonicalName.ToUTF8().data());
    }
    else if (initial) {
        // bbs supported languages
        //TODO: use a global one with Preference
        //wxLanguage supported_languages[]{
        //    wxLANGUAGE_ENGLISH,
        //    wxLANGUAGE_CHINESE_SIMPLIFIED,
        //    wxLANGUAGE_GERMAN,
        //    wxLANGUAGE_FRENCH,
        //    wxLANGUAGE_SPANISH,
        //    wxLANGUAGE_SWEDISH,
        //    wxLANGUAGE_DUTCH,
        //    wxLANGUAGE_HUNGARIAN,
        //    wxLANGUAGE_JAPANESE,
        //    wxLANGUAGE_ITALIAN
        //};
        //std::string cur_language = app_config->get("language");
        //if (cur_language != "") {
        //    //cleanup the language wrongly set before
        //    const wxLanguageInfo *langinfo = nullptr;
        //    bool embedded_language = false;
        //    int language_num = sizeof(supported_languages) / sizeof(supported_languages[0]);
        //    for (auto index = 0; index < language_num; index++) {
        //        langinfo = wxLocale::GetLanguageInfo(supported_languages[index]);
        //        std::string temp_lan = langinfo->CanonicalName.ToUTF8().data();
        //        if (cur_language == temp_lan) {
        //            embedded_language = true;
        //            break;
        //        }
        //    }
        //    if (!embedded_language)
        //        app_config->erase("app", "language");
        //}
    }

    if (! wxLocale::IsAvailable(language_info->Language)) {
    	// Loading the language dictionary failed.
        BOOST_LOG_TRIVIAL(warning) << "load_language: locale not available. requested locale=" << language_info->CanonicalName.ToUTF8().data()
                                   << ", dict_lang=" << static_cast<int>(language_dict)
                                   << ", initial=" << (initial ? "true" : "false")
                                   << ", config.language=" << app_config->get("language");
        boost::log::core::get()->flush();
    	wxString message = "Switching  MentorSlicer to language " + language_info->CanonicalName + " failed.";
        bool     had_C_lang_set = false;
#if !defined(_WIN32) && !defined(__APPLE__)
        // likely some linux system
        message += "\nYou may need to reconfigure the missing locales, likely by running the \"locale-gen\" and \"dpkg-reconfigure locales\" commands.\n";
        //had_C_lang_set = check_has_C_language_set();
#endif
        if (initial)
        	message + "\n\nApplication will close.";
        wxMessageBox(message, "MentorSlicer - Switching language failed", wxOK | wxICON_ERROR);
        if (initial) {
            if (!had_C_lang_set)
                std::exit(EXIT_FAILURE);
        }
		else
			return false;
    }

    // Release the old locales, create new locales.
    //FIXME wxWidgets cause havoc if the current locale is deleted. We just forget it causing memory leaks for now.
    m_wxLocale.release();
    m_wxLocale = Slic3r::make_unique<wxLocale>();
    m_wxLocale->Init(language_info->Language);
    // Override language at the active wxTranslations class (which is stored in the active m_wxLocale)
    // to load possibly different dictionary, for example, load Czech dictionary for Slovak language.
    wxTranslations::Get()->SetLanguage(language_dict);
    // PRINT-ADVISOR F8: the catalog keeps Creality's name (CrealityPrint.mo), not the process name.
    m_wxLocale->AddCatalog(SLIC3R_I18N_DOMAIN);
    BOOST_LOG_TRIVIAL(warning) << "load_language: wxLocale initialized. canonical=" << m_wxLocale->GetCanonicalName().ToUTF8().data()
                               << ", locale=" << m_wxLocale->GetLocale().ToUTF8().data()
                               << ", dict_lang=" << static_cast<int>(language_dict)
                               << ", initial=" << (initial ? "true" : "false");
    boost::log::core::get()->flush();
    m_imgui->set_language(into_u8(language_info->CanonicalName));
    //FIXME This is a temporary workaround, the correct solution is to switch to "C" locale during file import / export only.
    //wxSetlocale(LC_NUMERIC, "C");
    Preset::update_suffix_modified((_L("*") + " ").ToUTF8().data());
    HintDatabase::get_instance().reinit();
	return true;
}

Tab* GUI_App::get_tab(Preset::Type type)
{
    for (Tab* tab: tabs_list)
        if (tab->type() == type)
            return tab->completed() ? tab : nullptr; // To avoid actions with no-completed Tab
    return nullptr;
}

Tab* GUI_App::get_plate_tab()
{
    return plate_tab;
}

Tab* GUI_App::get_model_tab(bool part)
{
    return model_tabs_list[part ? 1 : 0];
}

Tab* GUI_App::get_layer_tab()
{
    return model_tabs_list[2];
}

ConfigOptionMode GUI_App::get_mode()
{
    if (!app_config->has("user_mode"))
        return comSimple;
    //BBS
    const auto mode = app_config->get("user_mode");
    return mode == "advanced" ? comAdvanced :
           mode == "simple" ? comSimple :
           mode == "develop" ? comDevelop : comSimple;
}

std::string GUI_App::get_mode_str()
{
    if (!app_config->has("user_mode"))
        return "simple";
    return app_config->get("user_mode");
}

void GUI_App::save_mode(const /*ConfigOptionMode*/ int mode, bool need_save)
{
    //BBS
    const std::string mode_str = mode == comAdvanced ? "advanced" :
                                 mode == comSimple ? "simple" :
                                 mode == comDevelop ? "develop" : "simple";
    if (need_save) {
        app_config->set("user_mode", mode_str);
    }
    update_mode(mode);
}

void GUI_App::update_develop_state()
{
    std::string mode = app_config->get("user_mode");
    ConfigOptionMode savedMode = mode == "develop" ? comDevelop : 
                                mode == "advanced" ? comAdvanced :
                                mode == "comSimple" ? comSimple : comAdvanced;


    std::string isDevelop = app_config->get("is_factory_mode");
    app_config->set("is_factory_mode", isDevelop == "true" ? "false" : "true");
    app_config->save();
    update_mode(savedMode);
}

// Update view mode according to selected menu
void GUI_App::update_mode(const int mode)
{
    sidebar().update_mode(mode);

    //BBS: GUI refactor
    if (mainframe->m_param_panel)
        mainframe->m_param_panel->update_mode();
    if (mainframe->m_param_dialog)
        mainframe->m_param_dialog->panel()->update_mode();
    mainframe->m_webview->update_mode();
    if (mainframe->m_topbar)
        mainframe->m_topbar->update_mode(mode);

#ifdef _MSW_DARK_MODE
    if (!wxGetApp().tabs_as_menu())
        dynamic_cast<Notebook*>(mainframe->m_tabpanel)->UpdateMode();
#endif

    for (auto tab : tabs_list) {
        tab->update_mode();
        if (tab->type() == Preset::TYPE_PRINT) {
            if (mode == comSimple) {
                std::string selectedItemName = plater()->get_process_toolbar().get_selected_item();
                if (selectedItemName == "Speed" || selectedItemName.empty()) {
                    auto item = plater()->get_process_toolbar().get_item(L("Speed"), false);
                    if (item) {
                        item->set_state(ProcessBar::GLToolbarItem::EState::Normal);
                    }
                    plater()->get_process_toolbar().select_item(L("Quality"));
                    tab->select_item(L("Quality"));
                }
            }
        }
    }
    for (auto tab : model_tabs_list)
        tab->update_mode();

    //BBS plater()->update_menus();

    plater()->canvas3D()->update_gizmos_on_off_state();
    plater()->update_mode();
}

void GUI_App::update_internal_development() {
    mainframe->m_webview->update_mode();
}

void GUI_App::open_preferences(size_t open_on_tab, const std::string& highlight_option)
{
    bool app_layout_changed = false;
    {
        // the dialog needs to be destroyed before the call to recreate_GUI()
        // or sometimes the application crashes into wxDialogBase() destructor
        // so we put it into an inner scope
        PreferencesDialog dlg(mainframe, open_on_tab, highlight_option);
        dlg.ShowModal();
        this->plater_->get_current_canvas3D()->force_set_focus();
        // BBS
        //app_layout_changed = dlg.settings_layout_changed();
#if ENABLE_GCODE_LINES_ID_IN_H_SLIDER
        if (dlg.seq_top_layer_only_changed() || dlg.seq_seq_top_gcode_indices_changed())
#else
        if (dlg.seq_top_layer_only_changed())
#endif // ENABLE_GCODE_LINES_ID_IN_H_SLIDER
            this->plater_->refresh_print();
#ifdef _WIN32
        if (is_editor()) {
            if (app_config->get("associate_3mf") == "true")
                associate_files(L"3mf");
            if (app_config->get("associate_stl") == "true")
                associate_files(L"stl");
            if (app_config->get("associate_step") == "true") {
                associate_files(L"step");
                associate_files(L"stp");
            }
            associate_url(L"crealityprint");
        }
        else {
            if (app_config->get("associate_gcode") == "true")
                associate_files(L"gcode");
        }
#endif // _WIN32
    }

    // BBS
    /*
    if (app_layout_changed) {
        // hide full main_sizer for mainFrame
        mainframe->GetSizer()->Show(false);
        mainframe->update_layout();
        mainframe->select_tab(size_t(0));
    }*/
}

void GUI_App::open_config_relate(size_t open_on_tab, const std::string& highlight_option)
{
    bool app_layout_changed = false;
    {
        // the dialog needs to be destroyed before the call to recreate_GUI()
        // or sometimes the application crashes into wxDialogBase() destructor
        // so we put it into an inner scope
        // PreferencesDialog dlg(mainframe, open_on_tab, highlight_option);
        ConfigRelateDialog dlg(mainframe, open_on_tab, highlight_option);
        dlg.ShowModal();
        this->plater_->get_current_canvas3D()->force_set_focus();
        // BBS
        // app_layout_changed = dlg.settings_layout_changed();
#if ENABLE_GCODE_LINES_ID_IN_H_SLIDER
        if (dlg.seq_top_layer_only_changed() || dlg.seq_seq_top_gcode_indices_changed())
#else
        if (dlg.seq_top_layer_only_changed())
#endif // ENABLE_GCODE_LINES_ID_IN_H_SLIDER
            this->plater_->refresh_print();
#ifdef _WIN32
        if (is_editor()) {
            if (app_config->get("associate_3mf") == "true")
                associate_files(L"3mf");
            if (app_config->get("associate_stl") == "true")
                associate_files(L"stl");
            if (app_config->get("associate_step") == "true") {
                associate_files(L"step");
                associate_files(L"stp");
            }
            associate_url(L"crealityprint");
        } else {
            if (app_config->get("associate_gcode") == "true")
                associate_files(L"gcode");
        }
#endif // _WIN32
    }

    // BBS
    /*
    if (app_layout_changed) {
        // hide full main_sizer for mainFrame
        mainframe->GetSizer()->Show(false);
        mainframe->update_layout();
        mainframe->select_tab(size_t(0));
    }*/
}

bool GUI_App::has_unsaved_preset_changes() const
{
    PrinterTechnology printer_technology = preset_bundle->printers.get_edited_preset().printer_technology();
    for (const Tab* const tab : tabs_list) {
        if (tab->supports_printer_technology(printer_technology) && tab->saved_preset_is_dirty())
            return true;
    }
    return false;
}

bool GUI_App::has_current_preset_changes() const
{
    PrinterTechnology printer_technology = preset_bundle->printers.get_edited_preset().printer_technology();
    for (const Tab* const tab : tabs_list) {
        if (tab->supports_printer_technology(printer_technology) && tab->current_preset_is_dirty())
            return true;
    }
    return false;
}

void GUI_App::update_saved_preset_from_current_preset()
{
    PrinterTechnology printer_technology = preset_bundle->printers.get_edited_preset().printer_technology();
    for (Tab* tab : tabs_list) {
        if (tab->supports_printer_technology(printer_technology))
            tab->update_saved_preset_from_current_preset();
    }
}

std::vector<std::pair<unsigned int, std::string>> GUI_App::get_selected_presets() const
{
    std::vector<std::pair<unsigned int, std::string>> ret;
    PrinterTechnology printer_technology = preset_bundle->printers.get_edited_preset().printer_technology();
    for (Tab* tab : tabs_list) {
        if (tab->supports_printer_technology(printer_technology)) {
            const PresetCollection* presets = tab->get_presets();
            ret.push_back({ static_cast<unsigned int>(presets->type()), presets->get_selected_preset_name() });
        }
    }
    return ret;
}

// To notify the user whether he is aware that some preset changes will be lost,
// UnsavedChangesDialog: "Discard / Save / Cancel"
// This is called when:
// - Close Application & Current project isn't saved
// - Load Project      & Current project isn't saved
// - Undo / Redo with change of print technologie
// - Loading snapshot
// - Loading config_file/bundle
// UnsavedChangesDialog: "Don't save / Save / Cancel"
// This is called when:
// - Exporting config_bundle
// - Taking snapshot
bool GUI_App::check_and_save_current_preset_changes(const wxString& caption, const wxString& header, bool remember_choice/* = true*/, bool dont_save_insted_of_discard/* = false*/)
{
    if (has_current_preset_changes()) {
        int act_buttons = ActionButtons::SAVE;
        if (dont_save_insted_of_discard)
            act_buttons |= ActionButtons::DONT_SAVE;
        if (remember_choice)
            act_buttons |= ActionButtons::REMEMBER_CHOISE;
        UnsavedChangesDialog dlg(caption, header, "", act_buttons);
        if (dlg.ShowModal() == wxID_CANCEL)
            return false;

        if (dlg.discard()) {
            PrinterTechnology printer_technology = preset_bundle->printers.get_edited_preset().printer_technology();
            for (const Tab* const tab : tabs_list) {
                if (tab->supports_printer_technology(printer_technology) && tab->current_preset_is_dirty())
                    tab->m_presets->discard_current_changes();
            }
            load_current_presets(false);
        }
        else if (dlg.save_preset())  // save selected changes
        {
            //BBS: add project embedded preset relate logic
            for (const UnsavedChangesDialog::PresetData& nt : dlg.get_names_and_types())
                preset_bundle->save_changes_for_preset(nt.name, nt.type, dlg.get_unselected_options(nt.type), nt.save_to_project);
            //for (const std::pair<std::string, Preset::Type>& nt : dlg.get_names_and_types())
            //    preset_bundle->save_changes_for_preset(nt.first, nt.second, dlg.get_unselected_options(nt.second));

            load_current_presets(false);

            // if we saved changes to the new presets, we should to
            // synchronize config.ini with the current selections.
            preset_bundle->export_selections(*app_config);

            //MessageDialog(nullptr, _L_PLURAL("Modifications to the preset have been saved",
            //                                 "Modifications to the presets have been saved", dlg.get_names_and_types().size())).ShowModal();
        }
    }

    return true;
}

void GUI_App::apply_keeped_preset_modifications()
{
    PrinterTechnology printer_technology = preset_bundle->printers.get_edited_preset().printer_technology();
    for (Tab* tab : tabs_list) {
        if (tab->supports_printer_technology(printer_technology))
            tab->apply_config_from_cache();
    }
    load_current_presets(false);
}

// This is called when creating new project or load another project
// OR close ConfigWizard
// to ask the user what should we do with unsaved changes for presets.
// New Project          => Current project is saved    => UnsavedChangesDialog: "Keep / Discard / Cancel"
//                      => Current project isn't saved => UnsavedChangesDialog: "Keep / Discard / Save / Cancel"
// Close ConfigWizard   => Current project is saved    => UnsavedChangesDialog: "Keep / Discard / Save / Cancel"
// Note: no_nullptr postponed_apply_of_keeped_changes indicates that thie function is called after ConfigWizard is closed
bool GUI_App::check_and_keep_current_preset_changes(const wxString& caption, const wxString& header, int action_buttons, bool* postponed_apply_of_keeped_changes/* = nullptr*/)
{
    if (has_current_preset_changes()) {
        bool is_called_from_configwizard = postponed_apply_of_keeped_changes != nullptr;

        UnsavedChangesDialog dlg(caption, header, "", action_buttons);
        if (dlg.ShowModal() == wxID_CANCEL)
            return false;

        auto reset_modifications = [this, is_called_from_configwizard]() {
            //if (is_called_from_configwizard)
            //    return; // no need to discared changes. It will be done fromConfigWizard closing

            PrinterTechnology printer_technology = preset_bundle->printers.get_edited_preset().printer_technology();
            for (const Tab* const tab : tabs_list) {
                if (tab->supports_printer_technology(printer_technology) && tab->current_preset_is_dirty())
                    tab->m_presets->discard_current_changes();
            }
            load_current_presets(false);
        };

        if (dlg.discard())
            reset_modifications();
        else  // save selected changes
        {
            //BBS: add project embedded preset relate logic
            const auto& preset_names_and_types = dlg.get_names_and_types();
            if (dlg.save_preset()) {
                for (const UnsavedChangesDialog::PresetData& nt : preset_names_and_types)
                    preset_bundle->save_changes_for_preset(nt.name, nt.type, dlg.get_unselected_options(nt.type), nt.save_to_project);

                // if we saved changes to the new presets, we should to
                // synchronize config.ini with the current selections.
                preset_bundle->export_selections(*app_config);

                //wxString text = _L_PLURAL("Modifications to the preset have been saved",
                //    "Modifications to the presets have been saved", preset_names_and_types.size());
                //if (!is_called_from_configwizard)
                //    text += "\n\n" + _L("All modifications will be discarded for new project.");

                //MessageDialog(nullptr, text).ShowModal();
                reset_modifications();
            }
            else if (dlg.transfer_changes() && (dlg.has_unselected_options() || is_called_from_configwizard)) {
                // execute this part of code only if not all modifications are keeping to the new project
                // OR this function is called when ConfigWizard is closed and "Keep modifications" is selected
                for (const UnsavedChangesDialog::PresetData& nt : preset_names_and_types) {
                    Preset::Type type = nt.type;
                    Tab* tab = get_tab(type);
                    std::vector<std::string> selected_options = dlg.get_selected_options(type);
                    if (type == Preset::TYPE_PRINTER) {
                        auto it = std::find(selected_options.begin(), selected_options.end(), "extruders_count");
                        if (it != selected_options.end()) {
                            // erase "extruders_count" option from the list
                            selected_options.erase(it);
                            // cache the extruders count
                            static_cast<TabPrinter*>(tab)->cache_extruder_cnt();
                        }
                    }
                    std::vector<std::string> selected_options2;
                    std::transform(selected_options.begin(), selected_options.end(), std::back_inserter(selected_options2), [](auto & o) {
                        auto i = o.find('#');
                        return i != std::string::npos ? o.substr(0, i) : o;
                    });
                    tab->cache_config_diff(selected_options2);
                    if (!is_called_from_configwizard)
                        tab->m_presets->discard_current_changes();
                }
                if (is_called_from_configwizard)
                    *postponed_apply_of_keeped_changes = true;
                else
                    apply_keeped_preset_modifications();
            }
        }
    }

    return true;
}

bool GUI_App::can_load_project()
{
    return true;
}

bool GUI_App::check_print_host_queue()
{
    wxString dirty;
    std::vector<std::pair<std::string, std::string>> jobs;
    // Get ongoing jobs from dialog
    mainframe->m_printhost_queue_dlg->get_active_jobs(jobs);
    if (jobs.empty())
        return true;
    // Show dialog
    wxString job_string = wxString();
    for (const auto& job : jobs) {
        job_string += format_wxstr("   %1% : %2% \n", job.first, job.second);
    }
    wxString message;
    message += _(L("The uploads are still ongoing")) + ":\n\n" + job_string +"\n" + _(L("Stop them and continue anyway?"));
    //wxMessageDialog dialog(mainframe,
    MessageDialog dialog(mainframe,
        message,
        wxString(SLIC3R_APP_NAME) + " - " + _(L("Ongoing uploads")),
        wxICON_QUESTION | wxYES_NO | wxNO_DEFAULT);
    if (dialog.ShowModal() == wxID_YES)
        return true;

    // TODO: If already shown, bring forward
    mainframe->m_printhost_queue_dlg->Show();
    return false;
}

bool GUI_App::checked_tab(Tab* tab)
{
    bool ret = true;
    if (find(tabs_list.begin(), tabs_list.end(), tab) == tabs_list.end() &&
        find(model_tabs_list.begin(), model_tabs_list.end(), tab) == model_tabs_list.end())
        ret = false;
    return ret;
}

// Update UI / Tabs to reflect changes in the currently loaded presets
//BBS: add preset combo box re-activate logic
void GUI_App::load_current_presets(bool active_preset_combox/*= false*/, bool check_printer_presets_ /*= true*/)
{
    // check printer_presets for the containing information about "Print Host upload"
    // and create physical printer from it, if any exists
    if (check_printer_presets_)
        check_printer_presets();

    PrinterTechnology printer_technology = preset_bundle->printers.get_edited_preset().printer_technology();
	this->plater()->set_printer_technology(printer_technology);
    for (Tab *tab : tabs_list)
		if (tab->supports_printer_technology(printer_technology)) {
			if (tab->type() == Preset::TYPE_PRINTER) {
				static_cast<TabPrinter*>(tab)->update_pages();
				// Mark the plater to update print bed by tab->load_current_preset() from Plater::on_config_change().
				this->plater()->force_print_bed_update();
			}
			tab->load_current_preset();
			//BBS: add preset combox re-active logic
			if (active_preset_combox)
				tab->reactive_preset_combo_box();
		}
    // BBS: model config
    for (Tab *tab : model_tabs_list)
		if (tab->supports_printer_technology(printer_technology)) {
            tab->request_page_tree_rebuild();
        }
}

bool GUI_App::OnExceptionInMainLoop()
{
    generic_exception_handle();
    return false;
}

#ifdef __APPLE__
// This callback is called from wxEntry()->wxApp::CallOnInit()->NSApplication run
// that is, before GUI_App::OnInit(), so we have a chance to switch GUI_App
// to a G-code viewer.
void GUI_App::OSXStoreOpenFiles(const wxArrayString &fileNames)
{
    //BBS: remove GCodeViewer as seperate APP logic
    /*size_t num_gcodes = 0;
    for (const wxString &filename : fileNames)
        if (is_gcode_file(into_u8(filename)))
            ++ num_gcodes;
    if (fileNames.size() == num_gcodes) {
        // Opening PrusaSlicer by drag & dropping a G-Code onto CrealityPrint icon in Finder,
        // just G-codes were passed. Switch to G-code viewer mode.
        m_app_mode = EAppMode::GCodeViewer;
        unlock_lockfile(get_instance_hash_string() + ".lock", data_dir() + "/cache/");
        if(app_config != nullptr)
            delete app_config;
        app_config = nullptr;
        init_app_config();
    }*/
    wxApp::OSXStoreOpenFiles(fileNames);
}

void GUI_App::MacOpenURL(const wxString& url)
{
    if (url.empty())
        return;
    start_download(boost::nowide::narrow(url));
}

// wxWidgets override to get an event on open files.
void GUI_App::MacOpenFiles(const wxArrayString &fileNames)
{
    bool single_instance = app_config->get("app", "single_instance") == "true";
    if (m_post_initialized && !single_instance) {
        bool has3mf = false;
        std::vector<wxString> names;
        for (auto & n : fileNames) {
            has3mf |= n.EndsWith(".3mf");
            names.push_back(n);
        }
        if (has3mf) {
            start_new_slicer(names);
            return;
        }
    }
    std::vector<std::string> files;
    std::vector<wxString>    gcode_files;
    std::vector<wxString>    non_gcode_files;
    BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << ", open files, size " << fileNames.size();
    for (const auto& filename : fileNames) {
        if (is_gcode_file(into_u8(filename)))
            gcode_files.emplace_back(filename);
        else {
            files.emplace_back(into_u8(filename));
            non_gcode_files.emplace_back(filename);
        }
    }
    //BBS: remove GCodeViewer as seperate APP logic
    /*if (m_app_mode == EAppMode::GCodeViewer) {
        // Running in G-code viewer.
        // Load the first G-code into the G-code viewer.
        // Or if no G-codes, send other files to slicer.
        if (! gcode_files.empty())
            this->plater()->load_gcode(gcode_files.front());
        if (!non_gcode_files.empty())
            start_new_slicer(non_gcode_files, true);
    } else*/
    {
        if (! files.empty()) {
            if (m_post_initialized) {
                wxArrayString input_files;
                for (size_t i = 0; i < non_gcode_files.size(); ++i) {
                    input_files.push_back(non_gcode_files[i]);
                }
                this->plater()->load_files(input_files);
            } else {
                for (size_t i = 0; i < files.size(); ++i) {
                    this->init_params->input_files.emplace_back(files[i]);
                }
            }
        } else {
            if (m_post_initialized) {
                this->plater()->load_gcode(gcode_files.front());
            } else {
                this->init_params->input_gcode = true;
                this->init_params->input_files = { into_u8(gcode_files.front()) };
            }
        }
        /*for (const wxString &filename : gcode_files)
            start_new_gcodeviewer(&filename);*/
    }
}

#endif /* __APPLE */

Sidebar& GUI_App::sidebar()
{
    return plater_->sidebar();
}

GizmoObjectManipulation *GUI_App::obj_manipul()
{
    // If this method is called before plater_ has been initialized, return nullptr (to avoid a crash)
    return (plater_ != nullptr) ? &plater_->get_view3D_canvas3D()->get_gizmos_manager().get_object_manipulation() : nullptr;
}

ObjectSettings* GUI_App::obj_settings()
{
    return sidebar().obj_settings();
}

ObjectList* GUI_App::obj_list()
{
    return sidebar().obj_list();
}

ObjectLayers* GUI_App::obj_layers()
{
    return sidebar().obj_layers();
}

Plater* GUI_App::plater()
{
    return plater_;
}

const Plater* GUI_App::plater() const
{
    return plater_;
}

ParamsPanel* GUI_App::params_panel()
{
    if (mainframe)
        return mainframe->m_param_panel;
    return nullptr;
}

ParamsDialog* GUI_App::params_dialog()
{
    if (mainframe)
        return mainframe->m_param_dialog;
    return nullptr;
}

Model& GUI_App::model()
{
    return plater_->model();
}

Downloader* GUI_App::downloader()
{
    return m_downloader.get();
}

std::string GUI_App::url_decode(std::string value) {
    return Http::url_decode(value);
}

std::string GUI_App::url_encode(std::string value) {
    return Http::url_encode(value);
}

void GUI_App::SaveProfile(json profileJson)
{
    if (!m_appconfig_new)
        return;

    app_config->set(std::string("firstguide"), "finish", "1");

    app_config->save();

    BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << " begin";

    // set filaments to app_config
    const std::string&                 section_name = AppConfig::SECTION_FILAMENTS;
    std::map<std::string, std::string> section_new;
    m_appconfig_new->clear_section(section_name);
    for (auto it = profileJson["filament"].begin(); it != profileJson["filament"].end(); ++it) {
        if (it.value()["selected"] == 1) {
            section_new[it.key()] = "true";
        }
    }
    m_appconfig_new->set_section(section_name, section_new);
    // set vendors to app_config
    Slic3r::AppConfig::VendorMap empty_vendor_map;
    m_appconfig_new->set_vendors(empty_vendor_map);
    for (auto it = profileJson["model"].begin(); it != profileJson["model"].end(); ++it) {
        if (it.value().is_object()) {
            json        temp_model  = it.value();
            std::string model_name  = temp_model["model"];
            std::string vendor_name = temp_model["vendor"];
            std::string selected    = temp_model["nozzle_selected"];
            boost::trim(selected);
            std::string nozzle;
            while (selected.size() > 0) {
                auto pos = selected.find(';');
                if (pos != std::string::npos) {
                    nozzle = selected.substr(0, pos);
                    m_appconfig_new->set_variant(vendor_name, model_name, nozzle, "true");
                    BOOST_LOG_TRIVIAL(info) << __FUNCTION__
                                            << boost::format("vendor_name %1%, model_name %2%, nozzle %3% selected") % vendor_name %
                                                   model_name % nozzle;
                    selected = selected.substr(pos + 1);
                    boost::trim(selected);
                } else {
                    m_appconfig_new->set_variant(vendor_name, model_name, selected, "true");
                    BOOST_LOG_TRIVIAL(info) << __FUNCTION__
                                            << boost::format("vendor_name %1%, model_name %2%, nozzle %3% selected") % vendor_name %
                                                   model_name % selected;
                    break;
                }
            }
        }
    }

    save_user_default_filaments(m_appconfig_new);

    return;
}

bool GUI_App::apply_config(AppConfig* app_config, AppConfig* app_config_new, PresetBundle* preset_bundle, const PresetUpdater* updater, bool& apply_keeped_changes)
{
    if(!app_config_new)
        return false;
    const auto enabled_vendors     = app_config_new->vendors();
    const auto old_enabled_vendors = app_config->vendors();

    const auto enabled_filaments     = app_config_new->has_section(AppConfig::SECTION_FILAMENTS) ?
                                           app_config_new->get_section(AppConfig::SECTION_FILAMENTS) :
                                           std::map<std::string, std::string>();
    const auto old_enabled_filaments = app_config->has_section(AppConfig::SECTION_FILAMENTS) ?
                                           app_config->get_section(AppConfig::SECTION_FILAMENTS) :
                                           std::map<std::string, std::string>();

    bool                     check_unsaved_preset_changes = false;
    std::vector<std::string> install_bundles;
    std::vector<std::string> remove_bundles;
    const auto               vendor_dir = (boost::filesystem::path(Slic3r::data_dir()) / PRESET_SYSTEM_DIR).make_preferred();
    for (const auto& it : enabled_vendors) {
        if (it.second.size() > 0) {
            auto vendor_file = vendor_dir / (it.first + ".json");
            if (!fs::exists(vendor_file)) {
                install_bundles.emplace_back(it.first);
            }
        }
    }

    // add the removed vendor bundles
    for (const auto& it : old_enabled_vendors) {
        if (it.second.size() > 0) {
            if (enabled_vendors.find(it.first) != enabled_vendors.end())
                continue;
            auto vendor_file = vendor_dir / (it.first + ".json");
            if (fs::exists(vendor_file)) {
                remove_bundles.emplace_back(it.first);
            }
        }
    }

    check_unsaved_preset_changes = (enabled_vendors != old_enabled_vendors) || (enabled_filaments != old_enabled_filaments);
    wxString header              = _L("The configuration package is changed in previous Config Guide");
    wxString caption             = _L("Configuration package changed");
    int      act_btns            = ActionButtons::KEEP | ActionButtons::SAVE;

    if (check_unsaved_preset_changes && !wxGetApp().check_and_keep_current_preset_changes(caption, header, act_btns, &apply_keeped_changes))
        return false;

    if (install_bundles.size() > 0) {
        // Install bundles from resources.
        // Don't create snapshot - we've already done that above if applicable.
        if (!updater->install_bundles_rsrc(std::move(install_bundles), false))
            return false;
    } else {
        BOOST_LOG_TRIVIAL(info) << "No bundles need to be installed from resource directory";
    }

    std::string       preferred_model;
    std::string       preferred_variant;
    PrinterTechnology preferred_pt   = ptFFF;
    auto get_preferred_printer_model = [preset_bundle, enabled_vendors, old_enabled_vendors, preferred_pt](const std::string& bundle_name,
                                                                                                           std::string&       variant) {
        const auto config = enabled_vendors.find(bundle_name);
        if (config == enabled_vendors.end())
            return std::string();

        const std::map<std::string, std::set<std::string>>& model_maps = config->second;
        // for (const auto& vendor_profile : preset_bundle->vendors) {
        for (const auto model_it : model_maps) {
            if (model_it.second.size() > 0) {
                // Prefer the standard nozzle when selecting a newly added printer.
                const auto default_variant = model_it.second.find("0.4");
                variant = default_variant != model_it.second.end() ? *default_variant : *model_it.second.begin();
                const auto config_old = old_enabled_vendors.find(bundle_name);
                if (config_old == old_enabled_vendors.end())
                    return model_it.first;
                const auto model_it_old = config_old->second.find(model_it.first);
                if (model_it_old == config_old->second.end())
                    return model_it.first;
                else if (model_it_old->second != model_it.second) {
                    for (const auto& var : model_it.second)
                        if (model_it_old->second.find(var) == model_it_old->second.end()) {
                            variant = var;
                            return model_it.first;
                        }
                }
            }
        }
        //}
        if (!variant.empty())
            variant.clear();
        return std::string();
    };
    // Prusa printers are considered first, then 3rd party.
    if (preferred_model = get_preferred_printer_model(PresetBundle::BBL_BUNDLE, preferred_variant); preferred_model.empty()) {
        for (const auto& bundle : enabled_vendors) {
            if (bundle.first == PresetBundle::BBL_BUNDLE) {
                continue;
            }
            if (preferred_model = get_preferred_printer_model(bundle.first, preferred_variant); !preferred_model.empty())
                break;
        }
    }

    std::string first_added_filament;

    // update the app_config
    app_config->set_section(AppConfig::SECTION_FILAMENTS, enabled_filaments);
    app_config->set_vendors(*m_appconfig_new);
    app_config->set_userPresets(*m_appconfig_new);

    if (check_unsaved_preset_changes)
        preset_bundle->load_presets(*app_config, ForwardCompatibilitySubstitutionRule::EnableSilentDisableSystem,
                                    {preferred_model, preferred_variant, first_added_filament, std::string()});

    // Update the selections from the compatibilty.
    preset_bundle->export_selections(*app_config);

    return true;
}

void GUI_App::run_script(wxString js)
{
    if (mainframe)
        return mainframe->RunScript(js);
}

Notebook* GUI_App::tab_panel() const
{
    if (mainframe)
        return mainframe->m_tabpanel;
    return nullptr;
}

NotificationManager * GUI_App::notification_manager()
{
    // During shutdown the Plater (and its pimpl) may already be destroyed while the
    // plater_ pointer is not yet null, so guard against use-after-free by also checking
    // the closing flag.
    if (m_is_closing || !plater_)
        return nullptr;
    return plater_->get_notification_manager();
}

// extruders count from edited printer preset
int GUI_App::extruders_edited_cnt() const
{
    const Preset& preset = preset_bundle->printers.get_edited_preset();
    return preset.printer_technology() == ptSLA ? 1 :
           preset.config.option<ConfigOptionFloats>("nozzle_diameter")->values.size();
}

// BBS
int GUI_App::filaments_cnt() const
{
    return preset_bundle->filament_presets.size();
}

PrintSequence GUI_App::global_print_sequence() const
{
    PrintSequence global_print_seq = PrintSequence::ByDefault;
    auto curr_preset_config = preset_bundle->prints.get_edited_preset().config;
    if (curr_preset_config.has("print_sequence"))
        global_print_seq = curr_preset_config.option<ConfigOptionEnum<PrintSequence>>("print_sequence")->value;
    return global_print_seq;
}

wxString GUI_App::current_language_code_safe() const
{
	// Translate the language code to a code, for which Prusa Research maintains translations.
	const std::map<wxString, wxString> mapping {
		{ "cs", 	"cs_CZ", },
		{ "sk", 	"cs_CZ", },
		{ "de", 	"de_DE", },
		{ "nl", 	"nl_NL", },
		{ "sv", 	"sv_SE", },
		{ "es", 	"es_ES", },
		{ "fr", 	"fr_FR", },
		{ "it", 	"it_IT", },
		{ "ja", 	"ja_JP", },
		{ "ko", 	"ko_KR", },
		{ "pl", 	"pl_PL", },
		{ "uk", 	"uk_UA", },
		{ "zh", 	"zh_CN", },
		{ "ru", 	"ru_RU", },
        { "tr", 	"tr_TR", },
        { "pt", 	"pt_BR", },
        { "vi",     "vi_VN", },
        { "id",     "id_ID", },
        { "th",     "th_TH", },
	};
	wxString language_code = this->current_language_code().BeforeFirst('_');
	auto it = mapping.find(language_code);
	if (it != mapping.end())
		language_code = it->second;
	else
		language_code = "en_US";
	return language_code;
}

wxString GUI_App::current_frontend_language_code() const
{
    const wxString language_code = this->current_language_code();

    // Chinese variants must be matched before removing the region because both share the "zh" prefix.
    if (language_code == "zh_TW")
        return "zh_TW";
    if (language_code == "zh_CN")
        return "zh_CN";

    const std::map<wxString, wxString> mapping {
        { "en", "en_GB" },
        { "ca", "ca_ES" },
        { "cs", "cs_CZ" },
        { "sk", "cs_CZ" },
        { "de", "de_DE" },
        { "es", "es_ES" },
        { "fr", "fr_FR" },
        { "hu", "hu_HU" },
        { "id", "id_ID" },
        { "it", "it_IT" },
        { "ja", "ja_JP" },
        { "ko", "ko_KR" },
        { "nl", "nl_NL" },
        { "pl", "pl_PL" },
        { "pt", "pt_BR" },
        { "ru", "ru_RU" },
        { "sv", "sv_SE" },
        { "th", "th_TH" },
        { "tr", "tr_TR" },
        { "uk", "uk_UA" },
        { "vi", "vi_VN" },
        { "zh", "zh_CN" },
    };

    const auto it = mapping.find(language_code.BeforeFirst('_'));
    return it != mapping.end() ? it->second : "en_GB";
}

// If we are switching from the FFF-preset to the SLA, we should to control the printed objects if they have a part(s).
// Because of we can't to print the multi-part objects with SLA technology.
bool GUI_App::may_switch_to_SLA_preset(const wxString& caption)
{
    if (model_has_multi_part_objects(model())) {
        // BBS: remove SLA related message
        return false;
    }
    return true;
}

bool GUI_App::run_wizard(ConfigWizard::RunReason reason, ConfigWizard::StartPage start_page)
{
    wxCHECK_MSG(mainframe != nullptr, false, "Internal error: Main frame not created / null");

    //auto wizard_t = new ConfigWizard(mainframe);
    //const bool res = wizard_t->run(reason, start_page);

    std::string strFinish = wxGetApp().app_config->get("firstguide", "finish");
    long        pStyle    = wxCAPTION | wxCLOSE_BOX | wxSYSTEM_MENU | wxRESIZE_BORDER;
    if (strFinish == "false" || strFinish.empty())
        pStyle = wxCAPTION | wxTAB_TRAVERSAL;


    const Preset& preset = preset_bundle->printers.get_selected_preset();
    if ((start_page == ConfigWizard::SP_FILAMENTS) && (preset.m_is_non_standard_printer))
    {
        MessageDialog msg(nullptr, _L("Selected model not a system model; no system consumables"), _L("Information"), wxOK);
        if (msg.ShowModal())
        {
            return false;
        }
    }

    GuideFrame wizard(this, pStyle);
    auto page = start_page == ConfigWizard::SP_FILAMENTS ? GuideFrame::BBL_FILAMENT_ONLY : GuideFrame::BBL_MODELS_ONLY;
    wizard.SetStartPage(page,false);
    bool       res = wizard.run();

    if (res) {
        load_current_presets();
        // BBS: remove SLA related message
    }

    return true;
}

void GUI_App::window_pos_save(wxTopLevelWindow* window, const std::string &name)
{
    if (name.empty()) { return; }
    const auto config_key = (boost::format("window_%1%") % name).str();

    WindowMetrics metrics = WindowMetrics::from_window(window);
    app_config->set(config_key, metrics.serialize());
    app_config->save();
}

bool GUI_App::window_pos_restore(wxTopLevelWindow* window, const std::string &name, bool default_maximized)
{
    if (name.empty()) { return false; }
    const auto config_key = (boost::format("window_%1%") % name).str();

    if (! app_config->has(config_key)) {
        //window->Maximize(default_maximized);
        return false;
    }

    auto metrics = WindowMetrics::deserialize(app_config->get(config_key));
    if (! metrics) {
        window->Maximize(default_maximized);
        return true;
    }

    const wxRect& rect = metrics->get_rect();
    window->SetPosition(rect.GetPosition());
    window->SetSize(rect.GetSize());
    window->Maximize(metrics->get_maximized());
    return true;
}

void GUI_App::window_pos_sanitize(wxTopLevelWindow* window)
{
    /*unsigned*/int display_idx = wxDisplay::GetFromWindow(window);
    wxRect display;
    if (display_idx == wxNOT_FOUND) {
        display = wxDisplay(0u).GetClientArea();
        window->Move(display.GetTopLeft());
    } else {
        display = wxDisplay(display_idx).GetClientArea();
    }

    auto metrics = WindowMetrics::from_window(window);
    metrics.sanitize_for_display(display);
    if (window->GetScreenRect() != metrics.get_rect()) {
        window->SetSize(metrics.get_rect());
    }
}

void GUI_App::window_pos_center(wxTopLevelWindow *window)
{
    /*unsigned*/int display_idx = wxDisplay::GetFromWindow(window);
    wxRect display;
    if (display_idx == wxNOT_FOUND) {
        display = wxDisplay(0u).GetClientArea();
        window->Move(display.GetTopLeft());
    } else {
        display = wxDisplay(display_idx).GetClientArea();
    }

    auto metrics = WindowMetrics::from_window(window);
    metrics.center_for_display(display);
    if (window->GetScreenRect() != metrics.get_rect()) {
        window->SetSize(metrics.get_rect());
    }
}

bool GUI_App::open_browser_with_warning_dialog(const wxString& url, int flags/* = 0*/)
{
    return wxLaunchDefaultBrowser(url, flags);
}

// static method accepting a wxWindow object as first parameter
// void warning_catcher{
//     my($self, $message_dialog) = @_;
//     return sub{
//         my $message = shift;
//         return if $message = ~/ GLUquadricObjPtr | Attempt to free unreferenced scalar / ;
//         my @params = ($message, 'Warning', wxOK | wxICON_WARNING);
//         $message_dialog
//             ? $message_dialog->(@params)
//             : Wx::MessageDialog->new($self, @params)->ShowModal;
//     };
// }

// Do we need this function???
// void GUI_App::notify(message) {
//     auto frame = GetTopWindow();
//     // try harder to attract user attention on OS X
//     if (!frame->IsActive())
//         frame->RequestUserAttention(defined(__WXOSX__/*&Wx::wxMAC */)? wxUSER_ATTENTION_ERROR : wxUSER_ATTENTION_INFO);
//
//     // There used to be notifier using a Growl application for OSX, but Growl is dead.
//     // The notifier also supported the Linux X D - bus notifications, but that support was broken.
//     //TODO use wxNotificationMessage ?
// }


#ifdef __WXMSW__
static bool set_into_win_registry(HKEY hkeyHive, const wchar_t* pszVar, const wchar_t* pszValue)
{
    // see as reference: https://stackoverflow.com/questions/20245262/c-program-needs-an-file-association
    wchar_t szValueCurrent[1000];
    DWORD dwType;
    DWORD dwSize = sizeof(szValueCurrent);

    int iRC = ::RegGetValueW(hkeyHive, pszVar, nullptr, RRF_RT_ANY, &dwType, szValueCurrent, &dwSize);

    bool bDidntExist = iRC == ERROR_FILE_NOT_FOUND;

    if ((iRC != ERROR_SUCCESS) && !bDidntExist)
        // an error occurred
        return false;

    if (!bDidntExist) {
        if (dwType != REG_SZ)
            // invalid type
            return false;

        if (::wcscmp(szValueCurrent, pszValue) == 0)
            // value already set
            return false;
    }

    DWORD dwDisposition;
    HKEY hkey;
    iRC = ::RegCreateKeyExW(hkeyHive, pszVar, 0, 0, 0, KEY_ALL_ACCESS, nullptr, &hkey, &dwDisposition);
    bool ret = false;
    if (iRC == ERROR_SUCCESS) {
        iRC = ::RegSetValueExW(hkey, L"", 0, REG_SZ, (BYTE*)pszValue, (::wcslen(pszValue) + 1) * sizeof(wchar_t));
        if (iRC == ERROR_SUCCESS)
            ret = true;
    }

    RegCloseKey(hkey);
    return ret;
}

static bool del_win_registry(HKEY hkeyHive, const wchar_t *pszVar, const wchar_t *pszValue)
{
    wchar_t szValueCurrent[1000];
    DWORD   dwType;
    DWORD   dwSize = sizeof(szValueCurrent);

    int iRC = ::RegGetValueW(hkeyHive, pszVar, nullptr, RRF_RT_ANY, &dwType, szValueCurrent, &dwSize);

    bool bDidntExist = iRC == ERROR_FILE_NOT_FOUND;

    if ((iRC != ERROR_SUCCESS) && !bDidntExist)
        return false;

    if (!bDidntExist) {
        DWORD dwDisposition;
        HKEY  hkey;
        iRC      = ::RegDeleteKeyExW(hkeyHive, pszVar, KEY_ALL_ACCESS, 0);
        if (iRC == ERROR_SUCCESS) {
            return true;
        }
    }

    return false;
}

#endif // __WXMSW__

void GUI_App::associate_files(std::wstring extend)
{
    return ;
#ifdef WIN32
    wchar_t app_path[MAX_PATH];
    ::GetModuleFileNameW(nullptr, app_path, sizeof(app_path));

    std::wstring prog_path = L"\"" + std::wstring(app_path) + L"\"";
    std::wstring prog_id = L" Creality.Slicer.1";
    std::wstring prog_desc = L"MentorSlicer";
    std::wstring prog_command = prog_path + L" \"%1\"";
    std::wstring reg_base = L"Software\\Classes";
    std::wstring reg_extension = reg_base + L"\\." + extend;
    std::wstring reg_prog_id = reg_base + L"\\" + prog_id;
    std::wstring reg_prog_id_command = reg_prog_id + L"\\Shell\\Open\\Command";

    bool is_new = false;
    is_new |= set_into_win_registry(HKEY_CURRENT_USER, reg_extension.c_str(), prog_id.c_str());
    is_new |= set_into_win_registry(HKEY_CURRENT_USER, reg_prog_id.c_str(), prog_desc.c_str());
    is_new |= set_into_win_registry(HKEY_CURRENT_USER, reg_prog_id_command.c_str(), prog_command.c_str());
    if (is_new)
        // notify Windows only when any of the values gets changed
        ::SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr);
#endif // WIN32
}

void GUI_App::disassociate_files(std::wstring extend)
{
    
#ifdef WIN32
    return;
    wchar_t app_path[MAX_PATH];
    ::GetModuleFileNameW(nullptr, app_path, sizeof(app_path));

    std::wstring prog_path = L"\"" + std::wstring(app_path) + L"\"";
    std::wstring prog_id = L" Creality.Slicer.1";
    std::wstring prog_desc = L"MentorSlicer";
    std::wstring prog_command = prog_path + L" \"%1\"";
    std::wstring reg_base = L"Software\\Classes";
    std::wstring reg_extension = reg_base + L"\\." + extend;
    std::wstring reg_prog_id = reg_base + L"\\" + prog_id;
    std::wstring reg_prog_id_command = reg_prog_id + L"\\Shell\\Open\\Command";

    bool is_new = false;
    is_new |= del_win_registry(HKEY_CURRENT_USER, reg_extension.c_str(), prog_id.c_str());

    bool is_associate_3mf  = app_config->get("associate_3mf") == "true";
    bool is_associate_stl  = app_config->get("associate_stl") == "true";
    bool is_associate_step = app_config->get("associate_step") == "true";
    if (!is_associate_3mf && !is_associate_stl && !is_associate_step)
    {
        is_new |= del_win_registry(HKEY_CURRENT_USER, reg_prog_id.c_str(), prog_desc.c_str());
        is_new |= del_win_registry(HKEY_CURRENT_USER, reg_prog_id_command.c_str(), prog_command.c_str());
    }

    if (is_new)
       ::SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr);
#endif // WIN32
}

void GUI_App::associate_url(std::wstring url_prefix)
{
#ifdef WIN32
    return ;
    boost::filesystem::path binary_path(boost::filesystem::canonical(boost::dll::program_location()));
    // the path to binary needs to be correctly saved in string with respect to localized characters
    wxString wbinary = wxString::FromUTF8(binary_path.string());
    std::string binary_string = (boost::format("%1%") % wbinary).str();
    BOOST_LOG_TRIVIAL(info) << "Downloader registration: Path of binary: " << binary_string;

    std::string key_string = "\"" + binary_string + "\" \"%1\"";

    wxRegKey key_first(wxRegKey::HKCU, "Software\\Classes\\" + url_prefix);
    wxRegKey key_full(wxRegKey::HKCU, "Software\\Classes\\" + url_prefix + "\\shell\\open\\command");
    if (!key_first.Exists()) {
        key_first.Create(false);
    }
    key_first.SetValue("URL Protocol", "");

    if (!key_full.Exists()) {
        key_full.Create(false);
    }
    key_full = key_string;
#elif defined(__linux__) && defined(SLIC3R_DESKTOP_INTEGRATION)
    DesktopIntegrationDialog::perform_downloader_desktop_integration(boost::nowide::narrow(url_prefix));
#endif // WIN32
}


void GUI_App::start_download(std::string url)
{
    std::cout << "start_download: "<<url;
    if (!plater_) {
        BOOST_LOG_TRIVIAL(error) << "Could not start URL download: plater is nullptr.";
        return;
    }
    if(boost::starts_with(url, "crealityprintlink://open?code=")||boost::starts_with(url, "crealityprintlink://open/?code="))
    {
        // Fork: these links carried a Creality Cloud code for the Home page, which no longer handles them.
        BOOST_LOG_TRIVIAL(info) << "start_download: crealityprintlink code link ignored";
        return;
    }
    //lets always init so if the download dest folder was changed, new dest is used
    boost::filesystem::path dest_folder(app_config->get("download_path"));
    if (!boost::filesystem::exists(dest_folder)) {
		    boost::filesystem::create_directory(dest_folder);
	    }
    if (dest_folder.empty() || !boost::filesystem::is_directory(dest_folder)) {
        std::string msg = _u8L("Could not start URL download. Destination folder is not set. Please choose destination folder in Configuration Wizard.");
        BOOST_LOG_TRIVIAL(error) << msg;
        show_error(nullptr, msg);
        return;
    }
    m_downloader->init(dest_folder);
    m_downloader->start_download(url);

}

bool is_soluble_filament(int extruder_id)
{
    auto& filament_presets = Slic3r::GUI::wxGetApp().preset_bundle->filament_presets;
    auto& filaments = Slic3r::GUI::wxGetApp().preset_bundle->filaments;

    if (extruder_id >= filament_presets.size()) return false;

    Slic3r::Preset* filament = filaments.find_preset(filament_presets[extruder_id]);
    if (filament == nullptr) return false;

    Slic3r::ConfigOptionBools* support_option = dynamic_cast<Slic3r::ConfigOptionBools*>(filament->config.option("filament_soluble"));
    if (support_option == nullptr) return false;

    return support_option->get_at(0);
};

bool has_filaments(const std::vector<string>& model_filaments) {
    auto& filament_presets = Slic3r::GUI::wxGetApp().preset_bundle->filament_presets;
    auto model_objects = Slic3r::GUI::wxGetApp().plater()->model().objects;
    const Slic3r::DynamicPrintConfig& config = wxGetApp().preset_bundle->full_config();
    Model::setExtruderParams(config, filament_presets.size());

    auto get_filament_name = [](int id) { return Model::extruderParamsMap.find(id) != Model::extruderParamsMap.end() ? Model::extruderParamsMap.at(id).materialName : "PLA"; };
    for (const ModelObject* mo : model_objects) {
        for (auto vol : mo->volumes) {
            auto ve = vol->get_extruders();
            for (auto id : ve) {
                auto name = get_filament_name(id);
                if (find(model_filaments.begin(), model_filaments.end(), name) != model_filaments.end()) return true;
            }
        }
    }
    return false;
}

bool is_support_filament(int extruder_id, bool strict_check)
{
    auto& filament_presets = Slic3r::GUI::wxGetApp().preset_bundle->filament_presets;
    auto& filaments = Slic3r::GUI::wxGetApp().preset_bundle->filaments;

    if (extruder_id >= filament_presets.size()) return false;

    Slic3r::Preset* filament = filaments.find_preset(filament_presets[extruder_id]);
    if (filament == nullptr) return false;

    std::string filament_type = filament->config.option<ConfigOptionStrings>("filament_type")->values[0];

    Slic3r::ConfigOptionBools* support_option = dynamic_cast<Slic3r::ConfigOptionBools*>(filament->config.option("filament_is_support"));

    if (!strict_check && (filament_type == "PETG" || filament_type == "PLA")) {
        std::vector<string> model_filaments;
        if (filament_type == "PETG")
            model_filaments.emplace_back("PLA");
        else {
            model_filaments = { "PETG", "TPU", "TPU-AMS" };
        }
        if (has_filaments(model_filaments)) return true;
    }
    if (support_option == nullptr) return false;
    return support_option->get_at(0);
};

bool GUI_App::LoadFile(std::string jPath, std::string& sContent)\
{
    try {
        boost::nowide::ifstream t(jPath);
        std::stringstream       buffer;
        buffer << t.rdbuf();
        sContent = buffer.str();
        BOOST_LOG_TRIVIAL(trace) << __FUNCTION__ << boost::format(", load %1% into buffer") % jPath;
    } catch (std::exception& e) {
        BOOST_LOG_TRIVIAL(error) << __FUNCTION__ << ",  got exception: " << e.what();
        return false;
    }

    return true;
}

int GUI_App::load_machine_preset_data()
{
    // Fork: the printer catalogue is read only from the bundled resources, never downloaded.
    const std::string vendor_preset_path = (boost::filesystem::path(Slic3r::resources_dir()) / PRESET_PROFILES_DIR /
                                            "Creality" / "machineList.json").make_preferred().string();
    if (!boost::filesystem::exists(vendor_preset_path)) {
        BOOST_LOG_TRIVIAL(error) << __FUNCTION__ << ": missing " << vendor_preset_path;
        return -1;
    }
    try {
        std::string contents;
        LoadFile(vendor_preset_path, contents);
        json jLocal = json::parse(contents);
        localize_printer_list_images(jLocal["printerList"],
                                     wxGetApp().get_server()->is_started() ? wxGetApp().get_server_port() : 0);
        nlohmann::json commandJson;
        commandJson["command"] = "get_machine_list";
        commandJson["data"]    = jLocal["printerList"];
        wxString strJS = wxString::Format("handleStudioCmd(%s)", commandJson.dump(-1, ' ', true));
        wxGetApp().CallAfter([this, strJS] { run_script(strJS.ToStdString()); });
    } catch (const std::exception& e) {
        BOOST_LOG_TRIVIAL(error) << __FUNCTION__ << ": parse " << vendor_preset_path << " failed: " << e.what();
        return -1;
    }
    return 0;
}

//mouse scheme
void GUI_App::on_interinstance_message(const std::string& msg)
{
    static constexpr const char* kPrefix = "CP_MOUSE_SCHEME=";
    if (msg.rfind(kPrefix, 0) == 0) { 
        int scheme = 0;
        try {
            scheme = std::stoi(msg.substr(std::strlen(kPrefix)));
        } catch (...) {
            return;
        }

        if (app_config)
            app_config->set("mouse_scheme", scheme ? "1" : "0");

        if (auto* plater = this->plater()) {
            auto apply = [&](Slic3r::GUI::GLCanvas3D* c) {
                if (!c)
                    return;
                c->set_mouse_scheme(scheme);
                if (auto* win = c->get_wxglcanvas()) {
                    win->Refresh(false);
#ifdef __WXMSW__
                    win->Update();
#endif
                }
            };
            apply(plater->get_view3D_canvas3D());
            apply(plater->get_preview_canvas3D());
            apply(plater->get_assmeble_canvas3D());
            apply(plater->get_current_canvas3D(false));
        }
    }
}

// Start-up refresh that used to run through the easy/Pro mode switch.
void GUI_App::refresh_after_startup()
{
    Plater* plater_ptr = plater();
    if (plater_ptr == nullptr)
        return;

    // Freeze the top-level frame so all sub-window repaints coalesce into a
    // single composite frame on Thaw.
    MainFrame* main_frame = mainframe;
    wxWindowUpdateLocker ui_locker(main_frame ? static_cast<wxWindow*>(main_frame) : static_cast<wxWindow*>(plater_ptr));

    // Sync the object list with the selected printer (the Pro branch of the old mode switch).
    if (ObjectList* list = obj_list())
        list->set_last_preset_name(preset_bundle->printers.get_selected_preset_name());
    plater_ptr->update();

    // Reload the parameter tabs so the field enable/disable linkage is
    // recomputed against the current config.
    for (Preset::Type type : {Preset::TYPE_PRINT, Preset::TYPE_FILAMENT, Preset::TYPE_PRINTER}) {
        if (Tab* tab = get_tab(type)) {
            tab->update_dirty();
            tab->reload_config();
            tab->update();
        }
    }

    if (main_frame != nullptr && main_frame->topbar() != nullptr)
        main_frame->topbar()->Refresh();
}

} // GUI
} //Slic3r
