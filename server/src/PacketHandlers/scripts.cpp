#include "network/packets/scripts.h"
#include "network/packets/players.h"
#include "network/packet_types.h"
#include "stdafx.h"
#include "CServerTime.h"
#include <map>
#include <cmath>
#include "CCoopMission.h"
#include "CSaveManager.h"

PACKET_HANDLER(ePacketType::MISSION_PLAYER_EQUIPMENT, Packets::Scripts::MissionPlayerEquipment* packet,
    CNetworkPlayer* pNetworkPlayer)
{
    if (!CCoopMission::IsScriptAuthority(pNetworkPlayer))
        return;
    auto target = CNetworkPlayerManager::GetPlayer(packet->playerid);
    if (target && target != pNetworkPlayer && CCoopMission::CanReceiveMissionPacket(target))
        GetPacketFactory().Send(*packet, target);
}

PACKET_HANDLER(ePacketType::MISSION_SCRIPTED_EXIT, Packets::Scripts::MissionScriptedExit* packet,
    CNetworkPlayer* pNetworkPlayer)
{
    if (!CCoopMission::IsScriptAuthority(pNetworkPlayer) || !pNetworkPlayer->m_bOnMission)
        return;
    auto target = CNetworkPlayerManager::GetPlayer(packet->playerid);
    if (target && target != pNetworkPlayer && CCoopMission::CanReceiveMissionPacket(target))
        GetPacketFactory().Send(*packet, target);
}

PACKET_HANDLER(ePacketType::ON_MISSION_FLAG_SYNC, Packets::Scripts::OnMissionFlagSync* pOnMissionFlagSync,
    CNetworkPlayer* pNetworkPlayer)
{
    // F: the owner of a coop side mission is the script authority while it runs, otherwise it is the host
    if (CCoopMission::IsScriptAuthority(pNetworkPlayer))
    {
        // E: remember whether the host is in a coop mission; his leaving then aborts it, and his finishing it
        // triggers a save of everyone
        if (pNetworkPlayer->m_bOnMission != pOnMissionFlagSync->bOnMission)
        {
            pNetworkPlayer->m_bOnMission = pOnMissionFlagSync->bOnMission;

            if (!pOnMissionFlagSync->bOnMission)
            {
                // E: the end of the host's mission triggers a save of everyone (a coop side mission owned by another
                // player does not: money and progress stay per player)
                if (pNetworkPlayer->m_bIsHost)
                {
                    CSaveManager::RequestSaveAll(nullptr);
                }
            }
        }

        // F: with a coop mission running only its participants get the mission state
        CCoopMission::Relay(*pOnMissionFlagSync, pNetworkPlayer, pNetworkPlayer);

        if (!pOnMissionFlagSync->bOnMission)
            CCoopMission::ClearLatePlayers();
    }
}

PACKET_HANDLER(ePacketType::SAVE_ALL_REQUEST, Packets::Players::SaveAllRequest* pSaveAllRequest,
    CNetworkPlayer* pNetworkPlayer)
{
    CSaveManager::RequestSaveAll(pNetworkPlayer);
}

PACKET_HANDLER(ePacketType::LAUNCH_MISSION_LOCAL, Packets::Scripts::LaunchMissionLocal* pLaunchMissionLocal,
    CNetworkPlayer* pNetworkPlayer)
{
    // story missions launched on every client stay with the host (a coop side mission does not own them). With the mission
    // lock an individual mission is played only by who started it, so it is not launched on the others
    if (pNetworkPlayer->m_bIsHost && !CConfigManager::GetConfigMissionLock())
    {
        GetPacketFactory().SendToAll(*pLaunchMissionLocal, pNetworkPlayer);
    }
}

PACKET_HANDLER(ePacketType::MINIGAME_VS_SCORE, Packets::Scripts::MinigameVsScore* pMinigameVsScore,
    CNetworkPlayer* pNetworkPlayer)
{
    pMinigameVsScore->playerid = pNetworkPlayer->m_playerId;
    GetPacketFactory().SendToAll(*pMinigameVsScore, pNetworkPlayer);
}

