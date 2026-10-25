#include "PrintAdvisorInternal.hpp"

// End-of-print verdict and archive of successful prints.
// - The monitor calls request_print_verdict() when a print completed and the bed cooled below
//   40 C (PrintAdvisorMonitor.cpp, bed_cooled). A dialog in the MCP Advisor style asks how the
//   print turned out; the answer is written to the print log with record_print_verdict(), the
//   same writer as POST /v1/print_verdict.
// - "Good, archive" also copies the open project (3MF), the G-code of the current plate, the
//   print log and a camera photo into <data_dir>/print-advisor/archive/<date>_<name>/, with a
//   metadata.json that lists what was skipped and why.
// - "Later" keeps the question pending in memory and in <data_dir>/print-advisor/pending_verdict.json;
//   it is asked again a few seconds after the next start of the Print Advisor server.
// - Routes: GET /v1/archive lists the archive; POST /v1/archive/open opens a temporary copy of an
//   archived project (untitled, so the archive is never overwritten) after a confirmation dialog
//   that also says the embedded profiles become the active selection. Nothing here ever starts
//   a print.
// Every dialog runs on the GUI thread, only while no other modal window is open.

#include "AdvisorBranding.hpp"
#include "PrintAdvisorPageBridge.hpp"
#include "slic3r/GUI/GUI.hpp"
#include "slic3r/GUI/GUI_App.hpp"
#include "slic3r/GUI/I18N.hpp"
#include "slic3r/GUI/MainFrame.hpp"
#include "slic3r/GUI/PartPlate.hpp"
#include "slic3r/GUI/Plater.hpp"
#include "slic3r/GUI/print_manage/App/DeviceTelemetryCache.h"
#include "libslic3r/Format/bbs_3mf.hpp"
#include "libslic3r/PresetBundle.hpp"
#include "libslic3r/Utils.hpp"

#include <boost/beast/core/detail/base64.hpp>
#include <boost/filesystem.hpp>
#include <boost/filesystem/fstream.hpp>
#include <boost/log/trivial.hpp>

#include <wx/button.h>
#include <wx/dialog.h>
#include <wx/evtloop.h>
#include <wx/radiobox.h>
#include <wx/sizer.h>
#include <wx/statbmp.h>
#include <wx/statline.h>
#include <wx/stattext.h>
#include <wx/textctrl.h>
#include <wx/timer.h>

