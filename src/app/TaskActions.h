#pragma once
#include "tasks/TaskCatalog.h"
#include <array>
#include <cstdint>
#include <memory>
namespace lholo::structure { struct JavaBlockState; struct LoadedStructure; }
namespace lholo::app {
using TaskAction = tasks::Action;
using TaskIntent = tasks::Intent;
struct TaskStatus {
    std::uint64_t revision{}, ack{};
    bool accepted{}, loading{}, worldAvailable{}, writable{};
    std::string activeTaskId, error, errorFileId;
    bool operator==(TaskStatus const&) const = default;
};
struct LoadedSchematic {
    std::string id, path, name;
    int sizeX{}, sizeY{}, sizeZ{};
};
struct TaskSnapshot : TaskStatus {
    std::vector<LoadedSchematic> schematics;
    tasks::Catalog catalog;
};
void loadTasks();
void shutdownTasks();
bool submitTaskIntent(TaskIntent);
TaskSnapshot getTaskSnapshot();
TaskStatus getTaskStatus();
void processTaskActions();
// Control-frame source edit. Publishes a new immutable source and schedules
// recomposition before a subsequent click can use the projection again.
bool editLoadedSchematic(std::shared_ptr<structure::LoadedStructure const> source,
    std::string const& region,
    std::array<int, 3> cell, structure::JavaBlockState const& state, std::string& error);
} // namespace lholo::app
