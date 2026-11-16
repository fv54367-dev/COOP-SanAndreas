#include "common.h"
#include "stdafx.h"
#include "WorldHooks.h"
#include "CNetworkVehicle.h"
#include "CNetworkPed.h"
#include <CEntryExit.h>
#include <CEntryExitMarkerSync.h>
#include <CEntryExitTransitionSync.h>
#include <CShotInfo.h>
#include <game_sa/CTagManager.h>
#include <COpCodeSync.h>
#include <CInteriorOwnership.h>
#include <CMissionMirror.h>

// static void __cdecl CWeather__ForceWeather_Hook(short id)
// {
//     CWeather::ForceWeather(id);
//     // CPacketHandler::GameWeatherTime__Trigger();
// }

// static void __cdecl CWeather__ForceWeatherNow_Hook(short id)
// {
//     plugin::Call<0x402157>(id);
//     // CPacketHandler::GameWeatherTime__Trigger();
// }

// static void __cdecl CWeather__SetWeatherToAppropriateTypeNow_Hook()
// {
//     CWeather::SetWeatherToAppropriateTypeNow();
//     // CPacketHandler::GameWeatherTime__Trigger();
// }

// place waypoint
static CdeclEvent<AddressList<0x5775D2, H_CALL>, PRIORITY_AFTER,
    ArgPick5N<eBlipType, 0, CVector, 1, eBlipColour, 2, eBlipDisplay, 3, char*, 4>,
    void(eBlipType, CVector, eBlipColour, eBlipDisplay, char*)>
    waypointPlaceEvent;
static void PlaceWaypointHook(
    eBlipType type, CVector posn, eBlipColour color, eBlipDisplay blipDisplay, char* scriptName)
{
    Packets::Players::PlayerPlaceWaypoint playerPlaceWaypoint{};
    playerPlaceWaypoint.place = true;
    playerPlaceWaypoint.position.x = posn.x;
    playerPlaceWaypoint.position.y = posn.y;
    GetPacketFactory().Send(playerPlaceWaypoint);
}

// hide waypoint
static void __fastcall CRadar__ClearBlip_Hook_Waypoint(int blipIndex, SKIP_EDX)
{
    Packets::Players::PlayerPlaceWaypoint playerPlaceWaypoint{};
    playerPlaceWaypoint.place = false;
    GetPacketFactory().Send(playerPlaceWaypoint);

    CRadar::ClearBlip(blipIndex);
}

// The real ABI of 0x736A50 is AddExplosion(CEntity* victim, CEntity* creator, eExplosionType type, CVector pos,
// uint time, uchar makeSound, float camShake, uchar visibility): the position goes by value (12 bytes).
// plugin-sdk's CExplosion::AddExplosion declares `CVector const&` (4 bytes), so it cannot be used here.
// Explosions of the simulation (vehicles destroyed by damage, grenades, rockets) are NOT sent from this hook:
// every client already simulates them on its own and a packet would duplicate the particles, the sound and the
// damage. Script explosions travel as synced opcodes instead (see COpCodeSync::syncedOpcodes).
static bool __cdecl CExplosion__AddExplosion(CEntity* newVictim, CEntity* newCreator, eExplosionType type, CVector pos,
    unsigned int time, unsigned char usesSound, float cameraShake, unsigned char isVisible)
{
    return plugin::CallAndReturn<bool, 0x736A50, CEntity*, CEntity*, eExplosionType, CVector, unsigned int,
        unsigned char, float, unsigned char>(newVictim, newCreator, type, pos, time, usesSound, cameraShake, isVisible);
}

// the owner pressed the detonator (CWeapon::Fire / script use_detonator): tell the other clients, they only have
// their own copies of his satchel charges and would keep them stuck forever otherwise
static void __cdecl CWorld__UseDetonator_Hook(CEntity* creator)
{
    if (creator && creator == FindPlayerPed(0))
    {
        Packets::World::DetonateProjectiles packet{};
        packet.creator.SetEntity(creator);
        GetPacketFactory().Send(packet);
    }

    CWorld::UseDetonator(creator);
}

