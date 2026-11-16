#pragma once

#include "CVector.h"
#include "network/ids.h"

#include <eModelID.h>
#include <ePedType.h>

#include <vector>

class CNetworkPlayer;

class CNetworkPed
{
public:
    CNetworkPed(
        pedid_t pedid, CNetworkPlayer* syncer, eModelID modelId, ePedType pedType, CVector pos, eCharCreatedBy createdBy);

    pedid_t m_pedId;
    CNetworkPlayer* m_pSyncer;
    eModelID m_nModelId;
    ePedType m_nPedType;
    CVector m_vecPos;
    eCharCreatedBy m_nCreatedBy;
    bool m_bInteriorPopulation = false;
    char m_szSpecialModelName[8];

    // B1 handover
    uint8_t m_nAreaCode = 0;                  // interior area of the syncer when the ped was created
    uint32_t m_nLastHandoverTime = 0;         // g_serverTime of the last handover (anti ping-pong)
    uint32_t m_nSyncerChangedAt = 0;          // g_serverTime of the last syncer change (any path)
    uint32_t m_nLastWrongSyncerNotice = 0;    // g_serverTime of the last "you don't sync it" sent to a former syncer
    std::vector<CNetworkPlayer*> m_vTried;    // players that already refused/let go of this ped
    // paramedic coop: the player whose mission script owns the patient while another player simulates it
    CNetworkPlayer* m_pMissionOwner = nullptr;
    CNetworkPlayer* m_pRecruiter = nullptr;

    // handover-proximity: the last PED_ONFOOT of its syncer (0 = in a vehicle)
    uint32_t m_nOnFootAt = 0;
    float m_fHeading = 0.0f;
    uint8_t m_nMoveState = 0;
    bool m_bAiming = false;
    uint8_t m_nHealth = 100;
};
