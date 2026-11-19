// Entry point for other mods in the same game (the Panel Admin): they send their actions through LHolo's
// connection to the server Companion, so one server pack serves both mods.
#include "structure/write/command/CommandTransport.h"
#include <algorithm>
#include <cstring>

extern "C" __declspec(dllexport) int lholo_companion_send(char const* command) {
    if (!command) return 3;
    try {
        return lholo::structure::write::command::queueCompanionCommand(command);
    } catch (...) {
        return 3;
    }
}

// Copies (and clears) the reason the last panel commands could not reach the server Companion. Returns its length.
extern "C" __declspec(dllexport) int lholo_companion_problem(char* buffer, int size) {
    if (!buffer || size <= 0) return 0;
    try {
        auto const text = lholo::structure::write::command::takeCompanionProblem();
        auto const n = std::min<std::size_t>(text.size(), static_cast<std::size_t>(size - 1));
        std::memcpy(buffer, text.data(), n);
        buffer[n] = 0;
        return static_cast<int>(n);
    } catch (...) {
        return 0;
    }
}
