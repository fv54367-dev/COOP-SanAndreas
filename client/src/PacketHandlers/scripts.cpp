#include "CSocialActions.h"
#include "CHighStakesRace.h"
#include "CCoopLowrider.h"
#include "network/packets/scripts.h"
#include "network/packet_types.h"
#include "stdafx.h"
#include "CMissionLock.h"
#include "CHomeInvasion.h"
#include "CActivityChallenge.h"
#include "CCutscenePartners.h"
#include "CCoopPool.h"
#include <CMissionMirror.h>
#include <CNetworkCheckpoint.h>
#include <CNetworkEntityBlip.h>
#include <CEntryExitMarkerSync.h>
#include <COpCodeSync.h>
#include <CUserDisplay.h>
#include <CTaskSequenceSync.h>
#include <CCutsceneSkipVote.h>
#include <Hooks/MissionAudioHooks.h>
#include <CMinigameVs.h>
#include <CVsLobby.h>
#include <CVsRace.h>
#include <CCoopMission.h>
#include <CEntryExitManager.h>
#include <CTimeCycle.h>
#include <CMissionMode.h>
#include <CSaveMenu.h>
#include <CConfigLoader.h>
#include <CCarEnterExit.h>
#include <CMissionSyncRules.h>
#include <CMissionHints.h>
#include <CPedIntelligence.h>
#include <Commands/Commands/CCommandPutNetworkPlayersInCar.h>
#include <Commands/Commands/CCommandTeleportPlayersToHostSafely.h>

void ClearNetworkMissionHud();

// F1 individual (or another scripts mode than the host): nothing of the authority's mission is shown here (the server doesn't relay it either; this covers the
// packets already on their way when the mode changed)
static bool IsOutOfHostMission()
{
    if (CMissionLock::IsLocalIntroRunning())
        return true;
	if (CCoopMission::IsScriptAuthority())
		return false;
	if (CMissionMode::IsIndividual())
		return true;
	// the host plays with the other scripts (original vs ours): his missions aren't ours
	for (auto pNetworkPlayer : CNetworkPlayerManager::m_pPlayers)
	{
		if (pNetworkPlayer->m_bIsHost)
			return pNetworkPlayer->m_bOtherScripts;
	}
	return false;
}

PACKET_HANDLER(ePacketType::MISSION_SCRIPTED_EXIT, Packets::Scripts::MissionScriptedExit* packet)
{
    if (!CConfigLoader::ms_bMissionSyncRules || !CConfigLoader::ms_bMissionScriptedExit || !CNetwork::m_bConnected ||
        packet->playerid != CNetworkPlayerManager::m_localPlayerId || CCoopMission::IsScriptAuthority() ||
        IsOutOfHostMission() || CCoopMission::HasLocalLeft() || COpCodeSync::IsLocalMissionRunning() ||
        (CCoopMission::IsActive() && !CCoopMission::IsLocalParticipant()) ||
        !CTheScripts::OnAMissionFlag || !CTheScripts::ScriptSpace[CTheScripts::OnAMissionFlag])
        return;

    const playerid_t authorityId = CCoopMission::IsRunning() ? CCoopMission::GetOwnerId() : playerid_t{};
    bool authorityPresent = false;
    std::string authorityName;
    for (auto remote : CNetworkPlayerManager::m_pPlayers)
        if (authorityId.valid() ? remote->m_playerId == authorityId : remote->m_bIsHost)
        {
            authorityPresent = remote->m_pPed != nullptr;
            authorityName = remote->GetName();
        }
    if (!authorityPresent)
        return;

    CPlayerPed* player = FindPlayerPed(0);
    if (!player || player->m_fHealth <= 0.0f)
        return;
    // T98: only the help box, never an exit (CMissionSyncRules::ProcessHostDriverHint)
    if (packet->opcode == CMissionSyncRules::HOST_DRIVER_HINT_OPCODE)
    {
        if (CConfigLoader::ms_bMissionHostDriverHint && player->m_nPedFlags.bInVehicle && player->m_pVehicle &&
            player->m_pVehicle->m_pDriver == player)
        {
            CMissionHints::Help("Solo " + authorityName + " puede manejar este vehiculo en esta mision");
            CMissionMirror::LogEventf("rx-host-driver-hint host=%s", authorityName.c_str());
        }
        return;
    }
    CCommandPutNetworkPlayersInCar::ClearPendingBoarding();
    if (!player->m_nPedFlags.bInVehicle || !player->m_pVehicle)
        return;

    const int pedRef = CPools::GetPedRef(player);
    if (packet->opcode == 0x0362)
    {
        // gta-reversed WarpCharFromCarToCoord: native cleanup before the ordered rule-teleport.
        player->m_pIntelligence->FlushImmediately(true);
    }
    else if (packet->opcode == 0x0622)
        Command<Commands::TASK_LEAVE_CAR_IMMEDIATELY>(pedRef, CPools::GetVehicleRef(player->m_pVehicle));
    else
        Command<Commands::TASK_LEAVE_ANY_CAR>(pedRef);
    CMissionMirror::LogEventf("rx-scripted-exit opcode=%04X", packet->opcode);
}

// a model id from the network: only vehicle models reach CREATE_CAR (an unknown id writes out of ms_aInfoForModel)
static bool IsVehicleModel(int model)
{
	return model >= 0 && model < 20000 && CModelInfo::ms_modelInfoPtrs[model] &&
		CModelInfo::ms_modelInfoPtrs[model]->GetModelType() == MODEL_INFO_VEHICLE;
}

// like the scripts' MARK_MODEL_AS_NO_LONGER_NEEDED after CREATE_CAR: GAME_REQUIRED would keep it out of the streamer forever
static void ReleaseModelIfWeRequested(int model, bool wasRequired)
{
	if (!wasRequired)
		CStreaming::SetModelIsDeletable(model);
}

