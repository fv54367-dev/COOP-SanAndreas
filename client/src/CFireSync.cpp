#include "stdafx.h"
#include "CFireSync.h"
#include <CFireManager.h>
#include <CTimer.h>

namespace
{
    enum class eFireSlotState : uint8_t
    {
        FREE,
        OWNED,   // started here, announced to the others with our id
        MIRROR,  // copy of another client's fire
    };

    struct SFireSlot
    {
        eFireSlotState state = eFireSlotState::FREE;
        uint32_t fireid = 0;          // id of the network fire this slot represents
        uint32_t linkedFireid = 0;    // OWNED slot that also represents a remote fire on the same spot
        CVector pos{};
        bool removedByNetwork = false;
    };

    SFireSlot ms_slots[MAX_NUM_FIRES];
    uint32_t ms_nextFireCounter = 1;
}

static uint32_t AllocateFireId()
{
    return (CNetworkPlayerManager::m_localPlayerId.value() << 24) | (ms_nextFireCounter++ & 0xFFFFFF);
}

static uint32_t GetFireOwner(uint32_t fireid)
{
    return fireid >> 24;
}

static int FindSlotByFireId(uint32_t fireid)
{
    for (int i = 0; i < MAX_NUM_FIRES; i++)
    {
        if (ms_slots[i].state != eFireSlotState::FREE &&
            (ms_slots[i].fireid == fireid || ms_slots[i].linkedFireid == fireid))
            return i;
    }
    return -1;
}

static int GetFireSlotIndex(CFire* fire)
{
    return (int)(fire - gFireManager.m_aFires);
}

static void SendRemove(uint32_t fireid, bool extinguishRequest)
{
    Packets::World::FireRemove packet{};
    packet.fireid = fireid;
    packet.extinguishRequest = extinguishRequest;
    GetPacketFactory().Send(packet);
}

void CFireSync::Process()
{
    if (!CNetwork::m_bAuthenticated)
        return;

    unsigned int now = CTimer::m_snTimeInMilliseconds;

    for (int i = 0; i < MAX_NUM_FIRES; i++)
    {
        CFire& fire = gFireManager.m_aFires[i];
        SFireSlot& slot = ms_slots[i];
        bool active = fire.m_nFlags.bActive;

        // a slot reused by a different fire since the last scan
        if (active && slot.state != eFireSlotState::FREE && DistanceBetweenPoints(slot.pos, fire.m_vecPosition) > 5.0f)
        {
            if (slot.state == eFireSlotState::OWNED)
                SendRemove(slot.fireid, false);
            slot = SFireSlot{};
        }

        if (!active)
        {
            if (slot.state == eFireSlotState::OWNED)
            {
                // our fire ended (burnt out or extinguished here): remove the copies
                SendRemove(slot.fireid, false);
                if (slot.linkedFireid)
                    SendRemove(slot.linkedFireid, true);
            }
            else if (slot.state == eFireSlotState::MIRROR && !slot.removedByNetwork)
            {
                // the copy was put out here: ask the owner to put the original out
                SendRemove(slot.fireid, true);
            }
            slot = SFireSlot{};
            continue;
        }

        if (slot.state != eFireSlotState::FREE)
        {
            slot.pos = fire.m_vecPosition;
            continue;
        }

        // a new local fire: announce it
        slot.state = eFireSlotState::OWNED;
        slot.fireid = AllocateFireId();
        slot.pos = fire.m_vecPosition;

        Packets::World::FireStart packet{};
        packet.fireid = slot.fireid;
        packet.pos = fire.m_vecPosition;
        packet.strength = std::clamp(fire.m_fStrength, 0.0f, 10.0f);
        packet.remainingTime = fire.m_nTimeToBurn > now ? std::min(fire.m_nTimeToBurn - now, 600000u) : 0;
        packet.hasTarget = false;

        if (fire.m_pEntityTarget && (fire.m_pEntityTarget->m_nType == ENTITY_TYPE_PED ||
                                        fire.m_pEntityTarget->m_nType == ENTITY_TYPE_VEHICLE))
        {
            packet.target.SetEntity(fire.m_pEntityTarget);
            packet.hasTarget = true;
        }

        GetPacketFactory().Send(packet);
    }
}

