#ifndef slic3r_GUI_PrinterCoverRoute_hpp_
#define slic3r_GUI_PrinterCoverRoute_hpp_

#include "libslic3r/PrinterCover.hpp"

#include <nlohmann/json.hpp>

namespace Slic3r { namespace GUI {

// Hex-encoded UTF-8 components survive both the frontend's encodeURI and the
// HTTP server's URL decoding, including names containing spaces, #, % or &.
inline std::string printer_cover_hex(const std::string& value)
{
    constexpr char digits[] = "0123456789abcdef";
    std::string encoded;
    for (unsigned char c : value) {
        encoded += digits[c >> 4];
        encoded += digits[c & 15];
    }
    return encoded;
}

inline std::string printer_cover_unhex(const std::string& value)
{
    if (value.empty() || value.size() % 2 != 0)
        return {};
    const auto digit = [](char c) -> int {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        return -1;
    };
    std::string decoded;
    for (size_t i = 0; i < value.size(); i += 2) {
        const int high = digit(value[i]), low = digit(value[i + 1]);
        if (high < 0 || low < 0) return {};
        decoded += static_cast<char>((high << 4) | low);
    }
    return decoded;
}

inline std::string printer_cover_url(int port, const std::string& vendor, const std::string& model)
{
    return "http://localhost:" + std::to_string(port) + "/printer-cover/" +
           printer_cover_hex(vendor) + "/" + printer_cover_hex(model);
}

// Fork: the pages must never load printer pictures or profile packages from the Creality CDN.
// Every machineList.json entry gets the local cover route instead of its pic-cdn.creality.com
// "thumbnail" and loses its file-cdn.creality.com "zipUrl" (unused by the pages).
// The covers are resources/profiles/Creality/<model>_cover.png, where <model> is "Creality " + name
// (names already starting with "Creality" or "SPARKX" are used as they are); a missing cover resolves
// to the default picture. port <= 0 (local HTTP server not started) gives an empty thumbnail,
// and the pages then show their built-in image.
inline void localize_printer_list_images(nlohmann::json& list, int port)
{
    if (!list.is_array())
        return;
    for (auto& printer : list) {
        if (!printer.is_object())
            continue;
        std::string model = printer.value("name", std::string());
        if (!model.empty() && model.rfind("Creality", 0) != 0 && model.rfind("SPARKX", 0) != 0)
            model = "Creality " + model;
        printer["thumbnail"] = port > 0 && !model.empty() ? printer_cover_url(port, "Creality", model) : std::string();
        printer.erase("zipUrl");
    }
}

// Resolve only model covers, never a client-provided filesystem path.
// Empty means an invalid route; a valid missing cover resolves to the default.
inline std::string printer_cover_route_path(const std::string& request_path,
                                           const std::string& data_root, const std::string& resource_root)
{
    const std::string prefix = "/printer-cover/";
    if (request_path.compare(0, prefix.size(), prefix) != 0)
        return {};
    const size_t slash = request_path.find('/', prefix.size());
    if (slash == std::string::npos)
        return {};
    const std::string vendor = printer_cover_unhex(request_path.substr(prefix.size(), slash - prefix.size()));
    const std::string model = printer_cover_unhex(request_path.substr(slash + 1));
    if (!valid_printer_cover_component(vendor) || !valid_printer_cover_component(model))
        return {};
    std::string cover = find_printer_cover(data_root, resource_root, vendor, model);
    if (cover.empty())
        cover = (std::filesystem::u8path(resource_root) / "images" / "printer_default.png").u8string();
    return cover;
}

}} // namespace Slic3r::GUI
#endif