PACKET_HANDLER(ePacketType::MISSION_PLAYER_EQUIPMENT, Packets::Scripts::MissionPlayerEquipment* packet)
{
    if (!CConfigLoader::ms_bMissionPlayerEquipment || packet->playerid != CNetworkPlayerManager::m_localPlayerId ||
        CCoopMission::IsScriptAuthority() || IsOutOfHostMission() || CCoopMission::HasLocalLeft() ||
        COpCodeSync::IsLocalMissionRunning() ||
        (CCoopMission::IsActive() && !CCoopMission::IsLocalParticipant()))
        return;

    CPlayerPed* localPlayer = FindPlayerPed(0);
    if (!localPlayer)
        return;
    const auto weaponType = static_cast<eWeaponType>(packet->weaponType);
    const auto info = CWeaponInfo::GetWeaponInfo(weaponType, 1);
    if (!info || info->m_nSlot < 0 || info->m_nSlot >= 13)
        return;

    if (packet->remove)
    {
        // ClearWeapon checks the exact type before clearing the shared slot.
        localPlayer->ClearWeapon(weaponType);
    }
    else if (packet->setAmmo)
    {
        // Match the exact weapon before the native SetCharAmmo path (gta-reversed).
        if (localPlayer->m_aWeapons[info->m_nSlot].m_eWeaponType != weaponType)
            return;
        localPlayer->SetAmmo(weaponType, packet->ammo);
    }
    else if (packet->equip)
    {
        // SET_CURRENT_CHAR_WEAPON chooses the player's slot only if he owns that weapon.
        if (localPlayer->m_aWeapons[info->m_nSlot].m_eWeaponType != weaponType)
            return;
        localPlayer->m_pPlayerData->m_nChosenWeapon = info->m_nSlot;
    }
    else
    {
        for (int model : {info->m_nModelId1, info->m_nModelId2})
        {
            if (model > 0 && CStreaming::ms_aInfoForModel[model].m_nLoadState != LOADSTATE_LOADED)
            {
                CStreaming::RequestModel(model, eStreamingFlags::GAME_REQUIRED);
                CStreaming::LoadAllRequestedModels(false);
            }
        }
        localPlayer->GiveWeapon(weaponType, packet->ammo, false);
        // the weapon keeps its own model reference (weapon-model-release does the same for the CUtil path)
        if (CConfigLoader::ms_bWeaponModelRelease)
            for (int model : {info->m_nModelId1, info->m_nModelId2})
                if (model > 0 && !(CStreaming::ms_aInfoForModel[model].m_nFlags & (MISSION_REQUIRED | KEEP_IN_MEMORY)))
                    CStreaming::SetModelIsDeletable(model);
    }
    CMissionMirror::LogEventf("rx-equipment weapon=%d ammo=%d equip=%d remove=%d setAmmo=%d", (int)packet->weaponType, packet->ammo,
        packet->equip ? 1 : 0, packet->remove ? 1 : 0, packet->setAmmo ? 1 : 0);
}

// the state the host's coop mission mirrors on us (the OnAMissionFlag and what he sent for the mission): used both
// when the host ends the mission normally and when he leaves in the middle of it (MISSION_ABORTED)
static void ClearHostMissionState()
{
    CCommandTeleportPlayersToHostSafely::ClearPendingTeleport();
    CCommandPutNetworkPlayersInCar::ClearPendingBoarding();
	CCoopMission::LocalCleanup();
}

PACKET_HANDLER(ePacketType::LAUNCH_MISSION_LOCAL, Packets::Scripts::LaunchMissionLocal* pLaunchMissionLocal)
{
	if (CLocalPlayer::m_bIsHost || CMissionMode::IsIndividual())
		return;

	if (pLaunchMissionLocal->missionId < 0 || pLaunchMissionLocal->missionId > 200)
		return;

	if (COpCodeSync::IsLocalMissionRunning())
		return;

	CPlayerPed* pPlayer = FindPlayerPed(0);
	if (!pPlayer || pPlayer->m_fHealth <= 0.0f)
		return;

	if (CWorld::Players[0].m_nPlayerState == PLAYERSTATE_HASDIED ||
		CWorld::Players[0].m_nPlayerState == PLAYERSTATE_HASBEENARRESTED)
		return;

	Command<Commands::LOAD_AND_LAUNCH_MISSION_INTERNAL>(pLaunchMissionLocal->missionId);

	if (CTheScripts::OnAMissionFlag != 0)
		CTheScripts::ScriptSpace[CTheScripts::OnAMissionFlag] = 1;

	CChat::AddMessage("{c7a6ff}[Coop]{d6dce8} Mision individual: cada uno juega la suya.");
}

PACKET_HANDLER(ePacketType::ON_MISSION_FLAG_SYNC, Packets::Scripts::OnMissionFlagSync* pOnMissionFlagSync)
{
	CSaveMenu::ms_bHostOnMission = pOnMissionFlagSync->bOnMission;

	if (CCoopMission::IsScriptAuthority() || IsOutOfHostMission())
		return;

	if (COpCodeSync::IsLocalMissionRunning())
		return; // playing an individual mission

	if (CTheScripts::OnAMissionFlag)
	{
        bool bWasOnMission = (bool)CTheScripts::ScriptSpace[CTheScripts::OnAMissionFlag];
		CTheScripts::ScriptSpace[CTheScripts::OnAMissionFlag] = pOnMissionFlagSync->bOnMission;
        if (bWasOnMission && pOnMissionFlagSync->bOnMission == false)
		{
			ClearHostMissionState();
			CMissionMirror::OnHostMissionEnded(false);
		}
	}
}

// E: the host left in the middle of a coop mission, the server aborted it for everyone
PACKET_HANDLER(ePacketType::MISSION_ABORTED, Packets::Scripts::MissionAborted* pMissionAborted)
{
	if (CLocalPlayer::m_bIsHost)
		return;

	if (COpCodeSync::IsLocalMissionRunning())
		return; // our own individual mission is not the host's

	ClearHostMissionState();
	ClearNetworkMissionHud();
	CMissionMirror::OnHostMissionEnded(true);

	CHud::SetHelpMessage("El host se desconecto: mision cancelada.", true, false, false);
	CChat::AddMessage("{c7a6ff}[Coop]{d6dce8} El host se desconecto: mision cancelada.");
}

PACKET_HANDLER(ePacketType::ENEX_SYNC, Packets::Scripts::EnExSync* pEnExSync)
{
	if (CLocalPlayer::m_bIsHost)
		return;

	CEntryExitMarkerSync::Receive(*pEnExSync);
}

