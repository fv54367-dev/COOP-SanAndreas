#include "stdafx.h"
#include "CMirrorBlips.h"
#include "CMissionSyncRules.h"
#include "CCommandsForAllNetworkPlayers.h"
#include "COpCodeSync.h"
#include <CCoopMission.h>

template <typename PacketT, typename Fill>
static void SendToParticipants(Fill fill);

template <typename PacketT, typename Fill>
static void SendToEveryNetworkPlayer(CRunningScript* script, Fill fill)
{
    // (the checkpoint commands use it too: only the blip packets are left to the mirror layer)
    if (CMirrorBlips::MutesCoopBlipCommands() && !std::is_same_v<PacketT, Packets::Blips::UpdateCheckpoint> &&
        !std::is_same_v<PacketT, Packets::Blips::RemoveCheckpoint>)
        return;

    // the checkpoints: the mirrored coordinate blip and the native cylinder take their place (CMissionSyncRules)
    if ((std::is_same_v<PacketT, Packets::Blips::UpdateCheckpoint> || std::is_same_v<PacketT, Packets::Blips::RemoveCheckpoint>) &&
        CMissionSyncRules::MutesCheckpointCommands(script))
        return;

    // only the coop owner's missions: not a mission the local player runs alone (not the authority, or a side mission
    // the authority chose to play individually, whose script isn't synced)
    if (!CCoopMission::IsScriptAuthority() || !COpCodeSync::IsScriptSynced(script))
        return;

    SendToParticipants<PacketT>(fill);
}

template <typename PacketT, typename Fill>
static void SendToParticipants(Fill fill)
{
    for (auto pNetworkPlayer : CNetworkPlayerManager::m_pPlayers)
    {
        if (!CCoopMission::ShouldIncludeNetworkPlayer(pNetworkPlayer))
            continue;

        PacketT packet{};
        packet.forWhoPlayerId = pNetworkPlayer->m_playerId;
        fill(packet);
        GetPacketFactory().Send(packet);
    }
}

void CCommandUpdateCheckpointForAllNetworkPlayers::Process(CRunningScript* script)
{
    script->CollectParameters(6);
    CVector position = *(CVector*)&ScriptParams[0];
    CVector radius = *(CVector*)&ScriptParams[3];
    SendToEveryNetworkPlayer<Packets::Blips::UpdateCheckpoint>(script, [&](auto& packet)
    {
        packet.position = position;
        packet.radius = radius;
    });
}

void CCommandRemoveCheckpointForAllNetworkPlayers::Process(CRunningScript* script)
{
    SendToEveryNetworkPlayer<Packets::Blips::RemoveCheckpoint>(script, [](auto&) {});
}

// a ped or car created by the script this frame has no network id until its network copy exists; a blip packet naming it
// reaches the others with an invalid id and is lost, so it waits here (3 s at most)
struct SDeferredBlip
{
    int handle;  // pool handle of the char / car
    bool bPed;
    int friendly, display, color, scale;
    uint32_t startTick;
    playerid_t forWho;  // a *ForNetworkPlayer command; invalid: every participant
};
static std::vector<SDeferredBlip> s_deferredBlips;

static bool HasNetworkId(CEntity* pEntity)
{
    CNetworkEntitySerializer serializer;
    serializer.SetEntity(pEntity);
    if (serializer.entityType == eNetworkEntityType::NETWORK_ENTITY_TYPE_PED)
        return serializer.pedid.valid();
    if (serializer.entityType == eNetworkEntityType::NETWORK_ENTITY_TYPE_VEHICLE)
        return serializer.vehicleid.valid();
    return true;
}

static void SendEntityBlip(playerid_t forWho, CEntity* pEntity, int friendly, int display, int color, int scale)
{
    auto fill = [&](auto& packet)
    {
        packet.entity.SetEntity(pEntity);
        packet.isFriendly = friendly != 0;
        packet.display = static_cast<eBlipDisplay>(display);
        packet.color = static_cast<eBlipColour>(color);
        packet.scale = scale;
    };
    if (!forWho.valid())
    {
        SendToParticipants<Packets::Blips::UpdateEntityBlip>(fill);
        return;
    }

    Packets::Blips::UpdateEntityBlip packet{};
    packet.forWhoPlayerId = forWho;
    fill(packet);
    GetPacketFactory().Send(packet);
}

static int GetEntityRef(CEntity* pEntity)
{
    return pEntity->m_nType == ENTITY_TYPE_PED ? CPools::GetPedRef((CPed*)pEntity) : CPools::GetVehicleRef((CVehicle*)pEntity);
}

