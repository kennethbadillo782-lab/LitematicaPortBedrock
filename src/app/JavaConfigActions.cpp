#include "app/JavaConfigActions.h"
#include "app/ToolActions.h"
#include "app/TaskContext.h"
#include "input/KeybindMulti.h"
#include "input/HotkeyTypes.h"
#include "input/MenuOpenRequest.h"
#include "structure/StructureUiState.h"
#include "structure/capture/StructureCapture.h"
#include "i18n/Translator.h"
#include <functional>
#include <atomic>
#include <deque>
#include <mutex>
#include <set>
#include <map>
#include <sstream>
#include <Windows.h>
namespace lholo::app {
namespace {
config::Registry registry;
std::mutex commandMutex,inputMutex;
std::deque<std::pair<std::string,std::string>> commands;
input::KeybindMulti engine;
std::string captureName,captureGroup;config::Json captureValue,captureParent;std::vector<std::string> capturedKeys,captureHeld;
std::atomic_bool ready{},mainCancel{true};
std::atomic<std::shared_ptr<config::Json const>> snapshot;
std::atomic<std::shared_ptr<JavaToolOptions const>> options{std::make_shared<JavaToolOptions>()};
std::uint64_t revision{};
std::string error,locale;
std::function<bool(std::string const&)> structureActionHook;std::atomic<std::uint64_t> rerenderRevision{};
bool invalidFile{};
std::filesystem::path explicitPath;
std::set<std::string> const supportedActions{
    "openGuiAreaSettings","openGuiPlacementSettings","openGuiLoadedSchematics","openGuiMainMenu","openGuiMaterialList","openGuiSchematicPlacements","openGuiSchematicVerifier","openGuiSelectionManager","openGuiSettings",
    "addSelectionBox","deleteSelectionBox","executeOperation","selectionModeCycle","nudgeSelectionNegative","nudgeSelectionPositive",
    "setAreaOrigin","setSelectionBoxPosition1","setSelectionBoxPosition2","layerModeNext","layerModePrevious","layerNext","layerPrevious",
    "operationModeChangeModifier","selectionNudgeModifier","toolPlaceCorner1","toolPlaceCorner2","toolSelectElements","toolSelectModifierBlock1","toolSelectModifierBlock2","toolEnabledToggle","schematicPlacementRotation","schematicPlacementMirror","unloadCurrentSchematic","rerenderSchematic","toggleAllRendering","toggleSchematicRendering","toggleSchematicBlockRendering","toggleOverlayRendering","toggleOverlayOutlineRendering","toggleOverlaySideRendering","renderOverlayThroughBlocks","togglePlacementBoxesRendering","toggleAreaSelectionBoxesRendering"
};
std::set<std::string> const supportedOptions{
    "toolItem","toolItemEnabled","executeRequireHoldingTool","reverseOperationModeDirection","selectionCornersMode","enableAreaSelectionBoxesRendering","enableRendering",
    "enableSchematicRendering","enableSchematicBlocksRendering","enableSchematicOverlay","schematicOverlayEnableOutlines","schematicOverlayEnableSides","schematicOverlayRenderThroughBlocks","enablePlacementBoxesRendering",
    "schematicOverlayTypeMissing","schematicOverlayTypeExtra","schematicOverlayTypeWrongBlock","schematicOverlayTypeWrongState",
    "ignoreCropAge","ignoreExistingBlocks","ignorableExistingBlocks","ignoreExistingFluids","schematicOverlayColorMissing","schematicOverlayColorExtra","schematicOverlayColorWrongBlock","schematicOverlayColorWrongState"
};
std::filesystem::path path(){if(!explicitPath.empty())return explicitPath;return detail::taskStorePath().parent_path().parent_path()/"config"/"litematica.json";}
std::vector<std::string> splitKeys(std::string const& text){
    std::vector<std::string> result;std::istringstream stream(text);std::string key;
    while(std::getline(stream,key,',')){auto a=key.find_first_not_of(" \t"),b=key.find_last_not_of(" \t");if(a==std::string::npos)continue;key=key.substr(a,b-a+1);
        static std::map<std::string,std::string> const aliases{{"ENTER","RETURN"},{"GRAVE_ACCENT","GRAVE"},{"LEFT_SUPER","LEFT_GUI"},{"RIGHT_SUPER","RIGHT_GUI"},{"KP_ADD","KP_PLUS"},{"KP_SUBTRACT","KP_MINUS"},{"KP_EQUAL","KP_EQUALS"},{"BUTTON_1","MOUSE_LEFT"},{"BUTTON_2","MOUSE_RIGHT"},{"BUTTON_3","MOUSE_MIDDLE"},{"BUTTON_4","MOUSE_BACK"},{"BUTTON_5","MOUSE_FORWARD"}};
        if(auto alias=aliases.find(key);alias!=aliases.end())key=alias->second;
        if(std::find(result.begin(),result.end(),key)==result.end())result.push_back(key);}
    return result;
}
void rebuildBindings(){
    std::vector<input::MultiBinding> bindings;
    for(auto const& definition:config::upstreamCatalog().at("hotkeys")){
        auto name=definition.at("name").get<std::string>();if(!supportedActions.contains(name))continue;
        auto value=registry.value("Hotkeys",name);auto settings=definition.at("settings");if(value.contains("settings"))settings.update(value.at("settings"));
        input::KeySettings policy;
        auto context=settings.at("context").get<std::string>(),action=settings.at("activate_on").get<std::string>();
        policy.context=context=="ANY"?input::KeyContext::Any:context=="GUI"?input::KeyContext::Gui:input::KeyContext::InGame;
        policy.action=action=="BOTH"?input::KeyAction::Both:action=="RELEASE"?input::KeyAction::Release:input::KeyAction::Press;
        policy.allowExtra=settings.at("allow_extra_keys");policy.allowEmpty=settings.at("allow_empty");policy.orderSensitive=settings.at("order_sensitive");policy.exclusive=settings.at("exclusive");policy.cancel=settings.at("cancel");
        if(name=="openGuiMainMenu")mainCancel=policy.cancel;
        bindings.push_back({name,splitKeys(value.at("keys")),policy});
    }
    // MaLiLib ConfigBooleanHotkeyed owns a binding in addition to its enabled value.
    for(auto const& definition:config::upstreamCatalog().at("options")){
        std::string name=definition.at("name"),group=definition.at("group");
        if(definition.at("type")!="ConfigBooleanHotkeyed"||!supportedOptions.contains(name))continue;
        auto value=registry.value(group,name);if(!value.is_object()||!value.contains("hotkey"))continue;
        auto key=value.at("hotkey"),settings=definition.at("settings");if(key.contains("settings"))settings.update(key.at("settings"));
        input::KeySettings policy;
        auto context=settings.at("context").get<std::string>(),action=settings.at("activate_on").get<std::string>();
        policy.context=context=="ANY"?input::KeyContext::Any:context=="GUI"?input::KeyContext::Gui:input::KeyContext::InGame;
        policy.action=action=="BOTH"?input::KeyAction::Both:action=="RELEASE"?input::KeyAction::Release:input::KeyAction::Press;
        policy.allowExtra=settings.at("allow_extra_keys");policy.allowEmpty=settings.at("allow_empty");policy.orderSensitive=settings.at("order_sensitive");policy.exclusive=settings.at("exclusive");policy.cancel=settings.at("cancel");
        bindings.push_back({"config:"+group+":"+name,splitKeys(key.at("keys")),policy});
    }
    std::lock_guard lock(inputMutex);engine.replace(std::move(bindings));
}
void applyOptions(){
    auto state=std::make_shared<JavaToolOptions>();
    state->item=registry.value("Generic","toolItem");state->enabled=registry.value("Generic","toolItemEnabled");
    state->requireHeld=registry.value("Generic","executeRequireHoldingTool");state->reverseCycle=registry.value("Generic","reverseOperationModeDirection");
    state->expandCorners=registry.value("Generic","selectionCornersMode")=="expand";
    auto rendering=registry.value("Visuals","enableAreaSelectionBoxesRendering");state->areaRendering=rendering.is_boolean()?rendering.get<bool>():rendering.at("enabled").get<bool>();
    auto flag=[&](char const* name,bool fallback){auto v=registry.value("Visuals",name);return v.is_boolean()?v.get<bool>():v.is_object()&&v.contains("enabled")?v.at("enabled").get<bool>():fallback;};
    state->blocks=flag("enableSchematicRendering",true)&&flag("enableSchematicBlocksRendering",true);
    state->overlay=flag("enableSchematicOverlay",true);state->outlines=flag("schematicOverlayEnableOutlines",true);state->sides=flag("schematicOverlayEnableSides",true);
    auto color=[&](char const* name,std::uint32_t fallback){auto v=registry.value("Colors",name);return v.is_string()?projection::overlayColorFromText(v.get<std::string>().c_str(),fallback):fallback;};
    auto& style=state->overlayStyle;
    style.missing=color("schematicOverlayColorMissing",style.missing);style.extra=color("schematicOverlayColorExtra",style.extra);
    style.wrongBlock=color("schematicOverlayColorWrongBlock",style.wrongBlock);style.wrongState=color("schematicOverlayColorWrongState",style.wrongState);
    style.showMissing=flag("schematicOverlayTypeMissing",true);style.showExtra=flag("schematicOverlayTypeExtra",true);
    style.showWrongBlock=flag("schematicOverlayTypeWrongBlock",true);style.showWrongState=flag("schematicOverlayTypeWrongState",true);
    state->ignoreCropAge=flag("ignoreCropAge",false);state->ignoreBlocks=flag("ignoreExistingBlocks",false);
    if(auto list=registry.value("Visuals","ignorableExistingBlocks");list.is_array())for(auto const& item:list)if(item.is_string())state->ignorable.insert(item.get<std::string>());
    state->rulesHash=std::hash<std::string>{}(std::to_string(state->ignoreCropAge)+(state->ignoreBlocks?"b":"n")+[&]{std::string text;for(auto const& id:state->ignorable)text+=id+";";return text;}());
    state->throughBlocks=flag("schematicOverlayRenderThroughBlocks",false);state->placementBoxes=flag("enablePlacementBoxesRendering",true);
    auto master=registry.value("Visuals","enableRendering");state->rendering=master.is_boolean()?master.get<bool>():master.at("enabled").get<bool>();
    options.store(std::move(state));
    auto draft=structure::capture::getSnapshot().draft;draft.expandCorners=options.load()->expandCorners;structure::capture::updateDraft(draft);
}
void publish(){
    locale=std::string(i18n::languageCode(i18n::language()));
    config::Json result{{"revision",++revision},{"error",error},{"options",config::Json::array()},{"hotkeys",config::Json::array()}};
    for(auto item:config::upstreamCatalog().at("options")){
        if(!item.value("listed",true))continue;
        std::string name=item.at("name"),group=item.at("group");item["value"]=registry.value(group,name);
        item["supported"]=supportedOptions.contains(name);item["label"]=config::translate(locale,item.at("labelKey").get<std::string>());
        item["comment"]=config::translate(locale,item.at("commentKey").get<std::string>());
        result["options"].push_back(std::move(item));
    }
    for(auto item:config::upstreamCatalog().at("hotkeys")){
        std::string name=item.at("name");item["value"]=registry.value("Hotkeys",name);item["supported"]=supportedActions.contains(name);
        item["label"]=config::translate(locale,item.at("labelKey").get<std::string>());item["comment"]=config::translate(locale,item.at("commentKey").get<std::string>());
        result["hotkeys"].push_back(std::move(item));
    }
    {std::lock_guard lock(inputMutex);result["capturing"]=captureName;}
    snapshot.store(std::make_shared<config::Json const>(std::move(result)));
}
std::string keyName(unsigned key){
    if((key>='A'&&key<='Z')||(key>='0'&&key<='9'))return std::string(1,static_cast<char>(key));
    if(key>=VK_F1&&key<=VK_F24)return "F"+std::to_string(key-VK_F1+1);
    if(key>=VK_NUMPAD0&&key<=VK_NUMPAD9)return "KP_"+std::to_string(key-VK_NUMPAD0);
    switch(key){
    case VK_LBUTTON:return "MOUSE_LEFT";case VK_RBUTTON:return "MOUSE_RIGHT";case VK_MBUTTON:return "MOUSE_MIDDLE";
    case VK_XBUTTON1:return "MOUSE_BACK";case VK_XBUTTON2:return "MOUSE_FORWARD";
    case VK_CONTROL:case VK_LCONTROL:return "LEFT_CONTROL";case VK_RCONTROL:return "RIGHT_CONTROL";
    case VK_MENU:case VK_LMENU:return "LEFT_ALT";case VK_RMENU:return "RIGHT_ALT";
    case VK_SHIFT:case VK_LSHIFT:return "LEFT_SHIFT";case VK_RSHIFT:return "RIGHT_SHIFT";
    case VK_PRIOR:return "PAGE_UP";case VK_NEXT:return "PAGE_DOWN";case VK_MULTIPLY:return "KP_MULTIPLY";case VK_SUBTRACT:return "KP_MINUS";
    case VK_ADD:return "KP_PLUS";case VK_DIVIDE:return "KP_DIVIDE";case VK_SPACE:return "SPACE";case VK_HOME:return "HOME";case VK_END:return "END";
    case VK_UP:return "UP";case VK_DOWN:return "DOWN";case VK_LEFT:return "LEFT";case VK_RIGHT:return "RIGHT";
    case VK_INSERT:return "INSERT";case VK_DELETE:return "DELETE";case VK_TAB:return "TAB";case VK_RETURN:return "RETURN";case VK_ESCAPE:return "ESCAPE";
    case VK_BACK:return "BACKSPACE";case VK_CAPITAL:return "CAPS_LOCK";case VK_SCROLL:return "SCROLL_LOCK";
    case VK_NUMLOCK:return "NUM_LOCK";case VK_SNAPSHOT:return "PRINT_SCREEN";case VK_PAUSE:return "PAUSE";
    case VK_LWIN:return "LEFT_GUI";case VK_RWIN:return "RIGHT_GUI";case VK_APPS:return "APPLICATION";
    case VK_DECIMAL:return "KP_PERIOD";case VK_SEPARATOR:return "KP_ENTER";
    case VK_OEM_1:return "SEMICOLON";case VK_OEM_PLUS:return "EQUALS";case VK_OEM_COMMA:return "COMMA";
    case VK_OEM_MINUS:return "MINUS";case VK_OEM_PERIOD:return "PERIOD";case VK_OEM_2:return "SLASH";
    case VK_OEM_3:return "GRAVE";case VK_OEM_4:return "LEFT_BRACKET";case VK_OEM_5:return "BACKSLASH";
    case VK_OEM_6:return "RIGHT_BRACKET";case VK_OEM_7:return "APOSTROPHE";
    default:return {};
    }
}
bool dispatch(std::string const& name,input::KeyAction action){
    (void)action;
    if(name.starts_with("config:")){
        auto separator=name.find(':',7);
        return queueJavaConfigCommand("toggleBoolean",config::Json{{"group",name.substr(7,separator-7)},{"name",name.substr(separator+1)}}.dump());
    }
    static std::map<std::string,std::string> const visualToggles{{"toggleAllRendering","enableRendering"},{"toggleSchematicRendering","enableSchematicRendering"},
        {"toggleSchematicBlockRendering","enableSchematicBlocksRendering"},{"toggleOverlayRendering","enableSchematicOverlay"},{"toggleOverlayOutlineRendering","schematicOverlayEnableOutlines"},
        {"toggleOverlaySideRendering","schematicOverlayEnableSides"},{"renderOverlayThroughBlocks","schematicOverlayRenderThroughBlocks"},{"togglePlacementBoxesRendering","enablePlacementBoxesRendering"},
        {"toggleAreaSelectionBoxesRendering","enableAreaSelectionBoxesRendering"}};
    if(auto toggle=visualToggles.find(name);toggle!=visualToggles.end())return queueJavaConfigCommand("toggleBoolean",config::Json{{"group","Visuals"},{"name",toggle->second}}.dump());
    using input::MenuTarget;
    if(name=="openGuiAreaSettings"){input::requestMenuOpen(MenuTarget::AreaEditor);return true;}
    if(name=="openGuiPlacementSettings"){input::requestMenuOpen(MenuTarget::Placement);return true;}
    if(name=="openGuiLoadedSchematics"){input::requestMenuOpen(MenuTarget::Loaded);return true;}
    if(name=="openGuiMainMenu"){input::requestMenuOpen();return true;}
    if(name=="openGuiMaterialList"){input::requestMenuOpen(MenuTarget::Materials);return true;}
    if(name=="openGuiSchematicPlacements"){input::requestMenuOpen(MenuTarget::Placements);return true;}
    if(name=="openGuiSchematicVerifier"){input::requestMenuOpen(MenuTarget::Verifier);return true;}
    if(name=="openGuiSelectionManager"){input::requestMenuOpen(MenuTarget::Areas);return true;}
    if(name=="openGuiSettings"){input::requestMenuOpen(MenuTarget::Config);return true;}
    if(name=="toolEnabledToggle")return queueJavaConfigCommand("toggleTool","");
    if(name=="toolPlaceCorner1"||name=="toolPlaceCorner2"||name=="toolSelectElements"){
        if(!toolStatus().held||(!options.load()->enabled||!options.load()->rendering))return false;
        return queueToolCommand("mouse",name=="toolPlaceCorner1"?"1":name=="toolPlaceCorner2"?"2":engine.held("toolSelectModifierBlock1")?"3 primary":engine.held("toolSelectModifierBlock2")?"3 secondary":"3 none");
    }
    if(name=="executeOperation")return queueToolCommand("execute");
    if(name=="addSelectionBox")return queueToolCommand("selection","addRegion");
    if(name=="deleteSelectionBox")return queueToolCommand("selection","removeRegion");
    if(name=="selectionModeCycle")return queueToolCommand("selection","mode");
    if(name=="setAreaOrigin")return queueToolCommand("originPlayer");
    if(name=="setSelectionBoxPosition1"||name=="setSelectionBoxPosition2")return queueToolCommand(name=="setSelectionBoxPosition1"?"corner1Player":"corner2Player");
    if(name=="nudgeSelectionNegative"||name=="nudgeSelectionPositive")return queueToolCommand("nudge",name=="nudgeSelectionNegative"?"back":"");
    if(name=="schematicPlacementRotation"||name=="schematicPlacementMirror"||name=="unloadCurrentSchematic")
        return structureActionHook&&structureActionHook(name);
    if(name=="rerenderSchematic"){++rerenderRevision;return true;}
    auto& state=structure::detail::StructureUiState::getInstance();
    if(name=="layerModeNext"||name=="layerModePrevious"){state.queueLayerModeDelta(name=="layerModeNext"?1:-1);return true;}
    if(name=="layerNext"||name=="layerPrevious"){state.queueLayerDelta(name=="layerNext"?1:-1);return true;}
    return false;
}
}
void setJavaStructureActionHook(std::function<bool(std::string const&)> hook){structureActionHook=std::move(hook);}
std::uint64_t javaRerenderRevision(){return rerenderRevision.load();}
bool javaConfigReady(){return ready.load();}
bool javaConfigCaptureActive(){std::lock_guard lock(inputMutex);return !captureName.empty();}
std::shared_ptr<config::Json const> javaConfigSnapshot(){return snapshot.load();}
std::shared_ptr<JavaToolOptions const> javaToolOptions(){return options.load();}
void loadJavaConfig(std::filesystem::path const& file){
    explicitPath=file;registry=config::Registry{};error.clear();invalidFile=false;
    try{
        if(!registry.loadFile(path())){
            auto legacy=structure::detail::StructureUiState::getInstance().inputHotkey(input::hotkeyIndex(input::HotkeyId::ExecuteOperation));
            auto key=keyName(legacy.key);
            if(!key.empty()){
                std::string keys;
                if(legacy.modifiers&1u)keys+="LEFT_CONTROL,";
                if(legacy.modifiers&2u)keys+="LEFT_ALT,";
                if(legacy.modifiers&4u)keys+="LEFT_SHIFT,";
                registry.set("Hotkeys","executeOperation",config::Json{{"keys",keys+key}});
                registry.saveFile(path());
            }
        }
    }catch(std::exception const& e){error=e.what();invalidFile=true;}
    // Do not overwrite an invalid user's configuration on startup.
    rebuildBindings();ready=true;publish();
}
bool queueJavaConfigCommand(std::string const& command,std::string const& value){std::lock_guard lock(commandMutex);if(commands.size()>=32)return false;commands.emplace_back(command,value);return true;}
void resetJavaConfigInput(){std::lock_guard lock(inputMutex);engine.reset();captureHeld.clear();capturedKeys.clear();if(!captureName.empty()){captureName.clear();queueJavaConfigCommand("captureCancel","");}}
bool javaModifierHeld(std::string const& name){std::lock_guard lock(inputMutex);return engine.held(name);}
bool javaKeyInput(unsigned key,bool down,bool gui){
    if(!ready)return false;std::lock_guard lock(inputMutex);auto name=keyName(key);
    if(name.empty())return false;
    if(!captureName.empty()){
        if(down&&key==VK_ESCAPE){captureName.clear();capturedKeys.clear();captureHeld.clear();queueJavaConfigCommand("captureCancel","");return true;}
        if(down){
            if(capturedKeys.size()<16&&std::find(capturedKeys.begin(),capturedKeys.end(),name)==capturedKeys.end())capturedKeys.push_back(name);
            if(std::find(captureHeld.begin(),captureHeld.end(),name)==captureHeld.end())captureHeld.push_back(name);
        }else{
            std::erase(captureHeld,name);
            if(captureHeld.empty()&&!capturedKeys.empty()){
                std::string keys;for(auto const& captured:capturedKeys){if(!keys.empty())keys+=',';keys+=captured;}
                captureValue["keys"]=keys;auto value=captureValue;
                if(captureGroup!="Hotkeys"){value=captureParent;value["hotkey"]=captureValue;}
                queueJavaConfigCommand("set",config::Json{{"group",captureGroup},{"name",captureName},{"value",value}}.dump());
                captureName.clear();capturedKeys.clear();
            }
        }
        return true;
    }
    bool result=engine.event(name,down,gui?input::KeyContext::Gui:input::KeyContext::InGame,dispatch);
    // Claim the release-only menu prefix on keydown, keeping the legacy menu handler from also arming it.
    return result||(down&&mainCancel.load()&&engine.held("openGuiMainMenu"));
}
void processJavaConfigActions(){
    static std::uint64_t world=~0ULL;auto current=detail::taskWorldRevision();if(world!=current&&ready){applyOptions();world=current;}
    std::deque<std::pair<std::string,std::string>> pending;{std::lock_guard lock(commandMutex);pending.swap(commands);}
    for(auto const& [command,text]:pending){try{
        if(command=="captureCancel"){publish();continue;}
        if(command=="capture"){
            auto request=config::Json::parse(text);std::string name=request.at("name"),group=request.value("group",std::string("Hotkeys"));
            bool supported=group=="Hotkeys"?supportedActions.contains(name):false;
            if(group!="Hotkeys")for(auto const& item:config::upstreamCatalog().at("options"))
                if(item.at("group")==group&&item.at("name")==name&&item.at("type")=="ConfigBooleanHotkeyed"&&supportedOptions.contains(name))supported=true;
            if(!supported)throw std::invalid_argument("Unsupported hotkey");
            {std::lock_guard lock(inputMutex);captureName=name;captureGroup=group;captureParent=registry.value(group,name);
                captureValue=group=="Hotkeys"?captureParent:captureParent.value("hotkey",config::Json{{"keys",""}});captureHeld.clear();capturedKeys.clear();engine.reset();}
            publish();continue;
        }
        auto next=registry;
        if(command=="toggleTool")next.set("Generic","toolItemEnabled",!registry.value("Generic","toolItemEnabled").get<bool>());
        else if(command=="cycleCorners")next.set("Generic","selectionCornersMode",registry.value("Generic","selectionCornersMode")=="expand"?"corners":"expand");
        else {
            auto request=config::Json::parse(text);std::string group=request.at("group"),name=request.at("name");
            if((group=="Hotkeys"?!supportedActions.contains(name):!supportedOptions.contains(name)))throw std::invalid_argument("This Java option is not implemented in Bedrock yet");
            if(command=="toggleBoolean"){
                auto value=next.value(group,name);
                if(!value.is_object()||!value.contains("enabled"))throw std::invalid_argument("Expected a boolean hotkey option");
                value["enabled"]=!value.at("enabled").get<bool>();if(!next.set(group,name,value))throw std::invalid_argument("Invalid toggle");
            }
            else if(command=="reset"){if(!next.reset(group,name))throw std::invalid_argument("Unknown Java option");}
            else if(command=="set"){if(!next.set(group,name,request.at("value")))throw std::invalid_argument("Invalid Java option value");}
            else throw std::invalid_argument("Unknown Java configuration command");
        }
        if(invalidFile&&std::filesystem::exists(path())){auto backup=path();backup+=L".invalid.bak";if(!std::filesystem::exists(backup))std::filesystem::copy_file(path(),backup);invalidFile=false;}
        next.saveFile(path());registry=std::move(next);applyOptions();rebuildBindings();error.clear();
    }catch(std::exception const& e){error=e.what();}publish();}
    if(locale!=i18n::languageCode(i18n::language()))publish();
}
}