PACKET_HANDLER(ePacketType::ADD_MESSAGE_GXT, Packets::Scripts::AddMessageGXT* pAddMessageGXT)
{
	if (CCoopMission::IsScriptAuthority() || IsOutOfHostMission())
		return;

	char gxt[8];
	strncpy_s(gxt, pAddMessageGXT->gxt, 8);
	gxt[7] = '\0';

	// the host's texts are also synced as opcodes; show each one once
	int kind = pAddMessageGXT->type == Packets::Scripts::AddMessageGXT::eGXTMsgType::sync_COMMAND_PRINT_BIG    ? 1
	           : pAddMessageGXT->type == Packets::Scripts::AddMessageGXT::eGXTMsgType::sync_COMMAND_PRINT_HELP ? 2
	                                                                                                           : 0;
	if (!COpCodeSync::ShouldShowNetworkText(gxt, kind, COpCodeSync::NETWORK_TEXT_FROM_PACKET))
	{
		CMissionMirror::LogEventf("rx-message type=%d key=%.8s dropped=dup", (int)pAddMessageGXT->type, gxt);
		return;
	}
	CMissionMirror::LogEventf("rx-message type=%d key=%.8s time=%u", (int)pAddMessageGXT->type, gxt, pAddMessageGXT->time);

	if (pAddMessageGXT->type == Packets::Scripts::AddMessageGXT::eGXTMsgType::sync_COMMAND_PRINT)
	{
		Command<Commands::PRINT>(gxt, pAddMessageGXT->time, pAddMessageGXT->flag);
	}
	else if (pAddMessageGXT->type == Packets::Scripts::AddMessageGXT::eGXTMsgType::sync_COMMAND_PRINT_BIG)
	{
		Command<Commands::PRINT_BIG>(gxt, pAddMessageGXT->time, pAddMessageGXT->flag);
	}
	else if (pAddMessageGXT->type == Packets::Scripts::AddMessageGXT::eGXTMsgType::sync_COMMAND_PRINT_NOW)
	{
		Command<Commands::PRINT_NOW>(gxt, pAddMessageGXT->time, pAddMessageGXT->flag);
	}
	else if (pAddMessageGXT->type == Packets::Scripts::AddMessageGXT::eGXTMsgType::sync_COMMAND_PRINT_HELP)
	{
		Command<Commands::PRINT_HELP>(gxt);
	}
	//CChat::AddMessage("%s %d %d %d", gxt, pAddMessageGXT->type, pAddMessageGXT->time, pAddMessageGXT->flag);
}

PACKET_HANDLER(ePacketType::REMOVE_MESSAGE_GXT, Packets::Scripts::RemoveMessageGXT* pRemoveMessageGXT)
{
	if (CCoopMission::IsScriptAuthority() || IsOutOfHostMission())
		return;

	char gxt[8];
	strncpy_s(gxt, pRemoveMessageGXT->gxt, 8);
	gxt[7] = '\0';

	Command<Commands::CLEAR_THIS_PRINT>(gxt);
	Command<Commands::CLEAR_THIS_BIG_PRINT>(gxt);
}

#if 0 // controlled with SCM
PACKET_HANDLER(ePacketType::START_CUTSCENE, Packets::Scripts::StartCutscene* pStartCutscene)
{
	if (CCutsceneMgr::ms_cutsceneLoadStatus == 2)
	{
		CCutsceneMgr::FinishCutscene();
		CCutsceneMgr::DeleteCutsceneData();
	}
	if (CCutsceneMgr::ms_cutsceneLoadStatus != 2)
	{
		CCutsceneMgr::LoadCutsceneData(pStartCutscene->name);
	}
	CGame::currArea = pStartCutscene->currArea;
	CCutsceneMgr::StartCutscene();
}

#endif

PACKET_HANDLER(ePacketType::SKIP_CUTSCENE, Packets::Scripts::SkipCutscene* pSkipCutscene)
{
	if (IsOutOfHostMission())
		return;

	CCutsceneSkipVote::HandlePacket(pSkipCutscene);
}

PACKET_HANDLER(ePacketType::PLAY_MISSION_AUDIO, Packets::Scripts::PlayMissionAudio* pPlayMissionAudio)
{	
	if (CCoopMission::IsScriptAuthority() || IsOutOfHostMission())
		return;

	MissionAudioHooks::HandleNetworkAudio(pPlayMissionAudio->slotid, pPlayMissionAudio->audioid, pPlayMissionAudio->preloadOnly);
}

static bool bTeleportAreaPending = false;
static DWORD nTeleportAreaReceived = 0;
static playerid_t teleportAreaOwner;
static CVector teleportAreaPosition;

static CNetworkPlayer* GetTeleportAuthority()
{
    const playerid_t authorityId = CCoopMission::IsRunning() ? CCoopMission::GetOwnerId() : playerid_t{};
    for (auto remote : CNetworkPlayerManager::m_pPlayers)
        if (authorityId.valid() ? remote->m_playerId == authorityId : remote->m_bIsHost)
            return remote;
    return nullptr;
}

// SET_CHAR_AREA_VISIBLE cleanup (gta-reversed CharacterCommands.cpp).
static void ApplyAuthorityArea(CPlayerPed* player, unsigned char area, const char* source)
{
    if (player->m_nAreaCode != area)
        CMissionMirror::LogEventf("teleport-area from=%d to=%d source=%s", (int)player->m_nAreaCode, (int)area, source);
    player->m_nAreaCode = area;
    if (area != 0)
        return;
    player->m_pEnex = nullptr;
    CEntryExitManager::ms_entryExitStackPosn = 0;
    CTimeCycle::StopExtraColour(false);
}

// Moved into the authority's interior: this game shows and streams it first, as CEntryExit's transition does (gta-reversed
// Streaming.cpp LoadScene 0x40ED10). Without it the interior had no collision here, the ped fell out of it and the game put
// him back on the street with the interior's empty view (Home Invasion, 2026-11-12)
// Same order as CEntryExit::TransitionFinished (EntryExit.cpp:408-446): the ped is already there, the camera jumps to him
// and only then the scene loads, so the next streaming update does not drop the interior for the old camera spot.
// Still the guest fell through the house with only its door drawn (Home Invasion, 2026-11-13): the fall guard below puts
// him back and loads it again.
static CVector s_interiorGuardPos;
static DWORD s_interiorGuardUntil = 0;
static int s_interiorGuardTries = 0;
static unsigned char s_interiorGuardArea = 0;

