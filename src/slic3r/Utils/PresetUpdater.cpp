#include "PresetUpdater.hpp"

#include <algorithm>
#include <boost/filesystem/operations.hpp>
#include <boost/nowide/fstream.hpp>
#include <functional>
#include <unordered_map>
#include <ostream>
#include <utility>
#include <stdexcept>
#include <boost/format.hpp>
#include <boost/algorithm/string.hpp>
#include <boost/filesystem.hpp>
#include <boost/filesystem/fstream.hpp>
#include <boost/lexical_cast.hpp>
#include <boost/log/trivial.hpp>

#include <wx/app.h>
#include <wx/msgdlg.h>

#include "libslic3r/libslic3r.h"
#include "libslic3r/format.hpp"
#include "libslic3r/Utils.hpp"
#include "libslic3r/PresetBundle.hpp"
#include "libslic3r_version.h"
#include "slic3r/GUI/GUI.hpp"
#include "slic3r/GUI/GUI_App.hpp"
#include "slic3r/GUI/I18N.hpp"
#include "slic3r/GUI/ConfigWizard.hpp"
#include "slic3r/GUI/GUI_App.hpp"
#include "slic3r/GUI/Plater.hpp"
#include "slic3r/GUI/format.hpp"
#include "slic3r/GUI/NotificationManager.hpp"
#include "slic3r/Config/Version.hpp"
#include "slic3r/GUI/MarkdownTip.hpp"
#include "libslic3r/miniz_extension.hpp"
#include "slic3r/GUI/GUI_Utils.hpp"
#include "slic3r/GUI/MainFrame.hpp"
#include "libslic3r/common_header/common_header.h"
#include "libslic3r_version.h"

namespace fs = boost::filesystem;
using Slic3r::GUI::Config::Version;


// FIXME: Incompat bundle resolution doesn't deal with inherited user presets

namespace Slic3r {


void copy_file_fix(const fs::path &source, const fs::path &target)
{
	BOOST_LOG_TRIVIAL(debug) << format("PresetUpdater: Copying %1% -> %2%", source, target);
	std::string error_message;
	//CopyFileResult cfr = Slic3r::GUI::copy_file_gui(source.string(), target.string(), error_message, false);
	CopyFileResult cfr = copy_file(source.string(), target.string(), error_message, false);
	if (cfr != CopyFileResult::SUCCESS) {
		BOOST_LOG_TRIVIAL(error) << "Copying failed(" << cfr << "): " << error_message;
		throw Slic3r::CriticalException(GUI::format(
				_L("Copying of file %1% to %2% failed: %3%"),
				source, target, error_message));
	}
	// Permissions should be copied from the source file by copy_file(). We are not sure about the source
	// permissions, let's rewrite them with 644.
	static constexpr const auto perms = fs::owner_read | fs::owner_write | fs::group_read | fs::others_read;
	fs::permissions(target, perms);
}

struct Update
{
	fs::path source;
	fs::path target;

	Version version;
	std::string vendor;
	//BBS: use changelog string instead of url
	std::string change_log;
	std::string descriptions;
    // CrealityPrint: add file filter support
    std::function<bool(const std::string)> file_filter;

	bool forced_update;
	//BBS: add directory support
	bool is_directory {false};

	Update() {}
	//BBS: add directory support
	//BBS: use changelog string instead of url
	Update(fs::path &&source, fs::path &&target, const Version &version, std::string vendor, std::string changelog, std::string description, bool forced = false, bool is_dir = false)
		: source(std::move(source))
		, target(std::move(target))
		, version(version)
		, vendor(std::move(vendor))
		, change_log(std::move(changelog))
		, descriptions(std::move(description))
		, forced_update(forced)
		, is_directory(is_dir)
	{}

    Update(fs::path &&source, fs::path &&target, const Version &version, std::string vendor, std::string changelog, std::string description, std::function<bool(const std::string)> file_filter,  bool forced = false, bool is_dir = false)
		: source(std::move(source))
		, target(std::move(target))
		, version(version)
		, vendor(std::move(vendor))
		, change_log(std::move(changelog))
		, descriptions(std::move(description))
        , file_filter(file_filter)
		, forced_update(forced)
		, is_directory(is_dir)
	{}

	//BBS: add directory support
	void install() const
	{
	    if (is_directory) {
            copy_directory_recursively(source, target, file_filter);
        }
        else {
            copy_file_fix(source, target);
        }
	}

