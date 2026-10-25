#ifndef slic3r_CrealityVersion_hpp_
#define slic3r_CrealityVersion_hpp_

#include <regex>
#include <string>

#include "Semver.hpp"

namespace Slic3r {

struct ParsedCrealityVersion
{
    Semver semver   = Semver::invalid();
    int    build_id = -1;

    bool valid() const { return semver != Semver::invalid(); }
};

inline int parse_build_id_string(const std::string& value)
{
    if (value.empty())
        return -1;

    try {
        size_t parsed = 0;
        int    build  = std::stoi(value, &parsed);
        return parsed == value.size() ? build : -1;
    } catch (...) {
        return -1;
    }
}

inline ParsedCrealityVersion parse_creality_version(const std::string& input, int fallback_build_id = -1)
{
    static const std::regex version_pattern(
        R"(^[Vv]?([0-9]+)\.([0-9]+)\.([0-9]+)(?:\.([0-9]+))?((?:-[0-9A-Za-z.-]+)?(?:\+[0-9A-Za-z.-]+)?)$)");

    ParsedCrealityVersion parsed;
    std::smatch           match;
    if (!std::regex_match(input, match, version_pattern))
        return parsed;

    std::string semver_text = match[1].str() + "." + match[2].str() + "." + match[3].str() + match[5].str();
    auto        semver      = Semver::parse(semver_text);
    if (!semver.has_value())
        return parsed;

    parsed.semver = *semver;
    if (match[4].matched)
        parsed.build_id = parse_build_id_string(match[4].str());
    if (parsed.build_id < 0)
        parsed.build_id = fallback_build_id;
    return parsed;
}

} // namespace Slic3r

#endif /* slic3r_CrealityVersion_hpp_ */