// PacketHandlers/world.cpp
extern void DetonateSatchelsOf(CEntity* creator);

// the game detonates every satchel in the world (single player only has the local player's ones); in coop this
// would blow up the charges of the other players too, so only the local player's charges detonate here
static void __cdecl CProjectileInfo__RemoveDetonatorProjectiles_Hook()
{
    DetonateSatchelsOf(FindPlayerPed(0));
}

static void __cdecl CWorld__Add_Hook(CEntity* entity)
{
    if (entity == nullptr)
        return;

    if (CNetwork::m_bAuthenticated)
    {
        bool dontCreateEntity = false;

        if (entity->m_nType == eEntityType::ENTITY_TYPE_VEHICLE)
        {
            CVehicle* vehicle = (CVehicle*)entity;
            if (CNetworkVehicleManager::GetVehicle(vehicle) != nullptr)  // fixes a strange bug
            {
                return;
            }
            CNetworkVehicle* networkVehicle = CNetworkVehicle::CreateHosted(vehicle);
        }
        else if (entity->m_nType == eEntityType::ENTITY_TYPE_PED)
        {
            CPed* ped = (CPed*)entity;

            CInteriorOwnership::ObserveLocalPed(ped);
            if (CConfigLoader::ms_bInteriorPopulationSync && FindPlayerPed(0) &&
                FindPlayerPed(0)->m_nAreaCode != CInteriorOwnership::GetLocalArea())
                CInteriorOwnership::Process();

            // guests of an interior don't sync (nor show) their own population: the owner's peds are shown instead
            if (CInteriorOwnership::ShouldHideLocalPed(ped))
            {
                CInteriorOwnership::HideLocalPed(ped);
            }
            // local actors of an individual mission stay local: the other players run their own copy
            else if (ped->m_nPedType > 1 && (CInteriorOwnership::IsServicePed(ped) ||
                !(ped->m_nCreatedBy == 2 && COpCodeSync::IsLocalOnlyMissionRunning())))
            {
                CNetworkPed* networkPed = CNetworkPed::CreateHosted(ped);
            }
        }
    }

    if (entity->IsVTableValid())
        CWorld::Add(entity);
}

// B2 handover: a vehicle with a player inside (local or remote) must never be handed over
static bool VehicleHasPlayerInside(CVehicle* vehicle)
{
    auto isPlayer = [](CPed* ped) { return ped != nullptr && (ped->IsPlayer() || CNetworkPlayerManager::GetPlayer(ped)); };

    if (isPlayer(vehicle->m_pDriver))
        return true;

    for (int i = 0; i < vehicle->m_nMaxPassengers && i < 8; i++)
    {
        if (isPlayer(vehicle->m_apPassengers[i]))
            return true;
    }

    return false;
}

