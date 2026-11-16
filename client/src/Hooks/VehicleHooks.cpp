#include "stdafx.h"
#include "CVsRace.h"
#include <CTrainSync.h>
#include "VehicleHooks.h"
#include "CKeySync.h"
#include "CAimSync.h"
#include "CNetworkVehicle.h"
#include "CConfigLoader.h"
#include "CNetworkPed.h"
#include <CCarGenerator.h>
#include <CPopCycle.h>
#include <CServerTime.h>
#include <CCarAI.h>
#include <CWanted.h>
#include <CCoopMission.h>
#include "CMissionMirror.h"

// The game only runs CAutomobile::HydraulicControl for the local player's car: send the pad direction and the
// hydraulic phase to the other clients, they replay it with the same function (see CVehicle__ProcessControl_Hook).
static void SendHydraulicState(CNetworkVehicle* pNetworkVehicle, CAutomobile* pAutomobile)
{
    CPad* pPad = CPad::GetPad(0);
    if (pPad == nullptr)
        return;

    static vehicleid_t lastVehicleId{};
    static uint16_t lastAngle = 0;
    static int16_t lastGunLeftRight = 0;
    static int16_t lastGunUpDown = 0;
    static bool lastJump = false;
    static bool hasSent = false;

    // same values CAutomobile::HydraulicControl reads from the pad (CPad::GetCarGunLeftRight / GetCarGunUpDown)
    int16_t gunLeftRight = pPad->Mode == 3 ? (pPad->NewState.DPadRight - pPad->NewState.DPadLeft) / 2 : pPad->NewState.RightStickX;
    int16_t gunUpDown = pPad->Mode == 3 ? (pPad->NewState.DPadUp - pPad->NewState.DPadDown) / 2 : pPad->NewState.RightStickY;
    bool jump = pPad->HornJustDown();

    if (hasSent && lastVehicleId == pNetworkVehicle->m_vehicleId && lastAngle == pAutomobile->m_wMiscComponentAngle &&
        lastGunLeftRight == gunLeftRight && lastGunUpDown == gunUpDown && lastJump == jump)
    {
        return;
    }

    Packets::Vehicles::VehicleHydraulicSync packet{};
    packet.vehicleid = pNetworkVehicle->m_vehicleId;
    packet.angle = pAutomobile->m_wMiscComponentAngle;
    packet.gunLeftRight = gunLeftRight;
    packet.gunUpDown = gunUpDown;
    packet.jump = jump;
    GetPacketFactory().Send(packet);

    lastVehicleId = pNetworkVehicle->m_vehicleId;
    lastAngle = pAutomobile->m_wMiscComponentAngle;
    lastGunLeftRight = gunLeftRight;
    lastGunUpDown = gunUpDown;
    lastJump = jump;
    hasSent = true;
}

// a heli copy is STATUS_ABANDONED: CHeli::ProcessFlyingCarStuff (0x6C4E60) turns the engine off and slows the rotor
// down every frame. Spin it up like the player branch does (+0.001 per step, 0.003 for the RC helis, up to 0.22)
static void SpinUpHeliRotor(CVehicle* vehicle, float rotorSpeedBefore)
{
    const bool rcHeli = vehicle->m_nModelIndex == 465 || vehicle->m_nModelIndex == 501;  // RC Raider, RC Goblin
    const float step = (rcHeli ? 0.003f : 0.001f) * CTimer::ms_fTimeStep;
    *(float*)((uintptr_t)vehicle + 0x84C) = std::min(0.22f, std::max(rotorSpeedBefore, 0.0f) + step);  // m_fHeliRotorSpeed
    vehicle->m_nVehicleFlags.bEngineOn = true;
}

// remote-heli-rotor-sync: the copy's CHeli::ProcessControl (0x6C7050) clears the searchlight every frame (it does not
// know the copy chases anybody, and a player's toggle is on his pad). The owner's state goes on: aimed at the nearest
// player, else straight down, like the police heli over its target
static void ApplyRemoteSearchLight(CVehicle* vehicle, bool on)
{
    uint8_t& enabled = *(uint8_t*)((uintptr_t)vehicle + 0xA11);       // m_bSearchLightEnabled
    float& intensity = *(float*)((uintptr_t)vehicle + 0xA00);         // m_fSearchLightIntensity
    CVector& target = *(CVector*)((uintptr_t)vehicle + 0x9F4);        // m_vecSearchLightTarget
    if (!on)
    {
        enabled = 0;
        intensity = 0.0f;
        return;
    }
    const CVector heli = vehicle->GetPosition();
    CPed* best = nullptr;
    float bestDist = 80.0f * 80.0f;
    auto consider = [&](CPed* ped) {
        if (!ped || ped->m_fHealth <= 0.0f || ped->m_nAreaCode != vehicle->m_nAreaCode || (ped->m_pVehicle == vehicle && ped->m_nPedFlags.bInVehicle))
            return;
        const CVector pos = ped->GetPosition();
        const float d = (pos.x - heli.x) * (pos.x - heli.x) + (pos.y - heli.y) * (pos.y - heli.y);
        if (d < bestDist && pos.z < heli.z)
        {
            bestDist = d;
            best = ped;
        }
    };
    consider(FindPlayerPed(0));
    for (auto networkPlayer : CNetworkPlayerManager::m_pPlayers)
        consider(networkPlayer->m_pPed);
    if (best)
        target = best->GetPosition();
    else
        target = CVector(heli.x, heli.y, CWorld::FindGroundZForCoord(heli.x, heli.y));
    enabled = 1;
    intensity = 1.0f;
}

// remote-boat-sync: CBoat's constructor anchors the boat (m_nBoatFlags.bAnchored) and only STATUS_PLAYER / SIMPLE /
// PHYSICS clear it (gta-reversed Boat.cpp, ProcessControl). A copy is STATUS_ABANDONED, so it kept zeroing its x/y speed
// and turning back to its first heading: no velocity, no wake, jerky turns. Free it while somebody drives it or it moves
static void FreeRemoteBoat(CVehicle* vehicle)
{
    CBoat* boat = static_cast<CBoat*>(vehicle);
    const bool moving = vehicle->m_vecMoveSpeed.x * vehicle->m_vecMoveSpeed.x + vehicle->m_vecMoveSpeed.y * vehicle->m_vecMoveSpeed.y > 0.0004f;
    if (vehicle->m_pDriver != nullptr || moving)
    {
        boat->m_nBoatFlags.bAnchored = false;
        boat->m_fAnchoredAngle = -10000.0f;
    }
}

