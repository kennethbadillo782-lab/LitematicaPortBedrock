#pragma once
#include "structure/write/command/CommandWriteSession.h"
class ClientInstance;
class LocalPlayer;
namespace lholo::structure::write::command {
struct Receipt { std::string id; Reply reply; std::string error; std::string messageIds; };
bool installCommandTransport();
void uninstallCommandTransport();
void sendCommand(ClientInstance&, LocalPlayer&, Dispatch const&);
void pollCompanionResults(ClientInstance&, LocalPlayer&);
bool companionHasCapacity();
std::string companionConnectionError();
std::optional<Receipt> takeCommandReceipt();
void clearCommandReceipts();
void forgetCommand(std::string const& id);
// LP3 bulk jobs: the whole plan is uploaded once as cuboids and the server executes it (fillBlocks) by itself.
struct BulkCuboid { int x, y, z, w, h, d, palette; };
struct BulkChunk { int cx, cz; std::vector<BulkCuboid> cuboids; };
struct CompanionJobStatus {
    bool active{};
    std::string state, error;
    std::uint64_t placed{}, failed{}, total{}, waiting{};
};
void startCompanionJob(std::string const& dimension, std::vector<std::string> const& palette, std::vector<BulkChunk> const& chunks, std::uint64_t totalBlocks);
CompanionJobStatus companionJobStatus();
bool companionJobBusy();
void cancelCompanionJob();
// Short commands from other mods (the Panel Admin) sharing this connection to the Companion.
// 0 queued, 1 Companion bridge disabled, 2 /litematica:hello not typed in this connection, 3 invalid, 4 queue full.
int queueCompanionCommand(std::string const& text);
bool companionCommandsPending();
// Reason the last queued panel commands were dropped (empty if none); reading it clears it.
std::string takeCompanionProblem();
}
