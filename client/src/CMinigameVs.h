#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "network/ids.h"

namespace Packets::Scripts
{
class MinigameVsScore;
}

class CMinigameVs
{
public:
    // called from Coop.MinigameVsReport (0x1D27)
    static void Report(int gameId, int score, int state);

    // a score update from another player, forwarded by the server
    static void OnPacket(const Packets::Scripts::MinigameVsScore& packet);

    // expiring entries and the end-of-match check
    static void Process();

    // the small score panel, drawn inside the ImGui frame
    static void Draw();
    static bool ShouldDraw();
};