static void LoadAuthorityInterior(CPlayerPed* player, unsigned char area, const CVector& pos)
{
    if (!CConfigLoader::ms_bTeleportLoadInterior || area == 0)
        return;
    if (CGame::currArea != area)
        CMissionMirror::LogEventf("teleport-load-interior area=%d at=%.1f,%.1f,%.1f", (int)area, pos.x, pos.y, pos.z);
    CGame::currArea = area;
    TheCamera.RestoreWithJumpCut();
    CStreaming::LoadScene(&pos);
    CStreaming::LoadAllRequestedModels(false);
    player->m_vecMoveSpeed = CVector(0.0f, 0.0f, 0.0f);
    s_interiorGuardPos = pos;
    s_interiorGuardArea = area;
    s_interiorGuardUntil = GetTickCount() + 6000;
    s_interiorGuardTries = 0;
}

static void GuardInteriorFall()
{
    if (!s_interiorGuardUntil)
        return;
    CPlayerPed* player = FindPlayerPed(0);
    if (GetTickCount() > s_interiorGuardUntil || !player || player->m_nAreaCode != s_interiorGuardArea ||
        player->m_fHealth <= 0.0f || player->m_nPedFlags.bInVehicle)
    {
        s_interiorGuardUntil = 0;
        return;
    }
    if (player->GetPosition().z > s_interiorGuardPos.z - 4.0f)
        return;
    CMissionMirror::LogEventf("teleport-interior-fall area=%d z=%.1f want=%.1f try=%d currArea=%d", (int)s_interiorGuardArea,
        player->GetPosition().z, s_interiorGuardPos.z, s_interiorGuardTries, (int)CGame::currArea);
    if (++s_interiorGuardTries > 3)
    {
        s_interiorGuardUntil = 0;
        return;
    }
    CGame::currArea = s_interiorGuardArea;
    player->Teleport(s_interiorGuardPos, false);
    TheCamera.RestoreWithJumpCut();
    CStreaming::LoadScene(&s_interiorGuardPos);
    CStreaming::LoadAllRequestedModels(false);
    player->m_vecMoveSpeed = CVector(0.0f, 0.0f, 0.0f);
    s_interiorGuardUntil = GetTickCount() + 6000;
}

void CCommandTeleportPlayersToHostSafely::ClearPendingTeleport()
{
    bTeleportAreaPending = false;
}

void CCommandTeleportPlayersToHostSafely::ProcessPendingTeleport()
{
    GuardInteriorFall();
    if (!bTeleportAreaPending)
        return;
    CPlayerPed* player = FindPlayerPed(0);
    CNetworkPlayer* authority = GetTeleportAuthority();
    if (!CNetwork::m_bConnected || !CConfigLoader::ms_bMissionSyncRules ||
        IsOutOfHostMission() || CCoopMission::HasLocalLeft() || CCoopMission::IsScriptAuthority() ||
        COpCodeSync::IsLocalMissionRunning() ||
        (CCoopMission::IsActive() && !CCoopMission::IsLocalParticipant()) ||
        !CTheScripts::OnAMissionFlag || !CTheScripts::ScriptSpace[CTheScripts::OnAMissionFlag] ||
        !authority || !authority->m_pPed || authority->m_playerId != teleportAreaOwner ||
        !player || player->m_fHealth <= 0.0f || CWorld::Players[0].m_nPlayerState == PLAYERSTATE_HASDIED ||
        CWorld::Players[0].m_nPlayerState == PLAYERSTATE_HASBEENARRESTED ||
        player->m_nPedFlags.bInVehicle)
    {
        ClearPendingTeleport();
        return;
    }
    if (COpCodeSync::bProcessingNetworkOpcode || GetTickCount() - nTeleportAreaReceived < 1200)
        return;
    ClearPendingTeleport();
    // his area is only synced on foot
    if (authority->m_pPed->m_nPedFlags.bInVehicle)
        return;
    const unsigned char area = authority->m_pPed->m_nAreaCode;
    if (player->m_nAreaCode == area)
        return;
    ApplyAuthorityArea(player, area, "recheck");
    player->Teleport(teleportAreaPosition, false);
    LoadAuthorityInterior(player, area, teleportAreaPosition);
    player->UpdateRwMatrix();
}

PACKET_HANDLER(ePacketType::TELEPORT_PLAYER_SCRIPTED, Packets::Scripts::TeleportPlayerScripted* pTeleportPlayerScripted)
{
    CCommandTeleportPlayersToHostSafely::ClearPendingTeleport();
    auto pPlayerPed = FindPlayerPed(0);
    if (!pPlayerPed || IsOutOfHostMission() || CCoopMission::HasLocalLeft() ||
        !CNetwork::m_bConnected || CCoopMission::IsScriptAuthority() || COpCodeSync::bProcessingNetworkOpcode ||
        COpCodeSync::IsLocalMissionRunning() ||
        (CCoopMission::IsActive() && !CCoopMission::IsLocalParticipant()) ||
        pTeleportPlayerScripted->playerid != CNetworkPlayerManager::m_localPlayerId ||
        pPlayerPed->m_fHealth <= 0.0f || CWorld::Players[0].m_nPlayerState == PLAYERSTATE_HASDIED ||
        CWorld::Players[0].m_nPlayerState == PLAYERSTATE_HASBEENARRESTED)
        return;
    ApplyAuthorityArea(pPlayerPed, pTeleportPlayerScripted->area, "packet");
    pPlayerPed->Teleport(pTeleportPlayerScripted->pos, false);
    pPlayerPed->m_fCurrentRotation = pTeleportPlayerScripted->heading.m_angle;
    pPlayerPed->m_fAimingRotation = pTeleportPlayerScripted->heading.m_angle;
    pPlayerPed->SetHeading(pTeleportPlayerScripted->heading.m_angle);
    pPlayerPed->UpdateRwMatrix();
    LoadAuthorityInterior(pPlayerPed, pTeleportPlayerScripted->area, pTeleportPlayerScripted->pos);

    // A later area opcode can arrive after the move; check the synced ped once.
    CNetworkPlayer* authority = GetTeleportAuthority();
    if (CConfigLoader::ms_bMissionSyncRules && authority && authority->m_pPed)
    {
        teleportAreaOwner = authority->m_playerId;
        teleportAreaPosition = pTeleportPlayerScripted->pos;
        nTeleportAreaReceived = GetTickCount();
        bTeleportAreaPending = true;
    }
}

