#include "stdafx.h"
#include "CMissionPhotos.h"
#include "CObjectSync.h"
#include "CServerTime.h"
#include "CCoopMission.h"
#include "CMissionMirror.h"
#include "COpCodeSync.h"
#include "network/packets/mission_photos.h"
#include <map>
#include <tuple>

// Adapted from Karimov-Akbar/sa-coop mission-objects/Objects.cpp (MIT).
// gta-reversed Weapon.cpp::TakePhotograph (0x73C1F0) computes both flags.
namespace
{
using Photo = Packets::Scripts::MissionPhoto;
Photo session;
CRunningScript* active = nullptr;
uint32_t nextEpoch = 0;
DWORD sentAt = 0, receivedAt = 0;
std::map<std::pair<bool, uint64_t>, DWORD> applied;

bool Enabled()
{
    auto local = FindPlayerPed(0);
    return CConfigLoader::ms_bCoopPhotos && CNetwork::m_bAuthenticated && local && local->IsAlive() &&
        !CCoopMission::IsLocalIndividualMission() && !CCoopMission::HasLocalLeft();
}
bool Eligible(CNetworkPlayer* player)
{
    auto local = FindPlayerPed(0);
    return local && player && player->m_pPed && player->m_pPed->IsVTableValid() &&
        !player->m_bDead && !player->m_bIndividualMode && player->m_pPed->m_fHealth > 0.0f &&
        player->m_pPed->m_nAreaCode == local->m_nAreaCode && CCoopMission::ShouldIncludeNetworkPlayer(player);
}
CRunningScript* MissionScript()
{
    for (auto script = CTheScripts::pActiveScripts; script; script = script->m_pNext)
        if (script->m_bIsMission && COpCodeSync::IsScriptSynced(script)) return script;
    return nullptr;
}
bool IsOwner(CNetworkPlayer* player)
{
    return player && (CCoopMission::IsRunning() ? player->m_playerId == CCoopMission::GetOwnerId() : player->m_bIsHost);
}
bool GuestReady()
{
    auto owner = CNetworkPlayerManager::GetPlayer(session.owner);
    return Enabled() && !CCoopMission::IsScriptAuthority() && session.epoch &&
        GetTickCount() - receivedAt < 1500 && IsOwner(owner) && Eligible(owner) &&
        owner->m_nRunningMission == session.mission;
}
void Reset()
{
    session = Photo{}; active = nullptr; applied.clear(); sentAt = 0;
}
}

void CMissionPhotos::OnCommandStart(CRunningScript* script, uint16_t opcode)
{
    // GTA reuses script slots on retry; SCRIPT_NAME starts a fresh attempt.
    if (script && script->m_bIsMission && opcode == 0x03A4) Reset();
}

void CMissionPhotos::Process()
{
    if (!Enabled()) { Reset(); return; }
    if (!CCoopMission::IsScriptAuthority())
    {
        if (!GuestReady()) Reset();
        return;
    }
    auto script = MissionScript();
    const int mission = CMissionMirror::GetRunningMissionId();
    if (!script || mission < 0) { Reset(); return; }
    if (active != script || session.mission != mission || !session.epoch)
    {
        Reset(); active = script;
        session.owner = CNetworkPlayerManager::m_localPlayerId;
        session.mission = mission;
        session.epoch = ++nextEpoch;
        if (!session.epoch) session.epoch = ++nextEpoch;
        session.request = true;
    }
    if (GetTickCount() - sentAt >= 500)
    {
        sentAt = GetTickCount(); GetPacketFactory().Send(session);
    }
}

