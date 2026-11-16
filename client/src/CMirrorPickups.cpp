#include "stdafx.h"
#include "CMirrorPickups.h"
#include "CMissionMirror.h"
#include "CCoopMission.h"
#include "CMissionMode.h"
#include "COpCodeSync.h"
#include "network/packets/mirror_pickups.h"
#include "network/packets/scripts.h"
#include <CPickups.h>
#include <vector>

// The mission mirror layer's pickups, see CMirrorPickups.h.

namespace
{
constexpr uint16_t OP_CREATE_PICKUP = 0x0213;
constexpr uint16_t OP_CREATE_PICKUP_WITH_AMMO = 0x032B;
constexpr uint16_t OP_REMOVE_PICKUP = 0x0215;

constexpr size_t MAX_PICKUPS = 64;

// the authority's pickups the others have a copy of
std::vector<uint32_t> ms_owned;

struct SPickupCopy
{
    uint32_t hostHandle = 0;
    int localHandle = -1;
    playerid_t senderId{};
};
std::vector<SPickupCopy> ms_copies;

// the authority's weapon pickups (mission-pickup-weapon-share): whoever collects one arms the others too
struct SOwnedWeapon
{
    uint32_t hostHandle = 0;
    eWeaponType type = WEAPON_UNARMED;
    int ammo = 0;
    bool guestCollected = false;
    bool removedByScript = false;  // REMOVE_PICKUP: nobody collected it
    CVector pos{};
};
std::vector<SOwnedWeapon> ms_ownedWeapons;

// gta-reversed Pickups.h AmmoForWeapon_OnStreet: what CPickup::Update gives for a pickup without its own ammo
constexpr uint16_t AMMO_ON_STREET[47]{0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 8, 8, 8, 8, 4, 4, 30, 10, 10, 15, 10, 10,
    60, 60, 80, 80, 60, 20, 10, 4, 3, 100, 500, 5, 1, 500, 500, 36, 0, 0, 1};

std::vector<SOwnedWeapon>::iterator FindOwnedWeapon(uint32_t hostHandle)
{
    return std::find_if(ms_ownedWeapons.begin(), ms_ownedWeapons.end(),
        [hostHandle](const SOwnedWeapon& weapon) { return weapon.hostHandle == hostHandle; });
}

// the collected ring HAS_PICKUP_BEEN_COLLECTED reads; IsPickUpPickedUp would consume the script's entry
bool IsInCollectedRing(uint32_t hostHandle)
{
    for (int i = 0; i < 20; i++)
    {
        if (static_cast<uint32_t>(CPickups::aPickUpsCollected[i]) == hostHandle)
            return true;
    }
    return false;
}

// the authority: a guest took the weapon, the script may wait for the host to own it (CRASH1 HAS_CHAR_GOT_WEAPON)
void GiveLocalWeapon(eWeaponType type, int ammo)
{
    CPlayerPed* player = FindPlayerPed(0);
    const auto info = CWeaponInfo::GetWeaponInfo(type, 1);
    if (!player || !info || info->m_nSlot < 0 || info->m_nSlot >= 13 || player->m_aWeapons[info->m_nSlot].m_eWeaponType == type)
        return;

    for (int model : {info->m_nModelId1, info->m_nModelId2})
    {
        if (model > 0 && CStreaming::ms_aInfoForModel[model].m_nLoadState != LOADSTATE_LOADED)
        {
            CStreaming::RequestModel(model, eStreamingFlags::GAME_REQUIRED);
            CStreaming::LoadAllRequestedModels(false);
        }
    }
    player->GiveWeapon(type, ammo, false);
    CMissionMirror::LogEventf("pickups weapon-share to=host weapon=%d ammo=%d", static_cast<int>(type), ammo);
}

// the authority collected it: the mission players get the same weapon (MISSION_PLAYER_EQUIPMENT, as a script GIVE_WEAPON)
void ShareWeaponWithGuests(const SOwnedWeapon& weapon, playerid_t except = playerid_t{})
{
    for (auto networkPlayer : CNetworkPlayerManager::m_pPlayers)
    {
        if (!CCoopMission::ShouldIncludeNetworkPlayer(networkPlayer) || networkPlayer->m_playerId == except)
            continue;
        Packets::Scripts::MissionPlayerEquipment packet{};
        packet.playerid = networkPlayer->m_playerId;
        packet.weaponType = static_cast<uint8_t>(weapon.type);
        packet.ammo = weapon.ammo;
        GetPacketFactory().Send(packet);
        CMissionMirror::LogEventf("pickups weapon-share to=%u weapon=%d ammo=%d", packet.playerid.value(),
            static_cast<int>(weapon.type), weapon.ammo);
    }
}

// collecting a pickup only sets its type to PICKUP_NONE, the handle stays valid until the slot is reused
bool IsPickupAlive(int handle)
{
    int index = CPickups::GetActualPickupIndex(handle);
    return index != -1 && CPickups::aPickUps[index].m_nPickupType != PICKUP_NONE;
}

bool IsOneShotType(uint8_t type)
{
    return type == PICKUP_ONCE || type == PICKUP_ONCE_TIMEOUT || type == PICKUP_ONCE_TIMEOUT_SLOW ||
           type == PICKUP_ONCE_FOR_MISSION;
}

// a receiver that plays alone (F1) or dropped out of the mission takes none of the authority's pickups
bool IsReceiverOut()
{
    return CMissionMode::IsIndividual() || CCoopMission::HasLocalLeft();
}

void Send(uint8_t op, uint32_t hostHandle)
{
    Packets::Mirror::MirrorPickup packet{};
    packet.op = op;
    packet.hostHandle = hostHandle;
    GetPacketFactory().Send(packet);
}

void DropCopies(const char* szReason)
{
    if (ms_copies.empty())
        return;

    // forget them first: Process must not take their removal for a collection
    std::vector<SPickupCopy> copies;
    copies.swap(ms_copies);
    for (const SPickupCopy& copy : copies)
    {
        if (IsPickupAlive(copy.localHandle))
            CPickups::RemovePickUp(copy.localHandle);
    }
    CMissionMirror::LogEventf("pickups drop n=%u reason=%s", static_cast<unsigned>(copies.size()), szReason);
}

// the authority: a guest collected his copy; the script must see its own pickup collected
void MarkOwnedCollected(uint32_t hostHandle, playerid_t collectorId)
{
    if (std::find(ms_owned.begin(), ms_owned.end(), hostHandle) == ms_owned.end())
        return;

    int index = CPickups::GetActualPickupIndex(static_cast<int>(hostHandle));
    if (index == -1 || CPickups::aPickUps[index].m_nPickupType == PICKUP_NONE)
        return;  // the authority (or someone else) got it first

    CPickup& pickup = CPickups::aPickUps[index];
    const uint8_t type = pickup.m_nPickupType;

    // gta-reversed Pickups.cpp: CPickup::Update collects a one-shot pickup with AddToCollectedPickupsArray (0x455240,
    // the ring HAS_PICKUP_BEEN_COLLECTED reads) and then removes it
    CPickups::AddToCollectedPickupsArray(index);
    if (IsOneShotType(type))
        CPickups::RemovePickUp(static_cast<int>(hostHandle));

    auto weapon = FindOwnedWeapon(hostHandle);
    if (weapon != ms_ownedWeapons.end())
    {
        weapon->guestCollected = true;
        if (CConfigLoader::ms_bMissionPickupWeaponShare)
        {
            GiveLocalWeapon(weapon->type, weapon->ammo);
            ShareWeaponWithGuests(*weapon, collectorId);
        }
    }

    CMissionMirror::LogEventf("pickups collected-by-guest handle=%08X type=%d player=%d", hostHandle, type,
        static_cast<int>(collectorId.value()));
}
}  // namespace