PACKET_HANDLER(ePacketType::RACE_VS_LOBBY, Packets::Scripts::RaceVsLobby* pRaceVsLobby, CNetworkPlayer* pNetworkPlayer)
{
    pRaceVsLobby->playerid = pNetworkPlayer->m_playerId;
    GetPacketFactory().SendToAll(*pRaceVsLobby, pNetworkPlayer);
}

PACKET_HANDLER(ePacketType::ENEX_SYNC, Packets::Scripts::EnExSync* pEnExSync, CNetworkPlayer* pNetworkPlayer)
{
    if (pNetworkPlayer->m_bIsHost)
    {
        Packets::Scripts::g_lastEnExData = *pEnExSync;
        Packets::Scripts::g_lastEnExData.serverTime = 0;  // force recalculation

        Packets::Scripts::g_pLastEnExPlayerOwner = pNetworkPlayer;

        GetPacketFactory().SendToAll(*pEnExSync, pNetworkPlayer);
    }
}

PACKET_HANDLER(
    ePacketType::ADD_MESSAGE_GXT, Packets::Scripts::AddMessageGXT* pAddMessageGXT, CNetworkPlayer* pNetworkPlayer)
{
    if (CCoopMission::IsScriptAuthority(pNetworkPlayer))
    {
        if (auto pTargetPlayer = CNetworkPlayerManager::GetPlayer(pAddMessageGXT->forWhoPlayerId))
        {
            // F: a non-participant gets nothing from a coop mission
            if (CCoopMission::CanReceiveMissionPacket(pTargetPlayer))
            {
                GetPacketFactory().Send(*pAddMessageGXT, pTargetPlayer);
            }
        }
    }
}

PACKET_HANDLER(ePacketType::REMOVE_MESSAGE_GXT, Packets::Scripts::RemoveMessageGXT* pRemoveMessageGXT,
    CNetworkPlayer* pNetworkPlayer)
{
    if (CCoopMission::IsScriptAuthority(pNetworkPlayer))
    {
        if (auto pTargetPlayer = CNetworkPlayerManager::GetPlayer(pRemoveMessageGXT->forWhoPlayerId))
        {
            if (CCoopMission::CanReceiveMissionPacket(pTargetPlayer))
            {
                GetPacketFactory().Send(*pRemoveMessageGXT, pTargetPlayer);
            }
        }
    }
}

#if 0  // controlled with SCM
PACKET_HANDLER(ePacketType::START_CUTSCENE, Packets::Scripts::StartCutscene* pStartCutscene, CNetworkPlayer* pNetworkPlayer)
{
    if (pNetworkPlayer->m_bIsHost)
    {
        GetPacketFactory().SendToAll(*pStartCutscene, pNetworkPlayer);
    }
}

#endif

PACKET_HANDLER(ePacketType::SKIP_CUTSCENE, Packets::Scripts::SkipCutscene* pSkipCutscene, CNetworkPlayer* pNetworkPlayer)
{
    // only the script authority decides when the scene is skipped, the rest are votes
    if (pSkipCutscene->skipNow && !CCoopMission::IsScriptAuthority(pNetworkPlayer))
        return;

    pSkipCutscene->senderPlayerid = pNetworkPlayer->m_playerId;
    CCoopMission::Relay(*pSkipCutscene, pNetworkPlayer, pNetworkPlayer);
}

PACKET_HANDLER(ePacketType::PLAY_MISSION_AUDIO, Packets::Scripts::PlayMissionAudio* pPlayMissionAudio,
    CNetworkPlayer* pNetworkPlayer)
{
    if (CCoopMission::IsScriptAuthority(pNetworkPlayer))
    {
        CCoopMission::Relay(*pPlayMissionAudio, pNetworkPlayer, pNetworkPlayer);
    }
}

PACKET_HANDLER(ePacketType::TELEPORT_PLAYER_SCRIPTED, Packets::Scripts::TeleportPlayerScripted* pTeleportPlayerScripted,
    CNetworkPlayer* pNetworkPlayer)
{
    if (CCoopMission::IsScriptAuthority(pNetworkPlayer))
    {
        if (auto pTargetPlayer = CNetworkPlayerManager::GetPlayer(pTeleportPlayerScripted->playerid))
        {
            if (CCoopMission::CanReceiveMissionPacket(pTargetPlayer))
            {
                GetPacketFactory().Send(*pTeleportPlayerScripted, pTargetPlayer);
            }
        }
    }
}

