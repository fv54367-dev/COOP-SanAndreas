#pragma once

#include <iostream>
#include <vector>
#include <algorithm>

#include "CVector.h"
#include "CNetworkPlayerManager.h"
#include <CDamageManager.h>

class CNetworkVehicle
{
public:
    CNetworkVehicle(vehicleid_t vehicleid, unsigned short model, CVector pos, float rot);

    vehicleid_t m_vehicleId;
    CNetworkPlayer* m_pSyncer = nullptr;
    unsigned short m_nModelId;
    CVector m_vecPosition;
    CVector m_vecRotation;
    CVector m_vecVelocity;
    CNetworkPlayer* m_pPlayers[8] = {nullptr};
    unsigned char m_nPrimaryColor;
    unsigned char m_nSecondaryColor;
    CDamageManager m_damageManager;
    std::vector<int> m_pComponents;
    uint8_t m_nCreatedBy;
    bool m_bUsedByPed = false;
    // the player being dragged out of this vehicle (announced jack): its own driver sync must not take the seat
    // back from the jacker while the drag animation plays
    playerid_t m_jackedVictimId{};

    // B1 handover
    uint8_t m_nAreaCode = 0;                // interior area of the syncer when the vehicle was created
    uint32_t m_nLastHandoverTime = 0;       // g_serverTime of the last handover (anti ping-pong)
    std::vector<CNetworkPlayer*> m_vTried;  // players that already refused/let go of this vehicle

    void ReassignSyncer(CNetworkPlayer* newSyncer);
    void SetOccupant(int8_t seatid, CNetworkPlayer* player);
    // true when the seat (0 = driver, 1..7 = passenger) is held by a player other than the given one
    bool IsSeatTakenByOther(int8_t seatid, CNetworkPlayer* player) const;
    // the driver syncs the vehicle when there is one, otherwise the first passenger
    void SyncToOccupant();

    ~CNetworkVehicle() {}
};