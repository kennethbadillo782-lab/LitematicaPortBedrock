#include "input/MenuOpenRequest.h"

#include <atomic>

namespace lholo::input {
namespace {
constexpr int kNone = -1;
std::atomic_int pending{kNone};
}
void requestMenuOpen(MenuTarget target) { pending.store(static_cast<int>(target), std::memory_order_release); }
std::optional<MenuTarget> consumeMenuOpenRequest() {
    auto const value = pending.exchange(kNone, std::memory_order_acq_rel);
    if (value == kNone) return std::nullopt;
    return static_cast<MenuTarget>(value);
}
void clearMenuOpenRequest() { pending.store(kNone, std::memory_order_release); }
}