bool CMirrorPickups::IsEnabled()
{
    return CConfigLoader::ms_bMirrorPickups;
}

void CMirrorPickups::OnCommandExecuted(CRunningScript* pScript, uint16_t opcode)
{
    opcode &= 0x7FFF;
    if (opcode == OP_REMOVE_PICKUP)
    {
        auto weapon = FindOwnedWeapon(static_cast<uint32_t>(ScriptParams[0]));
        if (weapon != ms_ownedWeapons.end())
            weapon->removedByScript = true;
        return;
    }
    if (opcode != OP_CREATE_PICKUP && opcode != OP_CREATE_PICKUP_WITH_AMMO)
        return;

    if (!IsEnabled() || !CNetwork::m_bAuthenticated || COpCodeSync::bProcessingNetworkOpcode ||
        CMissionMode::IsIndividual() || !CMissionMirror::IsFamilyScript(pScript))
    {
        return;
    }

    const uint32_t handle = static_cast<uint32_t>(ScriptParams[0]);
    int index = CPickups::GetActualPickupIndex(static_cast<int>(handle));
    if (index == -1 || CPickups::aPickUps[index].m_nPickupType == PICKUP_NONE)
    {
        CMissionMirror::LogEventf("pickups own-failed handle=%08X opcode=%04X script=%.8s", handle, opcode,
            pScript->m_szName);
        return;
    }

    if (std::find(ms_owned.begin(), ms_owned.end(), handle) != ms_owned.end())
        return;

    if (ms_owned.size() >= MAX_PICKUPS)
        ms_owned.erase(ms_owned.begin());
    ms_owned.push_back(handle);

    CPickup& pickup = CPickups::aPickUps[index];
    CVector pos = pickup.GetPosn();

    Packets::Mirror::MirrorPickup packet{};
    packet.op = MirrorPickupsWire::OP_CREATE;
    packet.hostHandle = handle;
    packet.modelId = static_cast<uint16_t>(pickup.m_nModelIndex);
    packet.pickupType = pickup.m_nPickupType;
    packet.ammo = static_cast<int32_t>(std::min(pickup.m_nAmmo, 99999u));
    packet.x = pos.x;
    packet.y = pos.y;
    packet.z = pos.z;
    GetPacketFactory().Send(packet);

    CMissionMirror::LogEventf("pickups own handle=%08X model=%d type=%d script=%.8s", handle, packet.modelId,
        packet.pickupType, pScript->m_szName);

    const int weaponType = CPickups::WeaponForModel(pickup.m_nModelIndex);
    if (weaponType > 0 && weaponType < 47 && (weaponType < 19 || weaponType > 21))
    {
        if (ms_ownedWeapons.size() >= MAX_PICKUPS)
            ms_ownedWeapons.erase(ms_ownedWeapons.begin());
        const int ammo = pickup.m_nAmmo ? static_cast<int>(std::min(pickup.m_nAmmo, 99999u)) : AMMO_ON_STREET[weaponType];
        ms_ownedWeapons.push_back({handle, static_cast<eWeaponType>(weaponType), ammo, false, false, pos});
    }
}

