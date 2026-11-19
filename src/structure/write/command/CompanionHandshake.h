#pragma once
#include <cstdint>
#include <string>
#include <string_view>
namespace lholo::structure::write::command {
// Pure state machine: no blocks may be reserved before a compatible server reply.
class CompanionHandshake {
    enum class State { Idle, Waiting, Ready, Failed } state{State::Idle};
    std::uint64_t started{};
    std::string reason;
public:
    void reset() { state=State::Idle; reason.clear(); }
    bool begin(std::uint64_t now) {
        if(state!=State::Idle)return false;
        state=State::Waiting;started=now;return true;
    }
    void observe(std::string_view text) {
        if(state!=State::Waiting)return;
        if(text.find("Litematica Realm Companion 0.12.0")!=std::string_view::npos){state=State::Ready;return;}
        if(text.find("Litematica Realm Companion ")!=std::string_view::npos){
            state=State::Failed;reason="El Realm tiene un Companion anterior. Actualiza el complemento del Realm a 0.12.0.";
        }
    }
    void tick(std::uint64_t now) {
        if(state==State::Waiting&&now-started>=5000){state=State::Failed;reason="El Realm no confirma Companion 0.12.0. Comprueba /litematica:hello, el complemento activo y tus permisos de operador.";}
    }
    bool ready() const { return state==State::Ready; }
    void fail(std::string message) { state=State::Failed;reason=std::move(message); }
    std::string const& error() const { return reason; }
};
}
