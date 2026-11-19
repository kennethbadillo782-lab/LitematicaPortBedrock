#pragma once
#include "tasks/TaskTypes.h"
#include <cstdint>
namespace lholo::tasks {
enum class Action {
    Create,
    Select,
    Rename,
    Delete,
    AddFile,
    Path,
    Visible,
    Move,
    RemoveFile,
    SelectFile,
    Activate,
    Close,
    DuplicateFile,
    LoadSchematic,
    UnloadSchematic,
    CreatePlacement,
    BrowseDirectory,
    SaveSchematic,
    RenamePlacement,
    ToolCommand,
    VerifierCommand,
    JavaConfigCommand,
    FileOperation, // Schematic Manager: rename / copy / delete a file in the browsed directory.
    Acknowledge // Complete a request without changing the catalog; not a public protocol action.
};
struct Intent {
    std::uint64_t id{}, revision{};
    Action action{};
    std::string taskId, fileId, value;
    int delta{};
};
void migrateToPlacementManager(Catalog&);
bool applyCatalogEdit(Catalog&, Intent const&);
} // namespace lholo::tasks
