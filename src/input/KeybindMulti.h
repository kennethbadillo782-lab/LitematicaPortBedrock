#pragma once
// Behaviour adapted from MaLiLib KeybindMulti/KeybindSettings (LGPL-3.0-or-later).
#include <algorithm>
#include <functional>
#include <string>
#include <vector>
namespace lholo::input {
enum class KeyContext {InGame,Gui,Any};
enum class KeyAction {Press,Release,Both};
struct KeySettings {
    KeyContext context{KeyContext::InGame};KeyAction action{KeyAction::Press};
    bool allowExtra{},orderSensitive{true},exclusive{},cancel{true},allowEmpty{};
};
struct MultiBinding {
    std::string name;std::vector<std::string> keys;KeySettings settings;
    bool pressed{},releaseArmed{},releaseSuppressed{};
};
class KeybindMulti {
    std::vector<std::string> held_;
    std::vector<MultiBinding> bindings_;
    unsigned triggered_{};
    bool matches(MultiBinding const& b,KeyContext context)const{
        if(b.keys.empty()||(b.settings.context!=KeyContext::Any&&b.settings.context!=context))return false;
        if(held_.size()<b.keys.size()||(!b.settings.allowExtra&&held_.size()!=b.keys.size()))return false;
        for(auto const& key:b.keys)if(std::find(held_.begin(),held_.end(),key)==held_.end())return false;
        if(!b.settings.orderSensitive)return true;
        std::size_t i=0;
        for(auto const& key:held_){if(key==b.keys[i]){if(++i==b.keys.size())return true;}
            else if(i>0||held_.size()==b.keys.size())return false;}
        return false;
    }
public:
    void replace(std::vector<MultiBinding> bindings){bindings_=std::move(bindings);reset();}
    void reset(){held_.clear();triggered_=0;for(auto& b:bindings_)b.pressed=b.releaseArmed=b.releaseSuppressed=false;}
    bool held(std::string const& name)const{for(auto const& b:bindings_)if(b.name==name)return b.pressed||(b.keys.empty()&&b.settings.allowEmpty);return false;}
    // Context loss clears all latches without firing release actions in chat/menus.
    void contextChanged(){reset();}
    bool event(std::string key,bool down,KeyContext context,std::function<bool(std::string const&,KeyAction)> const& callback){
        auto found=std::find(held_.begin(),held_.end(),key);
        if(down){if(found!=held_.end())return false;held_.push_back(key);}else{if(found==held_.end())return false;held_.erase(found);}
        bool cancel=false;
        // Longer combinations run before release-only prefixes such as M.
        std::vector<std::size_t> order;for(std::size_t i=0;i<bindings_.size();++i)order.push_back(i);
        std::stable_sort(order.begin(),order.end(),[&](auto a,auto b){return bindings_[a].keys.size()>bindings_[b].keys.size();});
        for(auto i:order){auto& b=bindings_[i];auto before=b.pressed;b.pressed=matches(b,context);
            bool const contains=std::find(b.keys.begin(),b.keys.end(),key)!=b.keys.end();
            if(down&&b.releaseArmed&&!contains&&!b.settings.allowExtra)b.releaseSuppressed=true;
            if(b.pressed&&!before){if(!b.releaseArmed){b.releaseArmed=true;b.releaseSuppressed=false;}
                if(b.settings.action!=KeyAction::Release&&(!b.settings.exclusive||triggered_==0)){
                    bool handled=callback(b.name,KeyAction::Press);if(handled&&b.settings.cancel){cancel=true;++triggered_;}}
            }
            if(!down&&contains&&b.releaseArmed){
                if(!b.releaseSuppressed&&(b.settings.context==KeyContext::Any||b.settings.context==context)&&b.settings.action!=KeyAction::Press&&(!b.settings.exclusive||triggered_==0)){
                    bool handled=callback(b.name,KeyAction::Release);if(handled&&b.settings.cancel){cancel=true;++triggered_;}}
                b.releaseArmed=false;
            }
        }
        if(held_.empty())triggered_=0;return cancel;
    }
};
}