bool CMissionPhotos::TakePhotograph(CWeapon* weapon, CEntity* owner, CVector* point)
{
    if (!GuestReady()) return weapon->TakePhotograph(owner, point);
    // Save and clear only replicated mission targets so an old photo cannot be reported as this shot.
    // No pointers survive the synchronous native call.
    std::vector<std::tuple<pedid_t, CPed*, bool>> peds;
    for (auto net : CNetworkPedManager::m_pPeds)
        if (net && !net->m_bSyncing && net->m_nCreatedBy == MISSION_CHAR &&
            net->m_pedId.creator() == session.owner && net->m_pPed &&
            CPools::ms_pPedPool->IsObjectValid(net->m_pPed) && net->m_pPed->IsVTableValid())
        {
            auto ped = net->m_pPed;
            if (ped->m_nAreaCode != FindPlayerPed(0)->m_nAreaCode) continue;
            peds.emplace_back(net->m_pedId, ped, bool(ped->m_nPedFlags.bHasBeenPhotographed));
            ped->m_nPedFlags.bHasBeenPhotographed = false;
        }
    std::vector<std::tuple<uint32_t, CObject*, bool>> objects;
    for (auto [id, object] : CObjectSync::GetPhotoTargets(false))
    {
        if (object->m_nAreaCode != FindPlayerPed(0)->m_nAreaCode) continue;
        objects.emplace_back(id, object, bool(object->m_nObjectFlags.bIsPhotographed));
        object->m_nObjectFlags.bIsPhotographed = false;
    }
    const bool taken = weapon->TakePhotograph(owner, point);
    Photo photo = session; photo.request = false; photo.shotAt = g_serverTime;
    int sent = 0;
    for (auto [id, ped, previous] : peds)
    {
        auto net = CNetworkPedManager::GetPed(id);
        if (!net || net->m_pPed != ped || !CPools::ms_pPedPool || !CPools::ms_pPedPool->IsObjectValid(ped) || !ped->IsVTableValid()) continue;
        if (taken && ped->m_nPedFlags.bHasBeenPhotographed)
        {
            photo.object = false; photo.ped = id; photo.entity = 0;
            GetPacketFactory().Send(photo); ++sent;
        }
        ped->m_nPedFlags.bHasBeenPhotographed |= previous;
    }
    for (auto [id, object, previous] : objects)
    {
        if (!CPools::ms_pObjectPool || !CPools::ms_pObjectPool->IsObjectValid(object) || !object->IsVTableValid()) continue;
        if (taken && object->m_nObjectFlags.bIsPhotographed)
        {
            photo.object = true; photo.entity = id; photo.ped = pedid_t{};
            GetPacketFactory().Send(photo); ++sent;
        }
        object->m_nObjectFlags.bIsPhotographed |= previous;
    }
    if (taken) CMissionMirror::LogEventf("coop-photos shot mission=%d epoch=%u targets=%d", session.mission, session.epoch, sent);
    return taken;
}

void CMissionPhotos::Receive(const Photo& photo)
{
    if (!Enabled() || !photo.epoch) return;
    auto sender = CNetworkPlayerManager::GetPlayer(photo.senderPlayerid);
    if (!Eligible(sender)) return;
    if (photo.request)
    {
        if (CCoopMission::IsScriptAuthority() || !IsOwner(sender) || photo.owner != sender->m_playerId ||
            sender->m_nRunningMission != photo.mission) return;
        session = photo; receivedAt = GetTickCount();
        return;
    }
    if (!CCoopMission::IsScriptAuthority() || !active || MissionScript() != active ||
        photo.owner != CNetworkPlayerManager::m_localPlayerId || photo.epoch != session.epoch ||
        photo.mission != session.mission || CMissionMirror::GetRunningMissionId() != session.mission) return;
    const uint64_t id = photo.object ? photo.entity : photo.ped.packed();
    const auto key = std::make_pair(photo.object, id);
    if (!id) return;
    // Capture time, not arrival time: network delay must not turn simultaneous photos into retries.
    const int32_t age = static_cast<int32_t>(g_serverTime - photo.shotAt);
    if (age < -1500 || age > 5000) return;
    // The script consumes flags, including bad/premature photos: later shots must be allowed.
    // Coalesce simultaneous shots from different partners without rearming a consumed flag.
    if (auto it = applied.find(key); it != applied.end() && static_cast<int32_t>(photo.shotAt - it->second) < 500) return;
    if (photo.object)
    {
        for (auto [objectId, object] : CObjectSync::GetPhotoTargets(true))
            if (objectId == photo.entity && object->m_nAreaCode == sender->m_pPed->m_nAreaCode)
            {
                object->m_nObjectFlags.bIsPhotographed = true;
                applied[key] = photo.shotAt;
                CMissionMirror::LogEventf("coop-photos applied object=%u epoch=%u sender=%u", photo.entity, photo.epoch, sender->m_playerId.value());
                break;
            }
    }
    else
    {
        auto net = CNetworkPedManager::GetPed(photo.ped);
        if (!net || !net->m_bSyncing || net->m_nCreatedBy != MISSION_CHAR ||
            photo.ped.creator() != session.owner || !net->m_pPed ||
            !CPools::ms_pPedPool->IsObjectValid(net->m_pPed) || !net->m_pPed->IsVTableValid() ||
            net->m_pPed->m_nAreaCode != sender->m_pPed->m_nAreaCode) return;
        net->m_pPed->m_nPedFlags.bHasBeenPhotographed = true;
        applied[key] = photo.shotAt;
        CMissionMirror::LogEventf("coop-photos applied ped=%s epoch=%u sender=%u", to_string(photo.ped).c_str(), photo.epoch, sender->m_playerId.value());
    }
}
