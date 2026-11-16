#pragma once
#include <cstdint>
class CRunningScript;
namespace Packets::Scripts { class CoopLowrider; }
class CCoopLowrider
{
public:
    static bool Enabled();
    static void Reset(bool gameRestarted);
    static bool OwnContest();
    static void Process();
    static void DrawDanceTeam();
    static void AfterScripts();
    static bool FilterDanceResult(CRunningScript* script, uint16_t opcode, bool state);
    static void AfterCommand(CRunningScript* script, uint16_t opcode);
    static void OnPacket(const Packets::Scripts::CoopLowrider& packet);
};
