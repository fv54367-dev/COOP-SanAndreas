// Script-driven doors replicate native state. Service doors mirror the native door object's motion.
// Adapted from Karimov-Akbar/sa-coop (MIT), features/garages/Garages.cpp (ScriptDoor types, door state handling).
// Layout and state values checked against gta-reversed Garage.h/Garage.cpp (OpenThisGarage / CloseThisGarage):
// CGarages::aGarages 0x96C048 (0xD8 each), NumGarages 0x96C024, m_fDoorPosition +0x38, m_szName +0x44,
// m_nType +0x4C, m_nDoorState +0x4D, flags +0x4E (bit 6 m_bDoorClosed: the door object's collision).
#include "stdafx.h"
#include "CGarageSync.h"
#include "CConfigLoader.h"
#include "CMissionMode.h"
#include <cstring>
#include <CObject.h>
#include <Patch.h>
#include <algorithm>

namespace
{
constexpr uintptr_t GARAGES = 0x96C048, NUM_GARAGES = 0x96C024;
constexpr size_t GARAGE_SIZE = 0xD8;
constexpr int MAX_GARAGES = 64;  // packet index range
enum : uint8_t { DOOR_CLOSED = 0, DOOR_OPEN = 1, DOOR_CLOSING = 2, DOOR_OPENING = 3, DOOR_WAITING_EXIT = 4, DOOR_CLOSED_DROPPED = 5 };

int8_t s_known[MAX_GARAGES];    // last state seen or applied: -1 unknown, 0 shut, 1 open
int8_t s_pending[MAX_GARAGES];  // remote state waiting to be applied: -1 none
uint32_t s_pendingHash[MAX_GARAGES];
struct VisualDoor
{
    bool active = false;
    bool open = false;
    uint32_t hash = 0;
    uint32_t changedAt = 0;
    float from = 0.0f;

    float Position(uint32_t now) const
    {
        const float progress = std::min((now - changedAt) / 850.0f, 1.0f);
        return from + ((open ? 1.0f : 0.0f) - from) * progress;
    }
};
VisualDoor s_visual[MAX_GARAGES];
bool s_init = false;

uint8_t* GarageAt(int i)
{
    const int count = *reinterpret_cast<int32_t*>(NUM_GARAGES);
    return i >= 0 && i < count && i < MAX_GARAGES ? reinterpret_cast<uint8_t*>(GARAGES + i * GARAGE_SIZE) : nullptr;
}
// script types: ONLY_TARGET_VEH, OPEN_FOR_TARGET_FREEZE_PLAYER, SCRIPT_ONLY_OPEN, SCRIPT_CONTROLLED,
// STAY_OPEN_WITH_CAR_INSIDE, CLOSE_WITH_CAR_DONT_OPEN_AGAIN, SCRIPT_OPEN_FREEZE_WHEN_CLOSING
bool Synced(uint8_t type) { return type == 1 || type == 14 || type == 15 || (type >= 19 && type <= 21) || type == 23; }
uint32_t NameHash(const uint8_t* g)
{
    uint32_t h = 2166136261u;
    for (int i = 0; i < 8 && g[0x44 + i]; ++i) h = (h ^ g[0x44 + i]) * 16777619u;
    return h;
}
int8_t IsOpen(const uint8_t* g)
{
    const uint8_t s = g[0x4D];
    return s == DOOR_OPEN || s == DOOR_OPENING || s == DOOR_WAITING_EXIT;
}
// gta-reversed CObject::ProcessGarageDoorBehaviour (0x44A4D0).
// Only the door object consumes the temporary position; CGarage::Update keeps local authority.
void __fastcall ProcessDoorObject(CObject* door, void*)
{
    const int index = door->m_nGarageDoorGarageIndex;
    auto* garage = s_init && CConfigLoader::ms_bGarageSync ? GarageAt(index) : nullptr;
    // only while the local door rests (a Pay 'n' Spray rests open, a hideout rests closed): local use wins
    if (!garage || !s_visual[index].active || s_visual[index].hash != NameHash(garage) ||
        (garage[0x4D] != DOOR_CLOSED && garage[0x4D] != DOOR_CLOSED_DROPPED && garage[0x4D] != DOOR_OPEN))
    {
        door->ProcessGarageDoorBehaviour();
        return;
    }

    auto& position = *reinterpret_cast<float*>(garage + 0x38);
    auto& flags = garage[0x4E];
    const float localPosition = position;
    const uint8_t localFlags = flags;
    position = s_visual[index].Position(GetTickCount());
    flags = position < 0.5f ? (flags | 0x40) : (flags & ~0x40);
    door->ProcessGarageDoorBehaviour();
    position = localPosition;
    flags = localFlags;
}

void Reset()
{
    std::memset(s_known, -1, sizeof(s_known));
    std::memset(s_pending, -1, sizeof(s_pending));
    for (auto& door : s_visual)
        door = {};
    s_init = true;
}
}  // namespace

