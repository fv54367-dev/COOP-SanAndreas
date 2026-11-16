#include "stdafx.h"
#include "CVehicleSurf.h"
#include <CGeneral.h>

static constexpr float MAX_LOOK_DOWN = 3.0f;  // metres from the player's centre to the vehicle under him
static constexpr float MAX_OFFSET = 12.0f;     // more than this from the vehicle's centre: something went wrong

static int ms_nVehicleRef = -1;
static CVector ms_vecOffset;  // the player's position in the vehicle's axes
static float ms_fLastVehicleHeading = 0.0f;

static void Message(const char* text)
{
    CChat::AddMessage("{c7a6ff}[Coop]{d6dce8} %s", text);
}

static float Dot(const CVector& a, const CVector& b)
{
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

static CVector ToLocal(const CMatrix& m, const CVector& world)
{
    const CVector d = world - m.pos;
    return CVector(Dot(d, m.right), Dot(d, m.up), Dot(d, m.at));
}

static CVector ToWorld(const CMatrix& m, const CVector& local)
{
    return m.pos + m.right * local.x + m.up * local.y + m.at * local.z;
}

// the vehicle right under the player's feet
static CVehicle* FindVehicleUnder(CPlayerPed* ped)
{
    const CVector from = ped->GetPosition();
    const CVector to = from - CVector(0.0f, 0.0f, MAX_LOOK_DOWN);
    CColPoint colPoint;
    CEntity* hit = nullptr;
    CEntity* savedIgnore = CWorld::pIgnoreEntity;
    CWorld::pIgnoreEntity = ped;
    const bool bHit = CWorld::ProcessLineOfSight(from, to, colPoint, hit, false, true, false, false, false, false, false, false);
    CWorld::pIgnoreEntity = savedIgnore;
    if (!bHit || hit == nullptr || hit->m_nType != ENTITY_TYPE_VEHICLE)
        return nullptr;
    return static_cast<CVehicle*>(hit);
}

static void Release(const char* why)
{
    if (ms_nVehicleRef == -1)
        return;
    ms_nVehicleRef = -1;
    if (why)
        Message(why);
}

bool CVehicleSurf::IsActive()
{
    return ms_nVehicleRef != -1;
}

void CVehicleSurf::Toggle()
{
    if (ms_nVehicleRef != -1)
    {
        Release("Ya no estas fijado al vehiculo.");
        return;
    }

    CPlayerPed* ped = FindPlayerPed(0);
    if (!ped || ped->m_fHealth <= 0.0f)
        return;
    if (ped->m_nPedFlags.bInVehicle)
    {
        Message("Bajate del vehiculo y subite arriba de uno para usar /fijar.");
        return;
    }

    CVehicle* vehicle = FindVehicleUnder(ped);
    if (!vehicle)
    {
        Message("No estas parado arriba de ningun vehiculo.");
        return;
    }

    ms_nVehicleRef = CPools::GetVehicleRef(vehicle);
    ms_vecOffset = ToLocal(*vehicle->GetMatrix(), ped->GetPosition());
    ms_fLastVehicleHeading = vehicle->GetHeading();
    Message("Fijado al vehiculo. /fijar otra vez o saltar para soltarte.");
}

// After CGame::Process (the world's physics): the player goes back to his place on the vehicle, with its speed there,
// and turns as much as the vehicle turned this frame.
void CVehicleSurf::Process()
{
    if (ms_nVehicleRef == -1)
        return;

    CPlayerPed* ped = FindPlayerPed(0);
    CVehicle* vehicle = CPools::GetVehicle(ms_nVehicleRef);
    if (!CConfigLoader::ms_bChatTpmFijar || !ped || !vehicle || !CNetwork::m_bConnected)
    {
        Release(vehicle || !ped ? nullptr : "El vehiculo ya no esta: te soltaste.");
        return;
    }
    if (ped->m_fHealth <= 0.0f || ped->m_nPedFlags.bInVehicle || vehicle->m_fHealth <= 0.0f ||
        ped->m_nAreaCode != vehicle->m_nAreaCode || ms_vecOffset.Magnitude() > MAX_OFFSET)
    {
        Release("Te soltaste del vehiculo.");
        return;
    }
    CPad* pad = CPad::GetPad(0);
    if (pad && !pad->DisablePlayerControls && pad->JumpJustDown())
    {
        Release("Te soltaste del vehiculo.");
        return;
    }

    const CMatrix& m = *vehicle->GetMatrix();
    const CVector world = ToWorld(m, ms_vecOffset);
    ped->SetPosn(world);
    ped->m_vecMoveSpeed = vehicle->GetSpeed(world - m.pos);
    ped->m_vecTurnSpeed = CVector(0.0f, 0.0f, 0.0f);

    const float heading = vehicle->GetHeading();
    float turn = heading - ms_fLastVehicleHeading;
    ms_fLastVehicleHeading = heading;
    if (turn > 3.14159265f)
        turn -= 6.2831853f;
    else if (turn < -3.14159265f)
        turn += 6.2831853f;
    ped->m_fCurrentRotation = CGeneral::LimitRadianAngle(ped->m_fCurrentRotation + turn);
    ped->m_fAimingRotation = CGeneral::LimitRadianAngle(ped->m_fAimingRotation + turn);
    ped->SetHeading(ped->m_fCurrentRotation);
}