// a boat copy is STATUS_ABANDONED: ProcessControl zeroes steering and gas and lets the propeller wind down. Replay the
// driver's inputs through the game's own CBoat::ProcessControlInputs (0x6F0A10, pad 0 holds the remote controller state)
// and the propeller speed law of ProcessControl, after the physics, so the rudder and the propeller show what he does
static void ApplyRemoteBoatInputs(CVehicle* vehicle)
{
    CBoat* boat = static_cast<CBoat*>(vehicle);
    plugin::CallMethodDyn<CVehicle*, unsigned char>(0x6F0A10, vehicle, 0);

    const float gas = vehicle->m_fGasPedal;
    const float stdSpeed = *(float*)0x8D33D0;
    const float maxSpeed = *(float*)0x8D33D4;
    const float rocSpeed = *(float*)0x8D33D8;
    float target = stdSpeed;
    float roc = rocSpeed;
    if (gas < 0.0f)
        target = (stdSpeed - 0.05f) * gas + stdSpeed;
    else if (gas > 0.0f)
    {
        target = (maxSpeed - stdSpeed) * gas + stdSpeed;
        roc *= 5.0f;
    }
    boat->m_fPropSpeed += (target - boat->m_fPropSpeed) * CTimer::ms_fTimeStep * roc;
    vehicle->m_nVehicleFlags.bEngineOn = true;
}