PACKET_HANDLER(ePacketType::PUT_IN_VEHICLE_SCRIPTED, Packets::Scripts::PutInVehicleScripted* pPutInVehicleScripted,
    CNetworkPlayer* pNetworkPlayer)
{
    if (CCoopMission::IsScriptAuthority(pNetworkPlayer))
    {
        if (auto pTargetPlayer = CNetworkPlayerManager::GetPlayer(pPutInVehicleScripted->playerid))
        {
            if (CCoopMission::CanReceiveMissionPacket(pTargetPlayer))
            {
                GetPacketFactory().Send(*pPutInVehicleScripted, pTargetPlayer);
            }
        }
    }
}

PACKET_HANDLER(ePacketType::ODDJOB_REWARD, Packets::Scripts::OddjobReward* pOddjobReward,
    CNetworkPlayer* pNetworkPlayer)
{
    if (CCoopMission::IsScriptAuthority(pNetworkPlayer))
    {
        if (auto pTargetPlayer = CNetworkPlayerManager::GetPlayer(pOddjobReward->playerid))
        {
            if (CCoopMission::CanReceiveMissionPacket(pTargetPlayer))
            {
                GetPacketFactory().Send(*pOddjobReward, pTargetPlayer);
            }
        }
    }
}

PACKET_HANDLER(ePacketType::ONSCREEN_TIMER_SYNC, Packets::Scripts::OnscreenTimerSync* pOnscreenTimerSync,
    CNetworkPlayer* pNetworkPlayer)
{
    if (CCoopMission::IsScriptAuthority(pNetworkPlayer))
    {
        CCoopMission::Relay(*pOnscreenTimerSync, pNetworkPlayer, pNetworkPlayer);
    }
}

PACKET_HANDLER(ePacketType::CAMERA_MIRROR, Packets::Scripts::CameraMirror* pCameraMirror,
    CNetworkPlayer* pNetworkPlayer)
{
    // only the script authority's camera defines the scene, clients just receive it
    if (CCoopMission::IsScriptAuthority(pNetworkPlayer))
    {
        CCoopMission::Relay(*pCameraMirror, pNetworkPlayer, pNetworkPlayer);
    }
}

PACKET_HANDLER(ePacketType::OPCODE_SYNC, Packets::Scripts::OpCodeSync* pOpCodeSync, CNetworkPlayer* pNetworkPlayer)
{
    // F: the owner of a coop mission syncs only to its participants; the host's story missions go to everyone
    CCoopMission::Relay(*pOpCodeSync, pNetworkPlayer, pNetworkPlayer);
}

PACKET_HANDLER(ePacketType::PERFORM_TASK_SEQUENCE, Packets::Scripts::PerformTaskSequence* pPerformTaskSequence, CNetworkPlayer* pNetworkPlayer)
{
    ids::WritePlayerId(&pPerformTaskSequence->buffer[0], pNetworkPlayer->m_playerId);
    GetPacketFactory().SendToAll(*pPerformTaskSequence, pNetworkPlayer);
}