void SendOrDeferEntityBlip(playerid_t forWho, CEntity* pEntity, int friendly, int display, int color, int scale)
{
    if (!HasNetworkId(pEntity))
    {
        DropDeferredEntityBlip(forWho, pEntity);
        s_deferredBlips.push_back({GetEntityRef(pEntity), pEntity->m_nType == ENTITY_TYPE_PED, friendly, display, color, scale,
            GetTickCount(), forWho});
        return;
    }

    SendEntityBlip(forWho, pEntity, friendly, display, color, scale);
}

void DropDeferredEntityBlip(playerid_t forWho, CEntity* pEntity)
{
    const int handle = GetEntityRef(pEntity);
    const bool bPed = pEntity->m_nType == ENTITY_TYPE_PED;
    s_deferredBlips.erase(std::remove_if(s_deferredBlips.begin(), s_deferredBlips.end(),
                              [&](const SDeferredBlip& blip)
                              { return blip.handle == handle && blip.bPed == bPed && (!forWho.valid() || blip.forWho == forWho); }),
        s_deferredBlips.end());
}

static void UpdateEntityBlip(CRunningScript* script, CEntity* pEntity, int friendly, int display, int color, int scale)
{
    if (!pEntity)
        return;

    // the mission mirror layer sends the authority's blips as they are (CMirrorBlips): the parameters are collected, nothing is sent
    if (CMirrorBlips::MutesCoopBlipCommands())
        return;

    if (!CCoopMission::IsScriptAuthority() || !COpCodeSync::IsScriptSynced(script))
        return;

    SendOrDeferEntityBlip(playerid_t{}, pEntity, friendly, display, color, scale);
}

void ProcessDeferredEntityBlips()
{
    if (CMirrorBlips::MutesCoopBlipCommands())
    {
        s_deferredBlips.clear();
        return;
    }
    for (auto it = s_deferredBlips.begin(); it != s_deferredBlips.end();)
    {
        CEntity* pEntity = it->bPed ? (CEntity*)CPools::GetPed(it->handle) : (CEntity*)CPools::GetVehicle(it->handle);
        if (pEntity == nullptr || GetTickCount() - it->startTick > 3000)
        {
            it = s_deferredBlips.erase(it);  // gone, or it never got an id
            continue;
        }

        if (HasNetworkId(pEntity))
        {
            if (it->forWho.valid() || CCoopMission::IsScriptAuthority())
                SendEntityBlip(it->forWho, pEntity, it->friendly, it->display, it->color, it->scale);
            it = s_deferredBlips.erase(it);
            continue;
        }

        ++it;
    }
}

static void RemoveEntityBlip(CRunningScript* script, CEntity* pEntity)
{
    if (!pEntity)
        return;

    DropDeferredEntityBlip(playerid_t{}, pEntity);
    SendToEveryNetworkPlayer<Packets::Blips::RemoveEntityBlip>(script, [&](auto& packet) { packet.entity.SetEntity(pEntity); });
}

// char, isFriendly, display, color, scale
void CCommandUpdateCharBlipForAllNetworkPlayers::Process(CRunningScript* script)
{
    script->CollectParameters(5);
    UpdateEntityBlip(script, CPools::GetPed(ScriptParams[0]), ScriptParams[1], ScriptParams[2], ScriptParams[3], ScriptParams[4]);
}

void CCommandRemoveCharBlipForAllNetworkPlayers::Process(CRunningScript* script)
{
    script->CollectParameters(1);
    RemoveEntityBlip(script, CPools::GetPed(ScriptParams[0]));
}

// car, isFriendly, display, color, scale
void CCommandUpdateCarBlipForAllNetworkPlayers::Process(CRunningScript* script)
{
    script->CollectParameters(5);
    UpdateEntityBlip(script, CPools::GetVehicle(ScriptParams[0]), ScriptParams[1], ScriptParams[2], ScriptParams[3], ScriptParams[4]);
}

void CCommandRemoveCarBlipForAllNetworkPlayers::Process(CRunningScript* script)
{
    script->CollectParameters(1);
    RemoveEntityBlip(script, CPools::GetVehicle(ScriptParams[0]));
}

void CCommandClearAllEntityBlipsForAllNetworkPlayers::Process(CRunningScript* script)
{
    SendToEveryNetworkPlayer<Packets::Blips::ClearEntityBlips>(script, [](auto&) {});
}
