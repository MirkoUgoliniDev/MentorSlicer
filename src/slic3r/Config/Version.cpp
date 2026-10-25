#include "Version.hpp"

#include <cctype>

#include "libslic3r/libslic3r.h"
#include "libslic3r/Config.hpp"
#include "libslic3r/Utils.hpp"

namespace Slic3r { 
namespace GUI {
namespace Config {


// Optimized lexicographic compare of two pre-release versions, ignoring the numeric suffix.
static int compare_prerelease(const char *p1, const char *p2)
{
	for (;;) {
		char c1 = *p1 ++;
		char c2 = *p2 ++;
		bool a1 = std::isalpha(c1) && c1 != 0;
		bool a2 = std::isalpha(c2) && c2 != 0;
		if (a1) {
			if (a2) {
				if (c1 != c2)
					return (c1 < c2) ? -1 : 1;
			} else
				return 1;
		} else {
			if (a2)
				return -1;
			else
				return 0;
		}
	}
	// This shall never happen.
	return 0;
}

bool Version::is_slic3r_supported(const Semver &slic3r_version) const
{ 
	if (! slic3r_version.in_range(min_slic3r_version, max_slic3r_version))
		return false;
	// Now verify, whether the configuration pre-release status is compatible with the Slic3r's pre-release status.
	// Alpha Slic3r will happily load any configuration, while beta Slic3r will ignore alpha configurations etc.
	const char *prerelease_slic3r = slic3r_version.prerelease();
	const char *prerelease_config = this->config_version.prerelease();
	if (prerelease_config == nullptr)
		// Released config is always supported.
		return true;
	else if (prerelease_slic3r == nullptr)
		// Released slic3r only supports released configs.
		return false;
	// Compare the pre-release status of Slic3r against the config.
	// If the prerelease status of slic3r is lexicographically lower or equal 
	// to the prerelease status of the config, accept it.
	return compare_prerelease(prerelease_slic3r, prerelease_config) != 1;
}

bool Version::is_current_slic3r_supported() const
{
	return this->is_slic3r_supported(Slic3r::SEMVER);
}

bool Version::is_current_slic3r_downgrade() const
{
	return Slic3r::SEMVER < min_slic3r_version;
}
#if 0
//TODO: This test should be moved to a unit test, once we have C++ unit tests in place.
static int version_test()
{
	Version v;
	v.config_version 	 = *Semver::parse("1.1.2");
	v.min_slic3r_version = *Semver::parse("1.38.0");
	v.max_slic3r_version = Semver::inf();
	assert(v.is_slic3r_supported(*Semver::parse("1.38.0")));
	assert(! v.is_slic3r_supported(*Semver::parse("1.38.0-alpha")));
	assert(! v.is_slic3r_supported(*Semver::parse("1.37.0-alpha")));
	// Test the prerelease status.
	assert(v.is_slic3r_supported(*Semver::parse("1.39.0-alpha")));
	assert(v.is_slic3r_supported(*Semver::parse("1.39.0-alpha1")));
	assert(v.is_slic3r_supported(*Semver::parse("1.39.0-alpha1")));
	assert(v.is_slic3r_supported(*Semver::parse("1.39.0-beta")));
	assert(v.is_slic3r_supported(*Semver::parse("1.39.0-beta1")));
	assert(v.is_slic3r_supported(*Semver::parse("1.39.0-beta1")));
	assert(v.is_slic3r_supported(*Semver::parse("1.39.0-rc2")));
	assert(v.is_slic3r_supported(*Semver::parse("1.39.0")));
	v.config_version 	 = *Semver::parse("1.1.2-alpha");
	assert(v.is_slic3r_supported(*Semver::parse("1.39.0-alpha")));
	assert(v.is_slic3r_supported(*Semver::parse("1.39.0-alpha1")));
	assert(! v.is_slic3r_supported(*Semver::parse("1.39.0-beta")));
	assert(! v.is_slic3r_supported(*Semver::parse("1.39.0-beta1")));
	assert(! v.is_slic3r_supported(*Semver::parse("1.39.0-beta1")));
	assert(! v.is_slic3r_supported(*Semver::parse("1.39.0-rc2")));
	assert(! v.is_slic3r_supported(*Semver::parse("1.39.0")));
	v.config_version 	 = *Semver::parse("1.1.2-alpha1");
	assert(v.is_slic3r_supported(*Semver::parse("1.39.0-alpha")));
	assert(v.is_slic3r_supported(*Semver::parse("1.39.0-alpha1")));
	assert(! v.is_slic3r_supported(*Semver::parse("1.39.0-beta")));
	assert(! v.is_slic3r_supported(*Semver::parse("1.39.0-beta1")));
	assert(! v.is_slic3r_supported(*Semver::parse("1.39.0-beta1")));
	assert(! v.is_slic3r_supported(*Semver::parse("1.39.0-rc2")));
	assert(! v.is_slic3r_supported(*Semver::parse("1.39.0")));
	v.config_version 	 = *Semver::parse("1.1.2-beta");
	assert(v.is_slic3r_supported(*Semver::parse("1.39.0-alpha")));
	assert(v.is_slic3r_supported(*Semver::parse("1.39.0-alpha1")));
	assert(v.is_slic3r_supported(*Semver::parse("1.39.0-beta")));
	assert(v.is_slic3r_supported(*Semver::parse("1.39.0-beta1")));
	assert(v.is_slic3r_supported(*Semver::parse("1.39.0-beta1")));
	assert(! v.is_slic3r_supported(*Semver::parse("1.39.0-rc")));
	assert(! v.is_slic3r_supported(*Semver::parse("1.39.0-rc2")));
	assert(! v.is_slic3r_supported(*Semver::parse("1.39.0")));
	v.config_version 	 = *Semver::parse("1.1.2-rc");
	assert(v.is_slic3r_supported(*Semver::parse("1.39.0-alpha")));
	assert(v.is_slic3r_supported(*Semver::parse("1.39.0-alpha1")));
	assert(v.is_slic3r_supported(*Semver::parse("1.39.0-beta")));
	assert(v.is_slic3r_supported(*Semver::parse("1.39.0-beta1")));
	assert(v.is_slic3r_supported(*Semver::parse("1.39.0-beta1")));
	assert(v.is_slic3r_supported(*Semver::parse("1.39.0-rc")));
	assert(v.is_slic3r_supported(*Semver::parse("1.39.0-rc2")));
	assert(! v.is_slic3r_supported(*Semver::parse("1.39.0")));
	v.config_version 	 = *Semver::parse("1.1.2-rc2");
	assert(v.is_slic3r_supported(*Semver::parse("1.39.0-alpha")));
	assert(v.is_slic3r_supported(*Semver::parse("1.39.0-alpha1")));
	assert(v.is_slic3r_supported(*Semver::parse("1.39.0-beta")));
	assert(v.is_slic3r_supported(*Semver::parse("1.39.0-beta1")));
	assert(v.is_slic3r_supported(*Semver::parse("1.39.0-beta1")));
	assert(v.is_slic3r_supported(*Semver::parse("1.39.0-rc")));
	assert(v.is_slic3r_supported(*Semver::parse("1.39.0-rc2")));
	assert(! v.is_slic3r_supported(*Semver::parse("1.39.0")));
	// Test the upper boundary.
	v.config_version 	 = *Semver::parse("1.1.2");
	v.max_slic3r_version = *Semver::parse("1.39.3-beta1");
	assert(v.is_slic3r_supported(*Semver::parse("1.38.0")));
	assert(! v.is_slic3r_supported(*Semver::parse("1.38.0-alpha")));
	assert(! v.is_slic3r_supported(*Semver::parse("1.38.0-alpha1")));
	assert(! v.is_slic3r_supported(*Semver::parse("1.37.0-alpha")));
	return 0;
}
static int version_test_run = version_test();
#endif

} // namespace Config
} // namespace GUI
} // namespace Slic3r
