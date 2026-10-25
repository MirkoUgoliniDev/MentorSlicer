#ifndef slic3r_GUI_ThirdPartyLibraries_hpp_
#define slic3r_GUI_ThirdPartyLibraries_hpp_

#include <map>
#include <string>
#include <vector>

namespace Slic3r {
namespace GUI {

// Where the latest release of a library can be read online. Queried only when the user
// presses "Check for updates" in Help -> Third-party libraries.
struct LibraryVersionSource
{
    enum class Kind {
        None,          // no machine-readable release list (or a modified in-tree copy)
        GitHubRelease, // GET /repos/<project>/releases/latest, tag_name
        GitHubTags,    // GET /repos/<project>/git/matching-refs/tags/<tag_prefix>, highest match
        GitLabRelease, // GET gitlab.com /projects/<project>/releases, highest match
        NuGet,         // GET api.nuget.org flat container index, highest match
    };
    Kind        kind = Kind::None;
    std::string project;
    std::string tag_prefix;
    // Regex on the tag; the capture groups are the version components (pre-releases do not match).
    std::string pattern;
};

enum class LibraryPolicy {
    Info,   // listed only: no decision about updates
    Update, // keep it on the latest release (security relevant)
    Hold,   // newer releases are left out on purpose, see `reason`
    Watch,  // to re-check: update when the condition in `reason` is met
};

struct ThirdPartyLibrary
{
    std::string          name;
    std::string          version; // in the Windows build, empty if unknown
    std::string          link;
    const char*          purpose; // English, marked with L() and translated when shown
    LibraryPolicy        policy;
    const char*          reason;  // English, empty for Info/Update
    LibraryVersionSource source;
    std::string          reference_latest; // latest release at THIRD_PARTY_REFERENCE_DATE
};

// Date of the latest versions written in the table (the reference check).
extern const char* const THIRD_PARTY_REFERENCE_DATE;

const std::vector<ThirdPartyLibrary>& third_party_library_list();

// Numeric comparison of dotted versions ("1.6.59" > "1.6.9"); missing components count as 0.
int compare_library_versions(const std::string& a, const std::string& b);

struct LibraryCheckResult
{
    std::string latest; // empty if never found
    std::string error;  // last check failure (English, translated when shown), empty on success
};

// Results of the last online check, in <data_dir>/cache/third-party-libraries.json.
struct LibraryCheckCache
{
    std::string                               checked_at; // local time, empty if never checked
    std::map<std::string, LibraryCheckResult> results;    // by library name
};

LibraryCheckCache load_library_check_cache();
bool              save_library_check_cache(const LibraryCheckCache& cache);

// Blocking HTTPS query of one source; call it from a worker thread.
// `github_limited` is set when GitHub answers with its rate limit and makes later GitHub
// queries fail at once (60 requests per hour without an account).
LibraryCheckResult check_latest_library_version(const LibraryVersionSource& source, bool& github_limited);

} // namespace GUI
} // namespace Slic3r

#endif // slic3r_GUI_ThirdPartyLibraries_hpp_
