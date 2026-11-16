#include "stdafx.h"
#include "CNetworkVehicle.h"
#include "CConfigLoader.h"

CNetworkVehicle::CNetworkVehicle(vehicleid_t vehicleid, int modelid, CVector pos, float rotation, unsigned char color1, unsigned char color2, unsigned char createdBy, bool createNow)
{
    if (auto vehicle = CNetworkVehicleManager::GetVehicle(vehicleid))
    {
        // duplicated id: drop the old one completely (the destructor removes and deletes its game vehicle)
        vehicle->m_bSyncing = false;
        CNetworkVehicleManager::Remove(vehicle);
        delete vehicle;
    }

    m_vehicleId = vehicleid;
    m_bSyncing = false;
    m_nModelId = modelid;
    m_nCreatedBy = createdBy;
    m_vecLastPos = pos;
    m_fLastHeading = rotation;
    m_nLastColor1 = color1;
    m_nLastColor2 = color2;

    if (!createNow)
    {
        m_bStreamedOut = true;
        return;
    }

    if (!CreateVehicle(vehicleid, modelid, pos, rotation, color1, color2))
        return;

}

bool CNetworkVehicle::CreateVehicle(vehicleid_t vehicleid, int modelid, CVector pos, float rotation, unsigned char color1, unsigned char color2)
{
    if (modelid < 400 || modelid > 611 || !CModelInfo::ms_modelInfoPtrs[modelid]
        || CModelInfo::ms_modelInfoPtrs[modelid]->GetModelType() != MODEL_INFO_VEHICLE)
        return false;

    unsigned char oldFlags = CStreaming::ms_aInfoForModel[modelid].m_nFlags;
    CStreaming::RequestModel(modelid, GAME_REQUIRED);
    CStreaming::LoadAllRequestedModels(false);

    if (!(oldFlags & GAME_REQUIRED))
    {
        CStreaming::SetModelIsDeletable(modelid);
        CStreaming::SetModelTxdIsDeletable(modelid);
    }

    if (CStreaming::ms_aInfoForModel[modelid].m_nLoadState != LOADSTATE_LOADED)
        return false;

    switch (((CVehicleModelInfo*)CModelInfo::ms_modelInfoPtrs[modelid])->m_nVehicleType)
    {
    case VEHICLE_MTRUCK:
        m_pVehicle = new CMonsterTruck(modelid, MISSION_VEHICLE); break;

    case VEHICLE_QUAD:
        m_pVehicle = new CQuadBike(modelid, MISSION_VEHICLE); break;

    case VEHICLE_HELI:
        m_pVehicle = new CHeli(modelid, MISSION_VEHICLE); break;

    case VEHICLE_PLANE:
        m_pVehicle = new CPlane(modelid, MISSION_VEHICLE); break;

    case VEHICLE_BIKE:
        m_pVehicle = new CBike(modelid, MISSION_VEHICLE);
        ((CBike*)m_pVehicle)->m_nDamageFlags |= 0x10; break;

    case VEHICLE_BMX:
        m_pVehicle = new CBmx(modelid, MISSION_VEHICLE);
        ((CBmx*)m_pVehicle)->m_nDamageFlags |= 0x10; break;

    case VEHICLE_TRAILER:
        m_pVehicle = new CTrailer(modelid, MISSION_VEHICLE); break;

    case VEHICLE_BOAT:
        m_pVehicle = new CBoat(modelid, MISSION_VEHICLE); break;

    case VEHICLE_TRAIN:
        return false;

    default:
        m_pVehicle = new CAutomobile(modelid, MISSION_VEHICLE, true); break;
    }

    if (!m_pVehicle)
        return false;

    m_pVehicle->RegisterReference((CEntity**)&m_pVehicle);

    m_pVehicle->SetPosn(pos);
    m_pVehicle->SetOrientation(0.0f, 0.0f, rotation);
    m_pVehicle->m_nStatus = 4;
    m_pVehicle->m_eDoorLock = DOORLOCK_UNLOCKED;
    m_pVehicle->m_nPrimaryColor = color1;
    m_pVehicle->m_nSecondaryColor = color2;
    CWorld::Add(m_pVehicle);

    return true;
}