void __fastcall CVehicle__ProcessControl_Hook()
{
    CVehicle* vehicle = nullptr;
    DWORD vtbl = 0;
    DWORD call_addr = 0;

    __asm
    {
        mov vehicle, ecx
        mov eax, [ecx]
        mov vtbl, eax
    }

    if (vtbl == 0x871120)       // CAutomobile
        call_addr = 0x6B1880;
    else if (vtbl == 0x8721A0)  // CBoat
        call_addr = 0x6F1770;
    else if (vtbl == 0x871360)  // CBike
        call_addr = 0x6B9250;
    else if (vtbl == 0x871948)  // CPlane
        call_addr = 0x6C9260;
    else if (vtbl == 0x871680)  // CHeli
        call_addr = 0x6C7050;
    else if (vtbl == 0x871528)  // CBmx
        call_addr = 0x6BFA30;
    else if (vtbl == 0x8717d8)  // CMonsterTruck
        call_addr = 0x6C8250;
    else if (vtbl == 0x871AE8)  // CQuadBike
        call_addr = 0x6CDCC0;
    else if (vtbl == 0x872370)  // CTrain
        call_addr = 0x6F86A0;

    // only the vtables above are hooked (see InjectHooks), anything else would call address 0
    if (call_addr == 0)
        return;

    // a train copy of the host is placed by CTrainSync (rail distance), never by this game's physics
    if (CTrainSync::OverrideCopyProcessControl(vehicle))
        return;

    // a null driver must never match a missing local ped: it would run the local player branch for an empty car
    CPlayerPed* pLocalPlayer = FindPlayerPed(0);
    if (pLocalPlayer != nullptr && vehicle->m_pDriver == pLocalPlayer)
    {
        plugin::CallMethod<0x502280, CAEVehicleAudioEntity*>(&vehicle->m_vehicleAudio);
        plugin::CallMethodDyn<CVehicle*>(call_addr, vehicle);

        // a forced bounce only changes the local player's suspension, the others need the input to replay it
        if (vehicle->m_nVehicleType == VEHICLE_AUTOMOBILE && vehicle->m_nHandlingFlags.bHydraulicInst)
        {
            if (CNetworkVehicle* pNetworkVehicle = CNetworkVehicleManager::GetVehicle(vehicle))
            {
                if (pNetworkVehicle->m_bSyncing)
                    SendHydraulicState(pNetworkVehicle, (CAutomobile*)vehicle);
            }
        }
        return;
    }

    if (CConfigLoader::ms_bSmoothVehicles)
    {
        if (CNetworkVehicle* pNetworkVehicle = CNetworkVehicleManager::GetVehicle(vehicle))
            pNetworkVehicle->ProcessPositionError();
    }

    CNetworkPlayer* player = CNetworkPlayerManager::GetPlayer(vehicle->m_pDriver);

    if (player == nullptr || player->m_pPed == nullptr)
    {
        plugin::CallMethod<0x502280, CAEVehicleAudioEntity*>(&vehicle->m_vehicleAudio);
        const float npcRotorBefore = *(float*)((uintptr_t)vehicle + 0x84C);
        const bool freeNpcBoat = CConfigLoader::ms_bRemoteBoatSync && vtbl == 0x8721A0;
        if (freeNpcBoat)
        {
            CNetworkVehicle* pBoatCopy = CNetworkVehicleManager::GetVehicle(vehicle);
            if (pBoatCopy != nullptr && !pBoatCopy->m_bSyncing)
                FreeRemoteBoat(vehicle);
        }
        plugin::CallMethodDyn<CVehicle*>(call_addr, vehicle);
        // a police / news heli of another game (NPC driver or none): same spin-up as a player's heli below
        if (vtbl == 0x871680 && CConfigLoader::ms_bRemoteHeliRotorSync && vehicle->m_nStatus != STATUS_WRECKED)
        {
            CNetworkVehicle* pCopy = CNetworkVehicleManager::GetVehicle(vehicle);
            if (pCopy != nullptr && !pCopy->m_bSyncing && pCopy->m_bRemoteRotorOn)
                SpinUpHeliRotor(vehicle, npcRotorBefore);
            if (pCopy != nullptr && !pCopy->m_bSyncing)
                ApplyRemoteSearchLight(vehicle, pCopy->m_bRemoteSearchLight);
        }

        // CNetworkPed* ped = CNetworkPedManager::GetPed(vehicle->m_pDriver);
        // if (ped && !ped->m_bSyncing)
        //{
        //     //memset(&vehicle->m_autoPilot, 0, sizeof CAutoPilot);
        //     vehicle->m_fGasPedal = ped->m_fGasPedal;
        //     vehicle->m_fBreakPedal = ped->m_fBreakPedal;
        //     vehicle->m_fSteerAngle = ped->m_fSteerAngle;
        // }
        return;
    }

    int playerNum = player->GetInternalId();

    if (playerNum == -1)
    {
        plugin::CallMethod<0x502280, CAEVehicleAudioEntity*>(&vehicle->m_vehicleAudio);
        plugin::CallMethodDyn<CVehicle*>(call_addr, vehicle);
        return;
    }

    // the audio is serviced in the local context: with the network player in focus the game treats his
    // vehicle as the local player's one and retunes the local radio to that vehicle's station
    player->m_pPed->m_nPedType = PED_TYPE_CIVMALE;

    CNetworkVehicle* pAudioVehicle = CNetworkVehicleManager::GetVehicle(vehicle);
    const bool applyRemoteHorn = CConfigLoader::ms_bRemoteHornSync && pAudioVehicle != nullptr &&
        !player->m_bDead && vehicle->m_nStatus != STATUS_WRECKED &&
        pAudioVehicle->m_playerDriverSnapshot.vehicleid == pAudioVehicle->m_vehicleId &&
        pAudioVehicle->m_playerDriverSnapshot.senderPlayerid.value == player->m_playerId;
    const unsigned char audioStatus = vehicle->m_nStatus;
    if (applyRemoteHorn)
    {
        // Physics can clear the counter between packets. GetHornState uses NPC patterns unless STATUS_PLAYER.
        vehicle->m_nHornCounter = pAudioVehicle->m_playerDriverSnapshot.horn ? 1 : 0;
        vehicle->m_nStatus = STATUS_PLAYER;
    }
    plugin::CallMethod<0x502280, CAEVehicleAudioEntity*>(&vehicle->m_vehicleAudio);
    if (applyRemoteHorn)
        vehicle->m_nStatus = audioStatus;

    player->m_pPed->m_nPedType = PED_TYPE_PLAYER1;

    CWorld::PlayerInFocus = playerNum;

    CKeySync::ApplyNetworkPlayerContext(player);
    CAimSync::ApplyNetworkPlayerContext(player);
    // CStatsSync::ApplyNetworkPlayerContext(player);

    player->m_pPed->m_fHealth = player->m_onFootSnapshotInterpolated.healthSnapshot.iHealth;
    player->m_pPed->m_fArmour = player->m_onFootSnapshotInterpolated.healthSnapshot.iArmour;

    bool savedLookingLeft = *(bool*)0xB6F1A4;
    bool savedLookingRight = *(bool*)0xB6F1A5;
    *(bool*)0xB6F1A4 = player->m_newControllerState.LeftShoulder2 > 0;
    *(bool*)0xB6F1A5 = player->m_newControllerState.RightShoulder2 > 0;

    // replay the sender's forced hydraulics through the game's own CAutomobile::HydraulicControl: the remote
    // player's controller state is already on pad 0, only the right stick and the key edge are missing
    CNetworkVehicle* pNetworkVehicle = CNetworkVehicleManager::GetVehicle(vehicle);
    CPad* pPad = CPad::GetPad(0);
    const bool applyHydraulics = pNetworkVehicle != nullptr && pPad != nullptr &&
        vehicle->m_nVehicleType == VEHICLE_AUTOMOBILE && vehicle->m_nHandlingFlags.bHydraulicInst &&
        pNetworkVehicle->m_bHasHydraulicState;
    const bool replayHydraulics = applyHydraulics &&
        (pNetworkVehicle->m_bHydraulicJump || pNetworkVehicle->m_hydraulicGunLeftRight != 0 ||
            pNetworkVehicle->m_hydraulicGunUpDown != 0 || pNetworkVehicle->m_hydraulicAngle >= 500);

    unsigned char savedStatus = 0;
    bool savedHydraulicInst = false;
    signed short savedRightStickX = 0;
    signed short savedRightStickY = 0;
    signed short savedOldShockButtonL = 0;
    signed short savedNewShockButtonL = 0;

    if (applyHydraulics)
        ((CAutomobile*)vehicle)->m_wMiscComponentAngle = pNetworkVehicle->m_hydraulicAngle;

    if (replayHydraulics)
    {
        CAutomobile* pAutomobile = (CAutomobile*)vehicle;

        savedStatus = vehicle->m_nStatus;
        savedHydraulicInst = vehicle->m_nHandlingFlags.bHydraulicInst;

        savedRightStickX = pPad->NewState.RightStickX;
        savedRightStickY = pPad->NewState.RightStickY;
        savedOldShockButtonL = pPad->OldState.ShockButtonL;
        savedNewShockButtonL = pPad->NewState.ShockButtonL;

        pPad->NewState.RightStickX = pNetworkVehicle->m_hydraulicGunLeftRight;
        pPad->NewState.RightStickY = pNetworkVehicle->m_hydraulicGunUpDown;

        if (pNetworkVehicle->m_bHydraulicJump)
        {
            pPad->OldState.ShockButtonL = 0;
            pPad->NewState.ShockButtonL = 255;
            pNetworkVehicle->m_bHydraulicJump = false;  // one shot, the game has to see the key going down
        }

        // the car of a network player is not always STATUS_PLAYER here: use the player path so the direction and
        // the key edge drive the suspension exactly like on the sender's game
        vehicle->m_nStatus = STATUS_PLAYER;
        pAutomobile->HydraulicControl();
        vehicle->m_nStatus = savedStatus;

        // the original ProcessControl must not call it again with the empty right stick and undo the movement
        vehicle->m_nHandlingFlags.bHydraulicInst = false;

        pPad->NewState.RightStickX = savedRightStickX;
        pPad->NewState.RightStickY = savedRightStickY;
        pPad->OldState.ShockButtonL = savedOldShockButtonL;
        pPad->NewState.ShockButtonL = savedNewShockButtonL;
    }

    // a copy is STATUS_ABANDONED: CHeli::ProcessFlyingCarStuff (0x6C4E60) then turns the engine off and slows the rotor
    // down every frame, so the others saw it still and without lights. Spin it up like the player branch does
    // (+0.001 per step, 0.003 for the RC helis, up to 0.22) while the driver says the engine is on
    const bool applyHeliRotor = CConfigLoader::ms_bRemoteHeliRotorSync && vtbl == 0x871680 &&
        pNetworkVehicle != nullptr && pNetworkVehicle->m_playerDriverSnapshot.vehicleid == pNetworkVehicle->m_vehicleId &&
        pNetworkVehicle->m_playerDriverSnapshot.senderPlayerid.value == player->m_playerId &&
        pNetworkVehicle->m_playerDriverSnapshot.planeGearState > 0.0f && vehicle->m_nStatus != STATUS_WRECKED;
    float& heliRotorSpeed = *(float*)((uintptr_t)vehicle + 0x84C);  // CAutomobile::m_fHeliRotorSpeed
    const float rotorSpeedBefore = heliRotorSpeed;
    if (vtbl == 0x871680)
    {
        // one line per change: tells whether the driver's "rotor on" bit arrives and the copy spins it (testeo 14-11)
        static CVehicle* lastHeli = nullptr;
        static int lastState = -1;
        const int state = (applyHeliRotor ? 1 : 0) | (pNetworkVehicle != nullptr ? 2 : 0) |
            (pNetworkVehicle != nullptr && pNetworkVehicle->m_playerDriverSnapshot.planeGearState > 0.0f ? 4 : 0);
        if (vehicle != lastHeli || state != lastState)
        {
            lastHeli = vehicle;
            lastState = state;
            CMissionMirror::LogEventf("heli-rotor model=%d apply=%d synced=%d rotorBit=%d status=%d rotor=%.3f engine=%d",
                vehicle->m_nModelIndex, state & 1, (state >> 1) & 1, (state >> 2) & 1, vehicle->m_nStatus, rotorSpeedBefore,
                vehicle->m_nVehicleFlags.bEngineOn ? 1 : 0);
        }
    }

    const bool remoteBoat = CConfigLoader::ms_bRemoteBoatSync && vtbl == 0x8721A0 && pNetworkVehicle != nullptr &&
        !pNetworkVehicle->m_bSyncing && vehicle->m_nStatus != STATUS_WRECKED;
    if (remoteBoat)
        FreeRemoteBoat(vehicle);

    CStatsSync::SaveLocalStats();
    plugin::CallMethodDyn<CVehicle*>(call_addr, vehicle);
    CStatsSync::RestoreLocalStats();

    if (remoteBoat)
        ApplyRemoteBoatInputs(vehicle);

    if (applyHeliRotor)
        SpinUpHeliRotor(vehicle, rotorSpeedBefore);
    if (CConfigLoader::ms_bRemoteHeliRotorSync && vtbl == 0x871680 && pNetworkVehicle != nullptr &&
        vehicle->m_nStatus != STATUS_WRECKED)
        ApplyRemoteSearchLight(vehicle, pNetworkVehicle->m_playerDriverSnapshot.vehicleid == pNetworkVehicle->m_vehicleId &&
            pNetworkVehicle->m_playerDriverSnapshot.planeGearState >= 2.0f);

    if (replayHydraulics)
        vehicle->m_nHandlingFlags.bHydraulicInst = savedHydraulicInst;

    CWorld::PlayerInFocus = 0;

    CKeySync::ApplyLocalContext();
    CAimSync::ApplyLocalContext();
    // CStatsSync::ApplyLocalContext();

    *(bool*)0xB6F1A4 = savedLookingLeft;
    *(bool*)0xB6F1A5 = savedLookingRight;
}

