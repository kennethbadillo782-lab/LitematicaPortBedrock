#include "structure/write/command/CommandWorldWriter.h"
#include "structure/write/command/SetBlockCommandBuilder.h"
#include "structure/write/command/CommandTransport.h"
#include "structure/write/command/RealmCompanion.h"
#include "structure/StructureFileLoader.h"
#include "ll/api/memory/Hook.h"
#include "ll/api/service/Bedrock.h"
#include "mc/client/game/ClientInstance.h"
#include "mc/client/game/ClientInstanceState.h"
#include "mc/client/player/LocalPlayer.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/block/Block.h"
#include "structure/write/StructureWritePlan.h"
#include "mc/platform/UUID.h"
#include "plugin/LHolo.h"
#include "ll/api/mod/NativeMod.h"
#include <atomic>
#include <algorithm>
#include <chrono>
#include <limits>
#include <mutex>
#include <string_view>

namespace lholo::structure::write::command {
namespace {
std::mutex mutex;
std::optional<Session> session;
std::uint64_t revision{}, sequence{};
std::atomic_bool installed{};
bool resumeRequested{};
std::string requestPrefix;
bool bulkStarted{}, bulkActive{}, bulkCancelled{};
std::uint64_t bulkTotal{};
std::string bulkError;
void logPause(bool wasPaused, std::string_view id = {}, std::string_view messageIds = {}) {
    if (!wasPaused && session->paused())
        LHolo::getInstance().getSelf().getLogger().warn("[command-write] paused: {}; request={}; messages={}",
            session->status().error, id, messageIds);
}
auto milliseconds() { return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
    std::chrono::steady_clock::now().time_since_epoch()).count()); }

