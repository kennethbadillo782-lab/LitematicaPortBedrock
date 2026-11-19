#include "structure/capture/SelectionInteraction.h"
#include <cstdio>
int runSelectionInteractionTests(){
    using namespace lholo::structure::capture;
    int failures=0,checks=0;auto check=[&](bool ok,char const* label){++checks;if(!ok){++failures;std::printf("SelectionInteraction: %s\n",label);}};
    Draft draft;draft.expandCorners=true;setToolCorner(draft,{-3,10,5},false);
    check(draft.first==draft.second&&draft.first==Point{-3,10,5},"first expand click starts both corners");
    setToolCorner(draft,{2,7,8},false);check(draft.first==Point{-3,7,5}&&draft.second==Point{2,10,8},"left expand grows all axes");
    setToolCorner(draft,{9,-2,0},true);check(draft.first==Point{9,-2,0}&&draft.second==draft.first,"right expand resets cuboid");
    draft.expandCorners=false;setToolCorner(draft,{-8,1,2},false);check(draft.selectedCorner==1&&draft.second==Point{9,-2,0},"corner mode only updates chosen corner");
    draft.selectedCorner=1;nudgeSelectedElement(draft,{-1,2,0});check(draft.first==Point{-9,3,2}&&draft.second==Point{9,-2,0},"nudge selected corner only");
    draft.selectedCorner=0;nudgeSelectedElement(draft,{1,0,0});check(draft.first==Point{-8,3,2}&&draft.second==Point{10,-2,0},"nudge box moves both corners");
    draft.first=Point{INT32_MAX,0,0};auto before=draft;try{nudgeSelectedElement(draft,{1,0,0});check(false,"overflow must throw");}catch(std::invalid_argument const&){}check(draft==before,"overflow leaves draft unchanged");
    draft={};draft.first=Point{-2,4,6};draft.second=Point{2,6,9};
    check(selectElementByRay(draft,{-1.5,4.5,0},{0,0,1})&&draft.selectedCorner==1,"parallel ray selects corner at negative x");
    check(selectElementByRay(draft,{0,5,0},{0,0,1})&&draft.selectedCorner==0,"ray selects box body");
    draft.origin=Point{8,8,8};check(selectElementByRay(draft,{8.5,8.5,0},{0,0,1})&&draft.selectedCorner==3,"ray selects manual origin");
    setToolCorner(draft,{-1,2,3},true);check(draft.origin==Point{-1,2,3}&&draft.first==Point{-2,4,6},"selected origin moves without moving corners");
    draft={};setSelectionMode(draft,true);draft.first=Point{0,0,10};draft.second=Point{1,1,11};syncSelectedRegion(draft);addRegion(draft,"near");draft.first=Point{0,0,3};draft.second=Point{1,1,4};syncSelectedRegion(draft);
    check(selectElementByRay(draft,{0.5,0.5,0},{0,0,1})&&draft.boxName=="near","nearest subregion selected");
    check(!selectElementByRay(draft,{100,100,0},{0,0,1}),"miss does not select arbitrary region");
    check(!intersectSelectionBox({0,0,0},{0,0,1},{0,0,201},{1,1,202},200),"maximum selection distance enforced");
    draft={};draft.first=Point{10,20,30};draft.second=Point{11,22,33};
    auto moved=selectionMovedTo(draft,{100,80,-20});
    check(moved.first==Point{100,80,-20}&&moved.second==Point{101,82,-17},"move translates the whole simple selection to its destination");
    draft.normalMode=true;draft.origin=Point{0,0,0};draft.regions={ {"a","A",Point{1,2,3},Point{2,3,4},true},{"b","B",Point{-3,0,1},Point{-1,2,3},false} };
    moved=selectionMovedTo(draft,{5,6,7});
    check(moved.origin==Point{5,6,7}&&moved.regions[0].first==Point{6,8,10}&&moved.regions[1].second==Point{4,8,10},"move preserves all subregion offsets, enabled flags and manual origin");
    draft={};draft.first=draft.second=Point{INT32_MIN,0,0};moved=selectionMovedTo(draft,{INT32_MAX,0,0});
    check(moved.first==Point{INT32_MAX,0,0},"move supports a valid delta wider than int32");
    draft.second=Point{INT32_MIN+1,0,0};before=draft;
    try{selectionMovedTo(draft,{INT32_MAX,0,0});check(false,"invalid destination must fail");}catch(std::invalid_argument const&){}
    check(draft==before,"failed move leaves the original selection unchanged");
    std::printf("SelectionInteractionTests: %d checks, %d failures\n",checks,failures);return failures;
}