static void __cdecl CWorld__Remove_Hook(CEntity* entity)
{
    if (entity == nullptr)
        return;

    // B2 handover: an ambient entity the game removes while it is still alive is not deleted for everyone; its
    // owner keeps the record as a remote streamed out copy and the server decides who hosts it next
    bool handover = CConfigLoader::ms_bHandover;

    if (entity->m_nType == eEntityType::ENTITY_TYPE_VEHICLE)
    {
        CVehicle* vehicle = (CVehicle*)entity;
        CNetworkVehicle* networkVehicle = CNetworkVehicleManager::GetVehicle(vehicle);
        if (networkVehicle && networkVehicle->m_bSyncing)
        {
            // C2: a police car is chasing its owner, freeing it would leave a cop with no target
            if (handover && CStreamingSync::IsAmbientVehicle(vehicle->m_nCreatedBy) &&
                vehicle->m_nStatus != STATUS_WRECKED && vehicle->m_fHealth > 0.0f && !VehicleHasPlayerInside(vehicle) &&
                !vehicle->IsLawEnforcementVehicle())
            {
                networkVehicle->Release();
            }
            else
            {
                CNetworkVehicleManager::Remove(networkVehicle);
                delete networkVehicle;
            }
        }
    }
    else if (entity->m_nType == eEntityType::ENTITY_TYPE_PED)
    {
        CPed* ped = (CPed*)entity;
        if (ped->m_nPedType > 3)
        {
            CNetworkPed* networkPed = CNetworkPedManager::GetPed(ped);
            if (networkPed && networkPed->m_bSyncing)
            {
                if (CConfigLoader::ms_bInteriorPopulationSync && networkPed->m_bInteriorPopulation)
                    CMissionMirror::LogEventf("interior-local-remove ped=%s model=%d area=%u health=%.1f",
                        to_string(networkPed->m_pedId).c_str(), ped->m_nModelIndex, ped->m_nAreaCode, ped->m_fHealth);
                // C2: the cops (SWAT / FBI / army are PED_TYPE_COP too) chase their owner: never hand them over
                if (handover && (CStreamingSync::IsAmbientPed(ped->m_nCreatedBy) ||
                    (CConfigLoader::ms_bInteriorPopulationSync && networkPed->m_bInteriorPopulation)) && ped->m_fHealth > 0.0f &&
                    !ped->IsPlayer() && ped->m_nPedType != PED_TYPE_COP)
                {
                    networkPed->Release();
                }
                else
                {
                    CNetworkPedManager::Remove(networkPed);
                    delete networkPed;
                }
            }
        }
    }

    if (entity && (unsigned int)*(void***)entity != 0x863C40)
    {
        CWorld::Remove(entity);
    }
}

static bool __fastcall CEntryExit__TransitionStarted_Hook(CEntryExit* This, SKIP_EDX, CPed* ped)
{
    bool result = This->TransitionStarted(ped);
    if (result)
    {
        CEntryExitTransitionSync::OnTransitionStarted(This, ped);
    }
    return result;
}

static bool __fastcall CEntryExit__TransitionFinished_Hook(CEntryExit* This, SKIP_EDX, CPed* ped)
{
    bool result = This->TransitionFinished(ped);
    if (result)
    {
        CEntryExitTransitionSync::OnTransitionFinished(This, ped);
    }
    return result;
}

int CWorld__SprayPaintWorld_Hook(CVector* posn, CVector* outDir, float radius, bool processTagAlphaState)
{
    CShotInfo* shotInfo = (CShotInfo*)((uintptr_t)posn - 0x4);

    if (FindPlayerPed(0) != shotInfo->m_pCreator)
    {
        auto networkPed = CNetworkPedManager::GetPed(
            shotInfo->m_pCreator);  // peds can respray tags too, for example Sweet on the 3rd mission "Tagging Up Turf"
        if (!networkPed || !networkPed->m_bSyncing)
        {
            return 0;
        }
    }

    // the sprayed tags reach the others through CCollectibleSync (only in coop mission mode)
    return CWorld::SprayPaintWorld(*posn, *outDir, radius, processTagAlphaState);
}

