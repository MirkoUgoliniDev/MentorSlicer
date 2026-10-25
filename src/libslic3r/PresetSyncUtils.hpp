#ifndef slic3r_PresetSyncUtils_hpp_
#define slic3r_PresetSyncUtils_hpp_

#include <algorithm>
#include <charconv>
#include <cstdint>
#include <map>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace Slic3r { namespace PresetSyncUtils {

inline std::string filename(const std::string& name)
{
    if (name.size() >= 5) {
        std::string extension = name.substr(name.size() - 5);
        for (char& ch : extension)
            if (ch >= 'A' && ch <= 'Z') ch += 'a' - 'A';
        if (extension == ".json") return name;
    }
    return name + ".json";
}

// An explicit, stable hash avoids std::hash's implementation-dependent filenames.
inline std::string filename_tag(const std::string& name)
{
    std::uint64_t hash = UINT64_C(14695981039346656037);
    for (unsigned char ch : name) {
        hash ^= ch;
        hash *= UINT64_C(1099511628211);
    }
    const char* hex = "0123456789abcdef";
    std::string tag(16, '0');
    for (int i = 15; i >= 0; --i) {
        tag[i] = hex[hash & 15];
        hash >>= 4;
    }
    return tag;
}

inline std::string collision_stem(const std::string& name)
{
    std::string stem = filename(name);
    stem.resize(stem.size() - 5);
    if (stem.size() > 180) {
        size_t end = 180;
        while ((static_cast<unsigned char>(stem[end]) & 0xc0) == 0x80) --end;
        stem.resize(end);
    }
    return stem + ".__cp_" + filename_tag(name);
}

inline bool is_collision_filename(const std::string& file_name, const std::string& name)
{
    const std::string prefix = collision_stem(name);
    if (file_name == prefix + ".json") return true;
    if (file_name.compare(0, prefix.size() + 1, prefix + "_") != 0 ||
        file_name.size() <= prefix.size() + 6 || file_name.substr(file_name.size() - 5) != ".json")
        return false;
    for (size_t i = prefix.size() + 1; i < file_name.size() - 5; ++i)
        if (file_name[i] < '0' || file_name[i] > '9') return false;
    return true;
}

template<class Equivalent>
std::string loaded_name(const std::string& file_name, const std::string& embedded_name, Equivalent equivalent)
{
    const std::string stem = file_name.substr(0, file_name.size() - 5);
    if (!embedded_name.empty() && (is_collision_filename(file_name, embedded_name) || equivalent(stem, embedded_name)))
        return embedded_name;
    return stem; // Preserve legacy files intentionally renamed outside the application.
}

// The caller checks real filesystem ownership, including Windows case folding.
template<class Available>
std::string available_filename(const std::string& name, Available available)
{
    const std::string plain = filename(name);
    if (available(plain)) return plain;
    const std::string stem = collision_stem(name);
    for (unsigned attempt = 0; attempt < 1000; ++attempt) {
        const std::string candidate = stem + (attempt == 0 ? "" : "_" + std::to_string(attempt)) + ".json";
        if (available(candidate)) return candidate;
    }
    throw std::runtime_error("Cannot allocate a unique preset filename: " + name);
}

}} // namespace Slic3r::PresetSyncUtils
#endif