namespace {
struct LowriderGuest { int32_t total = 0, beat = -1; bool done = false; };
struct LowriderSession {
    uint32_t id = 0, tick = 0, began = 0;
    uint8_t kind = 0;
    bool playing = false, stopped = false;
    std::map<uint32_t, LowriderGuest> guests;
};
std::map<uint32_t, LowriderSession> lowriderSessions;
bool LowriderEligible(CNetworkPlayer* owner, CNetworkPlayer* guest) {
    return owner && guest && owner != guest && !guest->m_bIndividualMode && guest->m_bVanillaScripts && guest->m_bSocialAlive &&
        guest->m_nAreaCode == owner->m_nAreaCode && CCoopMission::CanReceiveMissionPacket(guest);
}
}
PACKET_HANDLER(ePacketType::COOP_LOWRIDER, Packets::Scripts::CoopLowrider* packet, CNetworkPlayer* sender)
{
    using P = Packets::Scripts::CoopLowrider;
    for (auto it = lowriderSessions.begin(); it != lowriderSessions.end();)
        if (!CNetworkPlayerManager::GetPlayer(playerid_t(it->first)) || uint32_t(g_serverTime - it->second.tick) > 10000)
            it = lowriderSessions.erase(it);
        else ++it;
    auto owner = CNetworkPlayerManager::GetPlayer(packet->owner);
    if (!owner || !packet->session || !owner->m_bOnMission || !owner->m_bVanillaScripts ||
        !CCoopMission::IsScriptAuthority(owner) || !CCoopMission::CanReceiveMissionPacket(sender)) return;
    packet->sender = sender->m_playerId;
    if (packet->kind == 3) {
        if (sender != owner || packet->action != P::PRELOAD || packet->beat > 7 ||
            !std::isfinite(packet->pos.x) || !std::isfinite(packet->pos.y) ||
            !std::isfinite(packet->pos.z) || !std::isfinite(packet->heading)) return;
        CCoopMission::Relay(*packet, owner, owner);
        return;
    }
    auto it = lowriderSessions.find(packet->owner.value());
    if (packet->action == P::PRELOAD) {
        if (sender != owner || !std::isfinite(packet->pos.x) || !std::isfinite(packet->pos.y) ||
            !std::isfinite(packet->pos.z) || !std::isfinite(packet->heading)) return;
        if (it == lowriderSessions.end() || it->second.id != packet->session) {
            LowriderSession s; s.id = packet->session; s.kind = packet->kind; s.began = g_serverTime; s.playing = packet->kind == 2;
            it = lowriderSessions.insert_or_assign(packet->owner.value(), s).first;
        }
        if (it->second.kind != packet->kind || it->second.stopped) return;
        it->second.tick = g_serverTime;
        CCoopMission::Relay(*packet, owner, owner); return;
    }
    if (it == lowriderSessions.end() || it->second.id != packet->session || it->second.kind != packet->kind) return;
    auto& s = it->second;
    for (auto guest = s.guests.begin(); guest != s.guests.end();)
        if (!LowriderEligible(owner, CNetworkPlayerManager::GetPlayer(playerid_t(guest->first)))) guest = s.guests.erase(guest);
        else ++guest;
    if (packet->action <= P::TIME) {
        if (sender != owner || s.stopped) return;
        if (packet->action == P::PLAY) s.playing = true;
        if (packet->action == P::STOP) s.stopped = true;
        s.tick = g_serverTime; CCoopMission::Relay(*packet, owner, owner); return;
    }
    if (!LowriderEligible(owner, sender)) return;
    if (packet->action == P::JOIN) {
        if (s.stopped || uint32_t(g_serverTime - s.began) > 30000 || s.guests.count(sender->m_playerId.value())) return;
        s.guests.emplace(sender->m_playerId.value(), LowriderGuest{});
        GetPacketFactory().Send(*packet, owner); return;
    }
    auto guest = s.guests.find(sender->m_playerId.value());
    if (guest == s.guests.end() || guest->second.done || !s.playing ||
        (s.stopped && uint32_t(g_serverTime - s.tick) > 3000)) return;
    auto& score = guest->second;
    if (packet->action == P::POINTS) {
        if (packet->value < 0 || (packet->kind == 1 && (packet->value > 500 || packet->beat <= score.beat)) ||
            (packet->kind == 2 && (packet->value < score.total || packet->value - score.total > 500))) return;
        score.beat = packet->beat; score.total = packet->value;
    } else if (packet->action == P::DONE) score.done = true;
    else return;
    GetPacketFactory().Send(*packet, owner);
}