// disable paynspray if you are a passenger
static void __declspec(naked) CGarage__Update_Hook()
{
    static CPlayerPed* pPlayerPed;
    static CVehicle* pVehicle;

    __asm
        {
        push esi
        }

    pPlayerPed = FindPlayerPed(0);
    pVehicle = pPlayerPed ? pPlayerPed->m_pVehicle : nullptr;
    // on foot (no vehicle) the garage works as usual
    if (!pVehicle || pPlayerPed == pVehicle->m_pDriver)
    {
        __asm
        {
            pop esi

            mov al, [esi + 0x4E]  // 0x44B2DE
            test al, al  // 0x44B2E1

            push 0x44B2E3
            retn
        }
    }
    else
    {
        __asm
        {
            pop esi
            push 0x44B38D
            retn
        }
    }
}
void WorldHooks::InjectHooks()
{
    waypointPlaceEvent += PlaceWaypointHook;
    patch::RedirectCall(0x577582, CRadar__ClearBlip_Hook_Waypoint);

    patch::RedirectCall(0x458475, CExplosion__AddExplosion);
    patch::RedirectCall(0x47752F, CExplosion__AddExplosion);
    patch::RedirectCall(0x47E4E2, CExplosion__AddExplosion);
    patch::RedirectCall(0x5A13B9, CExplosion__AddExplosion);
    patch::RedirectCall(0x6A74EA, CExplosion__AddExplosion);
    patch::RedirectCall(0x6AF0BA, CExplosion__AddExplosion);
    patch::RedirectCall(0x6B3B93, CExplosion__AddExplosion);
    patch::RedirectCall(0x6B3E44, CExplosion__AddExplosion);
    patch::RedirectCall(0x6BEB08, CExplosion__AddExplosion);
    patch::RedirectCall(0x6C703D, CExplosion__AddExplosion);
    patch::RedirectCall(0x6F22BD, CExplosion__AddExplosion);
    patch::RedirectCall(0x737929, CExplosion__AddExplosion);
    patch::RedirectCall(0x737C72, CExplosion__AddExplosion);
    patch::RedirectCall(0x7388B7, CExplosion__AddExplosion);
    patch::RedirectCall(0x738952, CExplosion__AddExplosion);
    patch::RedirectCall(0x738AAE, CExplosion__AddExplosion);

    // satchel charges: the owner's detonation is mirrored on the other clients (CWeapon::Fire and the
    // use_detonator opcode are the only callers of CWorld::UseDetonator)
    patch::RedirectCall(0x742770, CWorld__UseDetonator_Hook);
    patch::RedirectCall(0x47AC56, CWorld__UseDetonator_Hook);
    patch::RedirectJump(0x738860, CProjectileInfo__RemoveDetonatorProjectiles_Hook);

    const int CWorld__Add_Addresses[] = {0x404BE4, 0x42BBC3, 0x42C5FC, 0x42CA98, 0x431BE6, 0x4320B1, 0x432239, 0x43A416,
        0x43A631, 0x44231F, 0x444E2C, 0x4456B4, 0x45733A, 0x4575C0, 0x45881B, 0x458E70, 0x45ACBE, 0x45BF05, 0x45CCBB,
        0x45ED24, 0x45ED7B, 0x45F5D5, 0x45F708, 0x45F846, 0x45F979, 0x45FAAC, 0x45FBF3, 0x461820, 0x4621B6, 0x46237F,
        0x462993, 0x467726, 0x469878, 0x469D6D, 0x47D9B7, 0x47F5EB, 0x4834D6, 0x484FF4, 0x491DFD, 0x49976D, 0x499A49,
        0x556169, 0x592FAE, 0x5936E6, 0x5A3775, 0x5B16AD, 0x5B5348, 0x5D2D10, 0x5D2E2E, 0x5D4B1D, 0x5D5125, 0x5E4471,
        0x612A10, 0x6136B7, 0x6138DF, 0x615868, 0x61B059, 0x61B09B, 0x61B117, 0x61B174, 0x61B20E, 0x61B264, 0x6A8BBE,
        0x6A9BE2, 0x6C685D, 0x6CD40E, 0x6CD71D, 0x6E44E0, 0x6EADF9, 0x6F2503, 0x6F3C8C, 0x717E7F, 0x717F3E};
    patch::RedirectCall(
        std::vector<int>(CWorld__Add_Addresses, CWorld__Add_Addresses + sizeof(CWorld__Add_Addresses) / 4),
        CWorld__Add_Hook);

    if (CCore::gvm.IsHoodlum())
    {
        patch::RedirectCall(0x15614C6, CWorld__Add_Hook);
        patch::RedirectCall(0x156D903, CWorld__Add_Hook);
    }

    const int CWorld__Remove_Addresses[] = {0x404B90, 0x404BED, 0x404C3E, 0x409E43, 0x4251E6, 0x425221, 0x42541E,
        0x4413CA, 0x442319, 0x449729, 0x4499F3, 0x449B43, 0x449CE0, 0x449E2A, 0x454CFB, 0x455488, 0x4556F1, 0x456C29,
        0x456E1E, 0x456EA0, 0x4571AD, 0x458E8A, 0x45BEFF, 0x45CCB5, 0x45D3C9, 0x45D3FE, 0x45ED11, 0x45ED69, 0x45FF78,
        0x45FFCB, 0x4610CC, 0x4698E4, 0x467B3C, 0x486D3E, 0x499D90, 0x49A45A, 0x53C98C, 0x5500C5, 0x5567CE, 0x5667B0,
        0x59163E, 0x593794, 0x5A1890, 0x5A18EA, 0x5A194E, 0x5A1A51, 0x5A32A2, 0x5A3FE2, 0x5AFE6E, 0x5D5112, 0x5E011C,
        0x5E03DC, 0x5E86AF, 0x6094FC, 0x610F26, 0x612305, 0x612F1A, 0x615EA6, 0x615F2D, 0x616404, 0x61AEE3, 0x61AF07,
        0x6C7B9A, 0x6CCCB2, 0x6D22D7, 0x6E3E7D, 0x6E3FFD, 0x6E4120, 0x6E51B4, 0x6F5DD7, 0x6F6A7B, 0x6F6B31, 0x6F7ACA,
        0x717897, 0x738AFF, 0x73997A, 0x739A17, 0x739AD0};
    patch::RedirectCall(
        std::vector<int>(CWorld__Remove_Addresses, CWorld__Remove_Addresses + sizeof(CWorld__Remove_Addresses) / 4),
        CWorld__Remove_Hook);

    // patch::RedirectJump(0x47D43E, CWeather__ForceWeather_Hook);
    // patch::RedirectJump(0x72A4F0, CWeather__ForceWeatherNow_Hook);
    // patch::RedirectCall(0x47679F, CWeather__SetWeatherToAppropriateTypeNow_Hook);

    patch::RedirectCall(0x73A0FF, CWorld__SprayPaintWorld_Hook);

    // patch::RedirectCall(0x533A8D, CEntryExitManager__AddOne_Hook);
    // patch::RedirectCall(0x5B812D, CEntryExitManager__AddOne_Hook);

    patch::RedirectCall(0x44104C, CEntryExit__TransitionStarted_Hook);
    patch::RedirectCall(0x440F89, CEntryExit__TransitionFinished_Hook);

    patch::RedirectJump(0x44B2DE, CGarage__Update_Hook);

    // patch::Nop(0x547D96, 10);
    // patch::Nop(0x547EDD, 10);
    // patch::Nop(0x5480B0, 10);
    // patch::Nop(0x548248, 10);
    // patch::RedirectCall(0x547D96, ApplyMoveAndTurnSpeed_Hook);
    // patch::RedirectCall(0x547EDD, ApplyMoveAndTurnSpeed_Hook);
    // patch::RedirectCall(0x5480B0, ApplyMoveAndTurnSpeed_Hook);
    // patch::RedirectCall(0x548248, ApplyMoveAndTurnSpeed_Hook);

    // patch::RedirectCall(0x56897B, CPhysical__ApplyMoveSpeed_Hook);
    // patch::RedirectCall(0x6A2A37, CPhysical__ApplyMoveSpeed_Hook);
    // patch::RedirectCall(0x6A2A69, CPhysical__ApplyMoveSpeed_Hook);
    // patch::RedirectCall(0x6B6697, CPhysical__ApplyMoveSpeed_Hook);
    // patch::RedirectCall(0x6B66C9, CPhysical__ApplyMoveSpeed_Hook);
    // patch::RedirectCall(0x6F970D, CPhysical__ApplyMoveSpeed_Hook);
}
