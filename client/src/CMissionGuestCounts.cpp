#include "stdafx.h"
#include "CMissionGuestCounts.h"
#include "CMissionSyncRules.h"
#include "CMissionMirror.h"
#include "CCoopMission.h"
#include "COpCodeSync.h"
#include "Commands/CommandHooks.h"
#include "network/packets/mission_guest_counts.h"
#include <map>

// Adapted from Karimov-Akbar/sa-coop MissionRules.cpp (MIT), see NOTICE.md.
namespace
{
using State = Packets::Scripts::MissionGuestCounts;
struct RemoteState { State state; DWORD received = 0; };
std::map<playerid_t, RemoteState> states;
State request;
DWORD requestAt = 0, sentAt = 0;
uint32_t nextEpoch = 0;
CRunningScript* activeScript = nullptr;

bool Eligible(CNetworkPlayer* player, CPed* local)
{
    return player && local && player->m_pPed && !player->m_bDead && !player->m_bIndividualMode &&
        player->m_pPed->IsVTableValid() && player->m_pPed->m_fHealth > 0.0f &&
        player->m_pPed->m_nAreaCode == local->m_nAreaCode && CCoopMission::ShouldIncludeNetworkPlayer(player);
}
CRunningScript* MissionScript()
{
    for (auto s = CTheScripts::pActiveScripts; s; s = s->m_pNext)
        if (s->m_bIsMission && COpCodeSync::IsScriptSynced(s))
            return s;
    return nullptr;
}
struct Spot
{
    const char* name;
    uint32_t start, at, length, hash;
    uint16_t opcode;
    int count, actor;
};
// Only independent branches that change objects/guards, never the authority's CJ.
constexpr Spot spots[] = {
    {"MAF4", 0x1E9490, 0x1ECB9D, 30721, 0x5B4C70DF, 0x0102, 8, 0},
    {"DES10", 0x1F0C91, 0x1F1A67, 4603, 0xF4F9C0B1, 0x00FE, 8, 0},
    {"MANSIO2", 0x25B5E6, 0x2614F8, 53511, 0x14481C7C, 0x09ED, 2, 1},
    {"MANSIO2", 0x25B5E6, 0x264BA2, 53511, 0x14481C7C, 0x09ED, 2, 1},
    // D (Cop Wheels, HEIST4): the load of a bike onto the Packer asks "is CJ in this bike" and "is he in the angled
    // area behind the truck"; a guest riding the bike answers both, so the owner's script attaches it natively
    // (HEIST4.txt 876 / 931) and the owner's CJ never moves. Needs coop-mission-machines.
    {"HEIST4", 0x246B1D, 0x248250, 13637, 0x68971E7B, 0x00DB, 2, 0},
    {"HEIST4", 0x246B1D, 0x2483E1, 13637, 0x68971E7B, 0x05FE, 9, 0},
};
const Spot* FindSpot(CRunningScript* script, uint16_t opcode)
{
    if (!CConfigLoader::ms_bVanillaScripts || script->m_pBaseIP != reinterpret_cast<uint8_t*>(0xA7A6A0))
        return nullptr;
    const auto ip = CommandHooks::GetCurrentCommandIP();
    const auto base = script->m_pBaseIP;
    for (const auto& spot : spots)
    {
        if (!_strnicmp(spot.name, "HEIST4", 8) && !CConfigLoader::ms_bCoopMissionMachines)
            continue;
        if (spot.opcode != opcode || _strnicmp(script->m_szName, spot.name, 8) ||
            reinterpret_cast<uintptr_t>(ip) != reinterpret_cast<uintptr_t>(base) + spot.at - spot.start)
            continue;
        // Recheck at each use: mission storage is reused, including retries of the same mission.
        uint32_t hash = 2166136261u;
        for (uint32_t i = 0; i < spot.length; ++i)
            hash = (hash ^ base[i]) * 16777619u;
        if (hash == spot.hash)
            return &spot;
    }
    return nullptr;
}
}