CNetworkVehicle::~CNetworkVehicle()
{
    if (m_pVehicle)
    {
        m_pVehicle->CleanUpOldReference((CEntity**)&m_pVehicle);
    }

    if (m_bSyncing)
    {
        Packets::Vehicles::VehicleRemove vehicleRemovePacket{};
        vehicleRemovePacket.vehicleid = m_vehicleId;
        GetPacketFactory().Send(vehicleRemovePacket);
    }
    else
    {
        DestroyGameVehicle();
    }
    m_pVehicle = nullptr;
}

// peds left inside a deleted vehicle keep their in-car state and tasks: put them next to it first
static void RemoveOccupants(CVehicle* pVehicle)
{
    CVector pos = pVehicle->GetPosition();
    auto warpOut = [&](CPed* pPed)
    {
        if (pPed && pPed->IsVTableValid() && pPed->m_nPedFlags.bInVehicle)
        {
            plugin::Command<Commands::WARP_CHAR_FROM_CAR_TO_COORD>(
                CPools::GetPedRef(pPed), pos.x + 2.0f, pos.y, pos.z + 0.5f);
        }
    };

    warpOut(pVehicle->m_pDriver);
    for (int i = 0; i < pVehicle->m_nMaxPassengers && i < 8; i++)
    {
        warpOut(pVehicle->m_apPassengers[i]);
    }
}

void CNetworkVehicle::DestroyGameVehicle()
{
    if (m_pVehicle && m_pVehicle->IsVTableValid())
    {
        RemoveOccupants(m_pVehicle);
        // Clear roof/support references after occupants have left, before freeing a carriage.
        if (m_pVehicle->m_nVehicleSubType == VEHICLE_TRAIN)
            CWorld::RemoveReferencesToDeletedObject(m_pVehicle);
        m_pVehicle->CleanUpOldReference((CEntity**)&m_pVehicle);

        if (m_nBlipHandle != -1)
        {
            CRadar::ClearBlipForEntity(eBlipType::BLIP_CAR, CPools::GetVehicleRef(m_pVehicle));
            m_nBlipHandle = -1;
        }
        CWorld::Remove(m_pVehicle);
        //CWorld::RemoveReferencesToDeletedObject(m_pVehicle); // ?
        delete m_pVehicle;
    }
    m_pVehicle = nullptr;
}

void CNetworkVehicle::StreamOut()
{
    if (m_bSyncing || !m_pVehicle || !m_pVehicle->m_matrix)
        return;

    m_vecLastPos = m_pVehicle->m_matrix->pos;
    m_fLastHeading = m_pVehicle->GetHeading();
    m_nLastColor1 = m_pVehicle->m_nPrimaryColor;
    m_nLastColor2 = m_pVehicle->m_nSecondaryColor;
    m_fLastHealth = m_pVehicle->m_fHealth;
    m_nPaintJob = (char)m_pVehicle->GetRemapIndex();

    if (m_pVehicle->m_nVehicleType == VEHICLE_AUTOMOBILE)
    {
        m_oldDamageState = ((CAutomobile*)m_pVehicle)->m_damageManager;
        m_bHasDamageState = true;
    }

    DestroyGameVehicle();
    m_bHasPosError = false;
    m_bHasRotError = false;
    m_bStreamedOut = true;
}

bool CNetworkVehicle::StreamIn()
{
    if (!m_bStreamedOut)
        return m_pVehicle != nullptr;

    if (!CreateVehicle(m_vehicleId, m_nModelId, m_vecLastPos, m_fLastHeading, m_nLastColor1, m_nLastColor2))
        return false;

    m_bStreamedOut = false;

    m_pVehicle->m_fHealth = m_fLastHealth;

    if (m_nPaintJob >= 0)
        m_pVehicle->SetRemap(m_nPaintJob);

    if (m_bHasDamageState && m_pVehicle->m_nVehicleType == VEHICLE_AUTOMOBILE)
    {
        CAutomobile* pAutomobile = (CAutomobile*)m_pVehicle;
        pAutomobile->m_damageManager = m_oldDamageState;
        pAutomobile->SetupDamageAfterLoad();
    }

    for (int componentId : m_aComponents)
    {
        CStreaming::RequestVehicleUpgrade(componentId, eStreamingFlags::GAME_REQUIRED);
        CStreaming::LoadAllRequestedModels(false);
        if (CStreaming::HasVehicleUpgradeLoaded(componentId))
            m_pVehicle->AddVehicleUpgrade(componentId);
    }

    return true;
}

