#pragma once
#include <cstdint>
class CRunningScript;
namespace Packets::Scripts { class StoryRace; }
class CHighStakesRace {
public:
    static bool Enabled();
    static bool OwnMarkers();
    static bool Active();
    static bool FilterStart(CRunningScript* script, uint16_t opcode, bool state);
    static bool StartRace(CRunningScript* script);
    static bool IsStoryLaunch(CRunningScript* script);
    static void AfterCommand(CRunningScript* script, uint16_t opcode);
    static void Process();
    static void Draw();
    static void HideNativeHud();
    static void OnPacket(const Packets::Scripts::StoryRace& packet);
};