static void __fastcall CCarCtrl__RemoveDistantCars_Hook()
{
    // if (CLocalPlayer::m_bIsHost)
    CCarCtrl::RemoveDistantCars();
}

static void __fastcall CVehicle__AddVehicleUpgrade_Hook(CVehicle* This, SKIP_EDX, int modelid)
{
    if (auto vehicle = CNetworkVehicleManager::GetVehicle(This))
    {
        Packets::Vehicles::VehicleComponentAdd packet{};
        packet.vehicleid = vehicle->m_vehicleId;
        packet.componentid = modelid;
        GetPacketFactory().Send(packet);
    }
    This->AddVehicleUpgrade(modelid);
}

static void __fastcall CVehicle__RemoveVehicleUpgrade_Hook(CVehicle* This, SKIP_EDX, int modelid)
{
    if (auto vehicle = CNetworkVehicleManager::GetVehicle(This))
    {
        Packets::Vehicles::VehicleComponentRemove packet{};
        packet.vehicleid = vehicle->m_vehicleId;
        packet.componentid = modelid;
        GetPacketFactory().Send(packet);
    }
    This->RemoveVehicleUpgrade(modelid);
}

static bool __fastcall CDamageManager__ApplyDamage_Hook(
    CDamageManager* This, SKIP_EDX, CAutomobile* dm_comp, tComponent compId, float intensity, float a5)
{
    CNetworkVehicle* vehicle = CNetworkVehicleManager::GetVehicle(dm_comp);
    if (vehicle != nullptr && vehicle->m_bSyncing)
    {
        return plugin::CallMethodAndReturn<bool, 0x6C24B0, CDamageManager*, CAutomobile*, tComponent, float, float>(
            This, dm_comp, compId, intensity, a5);
    }
    return false;
}

static void __fastcall CDamageManager__FuckCarCompletely_Hook(CDamageManager* This, SKIP_EDX, bool bDontDetachWheel)
{
    CAutomobile* pAutomobile = (CAutomobile*)((uintptr_t)This - 0x5A0);
    CNetworkVehicle* vehicle = CNetworkVehicleManager::GetVehicle(pAutomobile);
    if (vehicle != nullptr && vehicle->m_bSyncing)
    {
        This->FuckCarCompletely(bDontDetachWheel);
    }
}

static void __fastcall CVehicle__SetRemap_Hook(CVehicle* This, SKIP_EDX, int paintJobId)
{
    if (auto vehicle = CNetworkVehicleManager::GetVehicle(This))
        vehicle->m_nPaintJob = paintJobId;

    This->SetRemap(paintJobId);
}

// static bool __fastcall CAutomobile__ProcessAI_Hook(CAutomobile* This, SKIP_EDX, int a1)
//{
//     DWORD vtbl = *(DWORD*)(This);
//     DWORD call_addr = 0x6B4800;
//
//     if (vtbl == 0x871360) // CBike
//         call_addr = 0x6BC930;
//     else if (vtbl == 0x871AE8) // CQuadBike
//         call_addr = 0x6CE460;
//     else if (vtbl == 0x871C28) // CTrailer
//         call_addr = 0x6CF590;
//
//     CNetworkPed* networkPed = CNetworkPedManager::GetPed(This->m_pDriver);
//     if (networkPed && !networkPed->m_bSyncing)
//     {
//         This->m_fGasPedal = networkPed->m_fGasPedal;
//         This->m_fBreakPedal = networkPed->m_fBreakPedal;
//         This->m_fSteerAngle = networkPed->m_fSteerAngle;
//
//         return false;
//     }
//
//     return plugin::CallMethodAndReturnDyn<bool, CAutomobile*>(call_addr, This, a1);
// }