// B2 handover: the game called CWorld::Remove on a vehicle we host. Keep the record as a remote streamed out copy
// of another player (the game finishes deleting the game vehicle right after the hook) and offer it to the server.
void CNetworkVehicle::Release()
{
    if (!m_pVehicle || !m_pVehicle->IsVTableValid() || !m_pVehicle->m_matrix)
        return;

    m_vecLastPos = m_pVehicle->m_matrix->pos;
    m_fLastHeading = m_pVehicle->GetHeading();
    m_nLastColor1 = m_pVehicle->m_nPrimaryColor;
    m_nLastColor2 = m_pVehicle->m_nSecondaryColor;
    m_fLastHealth = m_pVehicle->m_fHealth;
    m_nPaintJob = (char)m_pVehicle->GetRemapIndex();

    if (m_pVehicle->m_nVehicleType == VEHICLE_AUTOMOBILE)
    {
        m_oldDamageState = ((CAutomobile*)m_pVehicle)->m_damageManager;
        m_bHasDamageState = true;
    }

    Packets::Vehicles::VehicleRelease packet{};
    packet.vehicleid = m_vehicleId;
    packet.pos = m_vecLastPos;
    packet.areaCode = m_pVehicle->m_nAreaCode;
    packet.reason = 0;
    GetPacketFactory().Send(packet);

    if (m_nBlipHandle != -1)
    {
        CRadar::ClearBlipForEntity(eBlipType::BLIP_CAR, CPools::GetVehicleRef(m_pVehicle));
        m_nBlipHandle = -1;
    }

    // same clean up as DestroyGameVehicle but without removing / deleting the game vehicle: the game is about to
    m_pVehicle->CleanUpOldReference((CEntity**)&m_pVehicle);

    m_pVehicle = nullptr;
    m_bSyncing = false;
    m_bStreamedOut = true;
    m_bHasPosError = false;
    m_bHasRotError = false;
}

bool CNetworkVehicle::HasDriver()
{
    if (m_pVehicle == nullptr)
        return false;

    return m_pVehicle->m_pDriver != nullptr;
}

CNetworkVehicle* CNetworkVehicle::CreateHosted(CVehicle* pVehicle)
{
    pVehicle->m_nTimeTillWeNeedThisCar += 5000;

    CNetworkVehicle* networkVehicle = new CNetworkVehicle();

    networkVehicle->m_pVehicle = pVehicle;
    pVehicle->RegisterReference((CEntity**)&networkVehicle->m_pVehicle);

    networkVehicle->m_vehicleId = CNetworkVehicleManager::AllocateVehicleId();
    networkVehicle->m_bSyncing = true;
    networkVehicle->m_nModelId = pVehicle->m_nModelIndex;
    networkVehicle->m_nPaintJob = (char)pVehicle->GetRemapIndex();
    networkVehicle->m_nCreatedBy = pVehicle->m_nCreatedBy;

    CNetworkVehicleManager::Add(networkVehicle);

    Packets::Vehicles::VehicleSpawn vehicleSpawnPacket{};
    vehicleSpawnPacket.vehicleid = networkVehicle->m_vehicleId;
    vehicleSpawnPacket.modelid = pVehicle->m_nModelIndex;
    vehicleSpawnPacket.pos = pVehicle->m_matrix->pos;
    vehicleSpawnPacket.rot = pVehicle->GetHeading();
    vehicleSpawnPacket.color1 = pVehicle->m_nPrimaryColor;
    vehicleSpawnPacket.color2 = pVehicle->m_nSecondaryColor;
    vehicleSpawnPacket.createdBy = (eVehicleCreatedBy)pVehicle->m_nCreatedBy;
    GetPacketFactory().Send(vehicleSpawnPacket);

    return networkVehicle;
}