static bool bBoardingPending = false;
static DWORD nBoardingReceived = 0;
static playerid_t boardingOwner;
static Packets::Scripts::PutInVehicleScripted pendingBoarding{};
// the native passenger entry we started: its timer warps us in when it runs out (gta-reversed
// TaskComplexEnterCarAsPassengerTimed.cpp, ControlSubTask), so it is cancelled if the car goes away meanwhile
static int nWalkingVehicleRef = -1;
static DWORD nWalkingStarted = 0;
static constexpr float BOARDING_WALK_CANCEL_RADIUS = 25.0f;

static bool HasWalkingBoardingTask(CPlayerPed* player)
{
    return player && player->m_fHealth > 0.0f && player->m_pIntelligence &&
        player->m_pIntelligence->m_TaskMgr.FindActiveTaskByType(TASK_COMPLEX_ENTER_CAR_AS_PASSENGER_TIMED);
}

static void CancelWalkingBoarding()
{
    if (nWalkingVehicleRef == -1)
        return;
    nWalkingVehicleRef = -1;
    CPlayerPed* player = FindPlayerPed(0);
    if (HasWalkingBoardingTask(player) && !player->m_nPedFlags.bInVehicle)
        Command<Commands::CLEAR_CHAR_TASKS>(CPools::GetPedRef(player));
}

static void ProcessWalkingBoarding()
{
    if (nWalkingVehicleRef == -1)
        return;
    CPlayerPed* player = FindPlayerPed(0);
    if (!CNetwork::m_bConnected || CMissionMode::IsIndividual() || !HasWalkingBoardingTask(player) ||
        player->m_nPedFlags.bInVehicle || CTimer::m_snTimeInMilliseconds - nWalkingStarted > 12000)
    {
        nWalkingVehicleRef = -1;
        return;
    }
    CVehicle* vehicle = CPools::GetVehicle(nWalkingVehicleRef);
    if (!vehicle || DistanceBetweenPoints(player->GetPosition(), vehicle->GetPosition()) > BOARDING_WALK_CANCEL_RADIUS)
    {
        CancelWalkingBoarding();
        CMissionMirror::LogEventf("rx-boarding cancelled: the car went away");
    }
}

static void ClearGuestWarps();

void CCommandPutNetworkPlayersInCar::ClearPendingBoarding()
{
    bBoardingPending = false;
    ClearGuestWarps();
    CancelWalkingBoarding();
}

static std::vector<Packets::Scripts::PutInVehicleScripted> guestWarps;
static std::vector<DWORD> guestWarpTimes;
static playerid_t guestWarpOwner;

static void ClearGuestWarps()
{
    guestWarps.clear();
    guestWarpTimes.clear();
}

static void ProcessGuestWarps()
{
    CPlayerPed* player = FindPlayerPed(0);
    bool authorityPresent = false;
    for (auto remote : CNetworkPlayerManager::m_pPlayers)
        if (guestWarpOwner.valid() ? remote->m_playerId == guestWarpOwner : remote->m_bIsHost)
            authorityPresent = remote->m_pPed && remote->m_pPed->m_fHealth > 0.0f;
    if (!authorityPresent || !CNetwork::m_bConnected || CMissionMode::IsIndividual() || IsOutOfHostMission() ||
        CCoopMission::HasLocalLeft() || COpCodeSync::IsLocalMissionRunning() ||
        guestWarpOwner != CCoopMission::GetOwnerId() || !CConfigLoader::ms_bMissionSyncRules ||
        (CCoopMission::IsActive() && !CCoopMission::IsLocalParticipant()) ||
        !CTheScripts::OnAMissionFlag || !CTheScripts::ScriptSpace[CTheScripts::OnAMissionFlag] ||
        !player || player->m_fHealth <= 0.0f)
    {
        ClearGuestWarps();
        return;
    }
    for (size_t i = 0; i < guestWarps.size();)
    {
        const auto& packet = guestWarps[i];
        auto network = CNetworkVehicleManager::GetVehicle(packet.vehicleid);
        CVehicle* vehicle = network ? network->m_pVehicle : nullptr;
        bool done = GetTickCount() - guestWarpTimes[i] >= 8000;
        if (!done && vehicle && !player->m_nPedFlags.bInVehicle)
        {
            const int seat = packet.seat;
            done = vehicle->m_fHealth <= 0.0f || vehicle->m_nAreaCode != player->m_nAreaCode;
            if (!done && seat == CCommandPutNetworkPlayersInCar::GUEST_DRIVER_SEAT && !vehicle->m_pDriver)
            {
                Command<Commands::WARP_CHAR_INTO_CAR>(CPools::GetPedRef(player), CPools::GetVehicleRef(vehicle));
                done = true;
            }
            else if (!done && seat >= 0 && seat < vehicle->m_nMaxPassengers && seat < 8 && !vehicle->m_apPassengers[seat])
            {
                Command<Commands::WARP_CHAR_INTO_CAR_AS_PASSENGER>(CPools::GetPedRef(player), CPools::GetVehicleRef(vehicle), seat);
                done = true;
            }
        }
        if (done)
        {
            guestWarps.erase(guestWarps.begin() + i);
            guestWarpTimes.erase(guestWarpTimes.begin() + i);
        }
        else ++i;
    }
}