namespace {
struct PoolSession {
    uint32_t id = 0, tick = 0, joined = 0;
    playerid_t partner{};
    bool active = false;
};
std::map<uint32_t, PoolSession> poolSessions;
bool PoolNear(CNetworkPlayer* a, CNetworkPlayer* b) {
    if (!a || !b || a == b || a->m_bIndividualMode || b->m_bIndividualMode ||
        !a->m_bVanillaScripts || !b->m_bVanillaScripts || a->m_nAreaCode != b->m_nAreaCode) return false;
    auto d = a->m_vecPos - b->m_vecPos;
    return std::isfinite(d.x) && std::isfinite(d.y) && std::isfinite(d.z) && d.x*d.x + d.y*d.y + d.z*d.z <= 64.0f;
}
bool PoolBusy(playerid_t id, uint32_t exceptOwner) {
    for (auto& [owner, s] : poolSessions)
        if (owner != exceptOwner && (owner == id.value() || s.partner == id)) return true;
    return false;
}
void PoolRelay(Packets::Scripts::CoopPool& p, CNetworkPlayer* owner, CNetworkPlayer* activePartner = nullptr) {
    for (auto target : CNetworkPlayerManager::m_pPlayers)
        if (target == activePartner || PoolNear(owner, target)) GetPacketFactory().Send(p, target);
}
}
PACKET_HANDLER(ePacketType::COOP_POOL, Packets::Scripts::CoopPool* p, CNetworkPlayer* sender)
{
    using P = Packets::Scripts::CoopPool;
    // Expired sessions and disconnected ids cannot reserve players indefinitely.
    for (auto it = poolSessions.begin(); it != poolSessions.end();)
        if (!CNetworkPlayerManager::GetPlayer(playerid_t(it->first)) || uint32_t(g_serverTime - it->second.tick) > 10000)
            it = poolSessions.erase(it);
        else ++it;
    auto owner = CNetworkPlayerManager::GetPlayer(p->owner);
    if (!owner || !p->session) return;
    p->sender = sender->m_playerId;
    auto it = poolSessions.find(p->owner.value());
    if (p->action == P::END) {
        if (it == poolSessions.end() || it->second.id != p->session ||
            (sender != owner && sender->m_playerId != it->second.partner)) return;
        p->partner = it->second.partner;
        // END must reach the partner even after a mode/area change.
        for (auto target : CNetworkPlayerManager::m_pPlayers)
            if (target != sender && (target == owner || target->m_playerId == p->partner || PoolNear(owner, target)))
                GetPacketFactory().Send(*p, target);
        poolSessions.erase(it); return;
    }
    if (CCoopMission::IsActive() || sender->m_bIndividualMode || !sender->m_bVanillaScripts || owner->m_bIndividualMode || !owner->m_bVanillaScripts) return;
    if (p->action == P::OFFER) {
        if (sender != owner || PoolBusy(p->owner, p->owner.value())) return;
        if (it != poolSessions.end() && (it->second.active || it->second.id != p->session)) return;
        auto& s = poolSessions[p->owner.value()]; s.id = p->session; s.tick = g_serverTime;
        p->partner = {}; PoolRelay(*p, owner); return;
    }
    if (it == poolSessions.end() || it->second.id != p->session) return;
    auto& s = it->second;
    if (!s.active && s.partner.valid() && uint32_t(g_serverTime - s.joined) >= 5000) s.partner = {};
    if (p->action == P::JOIN) {
        if (s.active || s.partner.valid() || !PoolNear(owner, sender) || PoolBusy(sender->m_playerId, p->owner.value())) return;
        s.partner = sender->m_playerId; s.joined = g_serverTime; p->partner = s.partner; GetPacketFactory().Send(*p, owner); return;
    }
    if (p->action == P::START) {
        auto partner = CNetworkPlayerManager::GetPlayer(s.partner);
        if (sender != owner || s.active || p->partner != s.partner || !PoolNear(owner, partner)) return;
        s.active = true; s.tick = g_serverTime; PoolRelay(*p, owner); return;
    }
    if (!s.active || p->partner != s.partner) return;
    auto partner = CNetworkPlayerManager::GetPlayer(s.partner);
    if (!partner || partner->m_bIndividualMode || !partner->m_bVanillaScripts) return;
    // The reserved pair is already admitted. POOL2 moves both actors during the camera setup.
    if (p->action == P::INPUT) {
        if (sender != partner || !std::isfinite(p->mouseX) || !std::isfinite(p->mouseY)) return;
        GetPacketFactory().Send(*p, owner);
    } else if ((p->action == P::VIEW || p->action == P::RESULT) && sender == owner) {
        s.tick = g_serverTime; PoolRelay(*p, owner, partner);
    }
}

