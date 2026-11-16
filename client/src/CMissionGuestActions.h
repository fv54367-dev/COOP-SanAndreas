#pragma once
#include <cstdint>
class CRunningScript;
namespace Packets::Scripts { class MissionGuestActions; }
class CMissionGuestActions
{
public:
    static void Process();
    static void OnCommandStart(CRunningScript* script, uint16_t opcode);
    static bool FilterCompare(CRunningScript* script, uint16_t opcode, bool state, bool& handled);
    static void Receive(const Packets::Scripts::MissionGuestActions& packet);
};
