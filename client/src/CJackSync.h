#pragma once

#include "network/ids.h"

class CNetworkPlayer;
class CNetworkPed;
class CPlayerPed;
class CPed;
class CVehicle;

namespace CJackSync
{
constexpr uint32_t WARP_SUPPRESS_MS = 3000;

// detects the local player being dragged out of a vehicle by any ped (NPC or another player's ped)
void Process();

void SuppressWarp(playerid_t playerid, uint32_t durationMs = WARP_SUPPRESS_MS);
bool IsWarpSuppressed(playerid_t playerid);

// Native jack owns both participants during replay, then the victim until it stands up.
bool IsPlayerJackInProgress(CPed* ped);

// the local game ped (NPC) warp suppression: PED_DRIVER_UPDATE / PED_PASSENGER_UPDATE must not put it back
void SuppressPedWarp(pedid_t pedid, uint32_t durationMs = WARP_SUPPRESS_MS);
bool IsPedWarpSuppressed(pedid_t pedid);

// C1: a ped is being pulled out of a car right now (the animation window, or its syncer has it pending):
// PED_ONFOOT must not warp it out / teleport it until the window is over
bool IsPedJackInProgress(pedid_t pedid);

// the local player started pulling a remote player out: tell everyone (the victim leaves its own car)
void NotifyRemoteJacked(CNetworkPlayer* victim, vehicleid_t vehicleid, int seatid, int door);

// the local player started pulling an NPC out: its syncer must release it instead of warping it back
void NotifyPedJacked(CNetworkPed* ped, vehicleid_t vehicleid, int seatid, int door);

// the local player is the victim of a remote jacker: play the animation and leave the seat
void ApplyLocalJacked(vehicleid_t vehicleid, int seatid, int door);

// an NPC was pulled out of a car: play the animation here and, on its syncer, leave the seat
void ApplyPedJacked(pedid_t pedid, vehicleid_t vehicleid, int seatid, int door, playerid_t thiefPlayerId);

// Replays both sides through EnterCar; false lets the existing victim-only fallback run.
bool ReplayThief(playerid_t thiefId, playerid_t victimId, vehicleid_t vehicleId, int seatid, int door);

// another player is being pulled out here: replay the animation, the position keeps coming from its sync
void PlayRemoteJacked(CPlayerPed* ped, CVehicle* vehicle, int seatid, int door);
}  // namespace CJackSync