void CGarageSync::HandlePacket(const Packets::World::GarageDoor& packet)
{
    if (!CConfigLoader::ms_bGarageSync || packet.index >= MAX_GARAGES) return;
    if (!s_init) Reset();
    if (packet.visualOnly || CMissionMode::IsIndividual())
    {
        auto* garage = GarageAt(packet.index);
        if (garage && (Synced(garage[0x4C]) == packet.visualOnly || NameHash(garage) != packet.nameHash))
            return;
        auto& visual = s_visual[packet.index];
        if (visual.active && visual.open == packet.open && visual.hash == packet.nameHash)
            return;
        const uint32_t now = GetTickCount();
        const float from = visual.active && visual.hash == packet.nameHash ? visual.Position(now) : 0.0f;
        visual = {true, packet.open, packet.nameHash, now, from};
        return;
    }
    s_pending[packet.index] = packet.open ? 1 : 0;
    s_pendingHash[packet.index] = packet.nameHash;
}

void CGarageSync::Process()
{
    static const bool hooked = [] {
        plugin::patch::RedirectCall(0x5A2B7A, ProcessDoorObject);
        return true;
    }();
    (void)hooked;
    if (!CConfigLoader::ms_bGarageSync || !CNetwork::m_bConnected || !CNetwork::m_bAuthenticated)
    {
        s_init = false;  // relearn the local world when sync becomes available
        return;
    }
    if (!FindPlayerPed(0)) return;
    if (!s_init) Reset();
    for (int i = 0; i < MAX_GARAGES; ++i)
    {
        uint8_t* g = GarageAt(i);
        if (!g || !g[0x4C]) continue;
        const bool visualOnly = !Synced(g[0x4C]);
        if (!visualOnly && CMissionMode::IsIndividual())
        {
            s_known[i] = -1;
            s_pending[i] = -1;
            continue;
        }
        const int8_t now = IsOpen(g);
        if (s_pending[i] >= 0)
        {
            const int8_t want = s_pending[i];
            s_pending[i] = -1;
            if (s_pendingHash[i] == NameHash(g) && want != now)
            {
                // same as CGarage::OpenThisGarage / CloseThisGarage: the door then slides by CGarage::Update
                if (want && (g[0x4D] == DOOR_CLOSED || g[0x4D] == DOOR_CLOSING || g[0x4D] == DOOR_CLOSED_DROPPED))
                    g[0x4D] = DOOR_OPENING;
                else if (!want && (g[0x4D] == DOOR_OPEN || g[0x4D] == DOOR_OPENING))
                    g[0x4D] = DOOR_CLOSING;
            }
            s_known[i] = IsOpen(g);  // what is really there, so a refused change is not sent back as ours
            continue;
        }
        if (s_known[i] < 0)
        {
            s_known[i] = now;  // first sight: not a change
            continue;
        }
        if (now != s_known[i])
        {
            s_known[i] = now;
            Packets::World::GarageDoor packet{};
            packet.index = static_cast<uint8_t>(i);
            packet.nameHash = NameHash(g);
            packet.open = now != 0;
            packet.visualOnly = visualOnly;
            if (visualOnly)
                s_visual[i] = {};
            GetPacketFactory().Send(packet);
        }
    }
}
