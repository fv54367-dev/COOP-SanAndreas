#pragma once

#include "network/packets/blips.h"

// Phase 6: the race checkpoints of the authority's synced mission (CREATE_CHECKPOINT 06D5: courier, Beat the Cock,
// Chiliad, the schools...) on the other players, with the original scripts (vanilla-scripts; ours send them with
// Coop.UpdateCheckpoint). The authority reads CTheScripts::ScriptCheckpointArray (0xA44070, 20 x {used, id, CCheckpoint*};
// CCheckpoint: type +0x00, position +0x10, direction +0x1C, size +0x2C; gta-reversed TheScripts.h, Checkpoint.h) 4 times
// a second and sends what changed; a receiver creates the same checkpoints with the same command (CREATE_CHECKPOINT /
// DELETE_CHECKPOINT), so the game draws them, and drops them 3 s after the last packet.
class CMirrorRaceCheckpoints
{
public:
    static void Process();
    static void Handle(Packets::Blips::MissionRaceCheckpoints* packet);
    static void Clear();
};
