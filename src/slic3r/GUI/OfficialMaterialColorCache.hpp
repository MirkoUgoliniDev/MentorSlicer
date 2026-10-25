#pragma once

#include "BaseColorSpectraCache.hpp"
#include "GUI.hpp"
#include "GUI_App.hpp"
#include "libslic3r/Utils.hpp"
#include "libslic3r_version.h"
#include "slic3r/Utils/Http.hpp"
#include <boost/filesystem.hpp>
#include <boost/nowide/fstream.hpp>
#include <boost/algorithm/string.hpp>
#include <nlohmann/json.hpp>
#include <boost/uuid/uuid_generators.hpp>
#include <boost/uuid/uuid_io.hpp>
#include <boost/log/trivial.hpp>
#include <vector>
#include <mutex>
#include <future>
#include <map>
#include <set>
#include <stdexcept>
#include <utility>

namespace Slic3r { namespace GUI {
namespace OfficialMaterialColors {
using Json = nlohmann::json;

inline boost::filesystem::path cache_path()
{
    return boost::filesystem::path(data_dir()) / "system" / "Creality" / "filament" / "filaments_color.json";
}

inline std::mutex& cache_mutex()
{
    static std::mutex mutex;
    return mutex;
}

inline std::string text(const Json& item, const char* key)
{
    return item.contains(key) && item[key].is_string() ? item[key].get<std::string>() : std::string();
}

inline std::string localized_color_name(const Json& color, const std::string& language)
{
    std::string locale = language;
    boost::trim(locale);
    boost::replace_all(locale, "-", "_");
    boost::to_lower(locale);
    const auto separator = locale.find('_');
    const std::string code = locale.substr(0, separator);

    // The two Chinese locales have separate backend fields; all other supported
    // locales use their two-letter language code. Missing names fall back to English.
    static const std::map<std::string, const char*> fields = {
        {"zh_cn", "colorCn"}, {"zh_tw", "colorZh"},
        {"en", "colorEn"}, {"de", "colorDe"}, {"fr", "colorFr"},
        {"it", "colorIt"}, {"es", "colorEs"}, {"pt", "colorPt"},
        {"ja", "colorJa"}, {"ko", "colorKo"}, {"nl", "colorNl"},
        {"sv", "colorSv"}, {"hu", "colorHu"}
    };
    const auto it = fields.find(code == "zh" ? locale : code);
    if (it != fields.end()) {
        std::string value = text(color, it->second);
        boost::trim(value);
        if (!value.empty()) return value;
    }
    std::string english = text(color, "colorEn");
    boost::trim(english);
    return english;
}

inline std::string normalize_hex(std::string value)
{
    boost::algorithm::trim(value);
    boost::algorithm::to_upper(value);
    if (value.size() != 9 || value[0] != '#' ||
        value.find_first_not_of("0123456789ABCDEF", 1) != std::string::npos)
        throw std::runtime_error("Invalid official RGBA color");
    return value;
}

inline Json normalize_colors(const Json& input)
{
    Json out = Json::array();
    const auto append = [&](const std::string& value) {
        std::vector<std::string> parts;
        boost::split(parts, value, boost::is_any_of(","));
        for (auto part : parts) out.push_back(normalize_hex(part));
    };
    if (input.is_string()) append(input.get<std::string>());
    else if (input.is_array()) {
        for (const auto& value : input) {
            if (!value.is_string()) throw std::runtime_error("Invalid color array");
            append(value.get<std::string>());
        }
    } else throw std::runtime_error("Missing color values");
    if (out.empty()) throw std::runtime_error("Empty color values");
    return out;
}

// Validate structure and colour values, independently of release versions.
inline void validate_cache(const Json& cache)
{
    if (!cache.is_object() || !cache.contains("data") || !cache["data"].is_array() || cache["data"].empty())
        throw std::runtime_error("Missing color cache data");
    for (const auto& item : cache["data"]) {
        if (!item.is_object() || text(item, "id").empty())
            throw std::runtime_error("Missing material ID in color cache");
        normalize_colors(item.at("colors"));
    }
}

inline bool initialize_from_resources()
{
    std::lock_guard<std::mutex> lock(cache_mutex());

    const auto target = cache_path();
    // Keep installation bookkeeping outside system/Creality: preset updates replace that directory.
    const auto marker = boost::filesystem::path(data_dir()) / "filaments_color_initialized_version.txt";
    static bool initialized_this_session = false;
    const bool cache_exists = boost::filesystem::exists(target);
    if (cache_exists && initialized_this_session) return true;
    std::string initialized_version;
    {
        boost::nowide::ifstream input(marker.string());
        std::getline(input, initialized_version);
    }
    if (cache_exists && initialized_version == SLIC3R_VERSION) {
        initialized_this_session = true;
        return true;
    }

    const auto bundled = boost::filesystem::path(resources_dir()) / "profiles" / "Creality" /
        "filament" / "filaments_color.json";
    const auto temporary = target.string() + ".tmp";
    const auto marker_temporary = marker.string() + ".tmp";
    try {
        boost::nowide::ifstream input(bundled.string());
        if (!input) throw std::runtime_error("Bundled color cache not found");
        Json cache;
        input >> cache;
        validate_cache(cache);

        boost::filesystem::create_directories(target.parent_path());
        {
            boost::nowide::ofstream output(temporary, std::ios::binary | std::ios::trunc);
            output << cache.dump(2);
            output.flush();
            if (!output) throw std::runtime_error("Cannot write bundled color cache");
            output.close();
            if (!output) throw std::runtime_error("Cannot close bundled color cache");
        }
        const auto error = Slic3r::rename_file(temporary, target.string());
        if (error) throw std::runtime_error(error.message());
        initialized_this_session = true;
        // Commit the marker only after successfully replacing the validated cache.
        {
            boost::nowide::ofstream output(marker_temporary, std::ios::binary | std::ios::trunc);
            output << SLIC3R_VERSION << '\n';
            output.close();
            if (!output) throw std::runtime_error("Cannot write color cache initialization version");
        }
        const auto marker_error = Slic3r::rename_file(marker_temporary, marker.string());
        if (marker_error) throw std::runtime_error(marker_error.message());
        BOOST_LOG_TRIVIAL(info) << "Official material color cache initialized from resources: "
                                << cache["data"].size();
        return true;
    } catch (const std::exception& e) {
        boost::system::error_code error;
        boost::filesystem::remove(temporary, error);
        boost::filesystem::remove(marker_temporary, error);
        BOOST_LOG_TRIVIAL(warning) << "Bundled official color cache unavailable: " << e.what();
        return false;
    }
}

}}
}
