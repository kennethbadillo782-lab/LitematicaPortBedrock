#pragma once
#include "structure/LayerSettings.h"
#include <string>
#include <vector>
#include <optional>

namespace lholo::tasks {
struct File {
    std::string id, path;
    bool visible{true};
    int x{}, y{}, z{}, rotation{};
    std::string mirror{"none"};
    structure::LayerSettings slice{};
    // Files record absolute coordinates when added. A missing dimension means
    // no player coordinates were available; loading will acquire them later.
    std::optional<int> dimension;
    std::string name;
    bool operator==(File const&) const = default;
};
struct Task {
    std::string id, name, selectedFileId;
    std::vector<File> files;
    bool operator==(Task const&) const = default;
};
struct Catalog {
    std::string selectedTaskId;
    std::vector<Task> tasks;
    bool placementManager{}; // v3 stores one flat placement list; tasks is a runtime adapter.
    bool operator==(Catalog const&) const = default;
};
std::string newId();
Task* findTask(Catalog&, std::string const& id);
File* findFile(Task&, std::string const& id);
} // namespace lholo::tasks