void CMirrorPickups::Process()
{
    if (!IsEnabled())
        return;

    if (!CNetwork::m_bAuthenticated)
    {
        ms_owned.clear();
        ms_ownedWeapons.clear();
        DropCopies("disconnected");
        return;
    }

    if (CCoopMission::IsScriptAuthority())
    {
        DropCopies("this-game-is-authority");

        // collected here, removed by the script or by the mission cleanup: gone for the others too
        for (auto it = ms_owned.begin(); it != ms_owned.end();)
        {
            if (IsPickupAlive(static_cast<int>(*it)))
            {
                ++it;
                continue;
            }

            Send(MirrorPickupsWire::OP_REMOVE, *it);
            auto weapon = FindOwnedWeapon(*it);
            if (weapon != ms_ownedWeapons.end())
            {
                // collected here by the authority himself (not by a guest, not removed by the script)
                // the script may already have consumed its ring entry (HAS_PICKUP_BEEN_COLLECTED): the host owning the
                // weapon of a pickup the script did not remove says the same
                CPlayerPed* host = FindPlayerPed(0);
                const auto info = CWeaponInfo::GetWeaponInfo(weapon->type, 1);
                // and standing on it: a mission-end sweep of a pickup the host walked past must not arm everyone
                const bool hostHasIt = host && info && host->m_aWeapons[info->m_nSlot].m_eWeaponType == weapon->type &&
                    (CVector2D(host->GetPosition()) - CVector2D(weapon->pos)).MagnitudeSqr() < 16.0f;
                if (CConfigLoader::ms_bMissionPickupWeaponShare && !weapon->guestCollected &&
                    (IsInCollectedRing(*it) || (hostHasIt && !weapon->removedByScript)))
                    ShareWeaponWithGuests(*weapon);
                ms_ownedWeapons.erase(weapon);
            }
            it = ms_owned.erase(it);
        }
        return;
    }

    // no longer the authority: the next one owns the pickups of its own game
    ms_owned.clear();
    ms_ownedWeapons.clear();

    if (IsReceiverOut())
    {
        DropCopies("receiver-out");
        return;
    }

    for (auto it = ms_copies.begin(); it != ms_copies.end();)
    {
        if (CNetworkPlayerManager::GetPlayer(it->senderId) == nullptr)
        {
            // its authority left: nobody would remove it anymore
            if (IsPickupAlive(it->localHandle))
                CPickups::RemovePickUp(it->localHandle);
            it = ms_copies.erase(it);
            continue;
        }

        if (IsPickupAlive(it->localHandle))
        {
            ++it;
            continue;
        }

        // the local player collected the copy (the game already gave the reward here)
        Send(MirrorPickupsWire::OP_COLLECTED, it->hostHandle);
        CMissionMirror::LogEventf("pickups collected handle=%08X", it->hostHandle);
        it = ms_copies.erase(it);
    }
}

