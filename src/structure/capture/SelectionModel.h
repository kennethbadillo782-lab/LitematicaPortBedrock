#pragma once
#include "structure/capture/StructureCapture.h"
#include <algorithm>
#include <stdexcept>

namespace lholo::structure::capture {
inline Bounds boxBounds(Point a, Point b) {
    return {{std::min(a.x,b.x),std::min(a.y,b.y),std::min(a.z,b.z)},
            {std::max(a.x,b.x),std::max(a.y,b.y),std::max(a.z,b.z)}};
}
inline SelectionBox* selectedRegion(Draft& draft) {
    auto found = std::find_if(draft.regions.begin(),draft.regions.end(),[&](auto const& box){return box.id==draft.selectedRegion;});
    return found==draft.regions.end()?nullptr:&*found;
}
inline void syncSelectedRegion(Draft& draft) {
    if (draft.normalMode) if (auto* box=selectedRegion(draft)) {
        box->first=draft.first; box->second=draft.second; box->name=draft.boxName;
    }
}
inline void selectRegion(Draft& draft, std::string const& id) {
    if(std::none_of(draft.regions.begin(),draft.regions.end(),[&](auto const& box){return box.id==id;}))throw std::invalid_argument("Unknown selection subregion");
    syncSelectedRegion(draft);
    draft.selectedRegion=id;
    if (auto* box=selectedRegion(draft)) {
        draft.first=box->first; draft.second=box->second; draft.boxName=box->name;
    } else {draft.selectedRegion.clear();draft.first.reset();draft.second.reset();}
}
inline void addRegion(Draft& draft, std::string name) {
    if (draft.regions.size()>=256) throw std::runtime_error("At most 256 selection subregions are supported");
    syncSelectedRegion(draft);
    std::string id;
    for (unsigned n=1;;++n) {
        id="region-"+std::to_string(n);
        if (std::none_of(draft.regions.begin(),draft.regions.end(),[&](auto const& box){return box.id==id;})) break;
    }
    if (name.empty()) name=id;
    draft.regions.push_back({id,std::move(name),{},{},true});
    selectRegion(draft,id);
}
inline void setSelectionMode(Draft& draft, bool normal) {
    if (normal==draft.normalMode) return;
    if (normal) {
        draft.simpleFirst=draft.first;draft.simpleSecond=draft.second;
        draft.normalMode=true;
        if (draft.regions.empty()) {
            draft.regions.push_back({"region-1",draft.boxName,draft.first,draft.second,true});
            draft.selectedRegion="region-1";
        }
        if (auto* box=selectedRegion(draft)) {draft.first=box->first;draft.second=box->second;draft.boxName=box->name;}
    } else {
        syncSelectedRegion(draft);draft.normalMode=false;
        draft.first=draft.simpleFirst;draft.second=draft.simpleSecond;
    }
}
inline std::vector<Bounds> selectionBounds(Draft const& draft) {
    std::vector<Bounds> boxes;
    if (draft.normalMode) {
        for (auto const& box:draft.regions) if (box.enabled&&box.first&&box.second) boxes.push_back(boxBounds(*box.first,*box.second));
    } else if (draft.first&&draft.second) boxes.push_back(boxBounds(*draft.first,*draft.second));
    return boxes;
}
inline std::optional<Bounds> enclosingBounds(Draft const& draft) {
    auto boxes=selectionBounds(draft);
    if (boxes.empty()) return {};
    auto bounds=boxes.front();
    for (auto const& box:boxes) {
        bounds.min=boxBounds(bounds.min,box.min).min;
        bounds.max=boxBounds(bounds.max,box.max).max;
    }
    return bounds;
}
inline Point selectionOrigin(Draft const& draft) {
    if (draft.origin) return *draft.origin;
    if (auto bounds=enclosingBounds(draft)) return bounds->min;
    return {};
}
}