void CCommandPutNetworkPlayersInCar::ProcessPendingBoarding()
{
    ProcessGuestWarps();
    ProcessWalkingBoarding();
    if (!bBoardingPending)
        return;
    CPlayerPed* player = FindPlayerPed(0);
    bool authorityPresent = false;
    for (auto remote : CNetworkPlayerManager::m_pPlayers)
        if (boardingOwner.valid() ? remote->m_playerId == boardingOwner : remote->m_bIsHost)
            authorityPresent = remote->m_pPed != nullptr;
    if (!authorityPresent || COpCodeSync::IsLocalMissionRunning() || !CConfigLoader::ms_bMissionSyncRules ||
        !CConfigLoader::ms_bMissionScriptedBoarding ||
        !CNetwork::m_bConnected || IsOutOfHostMission() || CCoopMission::HasLocalLeft() ||
        CCoopMission::IsScriptAuthority() || boardingOwner != CCoopMission::GetOwnerId() ||
        (CCoopMission::IsActive() && !CCoopMission::IsLocalParticipant()) ||
        !CTheScripts::OnAMissionFlag || !CTheScripts::ScriptSpace[CTheScripts::OnAMissionFlag] ||
        !player || player->m_fHealth <= 0.0f || player->m_nPedFlags.bInVehicle)
    {
        bBoardingPending = false;
        return;
    }
    CNetworkVehicle* network = CNetworkVehicleManager::GetVehicle(pendingBoarding.vehicleid);
    CVehicle* vehicle = network ? network->m_pVehicle : nullptr;
    const int seat = pendingBoarding.seat;
    if (!vehicle)
    {
        if (CTimer::m_snTimeInMilliseconds - nBoardingReceived >= 1500)
            bBoardingPending = false;
        return;
    }
    bBoardingPending = false;
    if (vehicle->m_fHealth <= 0.0f || vehicle->m_nAreaCode != player->m_nAreaCode ||
        seat < 0 || seat >= vehicle->m_nMaxPassengers || seat >= 8 || vehicle->m_apPassengers[seat])
        return;
    // mission-nines-board-near: still far from the car, we first appear at its door (on the ground, facing it) and walk in
    if (CConfigLoader::ms_bMissionNinesBoardNear && CMissionMirror::GetRunningMissionId() == 16 &&
        DistanceBetweenPoints(player->GetPosition(), vehicle->GetPosition()) > 6.0f)
    {
        const int door = CCarEnterExit::ComputeTargetDoorToEnterAsPassenger(vehicle, seat);
        CVector doorPos{};
        if (door >= 0)
            CCarEnterExit::GetPositionToOpenCarDoor(&doorPos, vehicle, door);
        const CVector center = vehicle->GetPosition();
        CVector outward = doorPos - center;
        outward.z = 0.0f;
        if (door < 0 || outward.Magnitude() < 0.1f)
            outward = vehicle->GetRight() * ((seat % 2) ? -1.0f : 1.0f);
        outward.Normalise();
        bool placed = false;
        for (const float extra : {0.8f, 1.6f, 2.4f})
        {
            CVector spot = (door >= 0 ? doorPos : center) + outward * extra;
            bool foundGround = false;
            const float groundZ = CWorld::FindGroundZFor3DCoord(spot.x, spot.y, center.z + 2.0f, &foundGround, nullptr);
            if (!foundGround || fabsf(groundZ - center.z) > 2.0f ||
                CWorld::TestSphereAgainstWorld(CVector(spot.x, spot.y, groundZ + 0.9f), 0.5f, vehicle, true, true, true, true, false, false))
                continue;
            spot.z = groundZ + 1.0f;
            player->Teleport(spot, false);
            const float heading = atan2f(-(center.x - spot.x), center.y - spot.y);
            player->m_fCurrentRotation = heading;
            player->m_fAimingRotation = heading;
            player->SetHeading(heading);
            player->UpdateRwMatrix();
            placed = true;
            break;
        }
        CMissionMirror::LogEventf("rx-boarding near-door seat=%d door=%d placed=%d", seat, door, placed ? 1 : 0);
        if (!placed)
            return;  // no free ground at the car: the rule of the other guests stays (nobody warped in)
    }
    // 05CA accepts a passenger index; the game converts it to a door (0x4903AB -> 0x64F190).
    CMissionMirror::LogEventf("rx-boarding walk=1 seat=%d", seat);
    Command<Commands::TASK_ENTER_CAR_AS_PASSENGER>(CPools::GetPedRef(player), CPools::GetVehicleRef(vehicle), 10000, seat);
    nWalkingVehicleRef = CPools::GetVehicleRef(vehicle);
    nWalkingStarted = CTimer::m_snTimeInMilliseconds;
}