void CMissionGuestCounts::OnCommandStart(CRunningScript* script, uint16_t opcode)
{
    // SCRIPT_NAME runs on each new mission, even when GTA reuses the same script slot.
    if (opcode == 0x03A4 && script && script->m_bIsMission)
    {
        activeScript = nullptr;
        states.clear();
        request = State{};
    }
}

void CMissionGuestCounts::Process()
{
    CPed* local = FindPlayerPed(0);
    if (!CConfigLoader::ms_bMissionGuestCounts || !CNetwork::m_bAuthenticated || !local ||
        !local->IsAlive() || CCoopMission::IsLocalIndividualMission() || CCoopMission::HasLocalLeft())
    {
        states.clear(); request = State{}; activeScript = nullptr; sentAt = 0;
        return;
    }
    const DWORD now = GetTickCount();
    if (CCoopMission::IsScriptAuthority())
    {
        auto script = MissionScript();
        const int mission = CMissionMirror::GetRunningMissionId();
        if (!script || mission < 0)
        {
            states.clear(); activeScript = nullptr; request = State{};
            return;
        }
        if (script != activeScript || request.mission != mission || !request.epoch)
        {
            activeScript = script; states.clear();
            request = State{};
            request.request = true;
            request.owner = CNetworkPlayerManager::m_localPlayerId;
            request.mission = mission;
            request.epoch = ++nextEpoch;
            if (!request.epoch) request.epoch = ++nextEpoch;
            sentAt = 0;
        }
        if (now - sentAt >= 200)
        {
            sentAt = now;
            GetPacketFactory().Send(request);
        }
        for (auto it = states.begin(); it != states.end(); )
            if (!Eligible(CNetworkPlayerManager::GetPlayer(it->first), local) || now - it->second.received > 600)
                it = states.erase(it);
            else ++it;
    }
    else
    {
        auto owner = CNetworkPlayerManager::GetPlayer(request.owner);
        const bool authority = owner && (CCoopMission::IsRunning() ? owner->m_playerId == CCoopMission::GetOwnerId() : owner->m_bIsHost);
        if (!request.epoch || now - requestAt > 600 || !authority || !Eligible(owner, local))
        {
            request = State{};
            return;
        }
        if (now - sentAt < 100) return;
        sentAt = now;
        State state = request;
        state.request = false;
        state.shooting = local->m_nPedFlags.bFiringWeapon;
        CPlayerPed* me = FindPlayerPed(0);
        // gta-reversed PlayerCommands.cpp IsPlayerTargettingChar (0457).
        CEntity* target = reinterpret_cast<CEntity*>(me->m_pTargetedObject);
        if ((!target || target->m_nType != ENTITY_TYPE_PED) && CCamera::m_bUseMouse3rdPerson && CPad::GetPad(0)->GetTarget())
            target = me->m_pPlayerTargettedPed;
        auto ped = target ? CNetworkPedManager::GetPed(target) : nullptr;
        state.target = ped ? ped->m_pedId : pedid_t{};
        GetPacketFactory().Send(state);
    }
}

void CMissionGuestCounts::Receive(const State& packet)
{
    if (!CConfigLoader::ms_bMissionGuestCounts || !CNetwork::m_bAuthenticated ||
        CCoopMission::IsLocalIndividualMission() || CCoopMission::HasLocalLeft() || !packet.epoch)
        return;
    const auto sender = CNetworkPlayerManager::GetPlayer(packet.senderPlayerid);
    if (!Eligible(sender, FindPlayerPed(0))) return;
    if (packet.request)
    {
        const bool authority = CCoopMission::IsRunning() ? sender->m_playerId == CCoopMission::GetOwnerId() : sender->m_bIsHost;
        if (CCoopMission::IsScriptAuthority() || !authority || packet.owner != sender->m_playerId ||
            sender->m_nRunningMission != packet.mission) return;
        request = packet; requestAt = GetTickCount();
    }
    else if (CCoopMission::IsScriptAuthority() && activeScript && packet.owner == CNetworkPlayerManager::m_localPlayerId &&
        packet.epoch == request.epoch && packet.mission == request.mission)
        states[sender->m_playerId] = {packet, GetTickCount()};
}

