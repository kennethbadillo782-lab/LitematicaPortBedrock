#include "oreui/CaptureBindings.h"
#include "oreui/CaptureProtocol.h"
#include "app/TaskContext.h"
#include "app/JavaConfigActions.h"
#include "structure/StructurePaths.h"
#include <filesystem>
#include <string_view>
#include "structure/capture/CaptureBounds.h"
#include "structure/capture/SelectionJson.h"
#include "i18n/Translator.h"
#include "ui/StructureFilePicker.h"
#include "nlohmann/json.hpp"
#include <mutex>
#include <utility>

namespace lholo::oreui {
namespace {
std::mutex mutex;
std::optional<std::string> intent;
ui::StructureFilePicker outputPicker{ui::selectMcstructureOutput, ui::StructureFileTypes::Projection};
std::optional<structure::capture::Request> inFlight;
std::string published, error;
i18n::TextKey customStatus{};
std::uint64_t ack{};
bool selecting{};


}
bool queueCaptureIntent(std::string_view payload) {
    std::lock_guard lock(mutex);
    if (payload.size() > 262144 || intent || inFlight || outputPicker.busy()) return false;
    intent = payload;
    return true;
}

std::optional<std::string> takeCaptureUpdate(bool force) {
    using namespace structure::capture;
    std::lock_guard lock(mutex);
    auto snapshot = getSnapshot();
    if (intent) {
        auto request = parseCaptureIntent(*std::exchange(intent, std::nullopt));
        if (request && request->id > ack) {
            error.clear();
            bool const emptyExport = request->action == Action::ExportEmpty;
            if (emptyExport) customStatus = i18n::TextKey::None;
            if (!emptyExport && (!snapshot.worldAvailable || request->revision != snapshot.revision)) {
                ack = request->id;
                error = i18n::tr(i18n::TextKey::OreUiCaptureChanged);

            } else if(request->action==Action::SelectionCommand&&request->command=="cornerMode"&&app::javaConfigReady()){
                if(!app::queueJavaConfigCommand("cycleCorners", ""))error="Configuration queue is full";ack=request->id;
            } else if (request->action == Action::Export || emptyExport) {
                try {
                    if (!emptyExport) {
                        // Like Litematica, the schematic goes straight into the schematics folder (the one the browser opens first).
                        auto name = snapshot.draft.name;
                        for (auto& c : name) if (std::string_view{"\\/:*?\"<>|"}.find(c) != std::string_view::npos || static_cast<unsigned char>(c) < 32) c = '_';
                        while (!name.empty() && (name.back() == ' ' || name.back() == '.')) name.pop_back();
                        if (name.empty()) name = "schematic";
                        auto directory = app::detail::taskStorePath().parent_path() / "schematics";
                        std::filesystem::create_directories(directory);
                        auto path = directory / structure::detail::pathFromUtf8(name + ".litematic");
                        for (int n = 2; std::filesystem::exists(path); ++n) path = directory / structure::detail::pathFromUtf8(name + " (" + std::to_string(n) + ").litematic");
                        inFlight = *request;
                        inFlight->output = std::move(path);
                        if (!submitAction(*inFlight)) { ack = inFlight->id; inFlight.reset(); }
                    } else if (outputPicker.start(std::filesystem::path{})) { inFlight = *request; selecting = true; }
                    else ack = request->id;
                } catch (std::exception const&) {
                    ack = request->id;
                    error = i18n::tr(i18n::TextKey::OreUiSaveFailed);
                }
            } else if (submitAction(*request)) inFlight = *request;
            else ack = request->id;
        }
    }
    if (auto result = outputPicker.takeResult(); result && inFlight && selecting) {
        selecting = false;
        if (result->path && (inFlight->action == Action::ExportEmpty
            || (snapshot.worldAvailable && snapshot.revision == inFlight->revision))) {
            inFlight->output = std::move(*result->path);
            if (!submitAction(*inFlight)) { ack = inFlight->id; inFlight.reset(); }
        } else {
            if (result->errorCode) error = i18n::tr(i18n::TextKey::OreUiSaveFailed);
            else if (result->path) error = i18n::tr(i18n::TextKey::OreUiCaptureChanged);
            ack = inFlight->id;
            inFlight.reset();
        }
    }
    if (inFlight && !selecting && completedAction() == inFlight->id && !actionPending()) {
        if (inFlight->action == Action::ExportEmpty)
            customStatus = emptyExportSucceeded() ? i18n::TextKey::CaptureStatusExported : i18n::TextKey::CaptureStatusWriteFailed;
        ack = inFlight->id;
        inFlight.reset();
        snapshot = getSnapshot();
    }
    // World/dimension changes dismiss a stale save dialog before it can export.
    if (inFlight && inFlight->action != Action::ExportEmpty
        && (!snapshot.worldAvailable || (selecting && snapshot.revision != inFlight->revision))) {
        outputPicker.cancel();
        ack = inFlight->id;
        inFlight.reset();
        selecting = false;
        error = i18n::tr(i18n::TextKey::OreUiCaptureChanged);
    }
    auto state = nlohmann::json{
        {"draft", selectionJson(snapshot.draft)},
        {"selections", [&] {auto list=nlohmann::json::array();for(auto const& saved:snapshot.selections)list.push_back({{"id",saved.id},{"name",saved.name}});return list;}()},
        {"analysis", snapshot.analysis},
        {"worldAvailable", snapshot.worldAvailable}, {"status", snapshot.status}, {"customStatus", i18n::tr(customStatus)},
        {"revision", snapshot.revision}, {"ack", ack}, {"maxEmptyCells", kMaximumEmptyStructureCells},
        {"busy", inFlight.has_value() || outputPicker.busy() || actionPending()}, {"error", error.empty() ? snapshot.error : error}
    }.dump();
    if (!force && published == state) return std::nullopt;
    published = state;
    return state;
}
void resetCaptureBindings() {
    std::lock_guard lock(mutex);
    intent.reset();
    outputPicker.cancel();
    if (inFlight) ack = inFlight->id;
    inFlight.reset();
    selecting = false;
    error.clear();
    customStatus = i18n::TextKey::None;
    published.clear();
}
void shutdownCaptureBindings() { resetCaptureBindings(); outputPicker.shutdown(); }
}