// disallow creating a parked vehicle if it is created by another player (does not work perfectly)
// also disallow creating a parked vehicle if the player is dead, fixes vehicle pool overf*ck
bool __fastcall CCarGenerator__CheckForBlockage_Hook(CCarGenerator* This, SKIP_EDX, int modelId)
{
    bool originalResult = This->CheckForBlockage(modelId);

    if (originalResult)
        return true;

    // loading a savegame, respawning or an interior transition can leave a frame without a local ped
    CPlayerPed* pLocalPlayer = FindPlayerPed(0);
    if (pLocalPlayer == nullptr)
        return true;

    if (pLocalPlayer->m_fHealth <= 0.0f)  // if is dead
        return true;

    CVector position = This->m_vecPosn.Uncompressed();

    for (auto vehicle : CPools::ms_pVehiclePool)
    {
        if (vehicle == nullptr)
            continue;

        if ((vehicle->GetPosition() - position).Magnitude() <= 5.0f)
        {
            return true;
        }
    }

    // only the player closest to the generator creates its car, otherwise every client creates its own copy
    // on the same spot and the overlapping vehicles explode
    if (CNetwork::m_bAuthenticated)
    {
        CVector2D localPos = pLocalPlayer->GetPosition();
        float localDistance = (CVector2D(position) - localPos).Magnitude();

        for (auto networkPlayer : CNetworkPlayerManager::m_pPlayers)
        {
            if (!networkPlayer->m_pPed)
                continue;

            float distance = (CVector2D(position) - CVector2D(networkPlayer->m_pPed->GetPosition())).Magnitude();
            if (distance < localDistance ||
                (distance == localDistance &&
                    networkPlayer->m_playerId.value() < CNetworkPlayerManager::m_localPlayerId.value()))
            {
                return true;
            }
        }
    }

    return false;
}

// police-per-player: the police cars this client traces around the local player (NumLawEnforcerCars)
static constexpr float ZONE_POLICE_CARS_RADIUS = 200.0f;

// police-per-player: the generation is running for the police branch only, the civilian cars are aborted
static bool s_bPoliceOnly = false;

// set by the zone filter of 0x430455 when it discarded the creation point of this call
static bool s_bPointFilterBlocked = false;

// police-per-player: the police cars THIS client hosts within ZONE_POLICE_CARS_RADIUS, used as NumLawEnforcerCars so
// every player keeps his own pursuit cap instead of sharing the game's global counter
static int CountNearbyPoliceCars(const CVector& pos)
{
    int count = 0;

    for (CNetworkVehicle* networkVehicle : CNetworkVehicleManager::m_pVehicles)
    {
        if (networkVehicle == nullptr || !networkVehicle->m_bSyncing || networkVehicle->m_pVehicle == nullptr)
            continue;

        if (!networkVehicle->m_pVehicle->IsLawEnforcementVehicle())
            continue;

        if (DistanceBetweenPoints(networkVehicle->m_pVehicle->GetPosition(), pos) <= ZONE_POLICE_CARS_RADIUS)
            count++;
    }

    return count;
}

// CCarCtrl::GenerateCarCreationCoors2 (0x424210) called from CCarCtrl::GenerateOneRandomCar (0x430455): it chooses
// the creation point of the traffic cars. Signature from plugin-sdk (CCarCtrl.h:44, same address). zone-population:
// the point belongs to the closest player only (the caller aborts on false, 0x431AAD)
static bool __cdecl CCarCtrl__GenerateCarCreationCoors2_Hook(CVector posn, float radius, float arg3, float arg4,
    bool arg5, float arg6, float arg7, CVector* pOrigin, CNodeAddress* pNodeAddress1, CNodeAddress* pNodeAddress2,
    float* arg11, bool arg12, bool arg13)
{
    bool bFound = CCarCtrl::GenerateCarCreationCoors2(posn, radius, arg3, arg4, arg5, arg6, arg7, pOrigin,
        pNodeAddress1, pNodeAddress2, arg11, arg12, arg13);

    // the chase cops of police-per-player are created even on a point owned by another player (s_bPoliceOnly)
    if (CConfigLoader::ms_bZonePopulation && !s_bPoliceOnly && bFound && pOrigin != nullptr &&
        !CStreamingSync::IsPointMine(*pOrigin))
    {
        s_bPointFilterBlocked = true;
        return false;
    }

    return bFound;
}

// CCarCtrl::ChooseModel (0x424CE0) called from CCarCtrl::GenerateOneRandomCar (0x43022A): -1 aborts the generation
// (0x430239), so with s_bPoliceOnly only the police branch of the original creates a car
// random-cops-cap: ChooseModel puts the car class here, 13 = the popcycle cops (0x424E0E)
static constexpr int CAR_CLASS_COPS = 13;
static constexpr float RANDOM_COPS_RADIUS = 150.0f;

static bool IsPoliceCarNear(const CVector& pos)
{
    for (auto vehicle : CPools::ms_pVehiclePool)
    {
        if (vehicle && vehicle->IsLawEnforcementVehicle() && vehicle->m_fHealth > 0.0f &&
            DistanceBetweenPoints(vehicle->GetPosition(), pos) <= RANDOM_COPS_RADIUS)
            return true;
    }
    return false;
}