namespace {
using ChallengePacket = Packets::Scripts::ActivityChallenge;
struct ChallengeSession {
    ChallengePacket packet;
    uint32_t tick = 0;
    bool active = false;
    uint8_t state[2]{};
    int32_t scores[2]{};
};
std::map<uint32_t, ChallengeSession> activitySessions;
bool ChallengeNear(CNetworkPlayer* a, CNetworkPlayer* b) {
    if (!a || !b || a == b || a->m_bIndividualMode || b->m_bIndividualMode ||
        !a->m_bVanillaScripts || !b->m_bVanillaScripts || a->m_nAreaCode != b->m_nAreaCode) return false;
    auto d = a->m_vecPos - b->m_vecPos;
    return std::isfinite(d.x) && std::isfinite(d.y) && std::isfinite(d.z) && d.x*d.x+d.y*d.y+d.z*d.z <= 144;
}
}
PACKET_HANDLER(ePacketType::ACTIVITY_CHALLENGE, Packets::Scripts::ActivityChallenge* p, CNetworkPlayer* sender)
{
    using P = Packets::Scripts::ActivityChallenge;
    for (auto it = activitySessions.begin(); it != activitySessions.end();)
        if (uint32_t(g_serverTime - it->second.tick) > (it->second.active ? 15000u : 20000u) ||
            !CNetworkPlayerManager::GetPlayer(it->second.packet.owner) ||
            !CNetworkPlayerManager::GetPlayer(it->second.packet.partner)) it = activitySessions.erase(it);
        else ++it;
    auto owner = CNetworkPlayerManager::GetPlayer(p->owner);
    auto partner = CNetworkPlayerManager::GetPlayer(p->partner);
    if (!owner || !partner || owner == partner || !p->session) return;
    auto it = activitySessions.find(p->owner.value());
    p->sender = sender->m_playerId;
    if (p->action == P::OFFER) {
        if (sender != owner || !ChallengeNear(owner, partner) || CCoopMission::IsActive() || partner->m_bOnMission) return;
        for (auto& [id, s] : activitySessions)
            if (s.packet.owner == p->owner || s.packet.partner == p->owner ||
                s.packet.owner == p->partner || s.packet.partner == p->partner) return;
        activitySessions.emplace(p->owner.value(), ChallengeSession{*p, g_serverTime});
        GetPacketFactory().Send(*p, partner); return;
    }
    if (it == activitySessions.end()) return;
    auto& s = it->second;
    if (p->session != s.packet.session || p->partner != s.packet.partner || p->kind != s.packet.kind || p->variant != s.packet.variant ||
        (sender != owner && sender != partner)) return;
    auto target = sender == owner ? partner : owner;
    if (p->action == P::DECLINE || p->action == P::LEAVE) {
        GetPacketFactory().Send(*p, target); activitySessions.erase(it); return;
    }
    if (p->action == P::ACCEPT) {
        if (sender != partner || s.active || CCoopMission::IsActive() || partner->m_bOnMission ||
            !ChallengeNear(owner, partner)) return;
        s.active = true; s.tick = g_serverTime; GetPacketFactory().Send(*p, owner);
        GetPacketFactory().Send(*p, partner); return;
    }
    if (!s.active || p->action != P::STATE) return;
    int slot = sender == owner ? 0 : 1;
    auto& state = s.state[slot];
    if (p->state < state) return;
    // Finished results are immutable, but their heartbeat must continue until both finish.
    if (state < 2) { state = p->state; s.scores[slot] = p->score; }
    p->state = state; p->score = s.scores[slot];
    GetPacketFactory().Send(*p, target);
    s.tick = g_serverTime;
    if (state == 3 || (s.state[0] >= 2 && s.state[1] >= 2)) activitySessions.erase(it);
}

