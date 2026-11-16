#pragma once

#include "CCustomCommandMgr.h"
#include "Commands/CCommandAddChatMessage.h"
#include "Commands/CCommandGetNetworkPlayerPedHandleTo.h"
#include "Commands/CCommandCollectNetworkPlayersForTheMission.h"
#include "Commands/CCommandIsNetworkPlayerActorValid.h"
#include "Commands/CCommandUpdateCarBlipForNetworkPlayer.h"
#include "Commands/CCommandRemoveCarBlipForNetworkPlayer.h"
#include "Commands/CCommandShowTextLowPriorityForNetworkPlayer.h"
#include "Commands/CCommandShowTextStyledForNetworkPlayer.h"
#include "Commands/CCommandShowTextHighPriorityForNetworkPlayer.h"
#include "Commands/CCommandShowTextBoxForNetworkPlayer.h"
#include "Commands/CCommandRemoveTextForNetworkPlayer.h"
#include "Commands/CCommandUpdateActorBlipForNetworkPlayer.h"
#include "Commands/CCommandRemoveActorBlipForNetworkPlayer.h"
#include "Commands/CCommandClearAllEntityBlipsForNetworkPlayer.h"
#include "Commands/CCommandIsHost.h"
#include "Commands/CCommandLocateAllPlayersOnFoot3D.h"
#include "Commands/CCommandUpdateCheckpointForNetworkPlayer.h"
#include "Commands/CCommandRemoveCheckpointForNetworkPlayer.h"
#include "Commands/CCommandEnableSyncingThisScript.h"
#include "Commands/CCommandGetPedInAreaWithModel.h"
#include "Commands/CCommandClaimPedOnRelease.h"
#include "Commands/CCommandCancelPedClaim.h"
#include "Commands/CCommandIsSyncingThisPed.h"
#include "Commands/CCommandPedResetAllClaims.h"
#include "Commands/CCommandPedTakeHost.h"
#include "Commands/CCommandGetNetworkPlayerInternalId.h"
#include "Commands/CCommandTeleportPlayersToHostSafely.h"
#include "Commands/CCommandLaunchMissionForNetworkPlayers.h"
#include "Commands/CCommandMinigameVsReport.h"
#include "Commands/CCommandPutNetworkPlayersInCar.h"
#include "Commands/CCommandGrantOddjobRewardToNetworkPlayers.h"
#include "Commands/CCommandsForAllNetworkPlayers.h"
#include "Commands/CCommandChooseSideMissionMode.h"
#include "Commands/CCommandIsCoopMission.h"
#include "Commands/CCommandIsAnyNetworkPlayerInArea3D.h"

