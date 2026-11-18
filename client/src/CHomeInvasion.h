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
    // guest: the owner's SET_AREA_VISIBLE is not replayed (each one goes in and out through the door himself)
    static bool SkipRemoteAreaVisible();
    // the owner's skipped SET_AREA_VISIBLE: 0 is the script taking him out of the house (the sleeper woke up)
    static void OnOwnerAreaVisible(int area);
    // guest not in the owner's area (outside while he is in the house): the owner's camera, bars and control stay his
    static bool SkipRemoteCamera(uint16_t opcode);
};
