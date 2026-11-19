#include "app/JavaConfigActions.h"
#include "oreui/TaskBindings.h"
#include "oreui/TaskProtocol.h"
#include "app/TaskContext.h"
#include "app/ToolActions.h"
#include "app/VerifierActions.h"
#include <filesystem>
#include <algorithm>
#include "structure/StructurePaths.h"
#include "ui/StructureFilePicker.h"
#include "i18n/Translator.h"
#include "nlohmann/json.hpp"
#include <mutex>
namespace lholo::oreui {
namespace {
std::mutex mutex;
ui::StructureFilePicker picker;
ui::StructureFilePicker savePicker{ui::selectMcstructureOutput};
std::optional<app::TaskIntent> saveRequest;
std::optional<app::TaskIntent> browse;
std::optional<app::TaskStatus> published;
bool publishedBusy{};
std::filesystem::path browserDirectory;
bool browserDirty{true};
std::uint64_t publishedToolRevision{},publishedVerifierRevision{},publishedConfigRevision{};
} // namespace
bool queueTaskIntent(std::string_view payload) {
    auto intent = parseTaskIntent(payload);
    if (!intent)
        return false;
    std::lock_guard lock(mutex);
    if(intent->action==app::TaskAction::SaveSchematic&&intent->value.empty()) {
        auto snapshot=app::getTaskSnapshot();
        auto source=std::find_if(snapshot.schematics.begin(),snapshot.schematics.end(),[&](auto const& item){return item.id==intent->fileId;});
        if(source==snapshot.schematics.end()||saveRequest)return false;
        try {if(!savePicker.start(structure::detail::pathFromUtf8(source->path)))return false;}
        catch(std::exception const&){return false;}
        saveRequest=std::move(intent);return true;
    }
    if (intent->action == app::TaskAction::FileOperation) {
        // Schematic Manager file operations. `fileId` is the operation, `value` the source file, `taskId` the new name.
        auto finish = [&](std::string error) {
            intent->action = app::TaskAction::Acknowledge;
            intent->value = std::move(error);
            return app::submitTaskIntent(std::move(*intent));
        };
        try {
            namespace fs = std::filesystem;
            auto const operation = intent->fileId;
            auto const source = structure::detail::pathFromUtf8(intent->value);
            auto extension = source.extension().wstring();
            std::transform(extension.begin(), extension.end(), extension.begin(), ::towlower);
            if (browserDirectory.empty() || !fs::is_regular_file(source) || !fs::equivalent(source.parent_path(), browserDirectory)
                || (extension != L".litematic" && extension != L".mcstructure"))
                return finish("Not a schematic file in the current directory");
            for (auto const& loaded : app::getTaskSnapshot().schematics)
                if (fs::exists(structure::detail::pathFromUtf8(loaded.path)) && fs::equivalent(structure::detail::pathFromUtf8(loaded.path), source))
                    return finish("Unload the schematic before changing its file");
            if (operation == "delete") {
                // Recoverable: the file is renamed, never erased.
                auto target = source;
                target += L".deleted";
                for (int n = 1; fs::exists(target); ++n) target = source.wstring() + L".deleted" + std::to_wstring(n);
                fs::rename(source, target);
            } else if (operation == "rename" || operation == "copy") {
                auto name = structure::detail::pathFromUtf8(intent->taskId);
                auto const text = name.wstring();
                if (text.empty() || text.size() > 200 || text.front() == L'.' || text.find_first_of(L"\\/:*?\"<>|") != std::wstring::npos)
                    return finish("Invalid file name");
                auto newExtension = name.extension().wstring();
                std::transform(newExtension.begin(), newExtension.end(), newExtension.begin(), ::towlower);
                if (newExtension.empty()) name += extension;
                else if (newExtension != extension) return finish("The extension must stay the same");
                auto target = browserDirectory / name;
                if (fs::exists(target)) return finish("A file with that name already exists");
                if (operation == "rename") fs::rename(source, target);
                else fs::copy_file(source, target);
            } else {
                return finish("Unknown file operation");
            }
            browserDirty = true;
            return finish({});
        } catch (std::exception const& error) { return finish(error.what()); }
    }
    if (intent->action == app::TaskAction::BrowseDirectory) {
        auto fail = [&](std::string error) {
            intent->action = app::TaskAction::Acknowledge;
            intent->value = std::move(error);
            return app::submitTaskIntent(std::move(*intent));
        };
        try {
            auto directory = intent->value.empty() ? app::detail::taskStorePath().parent_path() / "schematics"
                : structure::detail::pathFromUtf8(intent->value);
            if (intent->value.empty()) std::filesystem::create_directories(directory);
            if (!std::filesystem::is_directory(directory)) return fail("Directory does not exist");
            browserDirectory = std::filesystem::weakly_canonical(directory);
            browserDirty = true;
            intent->action = app::TaskAction::Acknowledge;
            intent->value.clear();
            return app::submitTaskIntent(std::move(*intent));
        } catch (std::exception const& error) { return fail(error.what()); }
    }
    if (intent->action == app::TaskAction::AddFile && intent->value.empty()) {
        auto snapshot = app::getTaskSnapshot();
        if (!snapshot.writable || snapshot.loading || snapshot.revision != intent->revision)
            return app::submitTaskIntent(std::move(*intent));
        if (browse)
            return false;
        auto* task = tasks::findTask(snapshot.catalog, intent->taskId);
        if (!task)
            return false;
        auto failed = [&] {
            intent->action = app::TaskAction::Acknowledge;
            intent->value = i18n::tr(i18n::TextKey::OreUiBrowseFailed);
            return app::submitTaskIntent(std::move(*intent));
        };
        try {
            if (!intent->fileId.empty()) {
                auto* file = tasks::findFile(*task, intent->fileId);
                if (!file)
                    return false;
                if (!picker.start(structure::detail::pathFromUtf8(file->path)))
                    return failed();
            } else if (!picker.start({}))
                return failed();
        } catch (std::exception const&) {
            return failed();
        }
        browse = std::move(intent);
        return true;
    }
    return app::submitTaskIntent(std::move(*intent));
}
std::optional<std::string> takeTaskUpdate(bool force) {
    std::lock_guard lock(mutex);
    if (auto selected = picker.takeResult(); selected && browse) {
        auto intent = std::move(*browse);
        browse.reset();
        if (selected->path) {
            intent.action = intent.fileId.empty() ? app::TaskAction::AddFile : app::TaskAction::Path;
            intent.value = structure::detail::pathToUtf8(*selected->path);
            app::submitTaskIntent(std::move(intent));
        } else {
            intent.action = app::TaskAction::Acknowledge;
            if (selected->errorCode)
                intent.value = i18n::tr(i18n::TextKey::OreUiBrowseFailed);
            app::submitTaskIntent(std::move(intent));
        }
    }
    if(auto result=savePicker.takeResult();result&&saveRequest){
        auto intent=std::move(*saveRequest);saveRequest.reset();
        if(result->path)intent.value=structure::detail::pathToUtf8(*result->path);
        else{intent.action=app::TaskAction::Acknowledge;if(result->errorCode)intent.value="Could not open save dialog";}
        app::submitTaskIntent(std::move(intent));
    }
    auto status = app::getTaskStatus();
    auto tool=app::toolStatus();
    auto javaConfig=app::javaConfigSnapshot();auto configRevision=javaConfig?javaConfig->at("revision").get<std::uint64_t>():0;
    auto verifier=app::verifierStatus();
    auto busy = picker.busy() || savePicker.busy();
    if (!force && publishedConfigRevision==configRevision && publishedVerifierRevision==verifier.revision && publishedToolRevision==tool.revision && !browserDirty && published == status && publishedBusy == busy)
        return {};
    auto s = app::getTaskSnapshot();
    auto list = nlohmann::json::array();
    for (auto const& task : s.catalog.tasks) {
        auto files = nlohmann::json::array();
        for (auto const& file : task.files)
            files.push_back({{"id", file.id},
                             {"path", file.path},
                             {"name", file.name.empty()?structure::detail::pathToUtf8(
                                          structure::detail::pathFromUtf8(file.path).filename()):file.name},
                             {"hidden", !file.visible},
                             {"dimension", file.dimension ? nlohmann::json(*file.dimension) : nlohmann::json(nullptr)}});
        list.push_back({{"id", task.id},
                        {"name", task.name},
                        {"selectedFileId", task.selectedFileId},
                        {"layers", files}});
    }
    auto sources = nlohmann::json::array();
    for (auto const& source : s.schematics)
        sources.push_back({{"id", source.id}, {"path", source.path}, {"name", source.name},
            {"size", {source.sizeX, source.sizeY, source.sizeZ}}});
    auto entries = nlohmann::json::array();
    std::string browserError;
    try {
        if (browserDirectory.empty()) {
            browserDirectory = app::detail::taskStorePath().parent_path() / "schematics";
            std::filesystem::create_directories(browserDirectory);
        }
        std::vector<std::filesystem::directory_entry> sorted;
        for (auto const& entry : std::filesystem::directory_iterator(browserDirectory)) {
            auto extension = entry.path().extension().wstring();
            std::transform(extension.begin(), extension.end(), extension.begin(), ::towlower);
            if (entry.is_directory() || extension == L".litematic" || extension == L".mcstructure")
                sorted.push_back(entry);
        }
        std::sort(sorted.begin(), sorted.end(), [](auto const& a, auto const& b) {
            if (a.is_directory() != b.is_directory()) return a.is_directory();
            return a.path().filename() < b.path().filename();
        });
        for (auto const& entry : sorted)
            entries.push_back({{"name", structure::detail::pathToUtf8(entry.path().filename())},
                {"path", structure::detail::pathToUtf8(entry.path())}, {"directory", entry.is_directory()}});
    } catch (std::exception const& error) { browserError = error.what(); }
    auto mismatchGroups=nlohmann::json::array();
    for(auto const& group:verifier.groups)mismatchGroups.push_back({{"id",group.id},{"kind",group.kind},{"expected",group.expected},
        {"found",group.found},{"count",group.count},{"ignored",group.ignored},{"positions",group.positions}});
    auto json = nlohmann::json{{"tasks", list},
                               {"javaConfig",javaConfig?*javaConfig:nlohmann::json(nullptr)},
                               {"verifier", {{"checked",verifier.checked},{"total",verifier.total},{"running",verifier.running},
                                   {"complete",verifier.complete},{"waitingChunks",verifier.waitingChunks},{"showMarkers",verifier.showMarkers},{"placementId",verifier.placementId},
                                   {"name",verifier.name},{"error",verifier.error},{"counts",verifier.counts},{"unlisted",verifier.unlistedMismatches},{"groups",mismatchGroups}}},
                               {"tool", {{"mode", static_cast<int>(tool.mode)}, {"creative",tool.creative},{"localWriteAvailable",tool.localWriteAvailable},{"companionAvailable",tool.companionAvailable},{"held",tool.held},
                                   {"running",tool.running},{"done",tool.done},{"total",tool.total},{"error",tool.error},
                                   {"moveTarget",tool.moveTarget?nlohmann::json(*tool.moveTarget):nlohmann::json(nullptr)},
                                   {"primary",tool.primary},{"secondary",tool.secondary},{"gridNegative",tool.gridNegative},
                                   {"gridPositive",tool.gridPositive},{"gridStep",tool.gridStep}}},
                               {"schematics", sources},
                               {"browser", {{"path", structure::detail::pathToUtf8(browserDirectory)},
                                   {"parent", structure::detail::pathToUtf8(browserDirectory.parent_path())},
                                   {"entries", entries}, {"error", browserError}}},
                               {"currentTaskId", s.catalog.selectedTaskId},
                               {"activeTaskId", s.activeTaskId},
                               {"revision", s.revision},
                               {"ack", s.ack},
                               {"accepted", s.accepted},
                               {"loading", s.loading},
                               {"browsing", busy},
                               {"worldAvailable", s.worldAvailable},
                               {"writable", s.writable},
                               {"error", s.error},
                               {"errorFileId", s.errorFileId}}
                    .dump();
    published = static_cast<app::TaskStatus const&>(s);
    publishedBusy = busy;
    publishedToolRevision=tool.revision;
    publishedVerifierRevision=verifier.revision;
    publishedConfigRevision=configRevision;
    browserDirty = false;
    return json;
}
void resetTaskBindings() {
    std::lock_guard lock(mutex);
    picker.cancel();
    savePicker.cancel();saveRequest.reset();
    browse.reset();
    published.reset();
}
void shutdownTaskBindings() {
    resetTaskBindings();
    picker.shutdown();
    savePicker.shutdown();
}
} // namespace lholo::oreui