void CNetworkVehicle::ApplyNetworkPosition(const CVector& target)
{
    if (m_pVehicle == nullptr || m_pVehicle->m_matrix == nullptr)
        return;

    if (!CConfigLoader::ms_bSmoothVehicles || m_bSyncing)
    {
        m_pVehicle->m_matrix->pos = target;
        m_bHasPosError = false;
        return;
    }

    CVector error = target - m_pVehicle->m_matrix->pos;
    float distance = error.Magnitude();

    // aircraft move fast, so they tolerate bigger errors before snapping
    CVehicle* pVehicle = m_pVehicle;
    bool isAircraft = pVehicle->m_nVehicleSubType == VEHICLE_PLANE || pVehicle->m_nVehicleSubType == VEHICLE_HELI;
    float snapDistance = isAircraft ? 20.0f : 5.0f;

    if (distance > snapDistance || distance < 0.05f)
    {
        m_pVehicle->m_matrix->pos = target;
        m_bHasPosError = false;
        return;
    }

    // apply a part of the error right away and spread the rest over the next frames
    m_pVehicle->m_matrix->pos += error * 0.3f;
    m_vecPosError = error * 0.7f;
    m_bHasPosError = true;
}

static float Dot(const CVector& a, const CVector& b)
{
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

static CVector Cross(const CVector& a, const CVector& b)
{
    return CVector(a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x);
}

static void SetOrthonormalRotation(CMatrix* matrix, CVector right, CVector forward)
{
    right.Normalise();
    forward -= right * Dot(right, forward);
    forward.Normalise();

    matrix->right = right;
    matrix->up = forward;
    matrix->at = Cross(right, forward);
}

void CNetworkVehicle::ApplyNetworkRotation(const CVector& right, const CVector& forward)
{
    if (m_pVehicle == nullptr || m_pVehicle->m_matrix == nullptr)
        return;

    CMatrix* matrix = m_pVehicle->m_matrix;

    if (!CConfigLoader::ms_bSmoothVehicles || m_bSyncing)
    {
        matrix->right = right;
        matrix->up = forward;
        m_bHasRotError = false;
        return;
    }

    // big turns (respawn, flip, warp) are applied at once
    if (Dot(matrix->right, right) < 0.94f || Dot(matrix->up, forward) < 0.94f)
    {
        SetOrthonormalRotation(matrix, right, forward);
        m_bHasRotError = false;
        return;
    }

    CVector rightError = right - matrix->right;
    CVector forwardError = forward - matrix->up;

    SetOrthonormalRotation(matrix, matrix->right + rightError * 0.3f, matrix->up + forwardError * 0.3f);
    m_vecRightError = rightError * 0.7f;
    m_vecForwardError = forwardError * 0.7f;
    m_bHasRotError = true;
}

void CNetworkVehicle::ProcessPositionError()
{
    if (m_bHasRotError)
    {
        if (m_bSyncing || !CConfigLoader::ms_bSmoothVehicles || m_pVehicle == nullptr || m_pVehicle->m_matrix == nullptr)
        {
            m_bHasRotError = false;
        }
        else
        {
            float fraction = CTimer::ms_fTimeStep * 0.02f / 0.1f;
            if (fraction > 1.0f)
                fraction = 1.0f;

            CVector rightStep = m_vecRightError * fraction;
            CVector forwardStep = m_vecForwardError * fraction;
            m_vecRightError -= rightStep;
            m_vecForwardError -= forwardStep;

            CMatrix* matrix = m_pVehicle->m_matrix;
            SetOrthonormalRotation(matrix, matrix->right + rightStep, matrix->up + forwardStep);

            if (m_vecRightError.Magnitude() < 0.002f && m_vecForwardError.Magnitude() < 0.002f)
                m_bHasRotError = false;
        }
    }

    if (!m_bHasPosError)
        return;

    if (m_bSyncing || !CConfigLoader::ms_bSmoothVehicles || m_pVehicle == nullptr || m_pVehicle->m_matrix == nullptr)
    {
        m_bHasPosError = false;
        return;
    }

    // CTimer::ms_fTimeStep is in 1/50 s units; catch up in about 100 ms
    float fraction = CTimer::ms_fTimeStep * 0.02f / 0.1f;
    if (fraction > 1.0f)
        fraction = 1.0f;

    CVector step = m_vecPosError * fraction;
    m_pVehicle->m_matrix->pos += step;
    m_vecPosError -= step;

    if (m_vecPosError.Magnitude() < 0.01f)
        m_bHasPosError = false;
}