bool CMissionGuestCounts::FilterCompare(CRunningScript* script, uint16_t opcode, bool state, bool& handled)
{
    handled = false;
    if (!CConfigLoader::ms_bMissionGuestCounts || !CNetwork::m_bAuthenticated ||
        COpCodeSync::bProcessingNetworkOpcode || !script->m_bIsMission ||
        !CCoopMission::IsScriptAuthority() || CCoopMission::IsLocalIndividualMission() || CCoopMission::HasLocalLeft() ||
        !COpCodeSync::IsScriptSynced(script))
        return state;
    CPed* local = FindPlayerPed(0);
    if (!local || !local->IsAlive()) return state;
    // A true generic condition needs no extension. Spots still need an explicit handled result.
    if (state && opcode != 0x00FE && opcode != 0x0102 && opcode != 0x09ED) return state;
    int params[10]; memcpy(params, ScriptParams, sizeof(params));
    int count = 0, actor = 0;
    const int own = CPools::GetPedRef(local);
    const Spot* spot = nullptr;
    switch (opcode)
    {
    case 0x051A:
        if (!CPools::GetPed(params[0])) return state;
        count = 2; actor = 1; break;
    case 0x051C:
        if (!CPools::GetVehicle(params[0])) return state;
        count = 2; actor = 1; break;
    case 0x02E0: count = 1; break;
    case 0x02D6: count = 6; params[5] = 0; break; // no extra area drawing
    case 0x0457:
    {
        if (params[0] != 0) return state;
        auto target = CNetworkPedManager::GetPed(CPools::GetPed(params[1]));
        if (!target) return state;
        for (auto player : CNetworkPlayerManager::m_pPlayers)
        {
            auto it = states.find(player->m_playerId);
            if (Eligible(player, local) && it != states.end() && GetTickCount() - it->second.received <= 600 &&
                it->second.state.target == target->m_pedId)
                return true;
        }
        return state;
    }
    default:
        spot = FindSpot(script, opcode);
        if (!spot) return state;
        handled = true;
        if (state) return true;
        count = spot->count; actor = spot->actor;
        if (count == 8) params[7] = 0; // keep native markers exclusive to the real script
        break;
    }
    if (params[actor] != own) return state;
    for (auto player : CNetworkPlayerManager::m_pPlayers)
    {
        if (!Eligible(player, local)) continue;
        CPed* ped = player->m_pPed;
        if (ped->m_nPedFlags.bInVehicle && !ped->m_pVehicle) continue;
        auto it = states.find(player->m_playerId);
        if ((opcode == 0x02E0 || opcode == 0x02D6) &&
            (it == states.end() || GetTickCount() - it->second.received > 600)) continue;
        params[actor] = CPools::GetPedRef(ped);
        const bool firing = ped->m_nPedFlags.bFiringWeapon;
        if (opcode == 0x02E0 || opcode == 0x02D6)
            ped->m_nPedFlags.bFiringWeapon = it->second.state.shooting;
        const bool yes = COpCodeSync::ProbeCondition(script, opcode, params, count);
        ped->m_nPedFlags.bFiringWeapon = firing;
        if (yes)
        {
            static DWORD lastLog = 0;
            const DWORD now = GetTickCount();
            if (now - lastLog >= 1000)
            {
                lastLog = now;
                CMissionMirror::LogEventf("guest-counts script=%.8s opcode=%04X spot=%06X player=%u",
                    script->m_szName, opcode, spot ? spot->at : 0, player->m_playerId.value());
            }
            return true;
        }
    }
    return state;
}