PACKET_HANDLER(ePacketType::CUTSCENE_PARTNERS, Packets::Scripts::CutscenePartners* packet, CNetworkPlayer* player)
{
    auto owner=CNetworkPlayerManager::GetPlayer(packet->owner);
    if(!owner || !CCoopMission::IsScriptAuthority(owner) || !owner->m_bOnMission) return;
    packet->sender=player->m_playerId;
    if(packet->kind==1) {
        if(player!=owner && CCoopMission::CanReceiveMissionPacket(player)) GetPacketFactory().Send(*packet,owner);
        return;
    }
    if(player!=owner || packet->count>3 || packet->scene[8] ||
        !std::isfinite(packet->at) || packet->at<0 || packet->at>3600000 || !std::isfinite(packet->heading)) return;
    auto finite=[](const CVector& v) { return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z) &&
        std::fabs(v.x)<50000 && std::fabs(v.y)<50000 && std::fabs(v.z)<50000; };
    if(!finite(packet->ground) || !finite(packet->via) || !finite(packet->seat[0]) || !finite(packet->seat[1])) return;
    for(int i=0;i<packet->count;++i) {
        auto p=CNetworkPlayerManager::GetPlayer(packet->partners[i]);
        if(!p || p==player || !CCoopMission::CanReceiveMissionPacket(p)) return;
        for(int j=0;j<i;++j) if(packet->partners[i]==packet->partners[j]) return;
    }
    CCoopMission::Relay(*packet,player,player);
}