void CFireSync::HandleStart(Packets::World::FireStart* packet)
{
    if (FindSlotByFireId(packet->fireid) != -1)
        return;

    // the same fire already burns here (both clients processed the same explosion): link them
    for (int i = 0; i < MAX_NUM_FIRES; i++)
    {
        CFire& fire = gFireManager.m_aFires[i];
        SFireSlot& slot = ms_slots[i];

        if (fire.m_nFlags.bActive && slot.state == eFireSlotState::OWNED && slot.linkedFireid == 0 &&
            DistanceBetweenPoints(fire.m_vecPosition, packet->pos) < 1.5f)
        {
            slot.linkedFireid = packet->fireid;
            return;
        }
    }

    unsigned int burnTime = packet->remainingTime + 1000;
    CFire* fire = nullptr;

    CEntity* target = packet->hasTarget ? packet->target.GetEntity() : nullptr;
    if (target)
    {
        // an entity that is already burning keeps its fire
        if (target->m_nType == ENTITY_TYPE_PED && ((CPed*)target)->m_pFire)
            return;
        if (target->m_nType == ENTITY_TYPE_VEHICLE && ((CVehicle*)target)->m_pFire)
            return;

        // network players are fireproof here (their own client handles their damage), which also stops the game
        // from setting them on fire: lift it only to attach the visual fire
        CPhysical* physical = (CPhysical*)target;
        bool wasFireProof = physical->m_nPhysicalFlags.bFireProof;
        bool isNetworkPlayer = target->m_nType == ENTITY_TYPE_PED && target != FindPlayerPed(0) &&
                               CNetworkPlayerManager::GetPlayer(target) != nullptr;
        if (isNetworkPlayer)
            physical->m_nPhysicalFlags.bFireProof = false;

        fire = gFireManager.StartFire(target, nullptr, packet->strength, 1, burnTime, 0);

        if (isNetworkPlayer)
            physical->m_nPhysicalFlags.bFireProof = wasFireProof;
    }
    else
    {
        fire = gFireManager.StartFire(packet->pos, packet->strength, 1, nullptr, burnTime, 0, 1);
    }

    if (!fire)
        return;

    fire->m_fStrength = packet->strength;
    fire->m_nNumGenerationsAllowed = 0;  // spreading comes from the owner
    fire->m_nTimeToBurn = CTimer::m_snTimeInMilliseconds + burnTime;

    SFireSlot& slot = ms_slots[GetFireSlotIndex(fire)];
    slot = SFireSlot{};
    slot.state = eFireSlotState::MIRROR;
    slot.fireid = packet->fireid;
    slot.pos = fire->m_vecPosition;
}

void CFireSync::HandleRemove(Packets::World::FireRemove* packet)
{
    int index = FindSlotByFireId(packet->fireid);
    if (index == -1)
        return;

    SFireSlot& slot = ms_slots[index];
    CFire& fire = gFireManager.m_aFires[index];
    bool isOwnFire = GetFireOwner(packet->fireid) == CNetworkPlayerManager::m_localPlayerId.value();

    if (packet->extinguishRequest)
    {
        // only the owner (or the client that linked it) puts its fire out; the next scan announces the end
        if ((isOwnFire || slot.linkedFireid == packet->fireid) && fire.m_nFlags.bActive)
            fire.Extinguish();
        return;
    }

    // the owner's fire ended
    if (slot.state == eFireSlotState::MIRROR)
    {
        slot.removedByNetwork = true;
        if (fire.m_nFlags.bActive)
            fire.Extinguish();
    }
    else if (slot.linkedFireid == packet->fireid)
    {
        slot.linkedFireid = 0;
        if (fire.m_nFlags.bActive)
            fire.Extinguish();
    }
}