PACKET_HANDLER(ePacketType::PUT_IN_VEHICLE_SCRIPTED, Packets::Scripts::PutInVehicleScripted* pPutInVehicleScripted)
{
	if (pPutInVehicleScripted->playerid != CNetworkPlayerManager::m_localPlayerId || IsOutOfHostMission())
		return;

	CPlayerPed* pPlayerPed = FindPlayerPed(0);
	if (!pPlayerPed)
		return;
    if (CConfigLoader::ms_bMissionSyncRules && CConfigLoader::ms_bMissionScriptedBoarding &&
        (CCoopMission::HasLocalLeft() || COpCodeSync::IsLocalMissionRunning() ||
         (CCoopMission::IsActive() && !CCoopMission::IsLocalParticipant()) || pPlayerPed->m_fHealth <= 0.0f ||
         (pPutInVehicleScripted->seat == CCommandPutNetworkPlayersInCar::PARKED_COPY_SEAT && pPlayerPed->m_nPedFlags.bInVehicle)))
        return;

    if (pPutInVehicleScripted->seat == CCommandPutNetworkPlayersInCar::GUEST_DRIVER_SEAT ||
        (pPutInVehicleScripted->model == 0 && pPutInVehicleScripted->seat >= 0 && !pPutInVehicleScripted->walk))
    {
        if (CMissionMode::IsIndividual() || CCoopMission::HasLocalLeft() || pPlayerPed->m_fHealth <= 0.0f) return;
        if (guestWarpOwner != CCoopMission::GetOwnerId()) ClearGuestWarps();
        guestWarpOwner = CCoopMission::GetOwnerId();
        guestWarps.push_back(*pPutInVehicleScripted);
        guestWarpTimes.push_back(GetTickCount());
        return;
    }

    if (pPutInVehicleScripted->walk)
    {
        if (!CConfigLoader::ms_bMissionSyncRules || !CConfigLoader::ms_bMissionScriptedBoarding ||
            pPutInVehicleScripted->seat < 0 || pPlayerPed->m_nPedFlags.bInVehicle)
            return;
        pendingBoarding = *pPutInVehicleScripted;
        boardingOwner = CCoopMission::GetOwnerId();
        nBoardingReceived = CTimer::m_snTimeInMilliseconds;
        bBoardingPending = true;
        return;
    }

	// the authority's objective vehicle (a bike with no seat for us): an equal one parked where it says, we get on it
	if (pPutInVehicleScripted->seat == CCommandPutNetworkPlayersInCar::PARKED_COPY_SEAT)
	{
		const int model = pPutInVehicleScripted->model;
		if (!IsVehicleModel(model))
			return;
		const bool wasRequired = CStreaming::ms_aInfoForModel[model].m_nFlags & GAME_REQUIRED;
		CStreaming::RequestModel(model, eStreamingFlags::GAME_REQUIRED);
		CStreaming::LoadAllRequestedModels(false);

		const CVector pos = pPutInVehicleScripted->pos;
		int vehicleHandle = 0;
		Command<Commands::CREATE_CAR>(model, pos.x, pos.y, pos.z, &vehicleHandle);
		ReleaseModelIfWeRequested(model, wasRequired);
		if (vehicleHandle == 0)
			return;
		Command<Commands::SET_CAR_HEADING>(vehicleHandle, pPutInVehicleScripted->heading);
		Command<Commands::CHANGE_CAR_COLOUR>(vehicleHandle, pPutInVehicleScripted->color1, pPutInVehicleScripted->color2);
		CCommandPutNetworkPlayersInCar::AddParkedCopy(vehicleHandle);
		CMissionMirror::LogEventf("rx-parked-copy model=%d at=%.1f,%.1f,%.1f", model, pos.x, pos.y, pos.z);
		return;
	}

	if (pPlayerPed->m_nPedFlags.bInVehicle)
		return;

	if (pPutInVehicleScripted->seat >= 0)
	{
		CNetworkVehicle* pNetworkVehicle = CNetworkVehicleManager::GetVehicle(pPutInVehicleScripted->vehicleid);
		CVehicle* pVehicle = pNetworkVehicle ? pNetworkVehicle->m_pVehicle : nullptr;

		if (pVehicle && pPutInVehicleScripted->seat < pVehicle->m_nMaxPassengers &&
			pVehicle->m_apPassengers[pPutInVehicleScripted->seat] == nullptr)
		{
			Command<Commands::WARP_CHAR_INTO_CAR_AS_PASSENGER>(CPools::GetPedRef(pPlayerPed), CPools::GetVehicleRef(pVehicle),
				pPutInVehicleScripted->seat);
			return;
		}
	}

	// no free passenger seat (or no room to reach it): an equal car of our own
	int model = pPutInVehicleScripted->model;
	if (!IsVehicleModel(model))
		return;
	const bool wasRequired = CStreaming::ms_aInfoForModel[model].m_nFlags & GAME_REQUIRED;
	CStreaming::RequestModel(model, eStreamingFlags::GAME_REQUIRED);
	CStreaming::LoadAllRequestedModels(false);

	const CVector pos = pPutInVehicleScripted->pos;
	int vehicleHandle = 0;
	Command<Commands::CREATE_CAR>(model, pos.x, pos.y, pos.z, &vehicleHandle);
	ReleaseModelIfWeRequested(model, wasRequired);
	if (vehicleHandle == 0)
		return;

	Command<Commands::SET_CAR_HEADING>(vehicleHandle, pPutInVehicleScripted->heading);
	Command<Commands::CHANGE_CAR_COLOUR>(vehicleHandle, pPutInVehicleScripted->color1, pPutInVehicleScripted->color2);
	Command<Commands::WARP_CHAR_INTO_CAR>(CPools::GetPedRef(pPlayerPed), vehicleHandle);
	// safe to release right away: it becomes a RANDOM_VEHICLE, but CVehicle::CanBeDeleted() is false while the
	// driver/passenger can't be deleted (CPed::CanBeDeletedEvenInVehicle: the player is PED_MISSION) and
	// CCarCtrl::RemoveDistantCars skips FindPlayerVehicle(); once the player leaves it, the population code removes it
	// like any other random car when it is far away / off screen
	Command<Commands::MARK_CAR_AS_NO_LONGER_NEEDED>(vehicleHandle);
}

static void ApplyOddjobReward(Packets::Scripts::OddjobReward* pPacket)
{
	if (pPacket->playerid != CNetworkPlayerManager::m_localPlayerId)
		return;

	int* pDone = nullptr;
	// a global variable of main.scm: aligned and inside the script space
	if (pPacket->doneFlagOffset >= 0 && pPacket->doneFlagOffset % 4 == 0 &&
		pPacket->doneFlagOffset <= 200000 - (int)sizeof(int))
		pDone = (int*)&CTheScripts::ScriptSpace[pPacket->doneFlagOffset];

	if (pDone && *pDone != 0)
		return;

	CPlayerInfo& info = CWorld::Players[0];
	CPlayerPed* ped = FindPlayerPed(0);

	switch (pPacket->rewardId)
	{
	case 1:
		info.m_nMaxArmour += 50;
		if (ped)
			ped->m_fArmour = (float)info.m_nMaxArmour;
		break;
	case 2:
		info.m_bDoesNotGetTired = true;
		break;
	case 3:
		info.m_bFireProof = true;
		break;
	default:
		return;
	}

	CStats::IncrementStat(STAT_PROGRESS_MADE, 1.0f);
	if (pDone)
		*pDone = 1;
	CChat::AddMessage("{c7a6ff}[Coop]{d6dce8} Reward unlocked!");
}

static bool IsValidOnscreenVarOffset(int32_t offset)
{
    return offset > 0 && offset <= 200000 - (int32_t)sizeof(int) && offset % sizeof(int) == 0;
}

