#include "ThirdPartyLibraries.hpp"

#include "I18N.hpp"
#include "libslic3r/Utils.hpp"
#include "slic3r/Utils/Http.hpp"

#include <algorithm>
#include <regex>

#include <boost/filesystem.hpp>
#include <boost/log/trivial.hpp>
#include <boost/nowide/fstream.hpp>
#include <nlohmann/json.hpp>

namespace Slic3r {
namespace GUI {

const char* const THIRD_PARTY_REFERENCE_DATE = "2026-10-25";

namespace {

using Kind = LibraryVersionSource::Kind;

// "v1.2.3", "1.2.3" or "1.2"
const char* const SEMVER = R"(^v?(\d+)\.(\d+)(?:\.(\d+))?$)";

LibraryVersionSource none() { return {}; }
LibraryVersionSource github(const char* project, const char* pattern = SEMVER) { return { Kind::GitHubRelease, project, "", pattern }; }
LibraryVersionSource github_tags(const char* project, const char* prefix, const char* pattern) { return { Kind::GitHubTags, project, prefix, pattern }; }

const char* const NO_REASON = "";
const char* const ENGINE_GEOMETRY = L("Slicing engine geometry: a newer version can change the G-code.");

std::vector<ThirdPartyLibrary> make_libraries()
{
    using P = LibraryPolicy;
    // Versions in use are those of the Windows build. Update reference_latest and
    // THIRD_PARTY_REFERENCE_DATE whenever the libraries are reviewed.
    return {
        { "Admesh", "", "https://admesh.readthedocs.io/", L("Repairs STL meshes (normals, holes) when models are loaded."), P::Info, NO_REASON, none(), "" },
        { "Anti-Grain Geometry", "2.4", "http://antigrain.com", L("2D rasterization of layer images for supports."), P::Info, NO_REASON, none(), "" },
        { "ArcWelderLib", "", "https://plugins.octoprint.org/plugins/arc_welder", L("Arc fitting: turns short G-code segments into arcs (G2/G3)."), P::Info, NO_REASON, none(), "" },
        { "Assimp", "6.0.5", "https://www.assimp.org", L("Imports GLB/glTF, FBX, DAE, 3DS, PLY and OFF models."), P::Update, NO_REASON, github("assimp/assimp"), "6.0.5" },
        { "Blosc", "1.17.0", "https://www.blosc.org", L("Data compression used by OpenVDB."), P::Hold, L("Patched fork used by OpenVDB: a newer version can change the results."), github("Blosc/c-blosc"), "1.21.6" },
        { "Boost", "1.84.0", "http://www.boost.org", L("General C++ building blocks (files, threads, network, text) used everywhere."), P::Hold, L("Updating it means rebuilding almost every other dependency; no relevant security issue."), github("boostorg/boost", R"(^boost-(\d+)\.(\d+)\.(\d+)$)"), "1.92.0" },
        { "Cereal", "1.3.0", "http://uscilab.github.io/cereal", L("Saves undo/redo snapshots and painted data."), P::Hold, L("Header-only, no useful changes in the newer release."), github("USCiLab/cereal"), "1.3.2" },
        { "CGAL", "5.4", "https://www.cgal.org", L("Exact geometry: mesh booleans (cut, merge, negative volumes)."), P::Hold, L("A newer version can change boolean results; if ever, follow OrcaSlicer (5.6.3)."), github("CGAL/cgal"), "6.2.1" },
        { "Clipper", "6.2.6", "http://www.angusj.co", L("2D polygon operations (offsets, intersections) of the slicing engine."), P::Hold, ENGINE_GEOMETRY, none(), "" },
        { "Clipper2", "1.5.2", "https://github.com/AngusJohnson/Clipper2", L("Newer 2D polygon library used by some engine features."), P::Hold, ENGINE_GEOMETRY, github("AngusJohnson/Clipper2", R"(^Clipper2_(\d+)\.(\d+)\.(\d+)$)"), "2.0.1" },
        { "Eigen3", "3.3.7", "http://eigen.tuxfamily.org", L("Linear algebra: vectors, matrices and transformations."), P::Hold, L("A newer version can change the G-code: update only together with OrcaSlicer."), { Kind::GitLabRelease, "libeigen%2Feigen", "", SEMVER }, "5.0.1" },
        { "Expat", "2.9.0", "http://www.libexpat.org", L("Reads the XML inside 3MF and AMF projects."), P::Update, NO_REASON, github("libexpat/libexpat", R"(^R_(\d+)_(\d+)_(\d+)$)"), "2.9.0" },
        { "fast_float", "", "https://github.com/fastfloat/fast_float", L("Fast number parsing (G-code, settings)."), P::Info, NO_REASON, none(), "" },
        { "FFmpeg", "8.1.3", "https://ffmpeg.org", L("Decodes the RTSP camera video of older Creality printers."), P::Watch, L("8.1 is the series of the monthly LGPL builds (BtbN) used by the fork: move on when a monthly build of the newer series exists."), github_tags("FFmpeg/FFmpeg", "n", R"(^n(\d+)\.(\d+)(?:\.(\d+))?$)"), "9.0.2" },
        { "FreeType", "2.15.0", "https://freetype.org", L("Renders fonts for text on models (Open Cascade)."), P::Update, NO_REASON, github_tags("freetype/freetype", "VER-", R"(^VER-(\d+)-(\d+)(?:-(\d+))?$)"), "2.15.0" },
        { "GLAD (OpenGL loader)", "2.0.8", "https://glad.dav1d.de", L("Loads the OpenGL functions of the 3D view."), P::Hold, L("No benefit from a newer version."), github("Dav1dde/glad"), "2.0.8" },
        { "GLEW", "2.2.0", "https://glew.sourceforge.net", L("OpenGL extension loader."), P::Info, NO_REASON, none(), "" },
        { "GLFW", "3.3.7", "https://www.glfw.org", L("OpenGL window library (dependency of the build)."), P::Hold, L("No benefit from a newer version."), github("glfw/glfw"), "3.5.1" },
        { "GMP", "5.0.1", "https://gmplib.org", L("Arbitrary-precision numbers for CGAL."), P::Hold, L("Precompiled, never reads user files."), none(), "" },
        { "GNU gettext", "", "https://www.gnu.org/software/gettext", L("Translations of the interface."), P::Info, NO_REASON, none(), "" },
        { "HIDAPI", "", "https://github.com/libusb/hidapi", L("USB devices such as the 3D mouse."), P::Info, NO_REASON, none(), "" },
        { "ImGUI", "1.83", "https://github.com/ocornut/imgui", L("Menus and tools drawn in the 3D view (gizmos, legend)."), P::Hold, L("Heavily modified by Bambu and Orca."), github("ocornut/imgui"), "1.93.0" },
        { "ImGuizmo", "", "https://github.com/CedricGuillemet/ImGuizmo", L("Move and rotate handles in the 3D view."), P::Info, NO_REASON, none(), "" },
        { "libcurl", "8.23.0", "https://curl.se/libcurl", L("HTTP/HTTPS connections (print hosts, this update check)."), P::Update, NO_REASON, github("curl/curl", R"(^curl-(\d+)_(\d+)_(\d+)$)"), "8.23.0" },
        { "Libigl", "", "https://libigl.github.io", L("Mesh geometry: picking and measuring in the 3D view."), P::Info, NO_REASON, none(), "" },
        { "libjpeg-turbo", "3.2.0", "https://libjpeg-turbo.org", L("JPEG images (textures, thumbnails)."), P::Update, NO_REASON, github("libjpeg-turbo/libjpeg-turbo"), "3.2.0" },
        { "libnest2d", "", "https://github.com/tamasmeszaros/libnest2d", L("Automatic arrangement of objects on the plate."), P::Info, NO_REASON, none(), "" },
        { "libnoise", "1.0", "https://github.com/qknight/libnoise", L("Noise patterns for fuzzy skin."), P::Info, NO_REASON, none(), "" },
        { "libpng", "1.6.59", "http://www.libpng.org", L("PNG images (icons, thumbnails, textures)."), P::Update, NO_REASON, github_tags("pnggroup/libpng", "v1.", R"(^v(\d+)\.(\d+)\.(\d+)$)"), "1.6.59" },
        { "libtiff (bundled with wxWidgets)", "", "https://libtiff.gitlab.io/libtiff", L("TIFF images in the interface toolkit."), P::Info, NO_REASON, none(), "" },
        { "lib_fts", "", "https://www.forrestthewoods.com", L("Fuzzy matching in the settings search."), P::Info, NO_REASON, none(), "" },
        { "MCUT", "1.2.0", "https://github.com/cutdigital/mcut", L("Mesh cutting (Cut tool)."), P::Hold, L("A newer version can change the cut results."), github("cutdigital/mcut"), "1.3.0" },
        { "Mesa 3D", "26.2.4", "https://mesa3d.org", L("Software OpenGL (--sw-renderer) for PCs without a working graphics driver."), P::Update, NO_REASON, github("pal1000/mesa-dist-win"), "26.2.4" },
        { "Microsoft Edge WebView2 SDK", "1.0.4314.35", "https://developer.microsoft.com/microsoft-edge/webview2", L("Web pages inside the app (Home, Device, send dialog)."), P::Update, NO_REASON, { Kind::NuGet, "microsoft.web.webview2", "", R"(^(\d+)\.(\d+)\.(\d+)\.(\d+)$)" }, "1.0.4314.35" },
        { "Miniz", "2.1.0", "https://github.com/richgel999/miniz", L("ZIP compression of 3MF files."), P::Hold, L("Modified by Bambu and Prusa."), github("richgel999/miniz"), "3.1.2" },
        { "MPFR", "3.0.0", "https://www.mpfr.org", L("Precise floating point for CGAL."), P::Hold, L("Precompiled, never reads user files."), none(), "" },
        { "Nanosvg", "", "https://github.com/memononen/nanosvg", L("SVG icons and SVG import."), P::Info, NO_REASON, none(), "" },
        { "nlohmann/json", "3.12.0", "https://json.nlohmann.me", L("Reads and writes JSON (profiles, web pages, Print Advisor)."), P::Update, NO_REASON, github("nlohmann/json"), "3.12.0" },
        { "NLopt", "2.5.0", "https://github.com/stevengj/nlopt", L("Numerical optimization (automatic arrangement and orientation)."), P::Hold, L("A newer version can change the automatic arrangement."), github("stevengj/nlopt"), "2.11.0" },
        { "Open Cascade", "7.6.0", "https://www.opencascade.com", L("Imports STEP CAD files and renders text on models."), P::Watch, L("2026 CVEs in the STEP/IGES/STL readers: update when a release with the fixes is confirmed (large update, STEP API changed)."), github("Open-Cascade-SAS/OCCT", R"(^V(\d+)[._](\d+)[._](\d+)$)"), "8.0.1" },
        { "OpenCV", "4.6.0", "https://opencv.org", L("Decodes the textures of imported models."), P::Hold, L("Built without TIFF (the exposed part); a newer version is a long rebuild with little benefit."), github("opencv/opencv"), "5.0.0" },
        { "OpenEXR", "2.5.5", "https://openexr.com", L("Used by OpenVDB."), P::Hold, L("Tied to OpenVDB."), github("AcademySoftwareFoundation/openexr"), "3.5.2" },
        { "OpenGL", "", "https://www.opengl.org", L("3D drawing."), P::Info, NO_REASON, none(), "" },
        { "OpenSSL", "3.5.9", "https://www.openssl.org", L("Encryption for HTTPS and the printer connection (wss); checked within the 3.5 LTS series."), P::Update, NO_REASON, github_tags("openssl/openssl", "openssl-3.5.", R"(^openssl-(\d+)\.(\d+)\.(\d+)$)"), "3.5.9" },
        { "OpenVDB", "8.2.0", "https://www.openvdb.org", L("Voxel volumes: hollowing and some offsets."), P::Hold, L("Patched fork: a newer version can change the results."), github("AcademySoftwareFoundation/openvdb"), "13.1.0" },
        { "Creality Print", "7.3.0", "https://github.com/CrealityOfficial/CrealityPrint", L("Direct base of this fork (printer pages, Creality profiles)."), P::Watch, L("A new release is merged with the upstream update procedure, not as a library."), github("CrealityOfficial/CrealityPrint"), "7.3.0" },
        { "OrcaSlicer", "", "https://github.com/SoftFever/OrcaSlicer", L("Base project of the slicer."), P::Info, NO_REASON, none(), "" },
        { "BambuStudio", "", "https://github.com/bambulab/BambuStudio", L("Base project of the slicer."), P::Info, NO_REASON, none(), "" },
        { "PrusaSlicer", "", "https://www.prusa3d.com", L("Base project of the slicer."), P::Info, NO_REASON, none(), "" },
        { "SuperSlicer", "", "https://github.com/supermerill/SuperSlicer", L("Base project of the slicer."), P::Info, NO_REASON, none(), "" },
        { "QOI", "", "https://qoiformat.org", L("Fast image format for G-code thumbnails."), P::Info, NO_REASON, none(), "" },
        { "Qhull", "2015.2", "http://qhull.org", L("Convex hulls (placement on the plate, supports)."), P::Hold, ENGINE_GEOMETRY, none(), "" },
        { "Real-Time DXT1/DXT5 C compression library", "", "https://github.com/Cyan4973/RygsDXTc", L("Texture compression for the 3D view."), P::Info, NO_REASON, none(), "" },
        { "SemVer", "", "https://semver.org", L("Compares version numbers (profiles, projects)."), P::Info, NO_REASON, none(), "" },
        { "TBB", "2021.5.0", "https://github.com/oneapi-src/oneTBB", L("Parallel computing on all CPU cores."), P::Hold, L("Tied to OpenVDB."), github("uxlfoundation/oneTBB"), "2023.1.0" },
        { "unordered_dense", "3.1.1", "https://github.com/martinus/unordered_dense", L("Fast hash tables."), P::Hold, L("No benefit from a newer version."), github("martinus/unordered_dense"), "5.3.3" },
        { "wxWidgets", "3.3.2", "https://www.wxwidgets.org", L("Interface toolkit (windows, buttons, dialogs)."), P::Hold, L("OrcaSlicer fork, long rebuild; no relevant security issue."), github("wxWidgets/wxWidgets"), "3.3.4" },
        { "zlib", "1.3.2", "http://zlib.net", L("Compression (PNG, ZIP, network)."), P::Update, NO_REASON, github("madler/zlib"), "1.3.2" },
    };
}

std::vector<long> version_parts(const std::string& version)
{
    std::vector<long> parts;
    long              current = -1;
    for (char c : version) {
        if (c >= '0' && c <= '9') {
            current = (current < 0 ? 0 : current * 10) + (c - '0');
        } else if (current >= 0) {
            parts.push_back(current);
            current = -1;
        }
    }
    if (current >= 0)
        parts.push_back(current);
    return parts;
}

// Version from a tag, or empty if the tag is not a release of the expected form.
std::string version_from_tag(const std::string& tag, const std::regex& re)
{
    std::smatch m;
    if (!std::regex_match(tag, m, re))
        return {};
    std::string version;
    for (size_t i = 1; i < m.size(); ++i) {
        if (!m[i].matched)
            continue;
        if (!version.empty())
            version += '.';
        version += m[i].str();
    }
    return version;
}

void keep_highest(std::string& best, const std::string& candidate)
{
    if (!candidate.empty() && (best.empty() || compare_library_versions(candidate, best) > 0))
        best = candidate;
}

boost::filesystem::path cache_path()
{
    return boost::filesystem::path(data_dir()) / "cache" / "third-party-libraries.json";
}

// GET with certificate verification; returns false and fills `error` on failure.
bool https_get(const std::string& url, bool is_github, std::string& body, std::string& error, bool& github_limited)
{
    bool ok = false;
    auto http = Http::get(url);
    if (is_github)
        http.header("Accept", "application/vnd.github+json");
    http.timeout_connect(10)
        .timeout_max(30)
        .ssl_verify_with_system_store()
        .on_complete([&](std::string response, unsigned) {
            body = std::move(response);
            ok   = true;
        })
        .on_error([&](std::string, std::string err, unsigned status) {
            if (is_github && (status == 403 || status == 429)) {
                github_limited = true;
                error          = L("GitHub request limit reached (60 per hour): try again later.");
            } else if (status == 404) {
                error = L("Release list not found.");
            } else if (status != 0) {
                error = "HTTP " + std::to_string(status);
            } else {
                error = err;
            }
        })
        .perform_sync();
    return ok;
}

} // namespace

const std::vector<ThirdPartyLibrary>& third_party_library_list()
{
    static const std::vector<ThirdPartyLibrary> libraries = make_libraries();
    return libraries;
}

int compare_library_versions(const std::string& a, const std::string& b)
{
    const std::vector<long> pa = version_parts(a);
    const std::vector<long> pb = version_parts(b);
    for (size_t i = 0; i < std::max(pa.size(), pb.size()); ++i) {
        const long va = i < pa.size() ? pa[i] : 0;
        const long vb = i < pb.size() ? pb[i] : 0;
        if (va != vb)
            return va < vb ? -1 : 1;
    }
    return 0;
}

LibraryCheckCache load_library_check_cache()
{
    LibraryCheckCache cache;
    try {
        boost::nowide::ifstream file(cache_path().string());
        if (!file)
            return cache;
        const nlohmann::json j = nlohmann::json::parse(file);
        cache.checked_at       = j.value("checked_at", std::string());
        const nlohmann::json results = j.value("results", nlohmann::json::object());
        for (auto it = results.begin(); it != results.end(); ++it)
            cache.results[it.key()] = { it.value().value("latest", std::string()), it.value().value("error", std::string()) };
    } catch (const std::exception& e) {
        BOOST_LOG_TRIVIAL(warning) << "Third-party libraries: cannot read the version cache: " << e.what();
        cache = {};
    }
    return cache;
}

bool save_library_check_cache(const LibraryCheckCache& cache)
{
    try {
        nlohmann::json results = nlohmann::json::object();
        for (const auto& [name, result] : cache.results)
            results[name] = { { "latest", result.latest }, { "error", result.error } };
        const nlohmann::json j = { { "checked_at", cache.checked_at }, { "results", results } };

        const boost::filesystem::path path = cache_path();
        boost::filesystem::create_directories(path.parent_path());
        boost::nowide::ofstream file(path.string(), std::ios::trunc);
        file << j.dump(2, ' ', false, nlohmann::json::error_handler_t::replace);
        return bool(file);
    } catch (const std::exception& e) {
        BOOST_LOG_TRIVIAL(warning) << "Third-party libraries: cannot write the version cache: " << e.what();
        return false;
    }
}

LibraryCheckResult check_latest_library_version(const LibraryVersionSource& source, bool& github_limited)
{
    LibraryCheckResult result;
    if (source.kind == Kind::None)
        return result;

    const bool is_github = source.kind == Kind::GitHubRelease || source.kind == Kind::GitHubTags;
    if (is_github && github_limited) {
        result.error = L("GitHub request limit reached (60 per hour): try again later.");
        return result;
    }

    std::string url;
    switch (source.kind) {
    case Kind::GitHubRelease: url = "https://api.github.com/repos/" + source.project + "/releases/latest"; break;
    case Kind::GitHubTags: url = "https://api.github.com/repos/" + source.project + "/git/matching-refs/tags/" + source.tag_prefix; break;
    case Kind::GitLabRelease: url = "https://gitlab.com/api/v4/projects/" + source.project + "/releases?per_page=20"; break;
    case Kind::NuGet: url = "https://api.nuget.org/v3-flatcontainer/" + source.project + "/index.json"; break;
    case Kind::None: break;
    }

    std::string body;
    if (!https_get(url, is_github, body, result.error, github_limited))
        return result;

    try {
        const std::regex     re(source.pattern);
        const nlohmann::json j = nlohmann::json::parse(body);
        std::string          latest;
        switch (source.kind) {
        case Kind::GitHubRelease: keep_highest(latest, version_from_tag(j.value("tag_name", std::string()), re)); break;
        case Kind::GitHubTags:
            for (const auto& ref : j) {
                std::string tag = ref.value("ref", std::string());
                if (tag.rfind("refs/tags/", 0) == 0)
                    tag.erase(0, 10);
                keep_highest(latest, version_from_tag(tag, re));
            }
            break;
        case Kind::GitLabRelease:
            for (const auto& release : j)
                keep_highest(latest, version_from_tag(release.value("tag_name", std::string()), re));
            break;
        case Kind::NuGet:
            for (const auto& version : j.value("versions", nlohmann::json::array()))
                if (version.is_string())
                    keep_highest(latest, version_from_tag(version.get<std::string>(), re));
            break;
        case Kind::None: break;
        }
        if (latest.empty())
            result.error = L("No release in the expected format.");
        else
            result.latest = latest;
    } catch (const std::exception& e) {
        result.error = e.what();
    }
    return result;
}

} // namespace GUI
} // namespace Slic3r
