#include "stdafx.h"
#include "CEntryExitMarkerSync.h"
#include "CEntryExitManager.h"
#include <CGame.h>
#include <CCoopMission.h>
#include <CMissionMode.h>
#include <CMissionMirror.h>
#include <COpCodeSync.h>

namespace
{
    struct EntryWait
    {
        playerid_t owner;
        int mission = -1;
        unsigned char area = 0;
        uint32_t startedAt = 0;
        bool started = false;
        bool finished = false;
    };
    EntryWait entryWait;

    void UpdateEntryWait()
    {
        const CPlayerPed* localPlayer = FindPlayerPed(0);
        if (!CConfigLoader::ms_bMissionInteriorSafety || !CNetwork::m_bConnected ||
            CCoopMission::IsScriptAuthority() || CMissionMode::IsIndividual() || CCoopMission::HasLocalLeft() ||
            COpCodeSync::IsLocalMissionRunning() ||
            (CCoopMission::IsActive() && !CCoopMission::IsLocalParticipant()) ||
            !CTheScripts::OnAMissionFlag || !CTheScripts::ScriptSpace[CTheScripts::OnAMissionFlag] ||
            !localPlayer || localPlayer->m_fHealth <= 0.0f ||
            CWorld::Players[0].m_nPlayerState == PLAYERSTATE_HASDIED ||
            CWorld::Players[0].m_nPlayerState == PLAYERSTATE_HASBEENARRESTED)
        {
            entryWait = {};
            return;
        }

        for (auto player : CNetworkPlayerManager::m_pPlayers)
        {
            const bool authority = CCoopMission::IsRunning() ? player->m_playerId == CCoopMission::GetOwnerId()
                                                           : player->m_bIsHost;
            if (!authority || !player->m_pPed || player->m_nRunningMission < 0 ||
                !CCoopMission::ShouldIncludeNetworkPlayer(player) || !player->m_pPed->m_nAreaCode ||
                player->m_pPed->m_fHealth <= 0.0f)
                continue;

            const auto area = player->m_pPed->m_nAreaCode;
            if (entryWait.owner != player->m_playerId || entryWait.mission != player->m_nRunningMission ||
                entryWait.area != area)
            {
                entryWait = {};
                entryWait.owner = player->m_playerId;
                entryWait.mission = player->m_nRunningMission;
                entryWait.area = area;
            }
            // Keep the consumed window until the authority leaves, even after another packet.
            if (entryWait.started && (localPlayer->m_nAreaCode != 0 || CGame::currArea != 0 ||
                CTimer::m_snTimeInMilliseconds - entryWait.startedAt >= 35000))
                entryWait.finished = true;
            return;
        }
        entryWait = {};
    }
}

void CEntryExitMarkerSync::Process()
{
    if (COpCodeSync::bProcessingNetworkOpcode)
        return;
    UpdateEntryWait();
    if (!ms_bDeferredAccess)
        return;
    if (!CNetwork::m_bConnected || CLocalPlayer::m_bIsHost)
    {
        ms_bDeferredAccess = false;
        return;
    }
    if (entryWait.started && !entryWait.finished)
        return;
    const CPlayerPed* localPlayer = FindPlayerPed(0);
    if (localPlayer && ((!localPlayer->m_nAreaCode && CGame::currArea == 0) ||
                        entryWait.finished || !CConfigLoader::ms_bMissionInteriorSafety))
        Receive(ms_lastData);
}

void CEntryExitMarkerSync::Send()
{
    Packets::Scripts::EnExSync packet{};
    packet.bDisabled = CEntryExitManager::ms_bDisabled;
    packet.bBurglary = CEntryExitManager::ms_bBurglaryHousesEnabled;
    packet.count = 0;

    for (auto pEntryExit : CEntryExitManager::mp_poolEntryExits)
    {
        packet.enexes[packet.count].areaId = pEntryExit->m_nArea;
        packet.enexes[packet.count].flags = pEntryExit->m_nFlags;
        packet.enexes[packet.count].rectLeft = static_cast<int16_t>(std::floor(pEntryExit->m_recEntrance.left));
        packet.enexes[packet.count].rectBottom = static_cast<int16_t>(std::floor(pEntryExit->m_recEntrance.bottom));

        packet.count++;
    }

    GetPacketFactory().Send(packet);

#ifdef PACKET_DEBUG_MESSAGES
    CChat::AddMessage("CEntryExitMarkerSync::Send %d %d %d", CEntryExitManager::ms_bDisabled,
        CEntryExitManager::ms_bBurglaryHousesEnabled, packet.count);
#endif
}

void CEntryExitMarkerSync::Receive(const Packets::Scripts::EnExSync& packet)
{
#ifdef PACKET_DEBUG_MESSAGES
    CChat::AddMessage("CEntryExitMarkerSync::Receive %d %d %d", CEntryExitManager::ms_bDisabled,
        CEntryExitManager::ms_bBurglaryHousesEnabled, packet.count);
#endif

    if (COpCodeSync::bProcessingNetworkOpcode)
        return;
    UpdateEntryWait();
    ms_lastData = packet;
    ms_bDeferredAccess = false;
    const CPlayerPed* localPlayer = FindPlayerPed(0);
    const unsigned char area = localPlayer ? localPlayer->m_nAreaCode : 0;
    // gta-reversed EntryExit.h: ENABLE_ACCESS (SWITCH_ENTRY_EXIT) is bit 14.
    CEntryExitManager::ms_bDisabled = packet.bDisabled;
    CEntryExitManager::ms_bBurglaryHousesEnabled = packet.bBurglary;
    for (size_t i = 0; i < packet.count; i++)
    {
        for (auto pEntryExit : CEntryExitManager::mp_poolEntryExits)
        {
            if (static_cast<int16_t>(std::floor(pEntryExit->m_recEntrance.left)) == packet.enexes[i].rectLeft &&
                static_cast<int16_t>(std::floor(pEntryExit->m_recEntrance.bottom)) == packet.enexes[i].rectBottom &&
                pEntryExit->m_nArea == packet.enexes[i].areaId)
            {
                auto flags = packet.enexes[i].flags;
                // A linked exit has the interior on its other endpoint (TransitionStarted).
                const bool occupied = area != 0 && (pEntryExit->m_nArea == area ||
                    (pEntryExit->m_pLink && pEntryExit->m_pLink->m_nArea == area));
                const bool authorityInterior = entryWait.area != 0 && (pEntryExit->m_nArea == entryWait.area ||
                    (pEntryExit->m_pLink && pEntryExit->m_pLink->m_nArea == entryWait.area));
                const bool waitingOutside = localPlayer && area == 0 && CGame::currArea == 0 &&
                    authorityInterior && !entryWait.finished;
                // Entry completion must apply this closure even though T7 protects occupied interiors.
                const bool entryCompleted = authorityInterior && entryWait.finished;
                if (CConfigLoader::ms_bMissionInteriorSafety &&
                    ((occupied && !entryCompleted) || waitingOutside) &&
                    pEntryExit->m_nFlags.bEnableAccess && !flags.bEnableAccess)
                {
                    if (waitingOutside && !entryWait.started)
                    {
                        entryWait.started = true;
                        entryWait.startedAt = CTimer::m_snTimeInMilliseconds;
                        CMissionMirror::LogEventf("enex-keep-open area=%u", entryWait.area);
                    }
                    flags.bEnableAccess = true;
                    ms_bDeferredAccess = true;
                }
                pEntryExit->m_nFlags = flags;
            }
        }
    }
}
