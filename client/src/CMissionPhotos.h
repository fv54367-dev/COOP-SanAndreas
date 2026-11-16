#pragma once
#include <cstdint>
class CWeapon;
class CEntity;
class CVector;
class CRunningScript;
namespace Packets::Scripts { class MissionPhoto; }
class CMissionPhotos
{
public:
    static void Process();
    static void OnCommandStart(CRunningScript* script, uint16_t opcode);
    static bool TakePhotograph(CWeapon* weapon, CEntity* owner, CVector* point);
    static void Receive(const Packets::Scripts::MissionPhoto& packet);
};