	friend std::ostream& operator<<(std::ostream& os, const Update &self)
	{
		os << "Update(" << self.source.string() << " -> " << self.target.string() << ')';
		return os;
	}
};

struct Incompat
{
	fs::path bundle;
	Version version;
	std::string vendor;
	//BBS: add directory support
	bool is_directory {false};

	Incompat(fs::path &&bundle, const Version &version, std::string vendor, bool is_dir = false)
		: bundle(std::move(bundle))
		, version(version)
		, vendor(std::move(vendor))
		, is_directory(is_dir)
	{}

	void remove() {
		// Remove the bundle file
		if (is_directory) {
			if (fs::exists(bundle))
                fs::remove_all(bundle);
		}
		else {
			if (fs::exists(bundle))
				fs::remove(bundle);
		}
	}

	friend std::ostream& operator<<(std::ostream& os , const Incompat &self) {
		os << "Incompat(" << self.bundle.string() << ')';
		return os;
	}
};

struct Updates
{
	std::vector<Incompat> incompats;
	std::vector<Update> updates;
};




struct PresetUpdater::priv
{
	fs::path rsrc_path;
	fs::path vendor_path;

    priv();

	//BBS: refine preset update logic
	bool install_bundles_rsrc(std::vector<std::string> bundles, bool snapshot) const;
	void check_installed_vendor_profiles() const;
	bool perform_updates(Updates &&updates, bool snapshot = true) const;
};

//BBS: change directories by design
PresetUpdater::priv::priv()
	: rsrc_path(fs::path(resources_dir()) / "profiles")
	, vendor_path(fs::path(Slic3r::data_dir()) / PRESET_SYSTEM_DIR)
{
	// Install indicies from resources. Only installs those that are either missing or older than in resources.
	check_installed_vendor_profiles();
}

bool PresetUpdater::priv::install_bundles_rsrc(std::vector<std::string> bundles, bool snapshot) const
{
	Updates updates;

	BOOST_LOG_TRIVIAL(info) << format("Installing %1% bundles from resources ...", bundles.size());

	for (const auto &bundle : bundles) {
		auto path_in_rsrc = (this->rsrc_path / bundle).replace_extension(".json");
		auto path_in_vendors = (this->vendor_path / bundle).replace_extension(".json");
		updates.updates.emplace_back(std::move(path_in_rsrc), std::move(path_in_vendors), Version(), bundle, "", "");

        //BBS: add directory support
        auto print_in_rsrc = this->rsrc_path / bundle;
		auto print_in_vendors = this->vendor_path / bundle;
        fs::path print_folder(print_in_vendors);
        if (fs::exists(print_folder))
            fs::remove_all(print_folder);
        fs::create_directories(print_folder);
		updates.updates.emplace_back(std::move(print_in_rsrc), std::move(print_in_vendors), Version(), bundle, "", "",[](const std::string name){
        // return false if name is end with .stl, case insensitive
        return boost::iends_with(name, ".stl") || boost::iends_with(name, ".png") || boost::iends_with(name, ".svg") ||
               boost::iends_with(name, ".jpeg") || boost::iends_with(name, ".jpg") || boost::iends_with(name, ".3mf");
        }, false, true);
	}

	return perform_updates(std::move(updates), snapshot);
}


//BBS: refine preset update logic
// Install indicies from resources. Only installs those that are either missing or older than in resources.
void PresetUpdater::priv::check_installed_vendor_profiles() const
{
    BOOST_LOG_TRIVIAL(info) << "[CrealityPrint Updater]:Checking whether the profile from resource is newer";

    AppConfig *app_config = GUI::wxGetApp().app_config;
    const auto enabled_vendors = app_config->vendors();

    //BBS: refine the init check logic
    std::vector<std::string> bundles;
    for (auto &dir_entry : boost::filesystem::directory_iterator(rsrc_path)) {
        const auto &path = dir_entry.path();
        std::string file_path = path.string();
        if (is_json_file(file_path)) {
            const auto path_in_vendor = vendor_path / path.filename();
            std::string vendor_name = path.filename().string();
            // Remove the .json suffix.
            vendor_name.erase(vendor_name.size() - 5);
            const bool is_core_vendor = (vendor_name == PresetBundle::BBL_BUNDLE);
            if ( fs::exists(path_in_vendor)) {
                if (enabled_vendors.find(vendor_name) != enabled_vendors.end()) {
                    Semver resource_ver = get_version_from_json(file_path);
                    Semver vendor_ver = get_version_from_json(path_in_vendor.string());

                    bool version_match = ((resource_ver.maj() == vendor_ver.maj()) && (resource_ver.min() == vendor_ver.min()));

                    if (!version_match || (vendor_ver < resource_ver)) {
                        BOOST_LOG_TRIVIAL(info) << "[CrealityPrint Updater]:found vendor "<<vendor_name<<" newer version "<<resource_ver.to_string() <<" from resource, old version "<<vendor_ver.to_string();
                        bundles.push_back(vendor_name);
                    }
                }
                else if (!is_core_vendor) {
                    //need to be removed because not installed
                    fs::remove(path_in_vendor);
                    const auto path_of_vendor = vendor_path / vendor_name;
                    if (fs::exists(path_of_vendor))
                        fs::remove_all(path_of_vendor);
                }
            }
            else if (is_core_vendor || (enabled_vendors.find(vendor_name) != enabled_vendors.end())) {//if vendor has no file, copy it from resource for BBL
                bundles.push_back(vendor_name);
            }
        }
    }

    if (bundles.size() > 0)
    {
        install_bundles_rsrc(bundles, false);
        //delete cache dir
        
        
        #ifdef __WXMAC__
            wxStandardPaths& stdPaths = wxStandardPaths::Get();
            wxString cacheBase = stdPaths.GetUserDir(wxStandardPaths::Dir_Cache);
            wxString appCacheDir = cacheBase + "/com.creality.crealityprint";
            if (wxDirExists(appCacheDir)) {
            if (!wxFileName::Rmdir(appCacheDir, wxPATH_RMDIR_RECURSIVE)) {
                BOOST_LOG_TRIVIAL(error) << "Failed to delete cache directory: " << appCacheDir;
                }
            }
        #endif
        #ifdef __linux__
            wxStandardPaths& stdPaths = wxStandardPaths::Get();
            wxString cacheBase = stdPaths.GetUserDir(wxStandardPaths::Dir_Cache);
            wxString appCacheDir = cacheBase + "/"+SLIC3R_APP_NAME;
            if (wxDirExists(appCacheDir)) {
            if (!wxFileName::Rmdir(appCacheDir, wxPATH_RMDIR_RECURSIVE)) {
                BOOST_LOG_TRIVIAL(error) << "Failed to delete cache directory: " << appCacheDir;
                }
            }
        #endif
       
        }   
}

//BBS: switch to new BBL.json configs
bool PresetUpdater::priv::perform_updates(Updates &&updates, bool snapshot) const
{
    //std::string vendor_path;
    //std::string vendor_name;
    if (updates.incompats.size() > 0) {
        BOOST_LOG_TRIVIAL(info) << format("[CrealityPrint Updater]:Deleting %1% incompatible bundles", updates.incompats.size());

        for (auto &incompat : updates.incompats) {
            BOOST_LOG_TRIVIAL(info) << '\t' << incompat;
            incompat.remove();
        }
    } else if (updates.updates.size() > 0) {

        BOOST_LOG_TRIVIAL(info) << format("[CrealityPrint Updater]:Performing %1% updates", updates.updates.size());

        for (const auto &update : updates.updates) {
            BOOST_LOG_TRIVIAL(info) << '\t' << update;

            update.install();
            //if (!update.is_directory) {
            //    vendor_path = update.source.parent_path().string();
            //    vendor_name = update.vendor;
            //}
        }

        //if (!vendor_path.empty()) {
        //    PresetBundle bundle;
        //    // Throw when parsing invalid configuration. Only valid configuration is supposed to be provided over the air.
        //    bundle.load_vendor_configs_from_json(vendor_path, vendor_name, PresetBundle::LoadConfigBundleAttribute::LoadSystem, ForwardCompatibilitySubstitutionRule::Disable);

        //    BOOST_LOG_TRIVIAL(info) << format("Deleting %1% conflicting presets", bundle.prints.size() + bundle.filaments.size() + bundle.printers.size());

        //    auto preset_remover = [](const Preset& preset) {
        //        BOOST_LOG_TRIVIAL(info) << '\t' << preset.file;
        //        fs::remove(preset.file);
        //    };

        //    for (const auto &preset : bundle.prints)    { preset_remover(preset); }
        //    for (const auto &preset : bundle.filaments) { preset_remover(preset); }
        //    for (const auto &preset : bundle.printers)  { preset_remover(preset); }
        //}
    }

    return true;
}

PresetUpdater::PresetUpdater() :
	p(new priv())
{}


// Public

PresetUpdater::~PresetUpdater() = default;

//BBS: add json related logic
bool PresetUpdater::install_bundles_rsrc(std::vector<std::string> bundles, bool snapshot) const
{
	return p->install_bundles_rsrc(bundles, snapshot);
}


}