static void ApplyNetworkClock(Packets::Scripts::OnscreenTimerSync* pPacket)
{
    static bool bClockFromNetwork = false;
    static uint32_t networkClockVarId = 0;

    COnscreenTimer& timer = CUserDisplay::OnscnTimer;

    if (!pPacket->enabled || !IsValidOnscreenVarOffset(pPacket->varOffset))
    {
        if (bClockFromNetwork && timer.m_Clock.m_nVarId == networkClockVarId)
            timer.ClearClock(networkClockVarId);

        bClockFromNetwork = false;
        return;
    }

    // a clock that isn't ours is on screen
    if (timer.m_Clock.m_nVarId != 0 && !bClockFromNetwork)
        return;

    *(int*)&CTheScripts::ScriptSpace[pPacket->varOffset] = pPacket->value;

    if (!bClockFromNetwork || timer.m_Clock.m_nVarId != (uint32_t)pPacket->varOffset)
    {
        if (bClockFromNetwork && timer.m_Clock.m_nVarId == networkClockVarId)
            timer.ClearClock(networkClockVarId);

        timer.AddClock(pPacket->varOffset, pPacket->textKey, pPacket->direction != 0);
        networkClockVarId = pPacket->varOffset;
        bClockFromNetwork = true;
    }

    timer.m_bPaused = pPacket->paused;
}

static void ApplyNetworkCounter(int index, Packets::Scripts::SOnscreenCounterState& state)
{
    static bool abCounterFromNetwork[4] = {};
    static uint32_t aNetworkCounterVarId[4] = {};

    COnscreenCounterEntry& counter = CUserDisplay::OnscnTimer.m_aCounters[index];

    if (!state.enabled || !IsValidOnscreenVarOffset(state.varOffset))
    {
        if (abCounterFromNetwork[index] && counter.m_nVarId == aNetworkCounterVarId[index])
            CUserDisplay::OnscnTimer.ClearCounter(aNetworkCounterVarId[index]);

        abCounterFromNetwork[index] = false;
        return;
    }

    // a counter that isn't ours uses this slot
    if (counter.m_nVarId != 0 && !abCounterFromNetwork[index])
        return;

    *(int*)&CTheScripts::ScriptSpace[state.varOffset] = state.value;

    if (!abCounterFromNetwork[index] || counter.m_nVarId != (uint32_t)state.varOffset)
    {
        if (abCounterFromNetwork[index] && counter.m_nVarId == aNetworkCounterVarId[index])
            CUserDisplay::OnscnTimer.ClearCounter(aNetworkCounterVarId[index]);

        CUserDisplay::OnscnTimer.AddCounter(state.varOffset, state.type, state.textKey, (unsigned short)index);
        aNetworkCounterVarId[index] = state.varOffset;
        abCounterFromNetwork[index] = true;
    }

    counter.m_nColourId = state.colour;
}

// drops the clock/counters that were coming from the host's mission (the mission ended or he left)
void ClearNetworkMissionHud()
{
    Packets::Scripts::OnscreenTimerSync packet{};
    ApplyNetworkClock(&packet);
    for (int i = 0; i < 4; i++)
        ApplyNetworkCounter(i, packet.counters[i]);
}

PACKET_HANDLER(ePacketType::ONSCREEN_TIMER_SYNC, Packets::Scripts::OnscreenTimerSync* pOnscreenTimerSync)
{
    if (CCoopMission::IsScriptAuthority() || IsOutOfHostMission())
        return;

    // playing an individual mission, don't touch its globals or HUD
    if (COpCodeSync::IsLocalMissionRunning())
        return;

    // he left the coop mission: no clock or counters of it
    if (CCoopMission::HasLocalLeft())
    {
        ClearNetworkMissionHud();
        return;
    }

    ApplyNetworkClock(pOnscreenTimerSync);

    for (int i = 0; i < 4; i++)
        ApplyNetworkCounter(i, pOnscreenTimerSync->counters[i]);
}

PACKET_HANDLER(ePacketType::ODDJOB_REWARD, Packets::Scripts::OddjobReward* pOddjobReward)
{
	ApplyOddjobReward(pOddjobReward);
}

PACKET_HANDLER(ePacketType::OPCODE_SYNC, Packets::Scripts::OpCodeSync* pOpCodeSync)
{
	if (IsOutOfHostMission())
		return;

	COpCodeSync::HandlePacket(pOpCodeSync->buffer, pOpCodeSync->size);
}


PACKET_HANDLER(ePacketType::PERFORM_TASK_SEQUENCE, Packets::Scripts::PerformTaskSequence* pPerformTaskSequence)
{
	CTaskSequenceSync::HandlePacket(pPerformTaskSequence->buffer, pPerformTaskSequence->size);
}

PACKET_HANDLER(ePacketType::MINIGAME_VS_SCORE, Packets::Scripts::MinigameVsScore* pMinigameVsScore)
{
	if (pMinigameVsScore->gameId == CVsRace::PROGRESS_GAME_ID)
	{
		CVsRace::OnProgress(*pMinigameVsScore);
		return;
	}
	if (pMinigameVsScore->gameId >= CVsRace::NPC_GAME_ID && pMinigameVsScore->gameId < CVsRace::NPC_GAME_ID + CVsRace::NPC_SLOTS)
	{
		CVsRace::OnNpcProgress(*pMinigameVsScore);
		return;
	}
	CMinigameVs::OnPacket(*pMinigameVsScore);
}

PACKET_HANDLER(ePacketType::RACE_VS_LOBBY, Packets::Scripts::RaceVsLobby* pRaceVsLobby)
{
	CVsLobby::OnPacket(*pRaceVsLobby);
}

PACKET_HANDLER(ePacketType::COOP_LOWRIDER, Packets::Scripts::CoopLowrider* packet)
{
    CCoopLowrider::OnPacket(*packet);
}

PACKET_HANDLER(ePacketType::COOP_POOL, Packets::Scripts::CoopPool* packet)
{
    CCoopPool::OnPacket(*packet);
}

PACKET_HANDLER(ePacketType::ACTIVITY_CHALLENGE, Packets::Scripts::ActivityChallenge* p)
{
    CActivityChallenge::OnPacket(*p);
}

PACKET_HANDLER(ePacketType::CUTSCENE_PARTNERS, Packets::Scripts::CutscenePartners* packet)
{
    CCutscenePartners::HandlePacket(*packet);
}

PACKET_HANDLER(ePacketType::SOCIAL_ACTION, Packets::Scripts::SocialAction* p) { CSocialActions::OnPacket(*p); }
PACKET_HANDLER(ePacketType::EMOTE_STATE, Packets::Scripts::EmoteState* p) { CSocialActions::OnAnimation(*p); }
PACKET_HANDLER(ePacketType::STORY_RACE, Packets::Scripts::StoryRace* packet)
{
    CHighStakesRace::OnPacket(*packet);
}
