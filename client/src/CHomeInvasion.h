#pragma once
#include <cstdint>
class CRunningScript;
class CObject;
class CPedClothesDesc;
namespace Packets::Scripts { class HomeInvasion; }
class CHomeInvasion
{
public:
    static void Process();
    static bool IsCarried(uint32_t id);
    static void BeforeObjectDelete(CObject* object);
    static bool FilterCompare(CRunningScript* script, uint16_t opcode, bool state, bool& handled);
    static void OnCommandStart(CRunningScript* script, uint16_t opcode);
    static void OnCommandExecuted(CRunningScript* script, uint16_t opcode);
    static void Receive(const Packets::Scripts::HomeInvasion& packet);
    static void StripMissionLook(CPedClothesDesc& clothes);
    // GUNS1 under coop-home-invasion: nobody drags the others in or out of the house (null = look for the mission)
    static bool SuppressGather(CRunningScript* script);
};
