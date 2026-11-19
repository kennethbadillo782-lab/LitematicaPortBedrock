#pragma once
#include "structure/capture/SelectionModel.h"
#include "nlohmann/json.hpp"
#include <limits>
#include <set>
namespace lholo::structure::capture {
using SelectionJson=nlohmann::json;
inline SelectionJson selectionPointJson(std::optional<Point> point) {
    return point?SelectionJson{{"x",point->x},{"y",point->y},{"z",point->z}}:SelectionJson(nullptr);
}
inline std::optional<Point> selectionPoint(SelectionJson const& point) {
    if (point.is_null()) return {};
    auto integer=[](auto const& number) {
        if (number.is_number_unsigned() && number.template get<std::uint64_t>() > static_cast<std::uint64_t>((std::numeric_limits<int>::max)()))
            throw std::invalid_argument("Selection coordinate exceeds int32");
        if (!number.is_number_integer() || number < (std::numeric_limits<int>::min)() || number > (std::numeric_limits<int>::max)())
            throw std::invalid_argument("Selection coordinate exceeds int32");
        return number.template get<int>();
    };
    return Point{integer(point.at("x")),integer(point.at("y")),integer(point.at("z"))};
}
inline SelectionJson selectionJson(Draft const& draft) {
    auto boxes=SelectionJson::array();
    for (auto const& box:draft.regions) boxes.push_back({{"id",box.id},{"name",box.name},
        {"first",selectionPointJson(box.first)},{"second",selectionPointJson(box.second)},{"enabled",box.enabled}});
    return {{"name",draft.name},{"boxName",draft.boxName},{"first",selectionPointJson(draft.first)},
        {"second",selectionPointJson(draft.second)},{"includeEntities",draft.includeEntities},
        {"normalMode",draft.normalMode},{"expandCorners",draft.expandCorners},{"selectedCorner",draft.selectedCorner},
        {"origin",selectionPointJson(draft.origin)},{"simpleFirst",selectionPointJson(draft.simpleFirst)},
        {"simpleSecond",selectionPointJson(draft.simpleSecond)},{"selectedRegion",draft.selectedRegion},
        {"selectionId",draft.selectionId},{"regions",boxes}};
}
inline Draft selectionDraft(SelectionJson const& json) {
    Draft draft;
    draft.name=json.value("name","Unnamed"); draft.boxName=json.value("boxName","Unnamed");
    draft.first=selectionPoint(json.at("first"));draft.second=selectionPoint(json.at("second"));
    draft.includeEntities=json.at("includeEntities");draft.normalMode=json.value("normalMode",false);
    draft.expandCorners=json.value("expandCorners",false);draft.selectedCorner=json.value("selectedCorner",1);
    draft.origin=selectionPoint(json.value("origin",SelectionJson(nullptr)));
    draft.simpleFirst=selectionPoint(json.value("simpleFirst",SelectionJson(nullptr)));
    draft.simpleSecond=selectionPoint(json.value("simpleSecond",SelectionJson(nullptr)));
    draft.selectedRegion=json.value("selectedRegion","");draft.selectionId=json.value("selectionId","");
    if (draft.name.size()>256||draft.boxName.size()>256||draft.selectionId.size()>128||draft.selectedCorner<0||draft.selectedCorner>3)
        throw std::invalid_argument("Invalid selection metadata");
    std::set<std::string> ids;
    if (json.contains("regions")) for (auto const& box:json.at("regions")) {
        SelectionBox region{box.at("id"),box.at("name"),selectionPoint(box.at("first")),selectionPoint(box.at("second")),box.at("enabled")};
        if (region.id.empty()||region.id.size()>128||region.name.size()>256||!ids.insert(region.id).second||draft.regions.size()>=256)
            throw std::invalid_argument("Invalid selection subregion");
        draft.regions.push_back(std::move(region));
    }
    if (draft.normalMode&&!draft.regions.empty()&&!selectedRegion(draft)) throw std::invalid_argument("Unknown selected region");
    return draft;
}
}