namespace {
using Social = Packets::Scripts::SocialAction;
struct SocialSession { Social p; uint32_t tick; bool active = false; };
std::map<uint32_t, SocialSession> socialSessions;
bool SocialReady(CNetworkPlayer* p) {
    return p && p->m_bSocialAlive && p->m_bSocialControls && !p->m_bIndividualMode && !p->m_bOnMission &&
        !p->m_vehicleId.valid() && p->m_nRunningMission < 0 && !(CCoopMission::IsActive() && CCoopMission::IsParticipant(p)) &&
        uint32_t(g_serverTime - p->m_nPosTime) < 3000;
}
bool SocialNear(CNetworkPlayer* a, CNetworkPlayer* b, bool pair) {
    if (!SocialReady(a) || !SocialReady(b) || a == b || a->m_nAreaCode != b->m_nAreaCode) return false;
    auto d = a->m_vecPos - b->m_vecPos;
    return std::isfinite(d.x) && std::isfinite(d.y) && std::isfinite(d.z) &&
        (!pair || d.x*d.x + d.y*d.y + d.z*d.z <= 16.0f);
}
void SocialSend(Social& p) {
    if (auto a = CNetworkPlayerManager::GetPlayer(p.owner)) GetPacketFactory().Send(p, a);
    if (auto b = CNetworkPlayerManager::GetPlayer(p.partner)) GetPacketFactory().Send(p, b);
}
void SocialExpire() {
    for (auto it = socialSessions.begin(); it != socialSessions.end();) {
        auto& s = it->second;
        if (uint32_t(g_serverTime - s.tick) > (s.active ? 5000u : 15000u) ||
            !SocialNear(CNetworkPlayerManager::GetPlayer(s.p.owner), CNetworkPlayerManager::GetPlayer(s.p.partner), s.p.kind == Social::PAIR)) {
            auto p = s.p; p.action = Social::CANCEL; p.reason = 1; SocialSend(p);
            it = socialSessions.erase(it);
        } else ++it;
    }
}
}
PACKET_HANDLER(ePacketType::SOCIAL_ACTION, Packets::Scripts::SocialAction* p, CNetworkPlayer* sender)
{
    SocialExpire();
    auto owner = CNetworkPlayerManager::GetPlayer(p->owner);
    auto partner = CNetworkPlayerManager::GetPlayer(p->partner);
    if (!owner || !partner || owner == partner || !p->session) return;
    auto it = socialSessions.find(p->owner.value());
    if (p->action == Social::OFFER) {
        if (sender != owner) return;
        uint8_t reason = 0;
        if (p->kind != Social::PAIR && !CConfigManager::GetConfigTpRequest()) reason = 2;
        if (!SocialNear(owner, partner, p->kind == Social::PAIR))
            reason = !SocialReady(owner) ? 5 : !SocialReady(partner) ? 6 : 7;  // 5: me, 6: the other one, 7: too far / other area
        for (auto& [id, s] : socialSessions)
            if (s.p.owner == p->owner || s.p.partner == p->owner || s.p.owner == p->partner || s.p.partner == p->partner) reason = 3;
        if (reason) { p->action = Social::CANCEL; p->reason = reason; GetPacketFactory().Send(*p, owner); return; }
        // Anchor always comes from the server's latest position, never the incoming packet.
        p->pos = owner->m_vecPos; p->area = owner->m_nAreaCode; p->heading = 0;
        socialSessions.emplace(p->owner.value(), SocialSession{*p, g_serverTime});
        GetPacketFactory().Send(*p, partner); return;
    }
    if (it == socialSessions.end()) return;
    auto& s = it->second;
    if (p->session != s.p.session || p->partner != s.p.partner || p->kind != s.p.kind || p->emote != s.p.emote ||
        (sender != owner && sender != partner)) return;
    if (p->action == Social::CANCEL) {
        auto out = s.p; out.action = Social::CANCEL; out.reason = 4; SocialSend(out); socialSessions.erase(it); return;
    }
    if (p->action != Social::ACCEPT || sender != partner || s.active ||
        !SocialNear(owner, partner, s.p.kind == Social::PAIR)) return;
    auto out = s.p; out.action = Social::START;
    auto anchor = out.kind == Social::GO ? partner : owner;
    out.pos = anchor->m_vecPos; out.area = anchor->m_nAreaCode;
    // Facing is geometrical, so both games get the same frame of reference.
    auto d = partner->m_vecPos - owner->m_vecPos;
    out.heading = std::atan2(-d.x, d.y);
    SocialSend(out);
    if (out.kind == Social::PAIR) { s.active = true; s.tick = g_serverTime; s.p = out; }
    else socialSessions.erase(it);
}
PACKET_HANDLER(ePacketType::EMOTE_STATE, Packets::Scripts::EmoteState* p, CNetworkPlayer* sender)
{
    SocialExpire();
    p->player = sender->m_playerId;
    if (!std::isfinite(p->time) || p->time < 0 || p->time > 3600 || p->block[15]) return;
    if (p->active && (!SocialReady(sender) || !p->hash || !p->block[0])) return;
    for (int i = 0; i < 15 && p->block[i]; ++i)
        if (!isalnum((unsigned char)p->block[i]) && p->block[i] != '_') return;
    for (auto& [id, s] : socialSessions)
        if (s.active && (s.p.owner == p->player || s.p.partner == p->player) && s.p.session == p->session)
            s.tick = g_serverTime;
    // The state is a heartbeat: late stream-in gets the current pose, and silence releases it.
    for (auto target : CNetworkPlayerManager::m_pPlayers)
        if (target != sender && !target->m_bIndividualMode) GetPacketFactory().Send(*p, target);
}
PACKET_HANDLER(ePacketType::STORY_RACE, Packets::Scripts::StoryRace* packet, CNetworkPlayer* sender)
{
    if (!CCoopMission::IsScriptAuthority(sender) || !sender->m_bVanillaScripts ||
        (!sender->m_bOnMission && packet->phase != Packets::Scripts::StoryRace::END &&
            packet->phase != Packets::Scripts::StoryRace::PREPARE) || packet->session == 0 ||
        packet->sequence == 0 || packet->phase > Packets::Scripts::StoryRace::END ||
        packet->place > packet->total || packet->lap > packet->laps ||
        !std::isfinite(packet->size) || packet->size < 1.0f || packet->size > 15.0f ||
        !std::isfinite(packet->heading) || std::fabs(packet->heading) > 720.0f) return;
    for (int i = 0; i < 3; ++i)
        if (!std::isfinite(packet->next[i]) || !std::isfinite(packet->after[i]) || !std::isfinite(packet->grid[i]) ||
            std::fabs(packet->next[i]) > 10000 || std::fabs(packet->after[i]) > 10000 ||
            std::fabs(packet->grid[i]) > 10000) return;
    auto target = CNetworkPlayerManager::GetPlayer(packet->target);
    if (!target || target == sender || !target->m_bVanillaScripts || !CCoopMission::CanReceiveMissionPacket(target)) return;
    packet->owner = sender->m_playerId;
    GetPacketFactory().Send(*packet, target);
}