class CCustomCommandRegistrar
{
public:
	static void Register()
	{
		CCustomCommandMgr::RegisterCommand(0x1D00, new CCommandAddChatMessage());
		CCustomCommandMgr::RegisterCommand(0x1D01, new CCommandGetNetworkPlayerPedHandleTo());
		CCustomCommandMgr::RegisterCommand(0x1D02, new CCommandCollectNetworkPlayersForTheMission());
		CCustomCommandMgr::RegisterCommand(0x1D03, new CCommandIsNetworkPlayerActorValid());
		CCustomCommandMgr::RegisterCommand(0x1D04, new CCommandUpdateCarBlipForNetworkPlayer());
		CCustomCommandMgr::RegisterCommand(0x1D05, new CCommandRemoveCarBlipForNetworkPlayer());
		CCustomCommandMgr::RegisterCommand(0x1D06, new CCommandUpdateActorBlipForNetworkPlayer());
		CCustomCommandMgr::RegisterCommand(0x1D07, new CCommandRemoveActorBlipForNetworkPlayer());
		CCustomCommandMgr::RegisterCommand(0x1D08, new CCommandShowTextLowPriorityForNetworkPlayer());
		CCustomCommandMgr::RegisterCommand(0x1D09, new CCommandShowTextStyledForNetworkPlayer());
		CCustomCommandMgr::RegisterCommand(0x1D0A, new CCommandShowTextHighPriorityForNetworkPlayer());
		CCustomCommandMgr::RegisterCommand(0x1D0B, new CCommandShowTextBoxForNetworkPlayer());
		CCustomCommandMgr::RegisterCommand(0x1D0C, new CCommandRemoveTextForNetworkPlayer());
		CCustomCommandMgr::RegisterCommand(0x1D0D, new CCommandClearAllEntityBlipsForNetworkPlayer());
		CCustomCommandMgr::RegisterCommand(0x1D0E, new CCommandIsHost());
		CCustomCommandMgr::RegisterCommand(0x1D0F, new CCommandLocateAllPlayersOnFoot3D());
		CCustomCommandMgr::RegisterCommand(0x1D10, new CCommandUpdateCheckpointForNetworkPlayer());
		CCustomCommandMgr::RegisterCommand(0x1D11, new CCommandRemoveCheckpointForNetworkPlayer());
		CCustomCommandMgr::RegisterCommand(0x1D12, new CCommandEnableSyncingThisScript());
		CCustomCommandMgr::RegisterCommand(0x1D15, new CCommandGetPedInAreaWithModel());
		CCustomCommandMgr::RegisterCommand(0x1D16, new CCommandClaimPedOnRelease());
		CCustomCommandMgr::RegisterCommand(0x1D17, new CCommandCancelPedClaim());
		CCustomCommandMgr::RegisterCommand(0x1D18, new CCommandIsSyncingThisPed());
		CCustomCommandMgr::RegisterCommand(0x1D19, new CCommandPedResetAllClaims());
		CCustomCommandMgr::RegisterCommand(0x1D1A, new CCommandPedTakeHost());
		CCustomCommandMgr::RegisterCommand(0x1D1B, new CCommandGetNetworkPlayerInternalId());
		CCustomCommandMgr::RegisterCommand(0x1D1C, new CCommandTeleportPlayersToHostSafely());
		CCustomCommandMgr::RegisterCommand(0x1D1D, new CCommandGrantOddjobRewardToNetworkPlayers());
		CCustomCommandMgr::RegisterCommand(0x1D1E, new CCommandUpdateCheckpointForAllNetworkPlayers());
		CCustomCommandMgr::RegisterCommand(0x1D1F, new CCommandRemoveCheckpointForAllNetworkPlayers());
		CCustomCommandMgr::RegisterCommand(0x1D20, new CCommandUpdateCharBlipForAllNetworkPlayers());
		CCustomCommandMgr::RegisterCommand(0x1D21, new CCommandRemoveCharBlipForAllNetworkPlayers());
		CCustomCommandMgr::RegisterCommand(0x1D22, new CCommandUpdateCarBlipForAllNetworkPlayers());
		CCustomCommandMgr::RegisterCommand(0x1D23, new CCommandRemoveCarBlipForAllNetworkPlayers());
		CCustomCommandMgr::RegisterCommand(0x1D24, new CCommandClearAllEntityBlipsForAllNetworkPlayers());
		CCustomCommandMgr::RegisterCommand(0x1D25, new CCommandTeleportAllNetworkPlayersToHost());
		CCustomCommandMgr::RegisterCommand(0x1D26, new CCommandLaunchMissionForNetworkPlayers());
		CCustomCommandMgr::RegisterCommand(0x1D27, new CCommandMinigameVsReport());
		CCustomCommandMgr::RegisterCommand(0x1D28, new CCommandPutNetworkPlayersInCar());
		CCustomCommandMgr::RegisterCommand(0x1D29, new CCommandChooseSideMissionMode());
		CCustomCommandMgr::RegisterCommand(0x1D2A, new CCommandIsCoopMission());
		CCustomCommandMgr::RegisterCommand(0x1D2B, new CCommandIsAnyNetworkPlayerInArea3D());
	}
};