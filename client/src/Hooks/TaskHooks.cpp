#include "stdafx.h"
#include "TaskHooks.h"
#include "CNetworkVehicle.h"
#include "CNetworkPed.h"
#include "CAimSync.h"
#include "CJackSync.h"
#include <CCarEnterExit.h>
#include <CKeySync.h>

// when local player enters any vehicle
static void __fastcall CTaskComplexEnterCarAsDriver__Ctor_Hook(
    CTaskComplexEnterCarAsDriver* This, SKIP_EDX, CVehicle* vehicle)
{
    CNetworkVehicle* networkVehicle = CNetworkVehicleManager::GetVehicle(vehicle);

    if (networkVehicle == nullptr)
    {
        //CChat::AddMessage("ERROR: this vehicle is not synced");
        plugin::CallMethod<0x6402F0, CTaskComplexEnterCarAsDriver*, CVehicle*>(This, vehicle);
        return;
    }

    // the destination seat (driver) is occupied by a network player: this is a jack, not a normal enter
    CPed* occupant = vehicle->m_pDriver;
    CNetworkPlayer* victim = CNetworkPlayerManager::GetPlayer(occupant);
    if (victim != nullptr && CConfigLoader::ms_bPlayerJackNative)
    {
        // CJackSync sends the jack and enter only when this task actually starts dragging.
        plugin::CallMethod<0x6402F0, CTaskComplexEnterCarAsDriver*, CVehicle*>(This, vehicle);
        return;
    }
    if (victim != nullptr)
    {
        int door = CCarEnterExit::ComputeTargetDoorToExit(vehicle, occupant);
        CJackSync::NotifyRemoteJacked(victim, networkVehicle->m_vehicleId, -1, door);
    }
    // an NPC driver is notified by CJackSync::Process when the drag really starts (PED_JACKED)

    Packets::Vehicles::VehicleEnter packet{};
    packet.seatid = 0;
    packet.vehicleid = networkVehicle->m_vehicleId;
    GetPacketFactory().Send(packet);

    plugin::CallMethod<0x6402F0, CTaskComplexEnterCarAsDriver*, CVehicle*>(This, vehicle);
}

// when the last CTaskComplexLeaveCar hook sent its VehicleExit (a normal exit takes up to a few seconds to
// actually leave the car; a scripted one that fast must not be mistaken for it)
static DWORD s_dwLeaveTaskExitAt = 0;

bool TaskHooks::LeaveTaskExitRecent()
{
    return s_dwLeaveTaskExitAt && GetTickCount() - s_dwLeaveTaskExitAt < 4000;
}

static void __fastcall CTaskComplexLeaveCar__Ctor_Hook(CTaskComplexLeaveCar* This, SKIP_EDX, CVehicle* vehicle,
    int targetDoor, int delayTime, bool sensibleLeaveCar, bool forceGetOut)
{
    if (CNetwork::m_bAuthenticated)
    {
        Packets::Vehicles::VehicleExit packet{};
        GetPacketFactory().Send(packet);
        s_dwLeaveTaskExitAt = GetTickCount();
    }

    plugin::CallMethod<0x63B8C0, CTaskComplexLeaveCar*, CVehicle*, int, int, bool, bool>(
        This, vehicle, targetDoor, delayTime, sensibleLeaveCar, forceGetOut);
}


bool __fastcall CTaskSimpleUseGun__SetPedPosition_Hook(CTaskSimpleUseGun* This, SKIP_EDX, CPed* pPed)
{
    if (!pPed->IsPlayer() || pPed == FindPlayerPed(0))
    {
        return plugin::CallMethodAndReturn<bool, 0x624ED0>(This, pPed);
    }

    CNetworkPlayer* pNetworkPlayer = CNetworkPlayerManager::GetPlayer(pPed);
    if (pNetworkPlayer == nullptr)
    {
        return plugin::CallMethodAndReturn<bool, 0x624ED0>(This, pPed);
    }

    int playerNum = pNetworkPlayer->GetInternalId();
    if (playerNum == -1)
    {
        return plugin::CallMethodAndReturn<bool, 0x624ED0>(This, pPed);
    }

    CWorld::PlayerInFocus = playerNum;
    CKeySync::ApplyNetworkPlayerContext(pNetworkPlayer);
    CAimSync::ApplyNetworkPlayerContext(pNetworkPlayer);

    bool result = plugin::CallMethodAndReturn<bool, 0x624ED0>(This, pPed);

    CWorld::PlayerInFocus = 0;
    CKeySync::ApplyLocalContext();
    CAimSync::ApplyLocalContext();

    return result;
}

void TaskHooks::InjectHooks()
{
    patch::RedirectCall(0x570A1B, CTaskComplexEnterCarAsDriver__Ctor_Hook);
    patch::RedirectCall(0x570A94, CTaskComplexEnterCarAsDriver__Ctor_Hook);

    patch::RedirectCall(0x57049C, CTaskComplexLeaveCar__Ctor_Hook);
    patch::RedirectCall(0x5703F7, CTaskComplexLeaveCar__Ctor_Hook);

    // patch::RedirectCall(0x62D59B, CTaskSimpleGangDriveBy__SetupStaticAnimForPlayer_Hook);

    // patch::RedirectCall(0x62D813, CTaskSimpleGangDriveBy__ProcessAiming_Hook);

    patch::SetPointer(0x86D744, CTaskSimpleUseGun__SetPedPosition_Hook);
}
