#pragma once

#include "structure/TaskProjectionView.h"

#include <array>
#include <cmath>
#include <limits>
#include <numeric>
#include <optional>
#include <vector>

namespace lholo::structure {

struct ProjectionRayHit {
    TaskCellOwner owner;
    std::array<long long, 3> worldCell{};
    std::array<int, 3> enteredFace{};
    double distance{};
};

// Ray traversal of the same visible, overlap-resolved cells used by the
// projection. A game-world hit is compared by the caller before editing.
inline std::optional<ProjectionRayHit> traceTaskProjection(
    TaskProjectionView const& view, std::array<double, 3> origin,
    std::array<double, 3> direction, double range) {
    if (!(range > 0) || !std::isfinite(range)) return {};
    double const length = std::hypot(direction[0], direction[1], direction[2]);
    if (!(length > 0) || !std::isfinite(length)) return {};
    for (auto& component : direction) component /= length;
    std::vector<std::size_t> candidates(view.files().size());
    std::iota(candidates.begin(), candidates.end(), 0);
    std::array<long long, 3> cell{};
    std::array<int, 3> step{};
    std::array<double, 3> delta{}, next{};
    double const infinity = std::numeric_limits<double>::infinity();
    for (int axis = 0; axis < 3; ++axis) {
        if (!std::isfinite(origin[axis]) ||
            origin[axis] < static_cast<double>(std::numeric_limits<int>::min()) - range - 1 ||
            origin[axis] > static_cast<double>(std::numeric_limits<int>::max()) + range + 1)
            return {};
        cell[axis] = static_cast<long long>(std::floor(origin[axis]));
        step[axis] = direction[axis] > 0 ? 1 : direction[axis] < 0 ? -1 : 0;
        delta[axis] = step[axis] ? std::abs(1.0 / direction[axis]) : infinity;
        next[axis] = step[axis] > 0 ? (cell[axis] + 1 - origin[axis]) * delta[axis]
            : step[axis] < 0 ? (origin[axis] - cell[axis]) * delta[axis] : infinity;
    }
    auto find = [&](double distance, std::array<int, 3> face)
        -> std::optional<ProjectionRayHit> {
        if (auto owner = view.ownerAt(candidates, cell))
            return ProjectionRayHit{*owner, cell, face, distance};
        return {};
    };
    if (auto hit = find(0.0, {})) return hit;
    for (;;) {
        int axis = next[0] <= next[1] && next[0] <= next[2] ? 0
            : next[1] <= next[2] ? 1 : 2;
        double const distance = next[axis];
        if (!std::isfinite(distance) || distance > range) return {};
        cell[axis] += step[axis];
        next[axis] += delta[axis];
        std::array<int, 3> face{};
        face[axis] = -step[axis];
        if (auto hit = find(distance, face)) return hit;
    }
}

} // namespace lholo::structure