#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <ctime>
#include <initializer_list>
#include <iterator>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace Slic3r {
namespace GUI {
namespace advisor {

using json = nlohmann::json;
namespace fs = boost::filesystem;

namespace {

constexpr int    RETRY_BLOCKED_MS     = 60 * 1000; // another modal window was open
constexpr int    ASK_AFTER_START_MS   = 20 * 1000; // pending question after the server start
constexpr size_t MAX_LISTED_ARCHIVES  = 100;
constexpr std::uintmax_t MAX_METADATA_BYTES = 256 * 1024;
constexpr size_t MAX_NOTE_CHARS       = 300;
// record_print_verdict (PrintAdvisorMonitor.cpp) refuses notes above 500 bytes.
constexpr size_t MAX_NOTE_BYTES       = 500;
constexpr long long TMP_MAX_AGE_S     = 24 * 60 * 60; // temporary project copies kept for a day

fs::path advisor_dir() { return fs::path(data_dir()) / "print-advisor"; }
fs::path archive_dir() { return advisor_dir() / "archive"; }
fs::path prints_dir() { return advisor_dir() / "prints"; }
fs::path pending_path() { return advisor_dir() / "pending_verdict.json"; }
fs::path tmp_dir() { return advisor_dir() / "tmp"; }

long long wall_ms()
{
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
}

std::string local_stamp(long long ms)
{
    const std::time_t time = std::time_t(ms / 1000);
    std::tm tm{};
#ifdef _WIN32
    localtime_s(&tm, &time);
#else
    localtime_r(&time, &tm);
#endif
    char buffer[32];
    std::strftime(buffer, sizeof(buffer), "%Y-%m-%d_%H%M", &tm);
    return buffer;
}

// Folder name part: letters, digits, '-', '_' and single dots, without the G-code extension,
// never ending with a dot or a space and never a Windows device name ("print" when empty).
// Archives made before these rules may have other names: they are found by listing the
// archive folder (existing_archive), never by recomputing the name.
std::string safe_name(std::string name)
{
    for (const char* ext : {".gcode", ".GCODE", ".gco", ".3mf"}) {
        const std::string e(ext);
        if (name.size() > e.size() && name.compare(name.size() - e.size(), e.size(), e) == 0) {
            name.resize(name.size() - e.size());
            break;
        }
    }
    std::string out;
    for (unsigned char c : name) {
        if (out.size() >= 60)
            break;
        const char kept = (std::isalnum(c) || c == '-' || c == '_' || c == '.') && c < 128 ? char(c) : '_';
        if (kept == '.' && !out.empty() && out.back() == '.')
            continue; // runs of dots collapse to one
        out += kept;
    }
    while (!out.empty() && out.front() == '.')
        out.erase(out.begin());
    while (!out.empty() && (out.back() == '.' || out.back() == ' '))
        out.pop_back();
    // Windows device names cannot be used as file or folder names, with or without an extension.
    std::string stem = out.substr(0, out.find('.'));
    std::transform(stem.begin(), stem.end(), stem.begin(), [](unsigned char c) { return char(std::toupper(c)); });
    static const char* const reserved[] = {"CON", "PRN", "AUX", "NUL", "COM1", "COM2", "COM3", "COM4", "COM5", "COM6", "COM7",
                                           "COM8", "COM9", "LPT1", "LPT2", "LPT3", "LPT4", "LPT5", "LPT6", "LPT7", "LPT8", "LPT9"};
    for (const char* device : reserved)
        if (stem == device) {
            out = "print_" + out;
            break;
        }
    return out.empty() ? std::string("print") : out;
}

// A single file or folder name (no separator, not "." or ".."), as taken from a directory
// listing. Names with dots inside ("a..b") are valid: archives made before safe_name collapsed
// dots may carry them.
bool plain_entry_name(const std::string& name)
{
    return !name.empty() && name.size() <= 200 && name != "." && name != ".." &&
           name.find_first_of("/\\:") == std::string::npos;
}

std::string text_of(const json& value, const std::string& fallback = {})
{
    return value.is_string() ? value.get<std::string>() : fallback;
}

// Field of a JSON object, or null (const operator[] asserts on a missing key).
json field(const json& object, const char* key)
{
    return object.is_object() ? object.value(key, json(nullptr)) : json(nullptr);
}

std::string to_u8(const wxString& text) { return into_u8(text); }

// Same test as the server (modal_ui_active): a modal wxDialog, a native modal dialog or the
// "Send to printer" dialog is open. The verdict dialog never opens inside or over one of them.
bool modal_window_open()
{
    return modal_ui_active();
}

bool write_json_file(const fs::path& path, const json& value)
{
    boost::system::error_code ec;
    fs::create_directories(path.parent_path(), ec);
    fs::ofstream file(path, std::ios::binary | std::ios::trunc);
    file << value.dump(2);
    return bool(file);
}

json read_json_file(const fs::path& path, std::uintmax_t max_bytes)
{
    boost::system::error_code ec;
    if (!fs::is_regular_file(path, ec) || ec || fs::file_size(path, ec) > max_bytes || ec)
        return nullptr;
    fs::ifstream file(path, std::ios::binary);
    std::string text((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    json value = json::parse(text, nullptr, false);
    return value.is_discarded() ? json(nullptr) : value;
}

// --- state ---------------------------------------------------------------------------------

struct State
{
    json pending{nullptr};   // {log, file, address, ended_ms, printer, filament, process, pending_since_ms}
    bool showing{false};     // a verdict dialog is open
    std::unique_ptr<wxTimer> timer;
    // Set when the server stops: the photo worker gives up.
    std::shared_ptr<std::atomic<bool>> stopping{std::make_shared<std::atomic<bool>>(false)};
    std::mutex archive_mutex; // metadata.json reads and writes (GUI thread, photo worker, server thread)
    // Photo worker of the last archive: joined when the server stops (camera_snapshot_now gives
    // up within 10 s, sooner once `stopping` is set) or before the next archive, if finished.
    std::thread photo_worker;
    std::shared_ptr<std::atomic<bool>> photo_worker_done{std::make_shared<std::atomic<bool>>(true)};
    // Temporary copy loaded by the last reopen_archived_project: it may back the open (untitled)
    // project, so prune_tmp_projects never deletes it.
    fs::path last_tmp_project;
};

// Never destroyed: the photo worker may still reference the mutex at exit.
State& state()
{
    static State* instance = new State();
    return *instance;
}

void save_pending();

// True when the print log still exists (logs can be pruned, or the data folder cleaned).
bool print_log_exists(const std::string& log_name)
{
    boost::system::error_code ec;
    return plain_entry_name(log_name) && fs::is_regular_file(prints_dir() / log_name, ec) && !ec;
}

// Drops the pending question of a print whose log no longer exists: its verdict can never be
// recorded, so asking again would never end. GUI thread.
bool drop_pending_without_log()
{
    State& st = state();
    if (!st.pending.is_object())
        return false;
    const std::string log_name = st.pending.value("log", std::string());
    if (print_log_exists(log_name))
        return false;
    BOOST_LOG_TRIVIAL(warning) << "Print Advisor: print log " << log_name << " no longer exists; verdict question dropped";
    append_history({{"action", "print_verdict_dialog"}, {"log", log_name}, {"outcome", "dropped_log_missing"}});
    st.pending = nullptr;
    save_pending();
    return true;
}

// The application is closing or the server stopped: nothing more may be done after a dialog.
bool flow_aborted()
{
    if (!wxTheApp || state().stopping->load())
        return true;
    MainFrame* mainframe = wxGetApp().mainframe;
    return !mainframe || !wxTopLevelWindows.Find(mainframe) || mainframe->IsBeingDeleted() || wxGetApp().plater() == nullptr;
}

void try_show();

void schedule(int ms)
{
    State& st = state();
    if (!st.timer) {
        st.timer = std::make_unique<wxTimer>();
        st.timer->Bind(wxEVT_TIMER, [](wxTimerEvent&) { try_show(); });
    }
    st.timer->StartOnce(ms);
}

void save_pending()
{
    State& st = state();
    boost::system::error_code ec;
    if (st.pending.is_null())
        fs::remove(pending_path(), ec);
    else
        write_json_file(pending_path(), st.pending);
}

// --- dialogs -------------------------------------------------------------------------------

enum VerdictChoice { CHOICE_LATER = wxID_CANCEL, CHOICE_GOOD = wxID_YES, CHOICE_BAD = wxID_NO };

// Header shared by the two dialogs: logo, "MCP Advisor" and the action title.
wxBoxSizer* branded_header(wxDialog* dialog, const wxString& action, wxStaticText** action_label)
{
    const advisor_branding::Palette colours = advisor_branding::palette();
    auto* header = new wxBoxSizer(wxHORIZONTAL);
    header->Add(new wxStaticBitmap(dialog, wxID_ANY, advisor_branding::logo(dialog->FromDIP(44))), 0,
                wxALIGN_CENTER_VERTICAL | wxRIGHT, dialog->FromDIP(12));
    auto* titles = new wxBoxSizer(wxVERTICAL);
    auto* brand = new wxStaticText(dialog, wxID_ANY, "MCP Advisor");
    brand->SetFont(brand->GetFont().Bold().Scaled(1.35f));
    brand->SetForegroundColour(advisor_branding::green);
    titles->Add(brand);
    auto* action_text = new wxStaticText(dialog, wxID_ANY, action);
    action_text->SetFont(action_text->GetFont().Bold().Scaled(1.1f));
    action_text->SetForegroundColour(colours.foreground);
    titles->Add(action_text, 0, wxTOP, dialog->FromDIP(2));
    header->Add(titles, 1, wxALIGN_CENTER_VERTICAL);
    *action_label = action_text;
    return header;
}

void apply_palette(wxDialog* dialog, std::initializer_list<wxStaticText*> labels, std::initializer_list<wxButton*> neutral_buttons)
{
    const advisor_branding::Palette colours = advisor_branding::palette();
    wxGetApp().UpdateDlgDarkUI(dialog);
    dialog->SetBackgroundColour(colours.background);
    for (wxStaticText* label : labels)
        label->SetForegroundColour(colours.foreground);
    for (wxButton* button : neutral_buttons) {
        button->SetBackgroundColour(colours.neutral_button);
        button->SetForegroundColour(colours.foreground);
    }
}

class VerdictDialog : public wxDialog
{
public:
    VerdictDialog(wxWindow* parent, const json& print)
        : wxDialog(parent, wxID_ANY, "MCP Advisor - " + _L("Print verdict"), wxDefaultPosition, wxDefaultSize, wxDEFAULT_DIALOG_STYLE)
    {
        auto* root = new wxBoxSizer(wxVERTICAL);
        wxStaticText* action = nullptr;
        root->Add(branded_header(this, _L("How did the print turn out?"), &action), 0, wxEXPAND | wxALL, FromDIP(16));
        root->Add(new wxStaticLine(this), 0, wxEXPAND | wxLEFT | wxRIGHT, FromDIP(16));

        const std::string file = sanitized_text(text_of(field(print, "file"), to_u8(_L("(unknown file)"))), 100);
        wxString message = wxString::Format(_L("How did the print of %s turn out?"), wxString::FromUTF8(file));
        message += "\n" + wxString::Format(_L("Printer: %s"), wxString::FromUTF8(sanitized_text(text_of(field(print, "printer"), "?"), 60)));
        const json filament = print.value("filament", json(nullptr));
        const std::string filament_name = filament.is_array() && !filament.empty() ? text_of(filament.front(), "?") : text_of(filament, "?");
        message += "\n" + wxString::Format(_L("Profiles: %s / %s"), wxString::FromUTF8(sanitized_text(filament_name, 60)),
                                           wxString::FromUTF8(sanitized_text(text_of(field(print, "process"), "?"), 60)));
        message += "\n\n" + _L("A good print is archived with the open project, the G-code of the current plate, the print log and a camera photo. "
                               "The verdict is written to the print log in every case.");
        auto* text = new wxStaticText(this, wxID_ANY, message);
        text->Wrap(FromDIP(560));
        root->Add(text, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, FromDIP(16));

        auto* buttons = new wxBoxSizer(wxHORIZONTAL);
        auto* later = new wxButton(this, CHOICE_LATER, _L("Later"));
        auto* bad = new wxButton(this, CHOICE_BAD, _L("Defects or failed"));
        auto* good = new wxButton(this, CHOICE_GOOD, _L("Good, archive"));
        buttons->Add(later, 0, wxRIGHT, FromDIP(8));
        buttons->AddStretchSpacer();
        buttons->Add(bad, 0, wxRIGHT, FromDIP(8));
        buttons->Add(good, 0);
        root->Add(buttons, 0, wxEXPAND | wxALL, FromDIP(16));
        for (wxButton* button : {bad, good})
            button->Bind(wxEVT_BUTTON, [this](wxCommandEvent& event) { EndModal(event.GetId()); });

        SetEscapeId(CHOICE_LATER);
        later->SetDefault();
        later->SetFocus();
        SetSizerAndFit(root);
        apply_palette(this, {action, text}, {later});
        bad->SetBackgroundColour(advisor_branding::red);
        bad->SetForegroundColour(*wxWHITE);
        good->SetBackgroundColour(advisor_branding::green);
        good->SetForegroundColour(*wxWHITE);
        CentreOnParent();
    }
};

struct Reason
{
    std::string verdict; // record_print_verdict value
    std::string note;
};

class ReasonDialog : public wxDialog
{
public:
    ReasonDialog(wxWindow* parent)
        : wxDialog(parent, wxID_ANY, "MCP Advisor - " + _L("Print verdict"), wxDefaultPosition, wxDefaultSize, wxDEFAULT_DIALOG_STYLE)
    {
        auto* root = new wxBoxSizer(wxVERTICAL);
        wxStaticText* action = nullptr;
        root->Add(branded_header(this, _L("What went wrong?"), &action), 0, wxEXPAND | wxALL, FromDIP(16));
        root->Add(new wxStaticLine(this), 0, wxEXPAND | wxLEFT | wxRIGHT, FromDIP(16));

        const wxString choices[] = {_L("Detached from the plate"), _L("Warping"), _L("Stringing"), _L("Under-extrusion"), _L("Other")};
        m_choice = new wxRadioBox(this, wxID_ANY, _L("Reason"), wxDefaultPosition, wxDefaultSize, 5, choices, 1, wxRA_SPECIFY_COLS);
        root->Add(m_choice, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, FromDIP(16));
        auto* note_label = new wxStaticText(this, wxID_ANY, _L("Notes (optional)"));
        root->Add(note_label, 0, wxLEFT | wxRIGHT | wxTOP, FromDIP(16));
        m_note = new wxTextCtrl(this, wxID_ANY, wxEmptyString, wxDefaultPosition, wxSize(FromDIP(520), FromDIP(60)), wxTE_MULTILINE);
        m_note->SetMaxLength(MAX_NOTE_CHARS);
        root->Add(m_note, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, FromDIP(4));

        auto* buttons = new wxBoxSizer(wxHORIZONTAL);
        buttons->AddStretchSpacer();
        auto* cancel = new wxButton(this, wxID_CANCEL, _L("Cancel"));
        auto* record = new wxButton(this, wxID_OK, _L("Record"));
        buttons->Add(cancel, 0, wxRIGHT, FromDIP(8));
        buttons->Add(record, 0);
        root->Add(buttons, 0, wxEXPAND | wxALL, FromDIP(16));

        SetEscapeId(wxID_CANCEL);
        SetAffirmativeId(wxID_OK);
        record->SetDefault();
        SetSizerAndFit(root);
        apply_palette(this, {action, note_label}, {cancel});
        const advisor_branding::Palette colours = advisor_branding::palette();
        m_choice->SetForegroundColour(colours.foreground);
        record->SetBackgroundColour(advisor_branding::red);
        record->SetForegroundColour(*wxWHITE);
        CentreOnParent();
    }

    Reason reason() const
    {
        static const char* const verdicts[] = {"detach", "warp", "stringing", "under_extrusion", "other"};
        static const char* const keys[] = {"detached", "warping", "stringing", "under_extrusion", "other"};
        const int index = std::clamp(m_choice->GetSelection(), 0, 4);
        Reason result;
        result.verdict = verdicts[index];
        result.note = keys[index];
        // Free text only in the note, sanitized like any text shown back to the user.
        const std::string text = sanitized_reason(json(to_u8(m_note->GetValue())));
        if (!text.empty())
            result.note += ": " + text;
        // The writer limits the note in bytes: 300 characters can take up to 1200.
        result.note = truncate_utf8_bytes(result.note, MAX_NOTE_BYTES);
        return result;
    }

private:
    wxRadioBox* m_choice{nullptr};
    wxTextCtrl* m_note{nullptr};
};

// --- archive -------------------------------------------------------------------------------

std::string mime_extension(const std::string& mime)
{
    if (mime == "image/jpeg")
        return "jpg";
    if (mime == "image/png")
        return "png";
    if (mime == "image/webp")
        return "webp";
    return "bin";
}

// Updates one top-level field of an archive's metadata.json. Any thread.
void update_metadata(const fs::path& dir, const std::string& key, const json& value)
{
    std::lock_guard<std::mutex> lock(state().archive_mutex);
    json metadata = read_json_file(dir / "metadata.json", MAX_METADATA_BYTES);
    if (!metadata.is_object())
        metadata = json::object();
    metadata[key] = value;
    write_json_file(dir / "metadata.json", metadata);
}

// Camera photo, taken through the device page on a worker thread: the page replies through the
// GUI thread, which must not be blocked. The result lands in metadata.json ("photo").
// Returns false when the previous worker is still running (it is never joined from the GUI
// thread while it waits for the page, which replies through the GUI thread).
bool archive_photo_async(const fs::path& dir)
{
    State& st = state();
    if (st.photo_worker.joinable()) {
        if (!st.photo_worker_done->load())
            return false;
        st.photo_worker.join();
    }
    const auto stopping = st.stopping;
    const auto done = std::make_shared<std::atomic<bool>>(false);
    st.photo_worker_done = done;
    st.photo_worker = std::thread([dir, stopping, done] {
        json photo;
        try {
            json frame = camera_snapshot_now(*stopping);
            if (!frame.value("success", false))
                photo = {{"status", "skipped"}, {"reason", frame.value("error", std::string("no frame"))}};
            else {
                const std::string& encoded = frame["data_base64"].get_ref<const std::string&>();
                std::string bytes(boost::beast::detail::base64::decoded_size(encoded.size()), '\0');
                const auto result = boost::beast::detail::base64::decode(&bytes[0], encoded.data(), encoded.size());
                bytes.resize(result.first);
                const std::string name = "camera." + mime_extension(frame.value("mime", std::string()));
                fs::ofstream file(dir / name, std::ios::binary | std::ios::trunc);
                file.write(bytes.data(), std::streamsize(bytes.size()));
                photo = file ? json{{"status", "saved"}, {"file", name}, {"width", frame.value("width", 0)}, {"height", frame.value("height", 0)}}
                             : json{{"status", "skipped"}, {"reason", "could not write the image file"}};
                if (frame.contains("warning"))
                    photo["warning"] = frame["warning"];
            }
        } catch (const std::exception& error) {
            photo = {{"status", "skipped"}, {"reason", error.what()}};
        }
        try {
            update_metadata(dir, "photo", photo);
        } catch (...) {
        }
        done->store(true);
    });
    return true;
}

// Unique archive folder for this print: <date>_<name>, then _2, _3...
fs::path new_archive_dir(const json& print)
{
    const std::string stem = local_stamp(wall_ms()) + "_" + safe_name(text_of(field(print, "file"), "print"));
    boost::system::error_code ec;
    for (int i = 1; i < 100; ++i) {
        const fs::path dir = archive_dir() / (stem + (i == 1 ? std::string() : "_" + std::to_string(i)));
        if (!fs::exists(dir, ec))
            return dir;
    }
    throw std::runtime_error("Too many archives with the same name");
}

// Archives the open project, the current plate G-code, the print log and (asynchronously) a
// camera photo. GUI thread. Returns the metadata; throws only when the folder cannot be created.
json archive_print(const json& print)
{
    const fs::path dir = new_archive_dir(print);
    boost::system::error_code ec;
    fs::create_directories(dir, ec);
    if (ec)
        throw std::runtime_error("Could not create the archive folder: " + ec.message());

    json metadata = {{"archive_version", 1}, {"created_ms", wall_ms()}, {"name", dir.filename().string()}, {"verdict", "ok"},
                     {"file", print.value("file", json(nullptr))}, {"log", print.value("log", json(nullptr))},
                     {"address", print.value("address", json(nullptr))}, {"started_ms", print.value("started_ms", json(nullptr))},
                     {"ended_ms", print.value("ended_ms", json(nullptr))},
                     {"profiles_at_print_start", {{"printer", print.value("printer", json(nullptr))}, {"filament", print.value("filament", json(nullptr))},
                                                  {"process", print.value("process", json(nullptr))}}}};
    // Printer model from the LAN telemetry, when the printer is connected.
    try {
        const json telemetry = current_device_telemetry();
        if (telemetry.is_object())
            metadata["printer_model"] = telemetry.value("model", telemetry.value("modelName", json(nullptr)));
    } catch (...) {
    }
    if (auto* bundle = wxGetApp().preset_bundle) {
        json filaments = json::array();
        for (const std::string& name : bundle->filament_presets)
            filaments.push_back(name);
        metadata["profiles_now"] = {{"printer", bundle->printers.get_edited_preset().name}, {"process", bundle->prints.get_edited_preset().name},
                                    {"filament", filaments}};
    }

    // Project: the open one, saved as a separate file (the user's project path is unchanged).
    json project = {{"status", "skipped"}, {"reason", "no project open"}};
    Plater* plater = wxGetApp().plater();
    if (plater) {
        const std::string open_name = to_u8(plater->get_project_filename(".3mf"));
        // File name only: the metadata goes to the MCP client, the user's folders do not.
        project["source"] = open_name.empty() ? json(nullptr) : json(fs::path(open_name).filename().string());
        const std::string printed = safe_name(text_of(field(print, "file")));
        const std::string open_stem = safe_name(fs::path(open_name).filename().string());
        if (!open_name.empty() && printed != "print" && open_stem.rfind(printed, 0) != 0 && printed.rfind(open_stem, 0) != 0)
            project["note"] = "The open project does not look like the printed file: it was saved anyway";
        if (plater->model().objects.empty())
            project["reason"] = "the open project has no objects";
        else {
            SaveStrategy strategy = SaveStrategy::SplitModel | SaveStrategy::ShareMesh | SaveStrategy::FullPathSources | SaveStrategy::Silence;
            PartPlate* plate = plater->get_partplate_list().get_curr_plate();
            if (plate && plate->is_slice_result_valid())
                strategy = strategy | SaveStrategy::WithGcode;
            int result = -1;
            try {
                result = plater->export_3mf(dir / "project.3mf", strategy);
            } catch (const std::exception& error) {
                project["reason"] = std::string("export failed: ") + error.what();
            }
            if (result >= 0) {
                project["status"] = "saved";
                project["file"] = "project.3mf";
                project.erase("reason");
            } else if (!project.contains("reason") || project["reason"] == "no project open")
                project["reason"] = "export_3mf failed";
        }
    }
    metadata["project"] = project;

    // G-code: the slice result of the current plate (the file sent to the printer is not kept).
    json gcode = {{"status", "skipped"}, {"reason", "no valid slice result for the current plate"}};
    if (plater) {
        PartPlate* plate = plater->get_partplate_list().get_curr_plate();
        const std::string source = plate && plate->is_slice_result_valid() ? plate->get_gcode_filename() : std::string();
        if (!source.empty() && fs::is_regular_file(fs::path(source), ec) && !ec) {
            std::string name = safe_name(text_of(field(print, "file"), "plate")) + ".gcode";
            fs::copy_file(fs::path(source), dir / name, fs::copy_options::overwrite_existing, ec);
            gcode = ec ? json{{"status", "skipped"}, {"reason", "copy failed: " + ec.message()}}
                       : json{{"status", "saved"}, {"file", name},
                              {"note", "Slice result of the current plate at archive time; it may differ from the file sent to the printer"}};
        }
    }
    metadata["gcode"] = gcode;

    // Print log (already holds the verdict line).
    json log = {{"status", "skipped"}, {"reason", "no print log"}};
    const std::string log_name = text_of(field(print, "log"));
    if (plain_entry_name(log_name)) {
        const fs::path source = prints_dir() / log_name;
        if (fs::is_regular_file(source, ec) && !ec) {
            fs::copy_file(source, dir / "print-log.jsonl", fs::copy_options::overwrite_existing, ec);
            log = ec ? json{{"status", "skipped"}, {"reason", "copy failed: " + ec.message()}} : json{{"status", "saved"}, {"file", "print-log.jsonl"}};
        } else
            log["reason"] = "print log not found (pruned?)";
    }
    metadata["print_log"] = log;

    // Camera photo: only when the device page is open; the worker fills "photo" later.
    const bool page = advisor_device_page_loaded();
    metadata["photo"] = page ? json{{"status", "pending"}} : json{{"status", "skipped"}, {"reason", "the Device tab is not open"}};
    {
        std::lock_guard<std::mutex> lock(state().archive_mutex);
        if (!write_json_file(dir / "metadata.json", metadata))
            BOOST_LOG_TRIVIAL(warning) << "Print Advisor: could not write " << (dir / "metadata.json").string();
    }
    if (page && !archive_photo_async(dir)) {
        metadata["photo"] = {{"status", "skipped"}, {"reason", "the photo of the previous archive is still being taken"}};
        update_metadata(dir, "photo", metadata["photo"]);
    }
    return metadata;
}

// Names of the selected printer, filament(s) and process profiles, for the archive route.
json selected_preset_names()
{
    auto* bundle = wxGetApp().preset_bundle;
    if (!bundle)
        return nullptr;
    json filaments = json::array();
    for (const std::string& name : bundle->filament_presets)
        filaments.push_back(name);
    return {{"printer", bundle->printers.get_selected_preset().name}, {"filament", filaments},
            {"process", bundle->prints.get_selected_preset().name}};
}

// --- question flow (GUI thread) --------------------------------------------------------------

void record(const json& print, const std::string& verdict, const std::string& note, json& outcome)
{
    try {
        // The MCP client may have recorded a verdict for this print while the question was open:
        // keep that one instead of writing a second verdict line.
        if (print_log_has_verdict(print.value("log", std::string()))) {
            outcome["verdict_recorded"]        = true;
            outcome["verdict_already_present"] = true;
            return;
        }
        json body = {{"name", print.value("log", std::string())}, {"verdict", verdict}};
        if (!note.empty())
            body["note"] = note;
        outcome["verdict_recorded"] = record_print_verdict(body).value("success", false);
    } catch (const std::exception& error) {
        outcome["verdict_recorded"] = false;
        outcome["verdict_error"] = error.what();
        BOOST_LOG_TRIVIAL(warning) << "Print Advisor: verdict not recorded: " << error.what();
    }
}

// Asks about the pending print and applies the answer to that print, even when a newer completed
// print replaces the pending question while a dialog is open (the new one is asked right after).
void show_dialog()
{
    State& st = state();
    const json print = st.pending;
    wxWindow* parent = wxGetApp().mainframe;
    if (parent)
        parent->Raise();
    // `showing` stays set through both dialogs and the archive, so try_show() does not re-enter.
    struct ShowingGuard
    {
        bool& flag;
        ~ShowingGuard() { flag = false; }
    } guard{st.showing};
    st.showing = true;

    int choice = CHOICE_LATER;
    {
        VerdictDialog dialog(parent, print);
        choice = dialog.ShowModal();
    }
    // The server may have stopped or the application started closing while the dialog was open:
    // the question stays pending (memory and file) and nothing is recorded or archived.
    if (flow_aborted())
        return;

    json outcome = {{"action", "print_verdict_dialog"}, {"log", print.value("log", json(nullptr))}, {"file", print.value("file", json(nullptr))},
                    {"outcome", "later"}};
    bool answered = false;
    if (choice == CHOICE_BAD) {
        Reason reason;
        bool recorded = false;
        {
            ReasonDialog dialog(parent);
            recorded = dialog.ShowModal() == wxID_OK;
            if (recorded)
                reason = dialog.reason();
        }
        if (flow_aborted())
            return;
        if (recorded) {
            answered = true;
            outcome["outcome"] = "defects";
            outcome["verdict"] = reason.verdict;
            outcome["note"] = reason.note;
            record(print, reason.verdict, reason.note, outcome);
        }
    } else if (choice == CHOICE_GOOD) {
        answered = true;
        outcome["outcome"] = "good";
        outcome["verdict"] = "ok";
        record(print, "ok", {}, outcome);
        // Archive only together with a verdict written now: a failed write keeps the question
        // pending (each retry would add another archive folder), and a verdict the assistant
        // already recorded may say otherwise.
        if (outcome.value("verdict_recorded", false) && !outcome.value("verdict_already_present", false)) {
            try {
                const json metadata = archive_print(print);
                outcome["archive"] = metadata.value("name", json(nullptr));
                const std::string dir = (archive_dir() / metadata.value("name", std::string())).string();
                print_advisor_notify(to_u8(_L("Print archived")), dir);
            } catch (const std::exception& error) {
                outcome["archive_error"] = error.what();
                print_advisor_notify(to_u8(_L("Archive failed")), error.what());
            }
        } else
            outcome["archive_skipped"] = true;
    }

    // A newer print may have replaced the pending question meanwhile: the answer above was
    // applied to `print` anyway; a "later" for `print` is lost, since only one question is kept.
    // Compared by log name: a repeated request for the same print only refreshes pending_since_ms.
    const bool replaced = !st.pending.is_object() ||
                          st.pending.value("log", std::string()) != print.value("log", std::string());
    // The answer counts only once it is in the print log: otherwise the question stays pending
    // (asked again after the next server start) and the user is told. A log that no longer
    // exists can never take the verdict: the question is dropped instead.
    const bool recorded = answered && outcome.value("verdict_recorded", false);
    const bool log_missing = answered && !recorded && !print_log_exists(print.value("log", std::string()));
    if (answered && recorded && outcome.value("verdict_already_present", false))
        print_advisor_notify(to_u8(_L("Print verdict already recorded")),
                             to_u8(_L("The assistant had already recorded a verdict for this print: your answer was not saved and "
                                      "nothing was archived.")));
    else if (log_missing)
        print_advisor_notify(to_u8(_L("Print verdict not recorded")), to_u8(_L("The print log no longer exists.")));
    else if (answered && !recorded)
        print_advisor_notify(to_u8(_L("Print verdict not recorded")),
                             sanitized_text(outcome.value("verdict_error", std::string("print log not writable")), 200));
    if (log_missing)
        outcome["log_missing"] = true;
    if (replaced)
        outcome["outcome"] = outcome["outcome"].get<std::string>() + "_then_replaced";
    else if (recorded || log_missing) {
        st.pending = nullptr;
        save_pending();
    }
    // Otherwise "later": stays pending (memory and file), asked again after the next server start.
    append_history(outcome);
    if (replaced) {
        st.showing = false;
        try_show();
    }
}

void try_show()
{
    State& st = state();
    if (!wxTheApp || st.pending.is_null() || st.showing || st.stopping->load())
        return;
    if (drop_pending_without_log())
        return;
    if (modal_window_open()) {
        schedule(RETRY_BLOCKED_MS);
        return;
    }
    try {
        show_dialog();
    } catch (const std::exception& error) {
        BOOST_LOG_TRIVIAL(error) << "Print Advisor verdict dialog failed: " << error.what();
    }
}

// --- routes --------------------------------------------------------------------------------

// Archive folder named `name`; the directory listing is the only accepted source.
fs::path existing_archive(const json& body)
{
    if (!body.contains("name") || !body["name"].is_string())
        throw std::runtime_error("name is required");
    const std::string name = body["name"].get<std::string>();
    if (!plain_entry_name(name))
        throw std::runtime_error("Invalid archive name");
    boost::system::error_code ec;
    for (fs::directory_iterator it(archive_dir(), ec), end; !ec && it != end; it.increment(ec))
        if (it->path().filename().string() == name && fs::is_directory(it->path(), ec))
            return it->path();
    throw std::runtime_error("Unknown archive: " + name);
}

json list_archive()
{
    std::vector<fs::path> dirs;
    boost::system::error_code ec;
    for (fs::directory_iterator it(archive_dir(), ec), end; !ec && it != end; it.increment(ec))
        if (fs::is_directory(it->path(), ec))
            dirs.push_back(it->path());
    std::sort(dirs.begin(), dirs.end(), [](const fs::path& a, const fs::path& b) { return a.filename().string() > b.filename().string(); });
    if (dirs.size() > MAX_LISTED_ARCHIVES)
        dirs.resize(MAX_LISTED_ARCHIVES);
    json archives = json::array();
    for (const fs::path& dir : dirs) {
        json metadata;
        {
            std::lock_guard<std::mutex> lock(state().archive_mutex);
            metadata = read_json_file(dir / "metadata.json", MAX_METADATA_BYTES);
        }
        // Archives made before the source was stored as a file name hold the full path.
        if (metadata.is_object() && metadata.contains("project") && metadata["project"].is_object() &&
            metadata["project"].contains("source") && metadata["project"]["source"].is_string())
            metadata["project"]["source"] = fs::path(metadata["project"]["source"].get<std::string>()).filename().string();
        json files = json::array();
        for (fs::directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec))
            if (fs::is_regular_file(it->path(), ec))
                files.push_back({{"name", it->path().filename().string()}, {"bytes", fs::file_size(it->path(), ec)}});
        archives.push_back({{"name", dir.filename().string()}, {"path", data_relative_path(dir)}, {"metadata", metadata}, {"files", files},
                            {"has_project", fs::is_regular_file(dir / "project.3mf", ec)}});
    }
    json pending = read_json_file(pending_path(), MAX_METADATA_BYTES);
    // Paths are relative to the MentorSlicer data folder.
    return {{"success", true}, {"folder", data_relative_path(archive_dir())}, {"archives", archives},
            {"pending_verdict", pending.is_object() ? pending : json(nullptr)}};
}

// Removes temporary project copies (reopen_archived_project) older than a day, except the one of
// the project currently open. Best effort. GUI thread.
void prune_tmp_projects()
{
    boost::system::error_code ec;
    if (!fs::is_directory(tmp_dir(), ec))
        return;
    fs::path open_project;
    if (Plater* plater = wxGetApp().plater()) {
        const std::string open_name = to_u8(plater->get_project_filename(".3mf"));
        if (!open_name.empty())
            open_project = fs::path(open_name);
    }
    const std::time_t now = std::time(nullptr);
    std::vector<fs::path> old;
    for (fs::directory_iterator it(tmp_dir(), ec), end; !ec && it != end; it.increment(ec)) {
        boost::system::error_code entry_ec;
        if (!fs::is_regular_file(it->path(), entry_ec) || entry_ec)
            continue;
        const std::time_t modified = fs::last_write_time(it->path(), entry_ec);
        if (entry_ec || now - modified < TMP_MAX_AGE_S)
            continue;
        if (!open_project.empty() && fs::equivalent(it->path(), open_project, entry_ec) && !entry_ec)
            continue;
        // The open project is untitled after a reopen (its file name is cleared): the copy it
        // was loaded from is known only from here.
        if (!state().last_tmp_project.empty() && fs::equivalent(it->path(), state().last_tmp_project, entry_ec) && !entry_ec)
            continue;
        old.push_back(it->path());
    }
    for (const fs::path& path : old) {
        fs::remove(path, ec);
        if (ec)
            BOOST_LOG_TRIVIAL(info) << "Print Advisor: could not remove the temporary project " << path.filename().string() << ": " << ec.message();
    }
}

json open_archive(const json& body, const std::atomic<bool>& stopping)
{
    const fs::path dir = existing_archive(body);
    const fs::path project = dir / "project.3mf";
    boost::system::error_code ec;
    if (!fs::is_regular_file(project, ec) || ec)
        throw std::runtime_error("This archive has no project.3mf");
    Plater* plater = wxGetApp().plater();
    if (!plater)
        throw std::runtime_error("Plater is unavailable");
    json metadata;
    {
        std::lock_guard<std::mutex> lock(state().archive_mutex);
        metadata = read_json_file(dir / "metadata.json", MAX_METADATA_BYTES);
    }
    const std::string file = metadata.is_object() ? text_of(field(metadata, "file"), "?") : "?";
    std::string question = to_u8(wxString::Format(_L("Open the archived project \"%s\" (print of %s) in MentorSlicer? "
                                                  "The current project is closed (you will be asked to save it) and no print is started. "
                                                  "The printer, filament and process profiles saved in the archived project are loaded "
                                                  "and become the active selection."),
                                               wxString::FromUTF8(sanitized_text(dir.filename().string(), 100)),
                                               wxString::FromUTF8(sanitized_text(file, 100))));
    const std::string reason = reason_text(body.value("reason", json(nullptr)));
    if (!reason.empty())
        question += "\n" + reason;
    if (!ask_user(to_u8(_L("Open archived project")), question, stopping))
        return {{"success", false}, {"cancelled", true}};
    const json presets_before = selected_preset_names();

    // A temporary copy is loaded, never the archived file, so a later "Save" cannot write over
    // the archive. The project is then made untitled again (same call as Plater::priv::reset):
    // "Save" asks for a name.
    prune_tmp_projects();
    fs::create_directories(tmp_dir(), ec);
    const fs::path temp = tmp_dir() / (dir.filename().string() + ".3mf");
    fs::copy_file(project, temp, fs::copy_options::overwrite_existing, ec);
    if (ec)
        throw std::runtime_error("Could not copy the archived project: " + ec.message());
    // The copy keeps the archive's modification time: date it now, so the one-day pruning
    // counts from the reopen.
    boost::system::error_code time_ec;
    fs::last_write_time(temp, std::time(nullptr), time_ec);
    state().last_tmp_project = temp;
    plater->load_project(wxString::FromUTF8(temp.string()));
    const bool opened = fs::equivalent(fs::path(to_u8(plater->get_project_filename(".3mf"))), temp, ec) && !ec;
    if (opened)
        plater->set_project_filename("");
    // Loading a 3MF also selects the profiles it embeds: report the selection before and after.
    const json presets_after = selected_preset_names();
    append_history({{"action", "archive_open"}, {"archive", dir.filename().string()}, {"opened", opened},
                    {"presets_before", presets_before}, {"presets_after", presets_after}});
    json result = {{"success", opened}, {"archive", dir.filename().string()}, {"project", data_relative_path(temp)},
                   {"presets_before", presets_before}, {"presets_after", presets_after},
                   {"warning", "A temporary copy of the archived project was opened as an untitled project: saving it asks for a "
                               "name (use Save As), the archived copy is never written. The profiles embedded in the project "
                               "are now the active selection (see presets_before/presets_after)"}};
    if (!opened)
        result["error"] = "The project was not opened (cancelled, or another project is loading)";
    return result;
}

} // namespace

void request_print_verdict(const json& print)
{
    if (!wxTheApp || !print.is_object() || !print.contains("log") || !print["log"].is_string())
        return;
    wxTheApp->CallAfter([print] {
        State& st = state();
        if (st.pending.is_object() && st.pending.value("log", std::string()) != print.value("log", std::string()))
            append_history({{"action", "print_verdict_dialog"}, {"log", st.pending.value("log", json(nullptr))}, {"outcome", "replaced"}});
        st.pending = print;
        st.pending["pending_since_ms"] = wall_ms();
        save_pending();
        const std::string file = sanitized_text(text_of(field(print, "file"), to_u8(_L("(unknown file)"))), 100);
        print_advisor_notify(to_u8(_L("Print finished")), to_u8(wxString::Format(_L("How did the print of %s turn out?"), wxString::FromUTF8(file))));
        try_show();
    });
}

void verdict_on_server_started()
{
    State& st = state();
    st.stopping = std::make_shared<std::atomic<bool>>(false);
    try {
        prune_tmp_projects();
    } catch (...) {
    }
    if (st.pending.is_null()) {
        json pending = read_json_file(pending_path(), MAX_METADATA_BYTES);
        if (pending.is_object() && pending.contains("log") && pending["log"].is_string())
            st.pending = std::move(pending);
    }
    if (st.pending.is_object())
        schedule(ASK_AFTER_START_MS);
}

void verdict_on_server_stopped()
{
    State& st = state();
    st.stopping->store(true);
    if (st.timer)
        st.timer->Stop();
    // The photo worker polls `stopping` every 100 ms and waits at most 10 s for the page.
    if (st.photo_worker.joinable())
        st.photo_worker.join();
}

void verdict_recorded_by_client(const std::string& log_name)
{
    State& st = state();
    // While the dialog is open the route is refused (modal window), so this runs only between
    // questions; a dialog would anyway discard its answer once the log holds a verdict.
    if (st.showing || !st.pending.is_object() || st.pending.value("log", std::string()) != log_name)
        return;
    append_history({{"action", "print_verdict_dialog"}, {"log", log_name}, {"outcome", "recorded_by_assistant"}});
    st.pending = nullptr;
    save_pending();
    if (st.timer)
        st.timer->Stop();
}

bool print_log_has_verdict(const std::string& log_name)
{
    if (!plain_entry_name(log_name))
        return false;
    fs::ifstream file(prints_dir() / log_name, std::ios::binary);
    std::string line;
    while (std::getline(file, line)) {
        // Cheap filter first: the log also holds thousands of telemetry points.
        if (line.find("\"verdict\"") == std::string::npos)
            continue;
        const json value = json::parse(line, nullptr, false);
        if (value.is_object() && value.contains("verdict") && value["verdict"].is_boolean() && value["verdict"].get<bool>())
            return true;
    }
    return false;
}

std::vector<AdvisorRoute> archive_routes()
{
    std::vector<AdvisorRoute> routes;
    // File reads only: runs on the server thread.
    routes.push_back({"/v1/archive", "GET", false, [](const json&, const std::atomic<bool>&) { return list_archive(); }});
    routes.push_back({"/v1/archive/open", "POST", true, [](const json& body, const std::atomic<bool>& stopping) { return open_archive(body, stopping); }});
    return routes;
}

} // namespace advisor
} // namespace GUI
} // namespace Slic3r
