#include "stdafx.h"
#include "CPickupSync.h"
#include <CPickups.h>

struct SSyncedPickup
{
    uint32_t pickupid;
    int handle;
};

static std::vector<SSyncedPickup> ms_syncedPickups;
static uint32_t ms_nextPickupCounter = 0;

static constexpr size_t MAX_SYNCED_PICKUPS = 64;
static constexpr int NUM_PICKUP_SLOTS = 620;

// state of every pickup slot right before a dead ped drops its weapons/money
static short ms_aSlotReferenceBefore[NUM_PICKUP_SLOTS];
static unsigned char ms_aSlotTypeBefore[NUM_PICKUP_SLOTS];

// collecting a pickup only sets its type to PICKUP_NONE, the handle stays valid until the slot is reused
static bool IsPickupAlive(int handle)
{
    int index = CPickups::GetActualPickupIndex(handle);
    return index != -1 && CPickups::aPickUps[index].m_nPickupType != PICKUP_NONE;
}

static void RemoveTrackedPickup(size_t index)
{
    ms_syncedPickups.erase(ms_syncedPickups.begin() + index);
}

static void TakePickupSnapshot()
{
    for (int i = 0; i < NUM_PICKUP_SLOTS; i++)
    {
        ms_aSlotReferenceBefore[i] = CPickups::aPickUps[i].m_nReferenceIndex;
        ms_aSlotTypeBefore[i] = CPickups::aPickUps[i].m_nPickupType;
    }
}

// sends every pickup created since TakePickupSnapshot (the game code that creates them is partly protected in the
// 1.0 exe, so instead of hooking it we compare the pickup slots)
static void AnnounceNewPickups()
{
    for (int i = 0; i < NUM_PICKUP_SLOTS; i++)
    {
        CPickup& pickup = CPickups::aPickUps[i];

        if (pickup.m_nPickupType == PICKUP_NONE)
            continue;

        if (pickup.m_nReferenceIndex == ms_aSlotReferenceBefore[i] && ms_aSlotTypeBefore[i] != PICKUP_NONE)
            continue;  // already existed (an existing pickup that got more ammo is not synced)

        if (ms_syncedPickups.size() >= MAX_SYNCED_PICKUPS)
            ms_syncedPickups.erase(ms_syncedPickups.begin());

        Packets::World::PickupSpawn packet{};
        packet.pickupid = (CNetworkPlayerManager::m_localPlayerId.value() << 24) | (ms_nextPickupCounter++ & 0xFFFFFF);
        packet.modelId = pickup.m_nModelIndex;
        packet.pickupType = pickup.m_nPickupType;
        packet.ammo = std::min(pickup.m_nAmmo, 99999u);
        packet.pos = pickup.GetPosn();
        GetPacketFactory().Send(packet);

        ms_syncedPickups.push_back({packet.pickupid, (int)(((uint32_t)(uint16_t)pickup.m_nReferenceIndex << 16) | (uint32_t)i)});
    }
}

// the client that syncs a dead ped creates its drops and sends them, the others skip theirs
static bool ShouldCreateDeadPedDrops(CPed* ped)
{
    if (!CNetwork::m_bAuthenticated)
        return true;

    // a network player's own client handles his drops
    if (ped != FindPlayerPed(0) && CNetworkPlayerManager::GetPlayer(ped))
        return false;

    if (auto networkPed = CNetworkPedManager::GetPed(ped))
        return networkPed->m_bSyncing;

    return true;
}

static void __fastcall CPed__CreateDeadPedWeaponPickups_Hook(CPed* This)
{
    if (!ShouldCreateDeadPedDrops(This))
        return;

    if (!CNetwork::m_bAuthenticated)
    {
        This->CreateDeadPedWeaponPickups();
        return;
    }

    TakePickupSnapshot();
    This->CreateDeadPedWeaponPickups();
    AnnounceNewPickups();
}

static void __fastcall CPed__CreateDeadPedMoney_Hook(CPed* This)
{
    if (!ShouldCreateDeadPedDrops(This))
        return;

    if (!CNetwork::m_bAuthenticated)
    {
        This->CreateDeadPedMoney();
        return;
    }

    TakePickupSnapshot();
    This->CreateDeadPedMoney();
    AnnounceNewPickups();
}

void CPickupSync::InjectHooks()
{
    // both inside CTaskComplexDie::CreateFirstSubTask
    patch::RedirectCall(0x630755, CPed__CreateDeadPedWeaponPickups_Hook);
    patch::RedirectCall(0x63075C, CPed__CreateDeadPedMoney_Hook);
}

void CPickupSync::Process()
{
    for (size_t i = 0; i < ms_syncedPickups.size();)
    {
        if (!IsPickupAlive(ms_syncedPickups[i].handle))
        {
            // picked up (or timed out) here, remove it for the others
            Packets::World::PickupRemove packet{};
            packet.pickupid = ms_syncedPickups[i].pickupid;
            GetPacketFactory().Send(packet);

            RemoveTrackedPickup(i);
            continue;
        }
        i++;
    }
}

void CPickupSync::HandleSpawn(Packets::World::PickupSpawn* packet)
{
    int modelId = packet->modelId;
    if (modelId <= 0 || modelId >= 20000 || !CModelInfo::ms_modelInfoPtrs[modelId])
        return;

    for (auto& pickup : ms_syncedPickups)
    {
        if (pickup.pickupid == packet->pickupid)
            return;
    }

    if (ms_syncedPickups.size() >= MAX_SYNCED_PICKUPS)
        ms_syncedPickups.erase(ms_syncedPickups.begin());

    // without its model loaded the pickup has no visible object
    if (CStreaming::ms_aInfoForModel[modelId].m_nLoadState != LOADSTATE_LOADED)
    {
        CStreaming::RequestModel(modelId, eStreamingFlags::GAME_REQUIRED);
        CStreaming::LoadAllRequestedModels(false);
    }

    int handle = CPickups::GenerateNewOne(packet->pos, modelId, packet->pickupType, packet->ammo, 0, false, nullptr);

    if (handle != -1)
        ms_syncedPickups.push_back({packet->pickupid, handle});
}

void CPickupSync::HandleRemove(Packets::World::PickupRemove* packet)
{
    for (size_t i = 0; i < ms_syncedPickups.size(); i++)
    {
        if (ms_syncedPickups[i].pickupid != packet->pickupid)
            continue;

        if (IsPickupAlive(ms_syncedPickups[i].handle))
            CPickups::RemovePickUp(ms_syncedPickups[i].handle);

        RemoveTrackedPickup(i);
        return;
    }
}
