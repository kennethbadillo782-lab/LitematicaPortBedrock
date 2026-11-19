#include "app/JavaConfigActions.h"
#include "app/ToolActions.h"
#include "input/MenuOpenRequest.h"
#include <Windows.h>
#include <cstdio>
#include <fstream>
namespace {
std::vector<std::pair<std::string,std::string>> toolCommands;
}
bool recordJavaToolCommand(std::string const& command,std::string const& value){toolCommands.emplace_back(command,value);return true;}
namespace lholo::app {
ToolStatus toolStatus(){ToolStatus status;status.held=true;status.creative=true;return status;}
}
int runJavaConfigActionsTests(){
    using namespace lholo;using config::Json;
    int checks=0,failures=0;auto check=[&](bool ok,char const* text){++checks;if(!ok){++failures;std::printf("JavaConfigActionsTests: %s\n",text);}};
    using M=app::ToolMode;
    M expected[]={M::AreaSelection,M::SchematicPlacement,M::Fill,M::ReplaceBlock,M::PasteSchematic,M::GridPaste,M::Move,M::Delete,M::Rebuild};
    auto mode=M::AreaSelection;for(int i=0;i<9;++i){check(mode==expected[i],"mode cycle visits every mode including creative operations");mode=app::nextToolMode(mode);}
    check(mode==M::AreaSelection&&app::nextToolMode(mode,true)==M::Rebuild,"mode cycle wraps in both directions");
    auto directory=std::filesystem::temp_directory_path()/"lholo_java_config_actions";
    auto path=directory/"litematica.json";std::filesystem::create_directories(directory);
    std::error_code ignored;std::filesystem::remove(path,ignored);
    app::loadJavaConfig(path);app::processJavaConfigActions();
    toolCommands.clear();app::javaKeyInput('V',true);app::javaKeyInput('V',false);
    check(toolCommands==std::vector<std::pair<std::string,std::string>>{{"execute",""}},"first config preserves legacy V as a real execute binding");
    toolCommands.clear();
    auto snapshot=app::javaConfigSnapshot();check(snapshot&&snapshot->at("options").size()==167&&snapshot->at("hotkeys").size()==78,"complete native catalog is published");
    input::clearMenuOpenRequest();check(app::javaKeyInput('M',true),"menu prefix is claimed");check(!input::consumeMenuOpenRequest(),"main does not open on press");
    app::javaKeyInput('V',true);check(input::consumeMenuOpenRequest()==input::MenuTarget::Verifier,"M+V routes to verifier");
    app::javaKeyInput('V',false);app::javaKeyInput('M',false);check(!input::consumeMenuOpenRequest(),"M+V release does not open main");
    app::javaKeyInput('M',true);app::javaKeyInput('M',false);check(input::consumeMenuOpenRequest()==input::MenuTarget::Main,"M alone opens main on release");
    app::javaKeyInput(VK_MULTIPLY,true);app::javaKeyInput(VK_MULTIPLY,false);check(input::consumeMenuOpenRequest()==input::MenuTarget::AreaEditor,"keypad multiply opens area editor");
    app::javaKeyInput(VK_SUBTRACT,true);app::javaKeyInput(VK_SUBTRACT,false);check(input::consumeMenuOpenRequest()==input::MenuTarget::Placement,"keypad minus opens placement settings");
    auto request=Json{{"group","Generic"},{"name","toolItemEnabled"},{"value",false}};
    app::queueJavaConfigCommand("set",request.dump());app::processJavaConfigActions();
    check(!app::javaToolOptions()->enabled&&std::filesystem::exists(path),"accepted change affects runtime and is saved");
    std::ifstream in(path);auto stored=Json::parse(in);in.close();check(stored.at("Generic").at("toolItemEnabled")==false,"Java category names used on disk");
    toolCommands.clear();app::javaKeyInput(VK_LBUTTON,true);app::javaKeyInput(VK_LBUTTON,false);check(toolCommands.empty(),"disabled tool cannot intercept mouse actions");
    app::queueJavaConfigCommand("reset",Json{{"group","Generic"},{"name","toolItemEnabled"}}.dump());app::processJavaConfigActions();
    check(app::javaToolOptions()->enabled,"reset restores actual runtime value");
    app::javaKeyInput(VK_LBUTTON,true);app::javaKeyInput(VK_LBUTTON,false);check(toolCommands==std::vector<std::pair<std::string,std::string>>{{"mouse","1"}},"tool click is queued exactly once");
    app::queueJavaConfigCommand("capture",Json{{"name","openGuiMainMenu"}}.dump());app::processJavaConfigActions();
    check(app::javaConfigCaptureActive(),"native key capture starts");
    app::javaKeyInput(VK_F7,true,true);app::javaKeyInput('M',true,true);app::javaKeyInput('M',false,true);app::javaKeyInput(VK_F7,false,true);
    app::processJavaConfigActions();check(!app::javaConfigCaptureActive(),"capture completes after all keys released");
    bool captured=false;for(auto const& item:app::javaConfigSnapshot()->at("hotkeys"))if(item.at("name")=="openGuiMainMenu")captured=item.at("value").at("keys")=="F7,M";
    check(captured,"native capture preserves arbitrary multi-key order");
    input::clearMenuOpenRequest();app::javaKeyInput(VK_F7,true);app::javaKeyInput('M',true);app::javaKeyInput('M',false);app::javaKeyInput(VK_F7,false);
    check(input::consumeMenuOpenRequest()==input::MenuTarget::Main,"captured chord actually controls native route");
    app::queueJavaConfigCommand("capture",Json{{"name","openGuiMainMenu"}}.dump());app::processJavaConfigActions();app::javaKeyInput(VK_ESCAPE,true,true);app::processJavaConfigActions();
    check(!app::javaConfigCaptureActive(),"Escape cancels capture");
    app::queueJavaConfigCommand("set",Json{{"group","Generic"},{"name","pasteIgnoreEntities"},{"value",true}}.dump());app::processJavaConfigActions();
    check(!app::javaConfigSnapshot()->at("error").get<std::string>().empty(),"unsupported effects are rejected rather than silently accepted");
    app::queueJavaConfigCommand("reset",Json{{"group","Hotkeys"},{"name","openGuiMainMenu"}}.dump());app::processJavaConfigActions();
    app::queueJavaConfigCommand("set",Json{{"group","Hotkeys"},{"name","openGuiMainMenu"},{"value",{{"keys","M"},{"settings",{{"cancel",false}}}}}}.dump());app::processJavaConfigActions();
    input::clearMenuOpenRequest();
    check(!app::javaKeyInput('M',true)&&!app::javaKeyInput('M',false),"cancel=false does not consume the physical keys");
    check(input::consumeMenuOpenRequest()==input::MenuTarget::Main,"non-cancelling binding still invokes its action");
    app::resetJavaConfigInput();input::clearMenuOpenRequest();
    app::javaKeyInput('M',true);app::javaKeyInput('R',true);app::javaKeyInput('R',false);app::javaKeyInput('M',false);
    app::processJavaConfigActions();
    check(!app::javaToolOptions()->rendering,"M+R disables the shared render gate");
    check(!input::consumeMenuOpenRequest(),"M+R does not open the menu on release");
    toolCommands.clear();app::javaKeyInput(VK_LBUTTON,true);app::javaKeyInput(VK_LBUTTON,false);
    check(toolCommands.empty(),"master rendering off disables tool mouse callbacks");
    app::javaKeyInput('M',true);app::javaKeyInput('R',true);app::javaKeyInput('R',false);app::javaKeyInput('M',false);app::processJavaConfigActions();
    check(app::javaToolOptions()->rendering,"M+R restores rendering without reloading the schematic");
    auto booleanValue=Json{{"enabled",true},{"hotkey",{{"keys","F8"},{"settings",{{"activate_on","RELEASE"},{"cancel",false}}}}},{"extension","retain"}};
    app::queueJavaConfigCommand("set",Json{{"group","Visuals"},{"name","enableAreaSelectionBoxesRendering"},{"value",booleanValue}}.dump());app::processJavaConfigActions();
    check(!app::javaKeyInput(VK_F8,true)&&app::javaToolOptions()->areaRendering,"boolean binding respects release activation and cancel=false");
    check(!app::javaKeyInput(VK_F8,false),"non-cancelling boolean release propagates");app::processJavaConfigActions();
    check(!app::javaToolOptions()->areaRendering,"option-owned keybinding toggles its actual effect");
    std::ifstream toggledFile(path);auto toggled=Json::parse(toggledFile);toggledFile.close();
    auto toggledOption=toggled.at("Visuals").at("enableAreaSelectionBoxesRendering");
    check(toggledOption.at("enabled")==false&&toggledOption.at("hotkey")==booleanValue.at("hotkey")&&toggledOption.at("extension")=="retain","toggle persists enabled without dropping key settings or extensions");
    app::queueJavaConfigCommand("capture",Json{{"group","Visuals"},{"name","enableAreaSelectionBoxesRendering"}}.dump());app::processJavaConfigActions();
    app::javaKeyInput(VK_F9,true,true);app::javaKeyInput(VK_F9,false,true);app::processJavaConfigActions();
    bool booleanCaptured=false;for(auto const& item:app::javaConfigSnapshot()->at("options"))if(item.at("name")=="enableAreaSelectionBoxesRendering")
        booleanCaptured=item.at("value").at("hotkey").at("keys")=="F9"&&item.at("value").at("enabled")==false;
    check(booleanCaptured,"boolean key capture edits the nested keybind and preserves enabled");
    app::javaKeyInput(VK_F9,true);app::javaKeyInput(VK_F9,false);app::processJavaConfigActions();
    check(app::javaToolOptions()->areaRendering,"captured boolean binding is active in the engine");
    app::queueJavaConfigCommand("reset",Json{{"group","Visuals"},{"name","enableAreaSelectionBoxesRendering"}}.dump());app::processJavaConfigActions();
    app::javaKeyInput(VK_F9,true);app::javaKeyInput(VK_F9,false);app::processJavaConfigActions();
    check(app::javaToolOptions()->areaRendering,"reset removes the option binding as well as restoring its boolean");
    app::queueJavaConfigCommand("capture",Json{{"name","openGuiMainMenu"}}.dump());app::processJavaConfigActions();
    app::javaKeyInput(VK_OEM_COMMA,true,true);app::javaKeyInput(VK_BACK,true,true);app::javaKeyInput(VK_BACK,false,true);app::javaKeyInput(VK_OEM_COMMA,false,true);app::processJavaConfigActions();
    bool specialCaptured=false;for(auto const& item:app::javaConfigSnapshot()->at("hotkeys"))if(item.at("name")=="openGuiMainMenu")specialCaptured=item.at("value").at("keys")=="COMMA,BACKSPACE";
    check(specialCaptured,"punctuation and navigation keys use Java storage names");
    app::queueJavaConfigCommand("set",Json{{"group","Hotkeys"},{"name","openGuiMainMenu"},{"value",{{"keys","ENTER"}}}}.dump());app::processJavaConfigActions();
    input::clearMenuOpenRequest();app::javaKeyInput(VK_RETURN,true);app::javaKeyInput(VK_RETURN,false);
    check(input::consumeMenuOpenRequest()==input::MenuTarget::Main,"legacy ENTER alias matches Java RETURN event");
    app::queueJavaConfigCommand("set",Json{{"group","Hotkeys"},{"name","openGuiMainMenu"},{"value",{{"keys","BUTTON_4"}}}}.dump());app::processJavaConfigActions();
    input::clearMenuOpenRequest();app::javaKeyInput(VK_XBUTTON1,true);app::javaKeyInput(VK_XBUTTON1,false);
    check(input::consumeMenuOpenRequest()==input::MenuTarget::Main,"Java legacy mouse-button alias matches the native side button");
    app::queueJavaConfigCommand("set",Json{{"group","Colors"},{"name","schematicOverlayColorMissing"},{"value","#2CFF8000"}}.dump());
    app::queueJavaConfigCommand("toggleBoolean",Json{{"group","Visuals"},{"name","enableSchematicOverlay"}}.dump());
    app::queueJavaConfigCommand("toggleBoolean",Json{{"group","Visuals"},{"name","schematicOverlayTypeExtra"}}.dump());
    app::queueJavaConfigCommand("set",Json{{"group","Visuals"},{"name","ignoreCropAge"},{"value",true}}.dump());
    app::processJavaConfigActions();
    auto options=app::javaToolOptions();
    check(options->overlayStyle.missing==0x000080FFu,"overlay colour option becomes ABGR rgb-only");
    check(!options->overlay&&!options->overlayStyle.showExtra&&options->overlayStyle.showMissing,"overlay toggles reach the render options");
    check(options->ignoreCropAge&&options->rulesHash!=0,"verifier ignore rules reach the options");
    check(projection::overlayColorFromText("#FF3333",0)==0x003333FFu&&projection::overlayColorFromText("bad",7)==7u,"colour text parser");
    app::resetJavaConfigInput();toolCommands.clear();std::filesystem::remove(path,ignored);std::filesystem::remove(directory,ignored);
    std::printf("JavaConfigActionsTests: %d checks, %d failures\n",checks,failures);return failures;
}
