#pragma once

#include <cstdint>
#include <filesystem>
#include <functional>
#include <stdexcept>
#include <string>
#include <vector>

namespace Slic3r::DataMigration {
namespace fs = std::filesystem;
class Error : public std::runtime_error { public: using std::runtime_error::runtime_error; };
// PRINT-ADVISOR F8: a failed one-time import of the Creality Print data folder. The message is
// complete and meant for the user (Italian, with the folders and the last-error file).
class ImportError : public Error { public: using Error::Error; };
// PRINT-ADVISOR F8: Creality Print 7.3 Alpha data folder, relative to %APPDATA% (wx GetUserConfigDir()).
// Imported once into the MentorSlicer data folder; the source is never modified.
inline constexpr const char* LEGACY_CREALITY_DATA_RELATIVE = "Creality/Creality Print/7.3 Alpha";
// Configuration file name inside LEGACY_CREALITY_DATA_RELATIVE (and in Creality's 6.0/7.0 folders).
inline constexpr const char* LEGACY_CREALITY_CONFIG = "Creality.conf";
// Executable file name of Creality Print, the writer of LEGACY_CREALITY_DATA_RELATIVE.
inline constexpr const wchar_t* LEGACY_CREALITY_EXE = L"CrealityPrint.exe";
struct Options {
    fs::path target;
    fs::path resources;
    std::string data_version = "7.3";
    std::string application_version;
    bool alpha = false;
    // PRINT-ADVISOR F8: expected target folder name; empty means "<data_version>[ Alpha]".
    std::string target_name;
    // PRINT-ADVISOR F8: application config file name inside the target (SLIC3R_APP_KEY ".conf").
    std::string config_name = LEGACY_CREALITY_CONFIG;
    // Read-only validation. A failure isolates the preset; it never rewrites JSON or .info.
    std::function<std::vector<std::string>(const fs::path&)> validate_preset;
};
struct Result {
    bool migrated = false;
    std::string source_version;
    size_t presets = 0;
    size_t quarantined = 0;
};
Result initialize(const Options& options);
// PRINT-ADVISOR F8: one-time copy of the legacy Creality Print data folder into a new target.
struct ImportOptions {
    fs::path source;              // e.g. %APPDATA%/LEGACY_CREALITY_DATA_RELATIVE
    fs::path target;              // e.g. %APPDATA%/MentorSlicer (empty or runtime-only)
    std::string config_name;      // LEGACY_CREALITY_CONFIG(.bak) is renamed to config_name(.bak)
    std::string application_version;
};
struct ImportResult {
    bool imported = false;
    size_t files = 0;
    std::uintmax_t bytes = 0;
};
// Returns without doing anything when the target already holds data (import marker, config or
// migration manifest) or when the source does not exist. Throws only ImportError.
ImportResult import_legacy_directory(const ImportOptions& options);
// Marker written inside "<target>/migration" by a completed import.
inline constexpr const char* IMPORT_MARKER = "imported-from-creality.json";
bool preserves_user_presets(const fs::path& target);
// Uses the same parser as the application, without saving or changing global data_dir.
std::vector<std::string> validate_preset_readonly(const fs::path& file);
} // namespace Slic3r::DataMigration
