#pragma once

#include <cstdint>
#include <vector>

#include "CVector.h"

class CNetworkPlayer;
class CNetworkPed;
class CNetworkVehicle;

// B1: server side handover ("traspaso") of ambient NPCs / vehicles between players.
// The owner that culls a hosted entity sends *__RELEASE; the server picks the nearest eligible player,
// assigns it to him (ASSIGN_* with takeover = true) or removes the entity if nobody wants it.
namespace CHandover
{
// a player without a position update for more than 5 s is not a candidate
bool IsPositionFresh(const CNetworkPlayer* pPlayer);

// nearest eligible player to `vecPos` (other than pSender, with a fresh position, same area, not in vTried),
// or nullptr. Ties go to the lower player id.
CNetworkPlayer* FindCandidate(CNetworkPlayer* pSender, const CVector& vecPos, uint8_t areaCode, float radius,
    const std::vector<CNetworkPlayer*>& vTried);

// PED_RELEASE / VEHICLE_RELEASE from the current syncer
void ReleasePed(CNetworkPed* pPed, CNetworkPlayer* pSender, const CVector& vecPos, float heading, uint8_t areaCode,
    uint8_t reason);
void ReleaseVehicle(CNetworkVehicle* pVehicle, CNetworkPlayer* pSender, const CVector& vecPos, uint8_t areaCode,
    uint8_t reason);

// give the ped to pNewSyncer (unassigns the old one); used by the paramedic coop patients
void AssignPed(CNetworkPed* pPed, CNetworkPlayer* pNewSyncer);

// disconnect: hand every entity hosted by the leaving player to someone else (or remove it, as before)
void HandoverPedsFrom(CNetworkPlayer* pPlayer);
void HandoverVehiclesFrom(CNetworkPlayer* pPlayer);

// the player is about to be deleted: drop every dangling reference to him
void ForgetPlayer(CNetworkPlayer* pPlayer);

// handover-proximity, every server loop: calm ambient peds on foot go to the player standing next to them
void ProcessProximity();
}  // namespace CHandover