// Greedy cuboid merge of one chunk's targets (same block, up to 16x32x16): thousands of blocks become a few records.
std::vector<BulkCuboid> greedyCuboids(std::vector<Target> const& targets, int cx, int cz) {
    std::vector<BulkCuboid> result;
    if (targets.empty()) return result;
    int yMin = std::numeric_limits<int>::max(), yMax = std::numeric_limits<int>::min();
    for (auto const& t : targets) { yMin = std::min(yMin, t.position[1]); yMax = std::max(yMax, t.position[1]); }
    std::vector<int> grid(static_cast<std::size_t>(yMax - yMin + 1) * 256, -1);
    auto at = [&](int lx, int y, int lz) -> int& { return grid[(static_cast<std::size_t>(y - yMin) * 16 + lx) * 16 + lz]; };
    for (auto const& t : targets) at(t.position[0] - cx * 16, t.position[1], t.position[2] - cz * 16) = t.palette;
    for (int y = yMin; y <= yMax; ++y)
        for (int lx = 0; lx < 16; ++lx)
            for (int lz = 0; lz < 16; ++lz) {
                int const palette = at(lx, y, lz);
                if (palette < 0) continue;
                int d = 1; while (lz + d < 16 && at(lx, y, lz + d) == palette) ++d;
                int w = 1;
                for (; lx + w < 16; ++w) { bool same = true; for (int k = 0; k < d && same; ++k) same = at(lx + w, y, lz + k) == palette; if (!same) break; }
                int h = 1;
                for (; y + h <= yMax && h < 32; ++h) { bool same = true; for (int i = 0; i < w && same; ++i) for (int k = 0; k < d && same; ++k) same = at(lx + i, y + h, lz + k) == palette; if (!same) break; }
                for (int i = 0; i < w; ++i) for (int j = 0; j < h; ++j) for (int k = 0; k < d; ++k) at(lx + i, y + j, lz + k) = -1;
                result.push_back({cx * 16 + lx, y, cz * 16 + lz, w, h, d, palette});
            }
    return result;
}
std::optional<bool> matches(LocalPlayer& player, Dispatch const& request) {
    if (!session || static_cast<int>(player.getDimensionId()) != session->dimension()) return {};
    auto& source = player.getDimensionBlockSource();
    auto const& p = request.target.position;
    BlockPos const pos{p[0], p[1], p[2]};
    if (!source.areChunksFullyLoaded(pos, pos)) return {};
    auto const& block = source.getBlock(pos);
    return request.target.palette == 0 ? block.isAir()
        : block.mSerializationIdHash == session->expected(request.target.palette);
}
void process(ClientInstance& client) {
    std::lock_guard lock(mutex);
    if (companionCommandsPending() && !(bulkStarted && (bulkActive || companionJobBusy())) && !(session && session->status().active)) {
        // Only panel commands are waiting: send them without touching (or resetting) the write session.
        if (auto* commandPlayer = client.getLocalPlayer(); commandPlayer && client.$getClientInstanceState() == ClientInstanceState::Playing)
            pollCompanionResults(client, *commandPlayer);
        return;
    }
    if (bulkStarted && (bulkActive || companionJobBusy())) {
        auto* bulkPlayer = client.getLocalPlayer();
        if (bulkActive && revision != detail::structureFileWorldRevision()) { cancelCompanionJob(); bulkActive = false; bulkCancelled = true; }
        if (client.$getClientInstanceState() == ClientInstanceState::Playing && bulkPlayer) {
            pollCompanionResults(client, *bulkPlayer);
            if (auto error = companionConnectionError(); !error.empty() && bulkActive) { cancelCompanionJob(); bulkActive = false; bulkError = std::move(error); }
        }
        if (bulkActive && !companionJobStatus().active) bulkActive = false;
        return;
    }
    if (!session || !session->status().active) { clearCommandReceipts(); return; }
    if (revision != detail::structureFileWorldRevision()) { session->cancel(); clearCommandReceipts(); return; }
    auto* player = client.getLocalPlayer();
    static unsigned receiptLogs{};
    while (auto receipt = takeCommandReceipt()) {
        if (receiptLogs++ < 40) {
            auto& logger = LHolo::getInstance().getSelf().getLogger();
            logger.info("[command-write] receipt id={} reply={} error={}", receipt->id.substr(receipt->id.size() > 12 ? receipt->id.size() - 12 : 0),
                static_cast<int>(receipt->reply), receipt->error); logger.flush();
        }
        bool const wasPaused = session->paused();
        session->receive(receipt->id, receipt->reply, std::move(receipt->error));
        logPause(wasPaused, receipt->id, receipt->messageIds);
    }
    if (client.$getClientInstanceState() != ClientInstanceState::Playing || !player) return;
    {
        // Log every state change (and a heartbeat every 5 s) so a stalled write can be diagnosed from the log alone.
        static State lastState = State::Idle; static std::uint64_t lastDone = ~0ULL, lastBeat{};
        auto const& status = session->status(); auto const now = milliseconds();
        if (status.state != lastState || status.done != lastDone && now - lastBeat > 5000 || now - lastBeat > 5000) {
            lastState = status.state; lastDone = status.done; lastBeat = now;
            auto& logger = LHolo::getInstance().getSelf().getLogger();
            logger.info("[command-write] state={} done={}/{} error={}", static_cast<int>(status.state), status.done, status.total, status.error);
            logger.flush();
        }
    }
    pollCompanionResults(client, *player);
    if(auto error=companionConnectionError();!error.empty()){
        session->fail(std::move(error));clearCommandReceipts();return;
    }
    for (auto const& request : session->checks(resumeRequested)) {
        auto const observed = matches(*player, request);
        if (observed.value_or(false)) {
            session->verify(request.id, true); forgetCommand(request.id);
        } else if (resumeRequested && observed.has_value() && session->retry(request.id)) forgetCommand(request.id);
    }
    if (std::exchange(resumeRequested, false)) session->resume();
    auto const feet = player->getFeetBlockPos();
    unsigned sent{};
    for (unsigned scanned = 0; scanned < Session::ScanBudget && sent < Session::BatchSize; ++scanned) {
        if (realmCompanionEnabled() && !companionHasCapacity()) break;
        bool const wasPaused = session->paused();
        auto request = session->next(milliseconds(), {feet.x,feet.y,feet.z}, static_cast<int>(player->getDimensionId()),
            requestPrefix + std::to_string(++sequence));
        logPause(wasPaused);
        if (!request) break;
        if (session->skipPlacedEnabled()
            && session->skipExisting(request->id, matches(*player, *request).value_or(false))) continue;
        sendCommand(client, *player, *request);
        ++sent;
    }
    if(realmCompanionEnabled()&&session->paused()){
        auto reason=session->status().error;
        if(reason=="writeWorld.commandUnknown")reason="El Realm no confirmó la escritura. Operación detenida; puede haber cambios parciales. Comprueba Companion 0.12.0 y revisa el área antes de repetir.";
        session->fail(std::move(reason));clearCommandReceipts();
    }
}
LL_TYPE_INSTANCE_HOOK(CommandWriteUpdate, ll::memory::HookPriority::Normal,
    ClientInstance, &ClientInstance::$update, bool, bool initialized, bool allowGameUpdate) {
    bool const result = origin(initialized, allowGameUpdate);
    auto client = ll::service::getClientInstance();
    if (client && &client.get() == this) {
        try { process(*this); }
        catch (std::exception const& error) {
            std::lock_guard lock(mutex);
            if (session && session->status().active && !session->paused()) {
                session->suspend();
                LHolo::getInstance().getSelf().getLogger().error("[command-write] paused: {}", error.what());
            }
        }
    }
    return result;
}
}
bool commandWorldAvailable() { return installed; }
bool installCommandWorldWriter() {
    if (installed) return true;
    if (!installCommandTransport()) return false;
    installed = CommandWriteUpdate::hook() == 0;
    if (!installed) uninstallCommandTransport();
    return installed;
}
void cancelCommandWrite() {
    std::lock_guard lock(mutex);
    if (bulkStarted && bulkActive) { cancelCompanionJob(); bulkActive = false; bulkCancelled = true; }
    if (session) session->cancel();
    resumeRequested = false; clearCommandReceipts();
}
void uninstallCommandWorldWriter() {
    if (installed) CommandWriteUpdate::unhook();
    installed = false; cancelCommandWrite(); uninstallCommandTransport();
}
Status commandWriteStatus() {
    std::lock_guard lock(mutex);
    if (bulkStarted) {
        auto const job = companionJobStatus();
        Status status; status.total = bulkTotal; status.done = std::min(bulkTotal, job.placed + job.failed); status.failedSkipped = job.failed;
        if (!bulkError.empty()) { status.state = State::Cancelled; status.error = bulkError; return status; }
        if (bulkCancelled || job.state == "cancelled") { status.state = State::Cancelled; return status; }
        if (job.active) { status.active = true; status.state = State::Running; return status; }
        if (job.state == "queued" || job.state == "running" || job.state == "waiting" || job.state == "done") {
            // Delivered: the server executes the job on its own and announces the result in chat.
            status.state = State::Complete; status.done = bulkTotal; return status;
        }
        status.state = State::Cancelled; status.error = job.error.empty() ? "writeWorld.commandUnknown" : job.error; return status;
    }
    return session ? session->status() : Status{};
}
void resumeCommandWrite() {
    std::lock_guard lock(mutex);
    resumeRequested = session && session->status().canResume;
}
void startCommandWrite(StructureWritePlan const& plan, std::uint64_t worldRevision, bool skipPlaced) {
    auto client = ll::service::getClientInstance();
    auto* player = client ? client->getLocalPlayer() : nullptr;
    if (!player || static_cast<int>(player->getDimensionId()) != plan.target.dimension)
        throw std::runtime_error("World context changed before command writing");
    auto& region = player->getDimensionBlockSource();
    std::array<int,3> maximum;
    if (!writeBounds(plan.target, plan.size, maximum)) throw std::runtime_error("Invalid command write coordinates");
    if (plan.target.position[1] < region.getMinHeight() || maximum[1] >= region.getMaxHeight())
        throw std::range_error("writeWorld.heightLimit");
    // Validate command-only limitations before any network side effect.
    auto built = makeCommandPlan(plan);
    if (realmCompanionEnabled()) {
        // LP3: upload the whole plan once as cuboids; the server places everything itself.
        std::vector<BulkChunk> chunks; std::uint64_t total = 0;
        for (auto const& chunk : built.chunks) {
            total += chunk.targets.size();
            auto cuboids = greedyCuboids(chunk.targets, chunk.position[0], chunk.position[1]);
            if (!cuboids.empty()) chunks.push_back({chunk.position[0], chunk.position[1], std::move(cuboids)});
        }
        std::lock_guard lock(mutex);
        clearCommandReceipts(); session.reset(); revision = worldRevision; resumeRequested = false;
        bulkStarted = true; bulkActive = total != 0; bulkCancelled = false; bulkError.clear(); bulkTotal = total;
        if (total != 0) startCompanionJob(built.dimensionName, built.palette, chunks, total);
        return;
    }
    {
        std::lock_guard lock(mutex);
        bulkStarted = false; bulkActive = false;
    }
    built.skipPlaced = skipPlaced;
    auto prefix = "lholo-world-write:" + mce::UUID::random().asString() + ':';
    std::lock_guard lock(mutex);
    clearCommandReceipts(); revision = worldRevision; resumeRequested = false;
    requestPrefix = std::move(prefix); sequence = 0;
    session.emplace(std::move(built));
}
}
