#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace lholo::structure::capture {

enum class CaptureMode : std::uint8_t { Client, Singleplayer };
enum class PointSlot : std::uint8_t { First, Second, Origin };

struct Point {
    int x{};
    int y{};
    int z{};

    bool operator==(Point const&) const = default;
};

struct SelectionBox {
    std::string id, name;
    std::optional<Point> first, second;
    bool enabled{true};
    bool operator==(SelectionBox const&) const = default;
};
struct Draft {
    CaptureMode         mode{CaptureMode::Client};
    std::optional<Point> first;
    std::optional<Point> second;
    bool                includeEntities{};
    std::string         name{"Unnamed"}, boxName{"Unnamed"};
    bool normalMode{}, expandCorners{};
    int selectedCorner{1}; // 0: region, 1/2: corners, 3: manual origin
    std::optional<Point> origin, simpleFirst, simpleSecond;
    std::string selectedRegion, selectionId;
    std::vector<SelectionBox> regions;

    bool operator==(Draft const&) const = default;
};

struct Snapshot {
    Draft       draft;
    bool        worldAvailable{};
    std::string status;
    std::uint64_t revision{};
    struct Saved {std::string id, name;};
    std::vector<Saved> selections;
    std::string error, analysis;
};

struct Bounds {
    Point min;
    Point max;
};

Snapshot              getSnapshot();
std::optional<Bounds> getBounds();
std::vector<Bounds> getRegionBounds();

// What the in-world renderer draws: every sub-region box (even with a single corner set), which one is selected,
// which corner (1 or 2) is the selected one and the explicit origin, exactly as Litematica's OverlayRenderer does.
struct SelectionRenderBox {
    std::optional<Point> first, second;
    bool selected{};
    int selectedCorner{}; // 1 or 2 for the selected box, otherwise 0
    bool operator==(SelectionRenderBox const&) const = default;
};
struct SelectionRenderData {
    std::vector<SelectionRenderBox> boxes;
    std::optional<Point> origin;
    bool operator==(SelectionRenderData const&) const = default;
};
SelectionRenderData getSelectionRenderData();
void                  updateDraft(Draft const& draft);
void                  setPointFromPlayer(PointSlot slot);
void                  exportStructure(Draft const& draft, std::filesystem::path const& output);
void                  clear();
// The file written by the last successful export, once; the application loads it into the schematic list.
std::optional<std::filesystem::path> takeExportedPath();
void selectionCommand(std::string const& action, std::string const& value = {});

} // namespace lholo::structure::capture
