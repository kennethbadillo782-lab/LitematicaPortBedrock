#include "structure/write/command/CommandTransport.h"
#include "structure/write/command/RealmCompanion.h"
#include "structure/write/command/CompanionHandshake.h"
#include "structure/write/command/CompanionWire.h"
#include "structure/StructureFileLoader.h"
#include "plugin/LHolo.h"
#include "ll/api/mod/NativeMod.h"
#include "ll/api/memory/Hook.h"
#include "mc/client/game/ClientInstance.h"
#include "mc/client/player/LocalPlayer.h"
#include "mc/client/network/ClientNetworkHandler.h"
#include "mc/network/PacketSender.h"
#include "mc/network/LoopbackPacketSender.h"
#include "mc/network/packet/CommandRequestPacket.h"
#include "mc/network/packet/CommandOutputPacket.h"
#include <chrono>
#include "nlohmann/json.hpp"
#include <sstream>
#include "mc/server/commands/CommandContext.h"
#include "mc/server/commands/PlayerCommandOrigin.h"
#include <deque>
#include <utility>
#include <cstdlib>
#include <stdexcept>
#include <algorithm>
#include <mutex>
#include <regex>
#include <set>
#include <unordered_map>

namespace lholo::structure::write::command {
namespace {
std::mutex mutex;
std::deque<Receipt> receipts;
std::unordered_map<mce::UUID, std::string> pending;
std::unordered_map<std::string, std::string> companionPending;
std::deque<Dispatch> companionQueue;
std::deque<std::pair<std::string,std::string>> companionFragments;
std::unordered_map<std::string, std::vector<std::string>> batchIds;
struct JobState {
    bool active{}, cancelRequested{};
    std::string id, state, error;
    std::deque<std::string> fragments;
    std::uint64_t placed{}, failed{}, total{}, waiting{}, uploadedAt{};
};
JobState job;
std::deque<std::string> adminQueue;
std::string adminProblem; // why the last panel commands were not sent, for the Panel Admin to show
std::uint64_t nowMs() { return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count()); }

unsigned diagnosticSends{},outputLogs{};
bool installed{};
CompanionHandshake handshake;
std::unique_ptr<CommandRequestPacketPayload> vanillaRequest;
std::uint64_t vanillaWorld{};
SubClientId vanillaSender{};
SerializationMode vanillaSerialization{};
thread_local bool sendingCompanion{};
LL_TYPE_INSTANCE_HOOK(CompanionVanillaRequest, ll::memory::HookPriority::Normal,
    LoopbackPacketSender, &LoopbackPacketSender::$sendToServer, void, Packet& packet) {
    if(!sendingCompanion && packet.getId()==MinecraftPacketIds::CommandRequest){
        auto const& request=static_cast<CommandRequestPacket const&>(packet);
        if(request.mCommand.get()=="/litematica:hello"||request.mCommand.get()=="litematica:hello"){
            std::lock_guard lock(mutex);
            vanillaRequest=std::make_unique<CommandRequestPacketPayload>(static_cast<CommandRequestPacketPayload const&>(request));
            vanillaWorld=detail::structureFileWorldRevision();vanillaSender=packet.mSenderSubId;
            vanillaSerialization=request.mSerializationMode;
            LHolo::getInstance().getSelf().getLogger().info(
                "[realm-transport] Captured vanilla hello request: origin={}, version={}, internal={}, requestIdLength={}, serialization={}",
                static_cast<int>(request.mOrigin->mType),static_cast<int>(request.mVersion),
                request.mInternalSource,request.mOrigin->mRequestId->size(),static_cast<int>(vanillaSerialization));
            LHolo::getInstance().getSelf().getLogger().flush();
        }
    }
    origin(packet);
}
LL_TYPE_INSTANCE_HOOK(CommandWriteOutput, ll::memory::HookPriority::Normal,
    ClientNetworkHandler, &ClientNetworkHandler::$handle, void,
    NetworkIdentifier const& source, CommandOutputPacket const& packet) {
    // Script command output may use a server-origin UUID. Correlate the result
    // using the operation's random token as well as the normal packet identity.
    bool recognized = false;
    {
        // Diagnostics: the first command outputs of each connection, to see exactly what the server answers.
        std::lock_guard lock(mutex);
        if(realmCompanionEnabled()&&outputLogs<80){
            ++outputLogs;
            std::string text;
            for(auto const& message:packet.mOutput->mMessages){
                text+=" [id="+message.mMessageId.substr(0,100);
                for(auto const& parameter:message.mParams)text+=" p="+parameter.substr(0,100);
                text+="]";
            }
            auto& log=LHolo::getInstance().getSelf().getLogger();
            log.info("[realm-transport] output success={} messages={} known={} pending={} companionPending={}{}",
                static_cast<int>(packet.mOutput->mSuccessCount),packet.mOutput->mMessages.size(),
                pending.contains(packet.mOriginData->mUUID.get()),pending.size(),companionPending.size(),text);
            log.flush();
        }
        auto inspect = [&](std::string const& text) {
            handshake.observe(text);
            if(text.find("Litematica Realm Companion ")!=std::string::npos){
                // The automatic greeting is answered silently.
                std::erase_if(pending,[](auto const& entry){return entry.second=="@companion-hello";});recognized=true;
            }
            if(text=="LP_WAIT") {
                std::erase_if(pending,[](auto const& entry){return entry.second=="@companion-poll";});recognized=true;
            }
            std::istringstream lines(text); std::string line;
            while (std::getline(lines, line)) {
                if (line.starts_with("LP3|")) {
                    // LP3|id|state|placed|failed|total|waiting|error
                    std::vector<std::string> columns; std::size_t from = 0;
                    for (int i = 0; i < 7; ++i) { auto bar = line.find('|', from); if (bar == std::string::npos) break; columns.push_back(line.substr(from, bar - from)); from = bar + 1; }
                    columns.push_back(line.substr(from));
                    recognized = true;
                    if (columns.size() < 8 || columns[1] != job.id || !job.active) continue;
                    auto const& state = columns[2];
                    if (state == "unknown") continue; // the server has not started receiving yet
                    job.state = state; job.placed = std::strtoull(columns[3].c_str(), nullptr, 10); job.failed = std::strtoull(columns[4].c_str(), nullptr, 10);
                    if (std::strtoull(columns[5].c_str(), nullptr, 10) != 0) job.total = std::strtoull(columns[5].c_str(), nullptr, 10);
                    job.waiting = std::strtoull(columns[6].c_str(), nullptr, 10); if (!columns[7].empty()) job.error = columns[7];
                    // Once the server holds the whole job (queued, running, waiting or done) the client's part is over: the
                    // server finishes it by itself and reports in chat, so the tool is free for the next operation.
                    if (state != "receiving") { job.active = false; std::erase_if(pending, [](auto const& entry) { return entry.second == "@companion-job"; }); }
                    continue;
                }
                if (line.starts_with("LP2|")) {
                    // LP2|prefix|first-last;first-last|seq:error;seq:error  (sequence numbers are the ids' numeric suffix)
                    std::vector<std::string> columns; std::size_t from = 0;
                    for (int i = 0; i < 3; ++i) { auto bar = line.find('|', from); if (bar == std::string::npos) break; columns.push_back(line.substr(from, bar - from)); from = bar + 1; }
                    columns.push_back(line.substr(from));
                    if (columns.size() < 4) continue;
                    auto const& prefix = columns[1];
                    std::vector<std::pair<long long, long long>> ranges; std::unordered_map<long long, std::string> errors;
                    std::istringstream rangeStream(columns[2]); std::string item;
                    while (std::getline(rangeStream, item, ';')) { auto dash = item.find('-'); if (dash != std::string::npos) ranges.emplace_back(std::atoll(item.c_str()), std::atoll(item.c_str() + dash + 1)); }
                    std::istringstream errorStream(columns[3]);
                    while (std::getline(errorStream, item, ';')) { auto colon = item.find(':'); if (colon != std::string::npos) errors[std::atoll(item.c_str())] = item.substr(colon + 1); }
                    for (auto entry = companionPending.begin(); entry != companionPending.end();) {
                        auto const& id = entry->first; auto colon = id.rfind(':');
                        if (colon == std::string::npos || id.compare(0, colon, prefix) != 0) { ++entry; continue; }
                        long long const seq = std::atoll(id.c_str() + colon + 1);
                        bool done = false;
                        for (auto const& [first, last] : ranges) if (seq >= first && seq <= last) { done = true; break; }
                        if (!done) { ++entry; continue; }
                        auto failure = errors.find(seq);
                        receipts.push_back({id, failure == errors.end() ? Reply::Success : Reply::Failed,
                            failure == errors.end() ? "" : "Realm companion: " + failure->second, {}});
                        entry = companionPending.erase(entry); recognized = true;
                    }
                    recognized = true;
                    continue;
                }
                if (!line.starts_with("LP1|")) continue;
                auto end=line.find('|',4);if(end==std::string::npos)continue;
                auto found=companionPending.find(line.substr(4,end-4));
                if(found==companionPending.end())continue;
                auto result=line.substr(end+1);
                receipts.push_back({found->second,result=="ok"?Reply::Success:Reply::Failed,
                    result=="ok"?"":"Realm companion: "+result,{}});
                companionPending.erase(found);recognized=true;
            }
        };
        for(auto const& message:packet.mOutput->mMessages){inspect(message.mMessageId);for(auto const& parameter:message.mParams)inspect(parameter);}
        if(recognized)std::erase_if(pending,[](auto const& entry){return entry.second=="@companion-poll";});
    }
    // Player origins need not preserve an arbitrary textual request ID. The
    // packet's origin UUID is the identity shared by request and response.
    std::optional<std::string> tracked;
    {
        std::lock_guard lock(mutex);
        auto request = pending.find(packet.mOriginData->mUUID.get());
        if (request != pending.end()) {
            tracked = std::move(request->second); pending.erase(request);
        }
    }
    if (!tracked) { if(!recognized)origin(source, packet); return; }
    auto const& id = *tracked;
    if (id == "@companion-hello") return;
    if (id == "@companion-job") {
        // A fragment normally succeeds silently; a rejection means the upload cannot complete.
        if (packet.mOutput->mSuccessCount > 0) return;
        std::lock_guard lock(mutex);
        if (job.active) {
            job.active = false; job.state = "failed";
            job.error = "El servidor rechazó un fragmento del trabajo. Comprueba que el Companion 0.12.0 esté activo en el mundo.";
            for (auto const& message : packet.mOutput->mMessages) job.error += " (" + message.mMessageId.substr(0, 60) + ")";
        }
        return;
    }

    if (id.starts_with("@companion-batch:")) {
        if (packet.mOutput->mSuccessCount > 0) return;
        std::lock_guard lock(mutex);
        auto batch = batchIds.find(id.substr(17));
        if (batch != batchIds.end()) {
            for (auto const& token : batch->second)
                if (companionPending.erase(token)) receipts.push_back({token, Reply::Failed, "Realm companion batch rejected. Verify server addon version 0.12.0.", {}});
            batchIds.erase(batch);
        }
        return;
    }
    if (id == "@companion-poll") {
        std::lock_guard lock(mutex);
        auto parse = [&](std::string const& text) {
            std::istringstream lines(text); std::string line;
            while (std::getline(lines, line)) {
                if (!line.starts_with("LP1|")) continue;
                auto end = line.find('|', 4); if (end == std::string::npos) continue;
                auto token = line.substr(4, end - 4);
                auto found = companionPending.find(token);
                if (found == companionPending.end()) continue;
                auto result = line.substr(end + 1);
                receipts.push_back({found->second, result == "ok" ? Reply::Success : Reply::Failed,
                    result == "ok" ? "" : "Realm companion: " + result, {}});
                companionPending.erase(found);
            }
        };
        for (auto const& message : packet.mOutput->mMessages) {
            parse(message.mMessageId); for (auto const& parameter : message.mParams) parse(parameter);
        }
        return;
    }
    {
        std::lock_guard lock(mutex);
        // A successful command output only means the server queued the action.
        // A later result poll confirms actual server execution.
        if (packet.mOutput->mSuccessCount > 0 && companionPending.contains(id)) return;
        companionPending.erase(id);
    }
    Receipt receipt{id, Reply::Failed, "writeWorld.commandFailed"};
    if (packet.mOutput->mSuccessCount > 0) { receipt.reply = Reply::Success; receipt.error.clear(); }
    else for (auto const& message : packet.mOutput->mMessages) {
        auto const& key = message.mMessageId;
        if (key == "commands.setblock.failed") {
            receipt.reply = Reply::Skip; receipt.error.clear(); break;
        }
        if (key == "commands.generic.syntax") {
            receipt.error = "writeWorld.commandSyntax"; break;
        }
        if (key == "commands.setblock.noChange") {
            receipt.reply = Reply::NoChange; receipt.error = "writeWorld.commandVerify"; break;
        }
        if (key == "commands.setblock.outOfWorld" || key == "commands.generic.outOfWorld"
            || key == "commands.generic.destinationNotLoaded") {
            receipt.reply = Reply::Unloaded; receipt.error = "writeWorld.commandWaiting"; break;
        }
        if (key == "commands.generic.error.permissions" || key == "commands.generic.disabled"
            || key == "commands.generic.unknown") receipt.error = "writeWorld.commandPermission";
    }
    // Transport preserves diagnostics; only the scheduler decides whether a
    // response actually pauses the task and warrants a single log entry.
    if (receipt.reply == Reply::Failed || receipt.reply == Reply::Unloaded) {
        for (auto const& message : packet.mOutput->mMessages) {
            if (!receipt.messageIds.empty()) receipt.messageIds += ", ";
            receipt.messageIds += message.mMessageId;
        }
    }
    std::lock_guard lock(mutex);
    receipts.push_back(std::move(receipt));
}
}
bool installCommandTransport() {
    if (!installed) {
        if (CommandWriteOutput::hook() != 0) return false;
        if(CompanionVanillaRequest::hook()!=0){CommandWriteOutput::unhook();return false;}
        installed = true;
    }
    return installed;
}
void clearCommandReceipts() { std::lock_guard lock(mutex); receipts.clear(); pending.clear(); companionPending.clear(); companionQueue.clear();companionFragments.clear();batchIds.clear();diagnosticSends=0;outputLogs=0; handshake.reset(); }
void forgetCommand(std::string const& id) {
    std::lock_guard lock(mutex);
    std::erase_if(pending, [&](auto const& entry) { return entry.second == id; });
    companionPending.erase(id);
}
void uninstallCommandTransport() {
    if (installed) { CommandWriteOutput::unhook(); CompanionVanillaRequest::unhook(); } installed = false; clearCommandReceipts();
}
void sendCommand(ClientInstance& client, LocalPlayer& player, Dispatch const& request) {
    auto text = request.text;
    if (realmCompanionEnabled()) {
        std::lock_guard lock(mutex);
        companionPending.emplace(request.id, request.id);
        companionQueue.push_back(request);
        return;
    }
    CommandContext context{text,
        std::make_unique<PlayerCommandOrigin>(player.getLevel(), player.getOrCreateUniqueID()),
        static_cast<int>(CurrentCmdVersion::Latest)};
    CommandRequestPacketPayload payload{context, false};
    CommandRequestPacket packet{std::move(payload)};
    {
        std::lock_guard lock(mutex);
        pending.emplace(packet.mOrigin->mUUID.get(), request.id);
    }
    client.getPacketSender().sendToServer(packet);
}
bool companionHasCapacity() {
    std::lock_guard lock(mutex);
    return handshake.ready() && companionPending.size() < 2048;
}
std::string companionConnectionError() { std::lock_guard lock(mutex); return handshake.error(); }
void pollCompanionResults(ClientInstance& client, LocalPlayer& player) {
    using clock = std::chrono::steady_clock;
    static auto nextWrite = clock::now(), nextPoll = clock::now();
    auto send = [&](std::string const& text, std::string const& tracking, bool track = true) {
        if(text.size()>CompanionJobCommandLimit){std::lock_guard lock(mutex);handshake.fail("Comando demasiado largo; envío bloqueado antes de llegar al servidor.");return;}
        std::unique_ptr<CommandRequestPacketPayload> payload;
        SubClientId sender{};SerializationMode serialization{};
        {
            std::lock_guard lock(mutex);
            if(vanillaRequest&&vanillaWorld==detail::structureFileWorldRevision()){
                payload=std::make_unique<CommandRequestPacketPayload>(*vanillaRequest);
                sender=vanillaSender;serialization=vanillaSerialization;
            }
        }
        if(!payload){
            // Same request the chat produces when the player types a command (player origin, version Latest = 55,
            // cereal serialization, primary client), so nothing has to be typed by hand. A typed request, when
            // there is one, is still preferred above.
            CommandContext context{text,std::make_unique<PlayerCommandOrigin>(player.getLevel(),player.getOrCreateUniqueID()),
                static_cast<int>(CurrentCmdVersion::Latest)};
            payload=std::make_unique<CommandRequestPacketPayload>(context,false);
            sender=SubClientId::PrimaryClient;serialization=SerializationMode::CerealOnly;
        }
        payload->mCommand=text;
        payload->mOrigin->mUUID=mce::UUID::random();
        CommandRequestPacket packet{std::move(*payload)};
        packet.mSenderSubId=sender;packet.mSerializationMode=serialization;
        if (track) { std::lock_guard lock(mutex); pending.emplace(packet.mOrigin->mUUID.get(), tracking); }
        struct SendingGuard { SendingGuard(){sendingCompanion=true;} ~SendingGuard(){sendingCompanion=false;} } guard;
        std::size_t queued{},fragments{},waiting{};bool log{};
        {std::lock_guard lock(mutex);log=diagnosticSends++<60;queued=companionQueue.size();fragments=companionFragments.size();waiting=companionPending.size();}
        if(log){auto& logger=LHolo::getInstance().getSelf().getLogger();logger.info("[realm-transport] sending kind={} bytes={} tracking={} queue={} fragments={} companionPending={}",text.substr(0,text.find(' ')),text.size(),tracking.substr(0,24),queued,fragments,waiting);logger.flush();}
        client.getPacketSender().sendToServer(packet);
    };
    if (!realmCompanionEnabled()) return;
    bool hello{};
    {
        std::lock_guard lock(mutex);
        auto now=static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(clock::now().time_since_epoch()).count());
        handshake.tick(now);hello=handshake.begin(now);
    }
    if(hello)send("/litematica:hello","@companion-hello");
    {
        std::lock_guard lock(mutex);
        // A Companion that answered with another version (or not at all) cannot run queued panel commands.
        if (!handshake.ready() && !handshake.error().empty() && !adminQueue.empty()) {
            LHolo::getInstance().getSelf().getLogger().warn("[realm-transport] panel commands dropped: {}", handshake.error());
            adminQueue.clear();
            adminProblem = handshake.error();
        }
    }
    {std::lock_guard lock(mutex);if(!handshake.ready())return;}
    {
        // Panel Admin commands: one every 50 ms, independent of pastes.
        static auto nextAdmin = clock::now();
        std::optional<std::string> command;
        { std::lock_guard lock(mutex); if (!adminQueue.empty() && clock::now() >= nextAdmin) { command = std::move(adminQueue.front()); adminQueue.pop_front(); nextAdmin = clock::now() + std::chrono::milliseconds(50); } }
        if (command) send(*command, "@companion-admin", false);
    }
    {
        // LP3: pace the upload, ask the server for progress, and relay a cancel request.
        bool busy{}, cancel{}, pollNow{}; std::optional<std::string> fragment; std::string jobId;
        {
            std::lock_guard lock(mutex);
            busy = job.active; cancel = job.cancelRequested; jobId = job.id; job.cancelRequested = false;
            auto const now = clock::now();
            if (busy && now >= nextWrite && !job.fragments.empty()) {
                fragment = std::move(job.fragments.front()); job.fragments.pop_front();
                nextWrite = now + std::chrono::milliseconds(15);
                if (job.fragments.empty()) job.uploadedAt = nowMs();
            }
            if (busy && now >= nextPoll) { nextPoll = now + std::chrono::milliseconds(500); pollNow = true; }
            if (busy && job.fragments.empty() && job.uploadedAt && job.state == "uploading" && nowMs() - job.uploadedAt > 20000) {
                job.active = false; job.state = "failed";
                job.error = "El servidor no confirmó el trabajo. Comprueba Companion 0.12.0 en el mundo y tus permisos de operador.";
                busy = false;
            }
        }
        if (cancel) send("/litematica:cancel", "@companion-poll", false);
        if (fragment) send(*fragment, "@companion-job");
        if (busy && pollNow) send("/litematica:jobstatus " + jobId, "@companion-poll", false);
        if (busy || cancel) return;
    }
    if (clock::now() >= nextWrite) {
        // LP2: consecutive writes along +Z of the same block collapse into one run, coordinates are relative to a batch
        // origin, and the palette travels once per batch. One fragment is sent every 25 ms.
        nextWrite = clock::now() + std::chrono::milliseconds(25);
        std::optional<std::pair<std::string,std::string>> fragment;
        {
            std::lock_guard lock(mutex);
            if (companionFragments.empty() && !companionQueue.empty()) {
                static std::regex const pattern(R"(^/execute in (overworld|nether|the_end) run setblock (-?\d+) (-?\d+) (-?\d+) (.+) replace$)");
                struct Run { long long seq; int palette, x, y, z, length; };
                std::vector<Run> runs; std::vector<std::string> palette; std::unordered_map<std::string, int> paletteIndex;
                std::vector<std::string> ids; std::string prefix, dimension; std::size_t paletteSize = 2; long long blocks = 0;
                while (!companionQueue.empty() && runs.size() < 150 && blocks < 3800) {
                    auto const& request = companionQueue.front();
                    auto const colon = request.id.rfind(':');
                    std::smatch match; std::string const requestPrefix = colon == std::string::npos ? std::string{} : request.id.substr(0, colon);
                    long long const seq = colon == std::string::npos ? -1 : std::atoll(request.id.c_str() + colon + 1);
                    if (seq < 0 || requestPrefix.empty() || !std::regex_match(request.text, match, pattern)) {
                        if (!runs.empty()) break;
                        receipts.push_back({request.id, Reply::Failed, "Realm companion: unsupported write command", {}});
                        companionPending.erase(request.id); companionQueue.pop_front(); continue;
                    }
                    if (!runs.empty() && (requestPrefix != prefix || match[1].str() != dimension)) break;
                    int const x = std::stoi(match[2].str()), y = std::stoi(match[3].str()), z = std::stoi(match[4].str());
                    auto const spec = match[5].str();
                    auto found = paletteIndex.find(spec);
                    int index;
                    if (found == paletteIndex.end()) {
                        auto const added = nlohmann::json(spec).dump().size() + 1;
                        if (palette.size() >= 64 || paletteSize + added + (runs.size() + 1) * 24 > 2300) break;
                        paletteSize += added; index = static_cast<int>(palette.size());
                        palette.push_back(spec); paletteIndex.emplace(spec, index);
                    } else {
                        index = found->second;
                        if (paletteSize + (runs.size() + 1) * 24 > 2300) break;
                    }
                    prefix = requestPrefix; dimension = match[1].str();
                    ids.push_back(request.id); ++blocks;
                    if (!runs.empty()) {
                        auto& last = runs.back();
                        if (last.palette == index && last.x == x && last.y == y && last.z + last.length == z
                            && last.seq + last.length == seq && last.length < 64) { ++last.length; companionQueue.pop_front(); continue; }
                    }
                    runs.push_back({seq, index, x, y, z, 1});
                    companionQueue.pop_front();
                }
                if (!runs.empty()) {
                    auto json = nlohmann::json::object();
                    json["f"] = prefix; json["d"] = dimension; json["p"] = palette;
                    json["s"] = runs.front().seq; json["o"] = {runs.front().x, runs.front().y, runs.front().z};
                    auto list = nlohmann::json::array();
                    for (auto const& run : runs)
                        list.push_back({run.seq - runs.front().seq, run.palette, run.x - runs.front().x, run.y - runs.front().y, run.z - runs.front().z, run.length});
                    json["r"] = std::move(list);
                    auto const text = json.dump();
                    std::string encoded; constexpr char hex[] = "0123456789abcdef";
                    for (unsigned char c : text) { encoded += hex[c >> 4]; encoded += hex[c & 15]; }
                    if (encoded.size() > 5200) {
                        for (auto const& id : ids) if (companionPending.erase(id)) receipts.push_back({id, Reply::Failed, "Realm companion: batch exceeds limit", {}});
                    } else {
                        auto const tag = mce::UUID::random().asString();
                        batchIds[tag] = std::move(ids);
                        if (batchIds.size() > 64) batchIds.erase(batchIds.begin());
                        for (auto& command : fragmentCompanionBatch(encoded, tag))
                            companionFragments.emplace_back(std::move(command), "@companion-batch:" + tag);
                    }
                }
            }
            if (!companionFragments.empty()) { fragment = std::move(companionFragments.front()); companionFragments.pop_front(); }
        }
        if (fragment) send(fragment->first, fragment->second);
    }
    if (clock::now() < nextPoll) return;
    nextPoll = clock::now() + std::chrono::milliseconds(250);
    // One compressed confirmation query per active operation prefix. The game's command parser also rejects commas
    // inside a String argument, so nothing is ever joined with commas.
    std::set<std::string> prefixes;
    {
        std::lock_guard lock(mutex);
        for (auto const& [token, id] : companionPending) {
            auto colon = token.rfind(':');
            if (colon != std::string::npos) prefixes.insert(token.substr(0, colon));
            if (prefixes.size() >= 2) break;
        }
    }
    for (auto const& prefix : prefixes) send("/litematica:ack " + prefix, "@companion-poll", false);
}
void startCompanionJob(std::string const& dimension, std::vector<std::string> const& palette, std::vector<BulkChunk> const& chunks, std::uint64_t totalBlocks) {
    std::vector<unsigned char> bytes;
    auto put16 = [&](unsigned value) { bytes.push_back(value & 255); bytes.push_back((value >> 8) & 255); };
    auto put32 = [&](int value) { auto raw = static_cast<std::uint32_t>(value); for (int i = 0; i < 4; ++i) bytes.push_back((raw >> (8 * i)) & 255); };
    nlohmann::json header{{"d", dimension}, {"p", palette}};
    auto const headerText = header.dump(-1, ' ', true);
    if (headerText.size() > 65535) throw std::length_error("writeWorld.commandUnsupported");
    put16(static_cast<unsigned>(headerText.size()));
    bytes.insert(bytes.end(), headerText.begin(), headerText.end());
    for (auto const& chunk : chunks) {
        for (std::size_t offset = 0; offset < chunk.cuboids.size(); offset += 60000) {
            auto const count = std::min<std::size_t>(60000, chunk.cuboids.size() - offset);
            put32(chunk.cx); put32(chunk.cz); put16(static_cast<unsigned>(count));
            for (std::size_t i = 0; i < count; ++i) {
                auto const& c = chunk.cuboids[offset + i];
                int const lx = c.x - chunk.cx * 16, lz = c.z - chunk.cz * 16;
                if (lx < 0 || lz < 0 || c.w < 1 || c.d < 1 || c.h < 1 || c.w > 16 || c.d > 16 || c.h > 256 || lx + c.w > 16 || lz + c.d > 16 || c.palette < 0 || c.palette > 65535)
                    throw std::invalid_argument("writeWorld.commandUnsupported");
                bytes.push_back(static_cast<unsigned char>(lx << 4 | lz)); put16(static_cast<unsigned>(c.y + 128));
                bytes.push_back(static_cast<unsigned char>((c.w - 1) << 4 | (c.d - 1))); bytes.push_back(static_cast<unsigned char>(c.h - 1)); put16(static_cast<unsigned>(c.palette));
            }
        }
    }
    std::string encoded; encoded.reserve(bytes.size() * 2); constexpr char hex[] = "0123456789abcdef";
    for (auto byte : bytes) { encoded += hex[byte >> 4]; encoded += hex[byte & 15]; }
    auto const id = mce::UUID::random().asString();
    // 400 hex characters per command (about 480 in total). Commands of about 980 characters dropped the multiplayer
    // (NetherNet) connection within a second in a hosted world on 2026-11-15, while 480 has carried jobs of millions of blocks.
    constexpr std::size_t FragmentHex = 400;
    auto const parts = (encoded.size() + FragmentHex - 1) / FragmentHex;
    if (parts > 20000) throw std::length_error("writeWorld.commandUnsupported");
    std::deque<std::string> fragments;
    for (std::size_t part = 0; part < parts; ++part) {
        auto text = "/litematica:job " + id + " " + std::to_string(part) + " " + std::to_string(parts) + " " + encoded.substr(part * FragmentHex, FragmentHex);
        if (text.size() > CompanionJobCommandLimit) throw std::length_error("Companion command too long");
        fragments.push_back(std::move(text));
    }
    std::lock_guard lock(mutex);
    std::erase_if(pending, [](auto const& entry) { return entry.second == "@companion-job"; });
    job = JobState{}; job.active = true; job.id = id; job.state = "uploading"; job.total = totalBlocks; job.fragments = std::move(fragments);
    auto& log = LHolo::getInstance().getSelf().getLogger();
    log.info("[realm-transport] job {} uploads {} cuboids in {} fragments ({} bytes), {} blocks", id, [&] { std::size_t n{}; for (auto const& c : chunks) n += c.cuboids.size(); return n; }(), parts, bytes.size(), totalBlocks);
    log.flush();
}
CompanionJobStatus companionJobStatus() {
    std::lock_guard lock(mutex);
    return {job.active, job.state, job.error, job.placed, job.failed, job.total, job.waiting};
}
bool companionJobBusy() { std::lock_guard lock(mutex); return job.active || job.cancelRequested; }
void cancelCompanionJob() {
    std::lock_guard lock(mutex);
    if (job.active) { job.active = false; job.cancelRequested = true; job.state = "cancelled"; job.fragments.clear(); }
    std::erase_if(pending, [](auto const& entry) { return entry.second == "@companion-job"; });
}
int queueCompanionCommand(std::string const& text) {
    if (!realmCompanionEnabled()) return 1;
    if (text.empty() || text.front() != '/' || text.size() > CompanionCommandLimit) return 3;
    std::lock_guard lock(mutex);
    if (adminQueue.size() >= 64) return 4;
    adminQueue.push_back(text);
    return 0;
}
bool companionCommandsPending() { std::lock_guard lock(mutex); return !adminQueue.empty(); }
std::string takeCompanionProblem() { std::lock_guard lock(mutex); return std::exchange(adminProblem, {}); }
std::optional<Receipt> takeCommandReceipt() {
    std::lock_guard lock(mutex);
    if (receipts.empty()) return {};
    auto result = std::move(receipts.front()); receipts.pop_front(); return result;
}
}
