#include "PrintAdvisorInternal.hpp"

#include "slic3r/GUI/GUI_App.hpp"
#include "slic3r/GUI/print_manage/App/WebSocketProxyAdvisor.hpp"
#include "libslic3r/PresetBundle.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <string>
#include <vector>

namespace Slic3r {
namespace GUI {
namespace advisor {

namespace {

using json = nlohmann::json;

bool is_idle(int state) { return state == 0 || state == 2 || state == 3 || state == 4; }
bool is_printing(int state) { return state == 1 || state == 5 || state == 6; }

// Current value of a telemetry field as text for the confirmation table.
std::string telemetry_text(const json& telemetry, const char* key, const char* unit)
{
    if (!telemetry.is_object() || !telemetry.contains(key))
        return "?";
    const double value = json_number(telemetry[key]);
    if (value < 0)
        return "?";
    return std::to_string(int(std::lround(value))) + unit;
}

// Whole number in [min, max], from a JSON number.
int integer_in_range(const json& body, const char* key, int min, int max)
{
    if (!body.contains(key) || !body[key].is_number())
        throw std::runtime_error(std::string(key) + " must be a number");
    const double value = body[key].get<double>();
    if (!std::isfinite(value) || value != std::floor(value) || value < min || value > max)
        throw std::runtime_error(std::string(key) + " must be a whole number between " + std::to_string(min) + " and " +
                                 std::to_string(max));
    return int(value);
}

// Direct LAN controls of the selected printer: fans, nozzle/bed target temperature, print speed
// and light. Each request sends only the fixed messages the device page sends for the same
// control (never free G-code), after a confirmation in MentorSlicer. Never starts a print.
json device_control(const json& body, const std::atomic<bool>& stopping)
{
    if (!body.is_object() || !body.contains("control") || !body["control"].is_string())
        throw std::runtime_error("control must be fan, temp, speed or light");
    const std::string control = body["control"].get<std::string>();
    auto* bundle = wxGetApp().preset_bundle;
    if (!bundle)
        throw std::runtime_error("PresetBundle is unavailable");

    // The light does not depend on the printer state.
    const DeviceSnapshot device = selected_device_snapshot(control != "light");
    const json telemetry = current_device_telemetry();
    const int state = device.state;
    // The device page refuses fan and light commands while the printer sleeps.
    if (telemetry.is_object() && telemetry.contains("sleepMode") && json_number(telemetry["sleepMode"]) == 1)
        throw std::runtime_error("La stampante è in sospensione: riattivala dalla scheda Dispositivo di MentorSlicer");

    std::vector<json> messages; // "params" objects, sent in order as {"method":"set","params":...}
    std::string title;
    std::string question;
    std::vector<AdvisorChangeRow> rows;
    json logged = {{"action", "device_control"}, {"control", control}};

    if (control == "fan") {
        if (!is_idle(state) && !is_printing(state))
            throw std::runtime_error(std::string("Fans can be set only while the printer is idle or printing (state: ") +
                                     state_name(state) + ")");
        const std::string fan = body.value("fan", std::string());
        const int percent = integer_in_range(body, "percent", 0, 100);
        // K1C mapping verified on the printer: P0 nozzle (part) fan, P2 big side fan
        // (fanAuxiliary), P1 rear exhaust fan (fanCase).
        const char* channel = nullptr;
        const char* label = nullptr;
        const char* telemetry_key = nullptr;
        if (fan == "part") {
            channel = "P0"; label = "Ventola pezzo (ugello)"; telemetry_key = "modelFanPct";
        } else if (fan == "side") {
            channel = "P2"; label = "Ventola laterale"; telemetry_key = "auxiliaryFanPct";
        } else if (fan == "exhaust") {
            channel = "P1"; label = "Ventola estrazione posteriore"; telemetry_key = "caseFanPct";
        } else {
            throw std::runtime_error("fan must be part, side or exhaust");
        }
        // Same rounding as the device page: Math.round(255 * percent / 100).
        const long pwm = std::lround(255.0 * percent / 100.0);
        messages.push_back({{"gcodeCmd", std::string("M106 ") + channel + " S" + std::to_string(pwm)}});
        title = "Ventola";
        question = std::string("Impostare la ") + label + " al " + std::to_string(percent) + "%?";
        rows.push_back({label, telemetry_text(telemetry, telemetry_key, "%"), std::to_string(percent) + "%"});
        logged["fan"] = fan;
        logged["percent"] = percent;
    } else if (control == "temp") {
        if (!is_printing(state))
            throw std::runtime_error(std::string("Temperatures can be changed only during a print (state: ") + state_name(state) + ")");
        const std::string target = body.value("target", std::string());
        const MachineLimits limits = machine_limits(bundle->printers.get_edited_preset().config);
        if (target == "nozzle") {
            const int value = integer_in_range(body, "value", 0, 350);
            const Preset& filament_preset = bundle->filaments.get_edited_preset();
            std::string filament_limits; // shown in the dialog: which profile the range comes from
            if (value != 0) {
                const DynamicPrintConfig& filament = filament_preset.config;
                const auto* low = filament.option<ConfigOptionInts>("nozzle_temperature_range_low");
                const auto* high = filament.option<ConfigOptionInts>("nozzle_temperature_range_high");
                if (!low || !high || low->values.empty() || high->values.empty() || low->values.front() <= 0 ||
                    high->values.front() <= low->values.front())
                    throw std::runtime_error("The active filament profile has no nozzle temperature range");
                if (value < low->values.front() || value > high->values.front())
                    throw std::runtime_error("Nozzle temperature must stay within the filament range " +
                                             std::to_string(low->values.front()) + "-" + std::to_string(high->values.front()) + " °C");
                filament_limits = sanitized_text(filament_preset.name) + " (" + std::to_string(low->values.front()) + "-" +
                                  std::to_string(high->values.front()) + " °C)";
                if (limits.max_nozzle_temp >= 0 && value > limits.max_nozzle_temp)
                    throw std::runtime_error("The printer reaches at most " + std::to_string(int(limits.max_nozzle_temp)) +
                                             " °C at the nozzle (" + limits.source + ")");
            }
            messages.push_back({{"nozzleTempControl", value}});
            title = "Temperatura ugello";
            question = value == 0 ? "SPEGNERE il riscaldamento dell'ugello durante la stampa?\n\nLa stampa in corso fallirà."
                                  : "Impostare l'ugello a " + std::to_string(value) + " °C durante la stampa?";
            rows.push_back({"Temperatura ugello", telemetry_text(telemetry, "targetNozzleTemp", " °C"), std::to_string(value) + " °C"});
            if (!filament_limits.empty())
                rows.push_back({"Filamento usato per i limiti", "-", filament_limits});
            logged["target"] = target;
            logged["value"] = value;
        } else if (target == "bed") {
            const int max_bed = limits.max_bed_temp >= 0 ? int(limits.max_bed_temp) : 120;
            const int value = integer_in_range(body, "value", 0, std::min(max_bed, 120));
            messages.push_back({{"bedTempControl", {{"num", 0}, {"val", value}}}});
            title = "Temperatura piatto";
            question = value == 0 ? "SPEGNERE il piatto durante la stampa?\n\nIl pezzo può staccarsi."
                                  : "Impostare il piatto a " + std::to_string(value) + " °C durante la stampa?";
            rows.push_back({"Temperatura piatto", telemetry_text(telemetry, "targetBedTemp0", " °C"), std::to_string(value) + " °C"});
            logged["target"] = target;
            logged["value"] = value;
        } else {
            throw std::runtime_error("target must be nozzle or bed");
        }
    } else if (control == "speed") {
        if (!is_printing(state))
            throw std::runtime_error(std::string("Print speed can be changed only during a print (state: ") + state_name(state) + ")");
        const int mode = integer_in_range(body, "mode", 25, 125);
        if (mode != 25 && mode != 50 && mode != 100 && mode != 125)
            throw std::runtime_error("mode must be 25, 50, 100 or 125");
        // Device page logic for LAN printers: printers that report speedMode use it for the
        // silent mode (25%) and leave it before setting another percentage.
        if (telemetry.is_object() && telemetry.contains("speedMode") && !telemetry["speedMode"].is_null()) {
            if (mode == 25) {
                messages.push_back({{"speedMode", 1}});
            } else {
                messages.push_back({{"speedMode", 0}});
                messages.push_back({{"setFeedratePct", mode}});
            }
        } else {
            messages.push_back({{"setFeedratePct", mode}});
        }
        title = "Velocità di stampa";
        question = "Impostare la velocità di stampa al " + std::to_string(mode) + "%?";
        rows.push_back({"Velocità di stampa", telemetry_text(telemetry, "curFeedratePct", "%"), std::to_string(mode) + "%"});
        logged["mode"] = mode;
    } else if (control == "light") {
        if (!body.contains("on") || !body["on"].is_boolean())
            throw std::runtime_error("on must be true or false");
        const bool on = body["on"].get<bool>();
        messages.push_back({{"lightSw", on ? 1 : 0}});
        title = "Luce";
        question = on ? "Accendere la luce della stampante?" : "Spegnere la luce della stampante?";
        std::string before = "?";
        if (telemetry.is_object() && telemetry.contains("lightSw"))
            before = json_number(telemetry["lightSw"]) == 1 ? "accesa" : "spenta";
        rows.push_back({"Luce", before, on ? "accesa" : "spenta"});
        logged["on"] = on;
    } else {
        throw std::runtime_error("control must be fan, temp, speed or light");
    }

    if (const std::string reason = reason_text(body.value("reason", json())); !reason.empty())
        question += "\n\n" + reason;
    if (!ask_user(title, question, stopping, rows))
        return {{"success", false}, {"cancelled", true}};
    check_device_unchanged(device);

    json sent = json::array();
    for (const json& params : messages) {
        const json message = {{"method", "set"}, {"params", params}};
        if (!WebSocketProxy::SendToDevice(device.address, message.dump())) {
            if (sent.empty())
                throw std::runtime_error("Apri la scheda Dispositivo di MentorSlicer");
            throw std::runtime_error("Only part of the command was sent (" + sent.dump() +
                                     "): open the Device tab of MentorSlicer and check the printer");
        }
        sent.push_back(message);
    }
    logged["sent"] = sent;
    append_history(std::move(logged));
    return {{"success", true}, {"sent", sent}, {"note", "queued to printer; verify with get_printer_status"}};
}

} // namespace

std::vector<AdvisorRoute> printer_control_routes()
{
    return {{"/v1/device_control", "POST", true, device_control}};
}

} // namespace advisor
} // namespace GUI
} // namespace Slic3r
