#include "config/ConfigRegistry.h"
#include "input/KeybindMulti.h"
#include <cstdio>
#include "fixtures/JavaConfig0291.h"
int runJavaConfigTests(){
    using namespace lholo::config;using namespace lholo::input;
    int checks=0,failures=0;auto check=[&](bool ok,char const* text){++checks;if(!ok){++failures;std::printf("JavaConfigTests: %s\n",text);}};
    auto const& catalog=upstreamCatalog();check(catalog.at("options").size()==168,"active declarations exclude seven commented options");
    check(catalog.at("hotkeys").size()==78,"all upstream hotkeys imported");
    Registry registry;auto initial=registry.serialize();
    check(registry.value("Generic","toolItem")=="minecraft:stick","tool default is upstream stick");
    check(registry.value("Generic","commandLimitPerTick")==8,"command limit default");
    check(registry.value("Hotkeys","openGuiMainMenu").at("keys")=="M","main menu binding storage matches Java");
    for(auto const& item:catalog.at("options")){
        auto group=item.at("group").get<std::string>(),name=item.at("name").get<std::string>();
        check(registry.set(group,name,registry.value(group,name)),"each imported default passes native validation");
    }
    check(!registry.set("Generic","commandLimitPerTick",0),"lower limit rejected");
    check(!registry.set("Generic","commandLimitPerTick",257),"upper limit rejected");
    check(!registry.set("Generic","commandLimitPerTick",1.5),"integer option rejects fractional input");
    check(!registry.set("Generic","toolItemEnabled","true"),"booleans cannot be stringified");
    check(registry.set("Generic","toolItem","minecraft:blaze_rod"),"string option edits");
    check(registry.reset("Generic","toolItem")&&registry.serialize()==initial,"reset restores exact original default");
    auto document=initial;document["Generic"]["extensionForFutureVersion"]=42;document["extensionRoot"]={{"keep",true}};
    document["Hotkeys"]["openGuiMainMenu"]={{"keys","F3,M"},{"settings",{{"activate_on","RELEASE"},{"context","INGAME"},{"allow_extra_keys",false}}}};
    registry.load(document);check(registry.serialize()==document,"Java categories, key settings and unknown extensions survive round trip");
    auto before=registry.serialize();document["Generic"]["commandLimitPerTick"]=-1;
    try{registry.load(document);check(false,"invalid load must throw");}catch(std::invalid_argument const&){}
    check(registry.serialize()==before,"invalid config load is transactional");
    check(translate("es-MX","litematica.gui.button.change_menu.schematic_manager")!= "litematica.gui.button.change_menu.schematic_manager","Spanish upstream keys resolve with locale alias");
    for(auto const& [code,language]:catalog.at("languages").items()){
        unsigned strings=0;for(auto const& [key,value]:language.items())if(value.is_string())++strings;
        check(strings>1000,"upstream locale includes Litematica and MaLiLib resources");
    }
    Registry captured;auto capturedDocument=Json::parse(javaConfig0291);
    captured.load(capturedDocument);auto upgraded=captured.serialize();
    for(auto const& [group,entries]:capturedDocument.items())for(auto const& [name,value]:entries.items())
        check(upgraded.at(group).at(name)==value,"captured Java 0.29.1 value survives native import/export");
    check(!upgraded.at("Generic").contains("loadEntireSchematics")&&upgraded==capturedDocument,"internal unlisted declaration is not added to the real Java file");
    check(captured.value("Hotkeys","executeOperation").at("keys")=="V","user binding from Java overrides empty upstream default");
    KeybindMulti engine;std::vector<std::string> events;
    auto callback=[&](std::string const& name,KeyAction action){events.push_back(name+(action==KeyAction::Press?":down":":up"));return true;};
    KeySettings release;release.action=KeyAction::Release;release.exclusive=true;
    engine.replace({{"main",{"M"},release},{"verifier",{"M","V"},{}}});
    engine.event("M",true,KeyContext::InGame,callback);check(events.empty(),"M waits for release");
    engine.event("M",false,KeyContext::InGame,callback);check(events==std::vector<std::string>{"main:up"},"M alone opens on release");
    events.clear();engine.event("M",true,KeyContext::InGame,callback);engine.event("V",true,KeyContext::InGame,callback);
    engine.event("V",false,KeyContext::InGame,callback);engine.event("M",false,KeyContext::InGame,callback);
    check(events==std::vector<std::string>{"verifier:down"},"M+V does not open main on prefix release");
    events.clear();engine.event("V",true,KeyContext::InGame,callback);engine.event("M",true,KeyContext::InGame,callback);check(events.empty(),"order-sensitive combination rejects V then M");
    engine.reset();events.clear();KeySettings anyOrder;anyOrder.orderSensitive=false;anyOrder.allowExtra=true;
    engine.replace({{"modifier",{"LEFT_ALT"},anyOrder},{"debug",{"F3","M"},{}}});
    engine.event("F3",true,KeyContext::InGame,callback);engine.event("M",true,KeyContext::InGame,callback);
    check(events==std::vector<std::string>{"debug:down"},"arbitrary non-modifier chord F3+M");
    engine.event("LEFT_ALT",true,KeyContext::InGame,callback);check(engine.held("modifier"),"modifier allows extra held keys");
    auto count=events.size();engine.event("LEFT_ALT",true,KeyContext::InGame,callback);check(events.size()==count,"OS repeat cannot retrigger");
    engine.contextChanged();check(!engine.held("modifier"),"context loss releases held modifier");
    events.clear();engine.event("LEFT_ALT",true,KeyContext::Gui,callback);check(events.empty(),"game binding ignored in GUI");
    engine.replace({{"both",{"A"},{KeyContext::Any,KeyAction::Both}}});events.clear();
    engine.event("A",true,KeyContext::Gui,callback);engine.event("A",false,KeyContext::Gui,callback);
    check(events==std::vector<std::string>{"both:down","both:up"},"both-edge action and any-context setting");
    std::printf("JavaConfigTests: %d checks, %d failures\n",checks,failures);return failures;
}
