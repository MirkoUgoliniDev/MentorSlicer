#ifndef slic3r_CrealityBedModelMapping_hpp_
#define slic3r_CrealityBedModelMapping_hpp_

#include <string>

namespace Slic3r {
class Preset;

namespace GUI {
namespace CrealityBedModelMapping {

// Returns the buildplate STL model absolute path for the given preset.
// For Creality system presets, applies legacy model mapping (F022/K2/K2 Pro/K2 Plus, otherwise K1).
// For project-embedded presets (for example loaded from 3mf) where `preset.vendor` is null, falls back to
// matching `printer_model` string for Creality / SPARKX.
std::string bed_model_path_for_preset(const Preset& preset);

// Some dedicated Creality bed STLs are authored at native scale and should not be auto-scaled.
bool bed_model_has_native_scale(const std::string& model_filename_or_path);

} // namespace CrealityBedModelMapping
} // namespace GUI
} // namespace Slic3r

#endif // slic3r_CrealityBedModelMapping_hpp_
