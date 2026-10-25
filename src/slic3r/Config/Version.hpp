#ifndef slic3r_GUI_ConfigIndex_
#define slic3r_GUI_ConfigIndex_

#include <string>
#include <vector>

#include <boost/filesystem/path.hpp>

#include "libslic3r/FileParserError.hpp"
#include "libslic3r/Semver.hpp"

namespace Slic3r { 
namespace GUI {
namespace Config {

// Configuration bundle version.
struct Version
{
	// Version of this config.
	Semver 		config_version     = Semver::invalid();
	// Minimum Slic3r version, for which this config is applicable.
	Semver 		min_slic3r_version = Semver::zero();
	// Maximum Slic3r version, for which this config is recommended.
	// Slic3r should read older configuration and upgrade to a newer format,
	// but likely there has been a better configuration published, using the new features.
	Semver 		max_slic3r_version = Semver::inf();
	// Single comment line.
	std::string comment;

	bool 		is_slic3r_supported(const Semver &slicer_version) const;
	bool 		is_current_slic3r_supported() const;
	bool 		is_current_slic3r_downgrade() const;
};

} // namespace Config
} // namespace GUI
} // namespace Slic3r

#endif /* slic3r_GUI_ConfigIndex_ */