void CMirrorPickups::HandlePacket(const Packets::Mirror::MirrorPickup& packet)
{
    if (!IsEnabled())
        return;

    const playerid_t sender = packet.playerid.value;
    if (sender == CNetworkPlayerManager::m_localPlayerId)
        return;

    if (packet.op == MirrorPickupsWire::OP_COLLECTED)
    {
        if (CCoopMission::IsScriptAuthority())
            MarkOwnedCollected(packet.hostHandle, sender);
        return;
    }

    if (packet.op == MirrorPickupsWire::OP_REMOVE)
    {
        for (auto it = ms_copies.begin(); it != ms_copies.end(); ++it)
        {
            if (it->hostHandle != packet.hostHandle || it->senderId != sender)
                continue;

            int localHandle = it->localHandle;
            ms_copies.erase(it);
            if (IsPickupAlive(localHandle))
                CPickups::RemovePickUp(localHandle);
            return;
        }
        return;
    }

    // OP_CREATE
    if (CCoopMission::IsScriptAuthority() || IsReceiverOut())
        return;

    for (const SPickupCopy& copy : ms_copies)
    {
        if (copy.hostHandle == packet.hostHandle && copy.senderId == sender)
            return;
    }

    int modelId = packet.modelId;
    if (modelId <= 0 || modelId >= 20000 || !CModelInfo::ms_modelInfoPtrs[modelId])
        return;

    // without its model loaded the pickup has no visible object
    if (CStreaming::ms_aInfoForModel[modelId].m_nLoadState != LOADSTATE_LOADED)
    {
        CStreaming::RequestModel(modelId, eStreamingFlags::GAME_REQUIRED);
        CStreaming::LoadAllRequestedModels(false);
    }

    if (ms_copies.size() >= MAX_PICKUPS)
    {
        if (IsPickupAlive(ms_copies.front().localHandle))
            CPickups::RemovePickUp(ms_copies.front().localHandle);
        ms_copies.erase(ms_copies.begin());
    }

    int handle = CPickups::GenerateNewOne(CVector(packet.x, packet.y, packet.z), modelId, packet.pickupType,
        static_cast<unsigned int>(packet.ammo), 0, false, nullptr);
    if (handle == -1)
    {
        CMissionMirror::LogEventf("pickups create-failed handle=%08X model=%d", packet.hostHandle, modelId);
        return;
    }

    ms_copies.push_back({packet.hostHandle, handle, sender});
    CMissionMirror::LogEventf("pickups copy handle=%08X model=%d type=%d", packet.hostHandle, modelId, packet.pickupType);
}
