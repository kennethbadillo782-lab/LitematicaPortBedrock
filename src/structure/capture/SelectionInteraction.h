#pragma once
#include "structure/capture/SelectionModel.h"
#include <array>
#include <cmath>
#include <limits>
namespace lholo::structure::capture {
inline void setToolCorner(Draft& draft,Point point,bool secondary) {
    if(draft.selectedCorner==3&&draft.origin){draft.origin=point;return;}
    if(draft.expandCorners){
        if(secondary){draft.first=draft.second=point;}
        else {auto a=draft.first.value_or(point),b=draft.second.value_or(point);auto bounds=boxBounds(a,b);
            draft.first=boxBounds(bounds.min,point).min;draft.second=boxBounds(bounds.max,point).max;}
        draft.selectedCorner=0;
    }else{(secondary?draft.second:draft.first)=point;draft.selectedCorner=secondary?2:1;}
    syncSelectedRegion(draft);
}
inline Point translatedPoint(Point point,std::array<int,3> delta) {
    std::array<long long,3> result{static_cast<long long>(point.x)+delta[0],static_cast<long long>(point.y)+delta[1],static_cast<long long>(point.z)+delta[2]};
    for(auto value:result)if(value<INT32_MIN||value>INT32_MAX)throw std::invalid_argument("Selection move exceeds int32 coordinates");
    return {static_cast<int>(result[0]),static_cast<int>(result[1]),static_cast<int>(result[2])};
}
// Move the active selection as one object. Use 64-bit deltas across the full int32 world range.
inline Draft selectionMovedTo(Draft draft,Point target){
    auto origin=selectionOrigin(draft);
    std::array<long long,3> delta{static_cast<long long>(target.x)-origin.x,static_cast<long long>(target.y)-origin.y,static_cast<long long>(target.z)-origin.z};
    auto shift=[&](std::optional<Point>& point){if(!point)return;std::array<long long,3> p{point->x+delta[0],point->y+delta[1],point->z+delta[2]};
        for(auto value:p)if(value<INT32_MIN||value>INT32_MAX)throw std::invalid_argument("Selection move exceeds int32 coordinates");
        point=Point{static_cast<int>(p[0]),static_cast<int>(p[1]),static_cast<int>(p[2])};};
    shift(draft.first);shift(draft.second);shift(draft.origin);
    if(draft.normalMode)for(auto& box:draft.regions){shift(box.first);shift(box.second);}
    return draft;
}
// Work on a copy: overflow in any corner must leave the entire selection unchanged.
inline void nudgeSelectedElement(Draft& draft,std::array<int,3> delta) {
    auto next=draft;
    if(next.selectedCorner==3){if(next.origin)next.origin=translatedPoint(*next.origin,delta);}
    else {
        if(next.first&&(next.selectedCorner==0||next.selectedCorner==1))next.first=translatedPoint(*next.first,delta);
        if(next.second&&(next.selectedCorner==0||next.selectedCorner==2))next.second=translatedPoint(*next.second,delta);
        syncSelectedRegion(next);
    }
    draft=std::move(next);
}
inline std::optional<double> intersectSelectionBox(std::array<double,3> eye,std::array<double,3> direction,Point first,Point second,double maximum) {
    auto bounds=boxBounds(first,second);std::array<double,3> low{double(bounds.min.x),double(bounds.min.y),double(bounds.min.z)};
    std::array<double,3> high{double(bounds.max.x)+1,double(bounds.max.y)+1,double(bounds.max.z)+1};
    double enter=0,leave=maximum;
    for(int a=0;a<3;++a){
        if(!std::isfinite(eye[a])||!std::isfinite(direction[a]))return {};
        if(direction[a]==0){if(eye[a]<low[a]||eye[a]>high[a])return {};continue;}
        auto start=(low[a]-eye[a])/direction[a],end=(high[a]-eye[a])/direction[a];if(start>end)std::swap(start,end);
        enter=std::max(enter,start);leave=std::min(leave,end);if(enter>leave)return {};
    }
    return enter;
}
inline bool selectElementByRay(Draft& draft,std::array<double,3> eye,std::array<double,3> direction,double maximum=200) {
    double best=maximum+1;std::string region;int corner=-1;
    auto offer=[&](std::optional<Point> first,std::optional<Point> second,std::string const& id,int type){
        if(!first||!second)return;auto distance=intersectSelectionBox(eye,direction,*first,*second,maximum);
        if(distance&&*distance<best){best=*distance;region=id;corner=type;}
    };
    if(draft.origin)offer(draft.origin,draft.origin,"",3);
    // Select corner cubes before the enclosing box at coincident boundaries.
    if(draft.normalMode){for(auto const& box:draft.regions){offer(box.first,box.first,box.id,1);offer(box.second,box.second,box.id,2);}}
    else {offer(draft.first,draft.first,"",1);offer(draft.second,draft.second,"",2);}
    if(corner<0){if(draft.normalMode){for(auto const& box:draft.regions)offer(box.first,box.second,box.id,0);}else offer(draft.first,draft.second,"",0);}
    if(corner<0)return false;
    if(draft.normalMode&&corner!=3)selectRegion(draft,region);
    draft.selectedCorner=corner;return true;
}
}