static int __cdecl CCarCtrl__ChooseModel_Hook(int* pModelIndex)
{
    if (s_bPoliceOnly)
        return -1;

    int model = CCarCtrl::ChooseModel(pModelIndex);
    CPlayerPed* player = FindPlayerPed(0);
    CWanted* wanted = FindPlayerWanted(0);
    if (!CConfigLoader::ms_bRandomCopsCap || model == -1 || *pModelIndex != CAR_CLASS_COPS || !player || !wanted ||
        wanted->m_nWantedLevel > 0 || !IsPoliceCarNear(player->GetPosition()))
        return model;

    // the patrol is drawn again as the zone's other traffic, so the street keeps its density
    for (int i = 0; i < 4 && model != -1 && *pModelIndex == CAR_CLASS_COPS; ++i)
        model = CCarCtrl::ChooseModel(pModelIndex);
    static DWORD nLastLog = 0;
    if (GetTickCount() - nLastLog > 30000)
    {
        nLastLog = GetTickCount();
        CMissionMirror::LogEventf("random-cops-cap redrawn=%d", *pModelIndex != CAR_CLASS_COPS ? 1 : 0);
    }
    return *pModelIndex == CAR_CLASS_COPS ? -1 : model;
}

// the game reads the counters of on-duty vehicles around the generation call: override NumRandomCars with the cars
// around the local player and (police-per-player) NumLawEnforcerCars with the police cars this client syncs
static void GenerateTrafficAroundLocalPlayer(CPlayerPed* player, int numCarsInRadius)
{
    int savedNumRandomCars = patch::GetInt(0x969094, false);
    int savedNumLawEnforcerCars = patch::GetInt(0x969098, false);
    int savedNumMissionCars = patch::GetInt(0x96909C, false);
    int savedNumAmbulancesOnDuty = patch::GetInt(0x9690A8, false);
    int savedNumFireTrucksOnDuty = patch::GetInt(0x9690AC, false);
    patch::SetInt(0x969094, numCarsInRadius, false);
    patch::SetInt(0x969098,
        CConfigLoader::ms_bPolicePerPlayer ? CountNearbyPoliceCars(player->GetPosition()) : 0, false);
    patch::SetInt(0x96909C, 0, false);
    patch::SetInt(0x9690A8, 0, false);
    patch::SetInt(0x9690AC, 0, false);

    // also dont generate vehicles when the player is a passenger
    // and his vehicle is driven by another player

    if (player->m_pVehicle && player->m_nPedFlags.bInVehicle)  // if the local player in a vehicle
    {
        // generate cars only if the player is a ...
        if (player->m_pVehicle->m_pDriver == player         // driver
            || player->m_pVehicle->m_pDriver == nullptr     // passenger without driver
            || !player->m_pVehicle->m_pDriver->IsPlayer())  // passenger with NPC driver
        {
            CCarCtrl::GenerateOneRandomCar();               // call original function
        }
    }
    else                                   // on foot
    {
        CCarCtrl::GenerateOneRandomCar();  // call original function
    }

    patch::SetInt(0x969094, savedNumRandomCars, false);
    patch::SetInt(0x969098, savedNumLawEnforcerCars, false);
    patch::SetInt(0x96909C, savedNumMissionCars, false);
    patch::SetInt(0x9690A8, savedNumAmbulancesOnDuty, false);
    patch::SetInt(0x9690AC, savedNumFireTrucksOnDuty, false);
}

// the simplest method to allow other players
// to create traffic vehicles without conflicts
void CCarCtrl__GenerateOneRandomCar_Hook()
{
    // a VS race has no traffic
    if (CVsRace::IsActive())
        return;

    // no local ped (loading / respawn): nothing to generate traffic around, and FindPlayerCoors would crash
    CPlayerPed* player = FindPlayerPed(0);
    if (player == nullptr)
        return;

    CVector pos = player->GetPosition();
    int numCarsInRadius = 0;

    for (auto vehicle : CPools::ms_pVehiclePool)
    {
        if (vehicle == nullptr)
            continue;

        if ((vehicle->GetPosition() - pos).Magnitude() <= 100.0f)
        {
            numCarsInRadius++;
        }
    }

    // zone-population off (the 300 m rule of today): a nearby player with a lower id already generates the traffic
    // for both. On: every player generates, split by creation point inside the original (0x430455)
    bool bBlocked = !CConfigLoader::ms_bZonePopulation && CStreamingSync::IsOtherPlayerGeneratingNearby();

    s_bPointFilterBlocked = false;

    if (!bBlocked)
        GenerateTrafficAroundLocalPlayer(player, numCarsInRadius);

    // police-per-player: a player with stars always generates his own chase cops (they chase him: they are hosted by
    // him), even when the 300 m / zone filter would have blocked the generation. The original is called again with
    // only its police branch enabled
    if (CConfigLoader::ms_bPolicePerPlayer && (bBlocked || s_bPointFilterBlocked))
    {
        CWanted* wanted = FindPlayerWanted(0);
        if (wanted != nullptr && wanted->m_nWantedLevel > 0)
        {
            s_bPoliceOnly = true;
            GenerateTrafficAroundLocalPlayer(player, numCarsInRadius);
            s_bPoliceOnly = false;
        }
    }
}

void CCarCtrl__UpdateCarAI_Hook(CVehicle* vehicle)
{
    if (auto networkVehicle = CNetworkVehicleManager::GetVehicle(vehicle))
    {
        if (!networkVehicle->m_bSyncing)
        {
            if (auto ped = vehicle->m_pDriver)
            {
                if (auto networkPed = CNetworkPedManager::GetPed(ped))
                {
                    if (!networkPed->m_bSyncing)
                    {
                        vehicle->m_fGasPedal = networkPed->m_fGasPedal;
                        vehicle->m_fBreakPedal = networkPed->m_fBreakPedal;
                        return;
                    }
                }
            }
        }
    }
    plugin::Call<0x41DA30>(vehicle);
}

void __fastcall CVehicle__SetVehicleCreatedBy_Hook(CVehicle* This, SKIP_EDX, int createdBy)
{
    This->SetVehicleCreatedBy(createdBy);

    if (!CCoopMission::IsScriptAuthority())
        return;

    if (auto pNetworkVehicle = CNetworkVehicleManager::GetVehicle(This))
    {
        Packets::Vehicles::SetVehicleCreatedBy packet{};
        packet.vehicleid = pNetworkVehicle->m_vehicleId;
        packet.createdBy = (eVehicleCreatedBy)createdBy;
        GetPacketFactory().Send(packet);
    }
}

void CTheScripts__CleanUpThisVehicle_Hook(CVehicle* vehicle)
{
    CTheScripts::CleanUpThisVehicle(vehicle);

    if (!CCoopMission::IsScriptAuthority())
        return;

    if (auto networkVehicle = CNetworkVehicleManager::GetVehicle(vehicle))
    {
        Packets::Vehicles::SetVehicleCreatedBy packet{};
        packet.vehicleid = networkVehicle->m_vehicleId;
        int createdBy = vehicle->m_nCreatedBy;
        packet.createdBy = (eVehicleCreatedBy)createdBy;
        GetPacketFactory().Send(packet);
    }
}

