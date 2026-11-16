#pragma once
#include <cstdint>
class CRunningScript;
namespace Packets::Scripts { class MissionGuestCounts; }
class CMissionGuestCounts
{
public:
    static void Process();
    static void OnCommandStart(CRunningScript* script, uint16_t opcode);
    static void Receive(const Packets::Scripts::MissionGuestCounts& packet);
    // Raw result, before NOT and the script's AND/OR accumulator.
    static bool FilterCompare(CRunningScript* script, uint16_t opcode, bool state, bool& handled);
};