CPed* BmxFix_FindPlayerPed_Hook(CVehicle* vehicle)
{
    return vehicle->m_pDriver;
}

void __fastcall CPlayerPed__DoStuffToGoOnFire_Hook(CPlayerPed* This, SKIP_EDX)
{
    if (This != nullptr && This->IsPlayer())
    {
        This->DoStuffToGoOnFire();
    }
}

void __fastcall CFireManager__StartFire_Hook(
    void* This, SKIP_EDX, CPed* entity, CEntity* playerPed, float a4, int a5, int time, uint8_t numGenerations)
{
    if (entity != nullptr)
    {
        plugin::CallMethod<0x53A050>(This, entity, playerPed, a4, a5, time, numGenerations);
    }
}

static void CCarAI__UpdateCarAI_Hook(CVehicle* pVehicle)
{
    if (pVehicle->m_pDriver)
    {
        CNetworkPed* networkPed = CNetworkPedManager::GetPed(pVehicle->m_pDriver);
        if (networkPed && !networkPed->m_bSyncing)
        {
            pVehicle->m_fGasPedal = networkPed->m_fGasPedal;
            pVehicle->m_fBreakPedal = networkPed->m_fBreakPedal;
            pVehicle->m_fSteerAngle = networkPed->m_fSteerAngle;
            return;
        }
    }

    CCarAI::UpdateCarAI(pVehicle);
}

static void CCarCtrl__SteerAICarWithPhysics_Hook(CVehicle* pVehicle)
{
    if (pVehicle->m_pDriver)
    {
        CNetworkPed* networkPed = CNetworkPedManager::GetPed(pVehicle->m_pDriver);
        if (networkPed && !networkPed->m_bSyncing)
        {
            pVehicle->m_fGasPedal = networkPed->m_fGasPedal;
            pVehicle->m_fBreakPedal = networkPed->m_fBreakPedal;
            pVehicle->m_fSteerAngle = networkPed->m_fSteerAngle;
            return;
        }
    }

    CCarCtrl::SteerAICarWithPhysics(pVehicle);
}

// train-sync: a follower skips the random-train generator, the host keeps trains near network players
static void __fastcall CTrainSync__Generation_Hook()
{
    CTrainSync::ProcessGeneration();
}

static void CCarCtrl__UpdateCarOnRails_Hook(CVehicle* pVehicle)
{
    if (pVehicle->m_pDriver)
    {
        CNetworkPed* networkPed = CNetworkPedManager::GetPed(pVehicle->m_pDriver);
        if (networkPed && !networkPed->m_bSyncing)
        {
            pVehicle->m_fGasPedal = networkPed->m_fGasPedal;
            pVehicle->m_fBreakPedal = networkPed->m_fBreakPedal;
            pVehicle->m_fSteerAngle = networkPed->m_fSteerAngle;
            return;
        }
    }
    CCarCtrl::UpdateCarOnRails(pVehicle);
}

void VehicleHooks::InjectHooks()
{
    patch::RedirectCall(0x53C1CB, CCarCtrl__RemoveDistantCars_Hook);

    patch::RedirectCall(0x46DCE4, CDamageManager__ApplyDamage_Hook);
    patch::RedirectCall(0x46DFFD, CDamageManager__ApplyDamage_Hook);
    patch::RedirectCall(0x6A7C2A, CDamageManager__ApplyDamage_Hook);
    patch::RedirectCall(0x6A7DD5, CDamageManager__ApplyDamage_Hook);
    patch::RedirectCall(0x6A7E07, CDamageManager__ApplyDamage_Hook);
    patch::RedirectCall(0x6A7E39, CDamageManager__ApplyDamage_Hook);
    patch::RedirectCall(0x6A7E91, CDamageManager__ApplyDamage_Hook);
    patch::RedirectCall(0x6A7F5B, CDamageManager__ApplyDamage_Hook);
    patch::RedirectCall(0x6A7F8D, CDamageManager__ApplyDamage_Hook);
    patch::RedirectCall(0x6A803F, CDamageManager__ApplyDamage_Hook);

    patch::RedirectCall(0x6B3858, CDamageManager__FuckCarCompletely_Hook);
    patch::RedirectCall(0x6B3C5C, CDamageManager__FuckCarCompletely_Hook);
    patch::RedirectCall(0x6C6E4C, CDamageManager__FuckCarCompletely_Hook);
    patch::RedirectCall(0x6CCE94, CDamageManager__FuckCarCompletely_Hook);

    patch::RedirectCall(0x44828D, CVehicle__SetRemap_Hook);
    patch::RedirectCall(0x44B184, CVehicle__SetRemap_Hook);
    patch::RedirectCall(0x49872F, CVehicle__SetRemap_Hook);

    patch::RedirectCall(0x431998, CVehicle__AddVehicleUpgrade_Hook);
    patch::RedirectCall(0x4732DE, CVehicle__AddVehicleUpgrade_Hook);
    patch::RedirectCall(0x4985DA, CVehicle__AddVehicleUpgrade_Hook);
    patch::RedirectCall(0x49B106, CVehicle__AddVehicleUpgrade_Hook);
    patch::RedirectCall(0x6B0BD7, CVehicle__AddVehicleUpgrade_Hook);
    patch::RedirectCall(0x6E3424, CVehicle__AddVehicleUpgrade_Hook);

    patch::RedirectCall(0x4732F2, CVehicle__RemoveVehicleUpgrade_Hook);
    patch::RedirectCall(0x498618, CVehicle__RemoveVehicleUpgrade_Hook);

    patch::SetPointer(0x871148, CVehicle__ProcessControl_Hook);
    patch::SetPointer(0x8721C8, CVehicle__ProcessControl_Hook);
    patch::SetPointer(0x871388, CVehicle__ProcessControl_Hook);
    patch::SetPointer(0x871970, CVehicle__ProcessControl_Hook);
    patch::SetPointer(0x8716A8, CVehicle__ProcessControl_Hook);
    patch::SetPointer(0x871550, CVehicle__ProcessControl_Hook);
    patch::SetPointer(0x871800, CVehicle__ProcessControl_Hook);
    patch::SetPointer(0x871B10, CVehicle__ProcessControl_Hook);
    patch::SetPointer(0x872398, CVehicle__ProcessControl_Hook);

    // patch::SetPointer(0x871228, CAutomobile__ProcessAI_Hook); // CAutomobile
    
    patch::RedirectCall(0x6B51CD, CCarAI__UpdateCarAI_Hook);
    patch::RedirectCall(0x6B52A2, CCarAI__UpdateCarAI_Hook);
    patch::RedirectCall(0x6BCC64, CCarAI__UpdateCarAI_Hook);
    patch::RedirectCall(0x6BCD7B, CCarAI__UpdateCarAI_Hook);
    patch::RedirectCall(0x6C1B72, CCarAI__UpdateCarAI_Hook);
    patch::RedirectCall(0x6C1C5B, CCarAI__UpdateCarAI_Hook);
    patch::RedirectCall(0x6F1925, CCarAI__UpdateCarAI_Hook);

    patch::RedirectCall(0x6B52A8, CCarCtrl__SteerAICarWithPhysics_Hook);
    patch::RedirectCall(0x6BCD81, CCarCtrl__SteerAICarWithPhysics_Hook);
    patch::RedirectCall(0x6C1C61, CCarCtrl__SteerAICarWithPhysics_Hook);
    patch::RedirectCall(0x6F197F, CCarCtrl__SteerAICarWithPhysics_Hook);

    patch::RedirectCall(0x6B51DD, CCarCtrl__UpdateCarOnRails_Hook);
    patch::RedirectCall(0x6BCC74, CCarCtrl__UpdateCarOnRails_Hook);
    patch::RedirectCall(0x6C1B82, CCarCtrl__UpdateCarOnRails_Hook);

    patch::RedirectCall(0x6F35D6, CCarGenerator__CheckForBlockage_Hook);
    patch::RedirectCall(0x6F35FF, CCarGenerator__CheckForBlockage_Hook);

    patch::RedirectCall(0x434251, CCarCtrl__GenerateOneRandomCar_Hook);
    patch::RedirectCall(0x434263, CCarCtrl__GenerateOneRandomCar_Hook);
    patch::RedirectCall(0x434268, CCarCtrl__GenerateOneRandomCar_Hook);

    // train-sync: the random-train generator call runs only on the train authority (CTrainSync::ProcessGeneration)
    if (*(uint8_t*)0x43426D == 0xE8)
        patch::RedirectCall(0x43426D, CTrainSync__Generation_Hook);
    else
        logger::warn("train-sync: unexpected bytes at 0x43426D, generator not wrapped");

    // zone-population: the creation point of the traffic cars is split between the players
    patch::RedirectCall(0x430455, CCarCtrl__GenerateCarCreationCoors2_Hook);
    // police-per-player: the retry of the police branch aborts the civilian model choice (0x424CE0)
    patch::RedirectCall(0x43022A, CCarCtrl__ChooseModel_Hook);

    patch::RedirectCall(0x6B51CD, CCarCtrl__UpdateCarAI_Hook);
    patch::RedirectCall(0x6B52A2, CCarCtrl__UpdateCarAI_Hook);
    patch::RedirectCall(0x6BCC64, CCarCtrl__UpdateCarAI_Hook);
    patch::RedirectCall(0x6BCD7B, CCarCtrl__UpdateCarAI_Hook);
    patch::RedirectCall(0x6C1B72, CCarCtrl__UpdateCarAI_Hook);
    patch::RedirectCall(0x6C1C5B, CCarCtrl__UpdateCarAI_Hook);
    patch::RedirectCall(0x6F1925, CCarCtrl__UpdateCarAI_Hook);
    patch::RedirectCall(0x6F1979, CCarCtrl__UpdateCarAI_Hook);

    patch::RedirectCall(0x445A20, CVehicle__SetVehicleCreatedBy_Hook);
    patch::RedirectCall(0x445E64, CVehicle__SetVehicleCreatedBy_Hook);
    patch::RedirectCall(0x469506, CVehicle__SetVehicleCreatedBy_Hook);
    patch::RedirectCall(0x469542, CVehicle__SetVehicleCreatedBy_Hook);
    patch::RedirectCall(0x469567, CVehicle__SetVehicleCreatedBy_Hook);
    patch::RedirectCall(0x46EC89, CVehicle__SetVehicleCreatedBy_Hook);
    patch::RedirectCall(0x4815E1, CVehicle__SetVehicleCreatedBy_Hook);
    patch::RedirectCall(0x6F3C33, CVehicle__SetVehicleCreatedBy_Hook);

    if (CCore::gvm.IsHoodlum())
    {
        patch::RedirectCall(0x156217F, CVehicle__SetVehicleCreatedBy_Hook);

        patch::RedirectCall(0x468AEB, CTheScripts__CleanUpThisVehicle_Hook);
        patch::RedirectCall(0x47D775, CTheScripts__CleanUpThisVehicle_Hook);
        patch::RedirectCall(0x64CED4, CTheScripts__CleanUpThisVehicle_Hook);
        patch::RedirectCall(0x64D186, CTheScripts__CleanUpThisVehicle_Hook);
    }
    else
    {
        patch::RedirectCall(0x45AE9F, CVehicle__SetVehicleCreatedBy_Hook);
        patch::RedirectCall(0x4866AF, CVehicle__SetVehicleCreatedBy_Hook);
    }

    // fix bmx on fire, now driver being set on fire instead of the local player, compatible with SilentPatch
    patch::SetRaw(0x53A984, (void*)"\x90\x57", 2);  // nop, push edi
    patch::SetRaw(0x53A9A7, (void*)"\x90\x57", 2);  // nop, push edi
    patch::RedirectCall(0x53A984 + 2, BmxFix_FindPlayerPed_Hook);
    patch::RedirectCall(0x53A9A7 + 2, BmxFix_FindPlayerPed_Hook);
    patch::RedirectCall(0x53A990, CPlayerPed__DoStuffToGoOnFire_Hook);
    patch::RedirectCall(0x53A9B7, CFireManager__StartFire_Hook);

    // patch::RedirectCall(0x6D6494, CVehicle__PreRenderDriverAndPassengers_Hook); // TODO: enable this and in
    // proccontrol too
}
