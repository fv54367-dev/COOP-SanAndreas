#include "stdafx.h"
#include "CGangSync.h"
#include "network/packets/gangs.h"
#include <CGangWars.h>
#include <CTheZones.h>
#include <eGangID.h>

namespace
{
constexpr int MAX_ZONE_INFOS = Packets::Gangs::MAX_ZONE_INFOS;
constexpr int ZONE_INFO_SIZE = Packets::Gangs::ZONE_INFO_SIZE;

// ---------------------------------------------------------------------------------------------------------
// game memory (GTA SA 1.0 US, verified in gta_sa.exe)
// ---------------------------------------------------------------------------------------------------------

// CTheZones::ZoneInfoArray:: the plugin-sdk member points to a different array, so the addresses are
// hardcoded here: 380 CZoneInfo entries of 0x11 bytes, int16 counter right before the array.
CZoneInfo* GetZoneInfos()
{
    return reinterpret_cast<CZoneInfo*>(0xBA1DF0);
}

int16_t& GetTotalZoneInfos()
{
    return *reinterpret_cast<int16_t*>(0xBA1DE8);
}

int ClampInt(int value, int min, int max)
{
    if (value < min)
        return min;
    if (value > max)
        return max;
    return value;
}

int GetZoneInfoCount()
{
    return ClampInt(GetTotalZoneInfos(), 0, MAX_ZONE_INFOS);
}

// CZoneInfo flags, bits 5-6: 0 = no gang colour on the radar, 1 = normal colour, 2 = blinking because the
// territory is being attacked (set by CGangWars::StartDefensiveGangWar).
constexpr int RADAR_MODE_SHIFT = 5;
constexpr int RADAR_MODE_MASK = 0x60;
constexpr int RADAR_MODE_NONE = 0;
constexpr int RADAR_MODE_ATTACK = 2;

int GetRadarMode(const CZoneInfo& zoneInfo)
{
    return (static_cast<uint8_t>(zoneInfo.m_nFlags) & RADAR_MODE_MASK) >> RADAR_MODE_SHIFT;
}

void SetRadarMode(CZoneInfo& zoneInfo, int mode)
{
    zoneInfo.m_nFlags = static_cast<char>((static_cast<uint8_t>(zoneInfo.m_nFlags) & ~RADAR_MODE_MASK) |
                                          ((mode << RADAR_MODE_SHIFT) & RADAR_MODE_MASK));
}

bool HasGangDensity(const uint8_t* data)
{
    for (int i = 0; i < 10; i++)
    {
        if (data[i] != 0)
            return true;
    }
    return false;
}

bool HasGangDensity(const CZoneInfo& zoneInfo)
{
    return HasGangDensity(reinterpret_cast<const uint8_t*>(&zoneInfo));
}

// What the host mirrors: the gang densities (+0x0) and the radar colour + flags (+0xB, 5 bytes). The dealer
// strength in between changes on its own while a war is fought and is not worth a packet.
bool SameZoneData(const uint8_t* a, const uint8_t* b)
{
    return memcmp(a, b, 10) == 0 && memcmp(a + 0xB, b + 0xB, 5) == 0;
}

// ---------------------------------------------------------------------------------------------------------
// gang radar colours (0x8D1344 = R, 0x8D1350 = G, 0x8D135C = B), used by CGangWars::GetGangColor
// ---------------------------------------------------------------------------------------------------------
constexpr uint8_t GANG_COLOUR_R[12] = {200, 70, 255, 0, 255, 200, 240, 0, 255, 255, 0, 0};
constexpr uint8_t GANG_COLOUR_G[12] = {0, 200, 200, 0, 220, 200, 140, 200, 255, 255, 0, 0};
constexpr uint8_t GANG_COLOUR_B[12] = {200, 0, 0, 200, 190, 200, 240, 255, 255, 255, 0, 0};

uint32_t GetGangColour(int gang)
{
    gang = ClampInt(gang, 0, 11);

    // packed exactly like the game: in memory the bytes are R, G, B, A
    return (static_cast<uint32_t>(GANG_COLOUR_R[gang]) << 24) |
           (static_cast<uint32_t>(GANG_COLOUR_G[gang]) << 16) |
           (static_cast<uint32_t>(GANG_COLOUR_B[gang]) << 8) | 0xFF;
}

// Same icons CGangWars::StartDefensiveGangWar picks for the blip.
int GetWarBlipSprite(int gang1)
{
    switch (gang1)
    {
        case GANG_BALLAS:
            return RADAR_SPRITE_GANGP;
        case GANG_VAGOS:
        case GANG_DANANGBOYS:
            return RADAR_SPRITE_GANGY;
        case GANG_RIFA:
            return RADAR_SPRITE_GANGB;
        default:
            return RADAR_SPRITE_ENEMYATTACK;
    }
}

// ---------------------------------------------------------------------------------------------------------
// state
// ---------------------------------------------------------------------------------------------------------

struct SZoneSnapshot
{
    bool initialized = false;
    uint8_t data[MAX_ZONE_INFOS][ZONE_INFO_SIZE]{};
    bool gangWarsActive = false;
    int numSpecificZones = 0;
    int16_t specificZones[6]{};
};

SZoneSnapshot s_hostZones;
uint32_t s_hostZonesTick = 0;

// client: territories the host sent, enforced again every second in case a local script changes them
bool s_abZoneFromHost[MAX_ZONE_INFOS]{};
uint8_t s_aHostZoneData[MAX_ZONE_INFOS][ZONE_INFO_SIZE]{};
uint32_t s_clientApplyTick = 0;

// client: the host's CGangWars globals that GANG_ZONES_SYNC carries (bGangWarsActive and the zones the
// wars are restricted to), enforced like the territories
bool s_bHaveWarGlobals = false;
bool s_bHostGangWarsActive = false;
int s_nHostNumSpecificZones = 0;
int16_t s_aHostSpecificZones[6]{};

struct SWarSnapshot
{
    bool valid = false;
    bool active = false;
    uint8_t state = 0;
    uint8_t attackState = 0;
    uint8_t gang1 = 0;
    int16_t zoneIndex = -1;
    CVector pointOfAttack{};
};

SWarSnapshot s_hostWar;
uint32_t s_hostWarTick = 0;

struct SWarState
{
    bool valid = false;
    bool active = false;
    int state = 0;
    int attackState = 0;
    int gang1 = 0;
    int zoneIndex = -1;
    CVector pointOfAttack{};
    uint32_t fightTimer = 0;
    uint32_t receivedAt = 0;
    int blip = -1;         // radar handle of the attack blip
    bool blipOwned = false;  // false: it is the copy CNetworkStaticBlip created from the host's blip
};

SWarState s_war;
bool s_bWasConnected = false;

bool SamePoint(const CVector& a, const CVector& b)
{
    return a.x == b.x && a.y == b.y && a.z == b.z;
}

bool SameWarSnapshot(const SWarSnapshot& a, const SWarSnapshot& b)
{
    return a.active == b.active && a.state == b.state && a.attackState == b.attackState && a.gang1 == b.gang1 &&
           a.zoneIndex == b.zoneIndex && SamePoint(a.pointOfAttack, b.pointOfAttack);
}

// ---------------------------------------------------------------------------------------------------------
// host
// ---------------------------------------------------------------------------------------------------------

void SnapshotHostZones()
{
    const int count = GetZoneInfoCount();
    const CZoneInfo* zones = GetZoneInfos();

    memset(s_hostZones.data, 0, sizeof(s_hostZones.data));
    for (int i = 0; i < count; i++)
    {
        memcpy(s_hostZones.data[i], &zones[i], ZONE_INFO_SIZE);
    }

    s_hostZones.gangWarsActive = CGangWars::bGangWarsActive;
    s_hostZones.numSpecificZones = ClampInt(CGangWars::NumSpecificZones, 0, 6);
    for (int i = 0; i < 6; i++)
    {
        s_hostZones.specificZones[i] =
            static_cast<int16_t>(ClampInt(CGangWars::aSpecificZones[i], 0, MAX_ZONE_INFOS - 1));
    }

    s_hostZones.initialized = true;
}

void SendZonesPacket(bool reset, const int* indexes, int indexCount)
{
    const int numSpecificZones = ClampInt(CGangWars::NumSpecificZones, 0, 6);

    int offset = 0;
    do
    {
        Packets::Gangs::GangZonesSync packet{};
        packet.reset = reset && offset == 0;
        packet.gangWarsActive = CGangWars::bGangWarsActive;
        packet.numSpecificZones = static_cast<uint8_t>(numSpecificZones);
        for (int i = 0; i < numSpecificZones; i++)
        {
            packet.specificZones[i] =
                static_cast<int16_t>(ClampInt(CGangWars::aSpecificZones[i], 0, MAX_ZONE_INFOS - 1));
        }

        while (offset < indexCount && packet.count < Packets::Gangs::MAX_ZONE_ENTRIES)
        {
            const int index = indexes[offset++];
            Packets::Gangs::GangZoneEntry& entry = packet.entries[packet.count++];
            entry.index = static_cast<int16_t>(index);
            memcpy(entry.data, &GetZoneInfos()[index], ZONE_INFO_SIZE);
        }

        GetPacketFactory().Send(packet);
    } while (offset < indexCount);
}

SWarSnapshot BuildWarSnapshot()
{
    SWarSnapshot snapshot;
    snapshot.valid = true;
    // CGangWars::GangWarGoingOn()
    snapshot.active = (CGangWars::State != NOT_IN_WAR || CGangWars::State2 == WAR_NOTIFIED);
    snapshot.state = static_cast<uint8_t>(CGangWars::State);
    snapshot.attackState = static_cast<uint8_t>(CGangWars::State2);
    snapshot.gang1 = static_cast<uint8_t>(ClampInt(CGangWars::Gang1, 0, 11));
    snapshot.pointOfAttack = CGangWars::PointOfAttack;

    const CZoneInfo* zoneInfo = CGangWars::pZoneInfoToFightOver;
    const CZoneInfo* base = GetZoneInfos();
    if (zoneInfo >= base && zoneInfo < base + MAX_ZONE_INFOS)
    {
        snapshot.zoneIndex = static_cast<int16_t>(zoneInfo - base);
    }

    return snapshot;
}

void SendWarState(const SWarSnapshot& snapshot)
{
    Packets::Gangs::GangWarState packet{};
    packet.active = snapshot.active;
    packet.state = snapshot.state;
    packet.attackState = snapshot.attackState;
    packet.gang1 = snapshot.gang1;
    packet.zoneIndex = snapshot.zoneIndex;
    packet.pointOfAttack = snapshot.pointOfAttack;
    packet.fightTimer = CGangWars::FightTimer > 0 ? static_cast<uint32_t>(CGangWars::FightTimer) : 0;
    GetPacketFactory().Send(packet);
}

void ProcessHostZones()
{
    const uint32_t now = GetTickCount();
    if (s_hostZones.initialized && now - s_hostZonesTick < 1000)
        return;
    s_hostZonesTick = now;

    if (!s_hostZones.initialized)
    {
        // first tick after connecting: remember the state, nothing to send yet
        SnapshotHostZones();
        return;
    }

    const int count = GetZoneInfoCount();
    const CZoneInfo* zones = GetZoneInfos();

    std::vector<int> changed;
    for (int i = 0; i < count; i++)
    {
        const uint8_t* data = reinterpret_cast<const uint8_t*>(&zones[i]);
        if (SameZoneData(data, s_hostZones.data[i]))
            continue;

        // only territories with gang influence are mirrored, but a zone that just lost its last density
        // still has to be sent so the clients clear it
        if (!HasGangDensity(data) && !HasGangDensity(s_hostZones.data[i]))
            continue;

        changed.push_back(i);
    }

    const int numSpecificZones = ClampInt(CGangWars::NumSpecificZones, 0, 6);

    bool warGlobalsChanged = CGangWars::bGangWarsActive != s_hostZones.gangWarsActive ||
                             numSpecificZones != s_hostZones.numSpecificZones;
    for (int i = 0; i < 6 && !warGlobalsChanged; i++)
    {
        const int zone = ClampInt(CGangWars::aSpecificZones[i], 0, MAX_ZONE_INFOS - 1);
        warGlobalsChanged = zone != s_hostZones.specificZones[i];
    }

    if (changed.empty() && !warGlobalsChanged)
        return;

    SendZonesPacket(false, changed.data(), static_cast<int>(changed.size()));
    SnapshotHostZones();
}

void ProcessHostWar()
{
    const uint32_t now = GetTickCount();
    if (s_hostWar.valid && now - s_hostWarTick < 100)
        return;
    s_hostWarTick = now;

    const SWarSnapshot snapshot = BuildWarSnapshot();

    if (!s_hostWar.valid)
    {
        // remember the current war, the clients got it with PLAYER_CONNECTED
        s_hostWar = snapshot;
        return;
    }

    if (SameWarSnapshot(snapshot, s_hostWar))
        return;

    SendWarState(snapshot);
    s_hostWar = snapshot;
}

// ---------------------------------------------------------------------------------------------------------
// client
// ---------------------------------------------------------------------------------------------------------

// the attacked territory blinks red, exactly like StartDefensiveGangWar does on the host
void SetZoneAttackMode(int index, bool attack)
{
    // the array is always allocated, even before the game initializes the zones (ApplyHostZones mirrors it
    // again after a load)
    if (index < 0 || index >= MAX_ZONE_INFOS)
        return;

    CZoneInfo& zoneInfo = GetZoneInfos()[index];

    if (attack)
    {
        SetRadarMode(zoneInfo, RADAR_MODE_ATTACK);
        zoneInfo.m_ZoneColor = CRGBA(255, 0, 0, 160);
    }
    else if (GetRadarMode(zoneInfo) == RADAR_MODE_ATTACK)
    {
        // the host repaints the territories when its war ends (FillZonesWithGangColours) and that change
        // arrives in the next GANG_ZONES_SYNC, so the colour is left alone here
        SetRadarMode(zoneInfo, RADAR_MODE_NONE);
    }

    // keep the mirror consistent, otherwise ApplyHostZones would put the old value back one second later
    if (s_abZoneFromHost[index])
    {
        memcpy(s_aHostZoneData[index], &zoneInfo, ZONE_INFO_SIZE);
    }
}

void ClearWarBlip(bool force = false)
{
    if (s_war.blip >= 0 && (s_war.blipOwned || force))
    {
        CRadar::ClearBlip(s_war.blip);
    }

    s_war.blip = -1;
    s_war.blipOwned = false;
}

// The host's blip is also mirrored by CNetworkStaticBlip (the host's SetBlipSprite call marks the blips to
// be resent), so reuse that copy instead of stacking a second icon on the same spot.
int FindMirroredWarBlip(const CVector& pos)
{
    for (int i = 0; i < MAX_RADAR_TRACES; i++)
    {
        const tRadarTrace& trace = CRadar::ms_RadarTrace[i];
        if (!trace.m_bInUse || trace.m_nBlipType != BLIP_COORD)
            continue;
        if (trace.m_nBlipDisplay == BLIP_DISPLAY_NEITHER)
            continue;
        if (trace.m_vecPos.x < pos.x - 3.0f || trace.m_vecPos.x > pos.x + 3.0f || trace.m_vecPos.y < pos.y - 3.0f ||
            trace.m_vecPos.y > pos.y + 3.0f)
            continue;

        switch (trace.m_nRadarSprite)
        {
            case RADAR_SPRITE_ENEMYATTACK:
            case RADAR_SPRITE_GANGP:
            case RADAR_SPRITE_GANGY:
            case RADAR_SPRITE_GANGB:
                return i;
        }
    }

    return -1;
}

void EnsureWarBlip()
{
    if (s_war.blip >= 0 && CRadar::GetActualBlipArrayIndex(s_war.blip) == -1)
    {
        // cleared by a static blips snapshot
        s_war.blip = -1;
    }

    if (s_war.blip >= 0)
        return;

    const int mirrored = FindMirroredWarBlip(s_war.pointOfAttack);
    if (mirrored >= 0)
    {
        s_war.blip = CRadar::GetNewUniqueBlipIndex(mirrored);
        s_war.blipOwned = false;
        return;
    }

    s_war.blip = CRadar::SetCoordBlip(BLIP_COORD, s_war.pointOfAttack, GetGangColour(s_war.gang1),
                                     BLIP_DISPLAY_BLIP_ONLY, nullptr);
    CRadar::SetBlipSprite(s_war.blip, GetWarBlipSprite(s_war.gang1));
    s_war.blipOwned = true;
}

// CGangWars::Update blinks the blip on the host, but it does not run on the clients, so it is done here
void UpdateWarBlip()
{
    if (!s_war.valid || s_war.attackState != WAR_NOTIFIED || s_war.blip < 0)
        return;

    if (CRadar::GetActualBlipArrayIndex(s_war.blip) == -1)
    {
        s_war.blip = -1;
        return;
    }

    // FightTimer is only sent when the war state changes, extrapolate it locally for the blink speed
    const uint32_t elapsed = GetTickCount() - s_war.receivedAt;
    const uint32_t fightTimer = s_war.fightTimer > elapsed ? s_war.fightTimer - elapsed : 0;

    int blinkPeriod = 7;
    if (fightTimer > 120000)
        blinkPeriod = 10;
    else if (fightTimer > 60000)
        blinkPeriod = 9;
    else if (fightTimer > 30000)
        blinkPeriod = 8;

    const bool visible = ((GetTickCount() >> blinkPeriod) & 1) == 0;
    CRadar::ChangeBlipDisplay(s_war.blip, visible ? BLIP_DISPLAY_BLIP_ONLY : BLIP_DISPLAY_NEITHER);
}

void ApplyWarState(Packets::Gangs::GangWarState* pPacket)
{
    const bool wasNotified = s_war.valid && s_war.attackState == WAR_NOTIFIED;
    const bool isNotified = pPacket->attackState == WAR_NOTIFIED;
    const bool pointChanged = !s_war.valid || !SamePoint(s_war.pointOfAttack, pPacket->pointOfAttack);

    s_war.valid = true;
    s_war.active = pPacket->active;
    s_war.state = pPacket->state;
    s_war.attackState = pPacket->attackState;
    s_war.gang1 = pPacket->gang1;
    s_war.zoneIndex = pPacket->zoneIndex;
    s_war.pointOfAttack = pPacket->pointOfAttack;
    s_war.fightTimer = pPacket->fightTimer;
    s_war.receivedAt = GetTickCount();

    // mirror the host's war so scripts, streaming and the radar behave the same on every client
    CGangWars::State = static_cast<eGangWarState>(pPacket->state);
    CGangWars::State2 = static_cast<eGangAttackState>(pPacket->attackState);
    CGangWars::Gang1 = pPacket->gang1;
    CGangWars::PointOfAttack = pPacket->pointOfAttack;
    CGangWars::FightTimer = static_cast<int>(pPacket->fightTimer);

    if (pPacket->zoneIndex >= 0 && pPacket->zoneIndex < MAX_ZONE_INFOS)
    {
        CGangWars::pZoneInfoToFightOver = &GetZoneInfos()[pPacket->zoneIndex];
    }

    if (isNotified)
    {
        // same notice the host shows in StartDefensiveGangWar
        if (!wasNotified)
        {
            CHud::SetHelpMessage(TheText.Get("GW_ATK"), true, false, true);
        }

        SetZoneAttackMode(s_war.zoneIndex, true);

        if (pointChanged)
        {
            ClearWarBlip();
        }
        EnsureWarBlip();
    }
    else
    {
        ClearWarBlip();

        // the attacked territory keeps its attack colour while the fight is going on (State 1..6 while the
        // player is there); the game itself only repaints the zones when the war is over
        if (pPacket->state == NOT_IN_WAR && pPacket->attackState == NO_ATTACK)
        {
            SetZoneAttackMode(s_war.zoneIndex, false);
        }
    }
}

void ApplyHostZones()
{
    const uint32_t now = GetTickCount();
    if (now - s_clientApplyTick < 1000)
        return;
    s_clientApplyTick = now;

    const int count = GetZoneInfoCount();
    CZoneInfo* zones = GetZoneInfos();

    for (int i = 0; i < count; i++)
    {
        if (!s_abZoneFromHost[i])
            continue;

        // a local script (or the game itself) changed the territory: put the host's values back
        if (memcmp(&zones[i], s_aHostZoneData[i], ZONE_INFO_SIZE) == 0)
            continue;

        memcpy(&zones[i], s_aHostZoneData[i], ZONE_INFO_SIZE);
    }

    if (!s_bHaveWarGlobals)
        return;

    if (CGangWars::bGangWarsActive != s_bHostGangWarsActive)
    {
        CGangWars::bGangWarsActive = s_bHostGangWarsActive;
    }

    if (CGangWars::NumSpecificZones != s_nHostNumSpecificZones)
    {
        CGangWars::NumSpecificZones = s_nHostNumSpecificZones;
    }

    for (int i = 0; i < 6; i++)
    {
        if (CGangWars::aSpecificZones[i] != s_aHostSpecificZones[i])
        {
            CGangWars::aSpecificZones[i] = s_aHostSpecificZones[i];
        }
    }
}

void ResetOnDisconnect()
{
    if (!s_bWasConnected)
        return;

    s_bWasConnected = false;

    ClearWarBlip(true);
    s_war = SWarState{};

    memset(s_abZoneFromHost, 0, sizeof(s_abZoneFromHost));
    s_bHaveWarGlobals = false;
    s_hostZones.initialized = false;
    s_hostWar.valid = false;
}

// ---------------------------------------------------------------------------------------------------------
// CGame::Process calls CGangWars::Update (0x446610) from 0x53C122. Territories and wars are simulated by
// the host only, so on a connected client the update must not run: it would start its own wars, clear the
// host's attack blip and repaint the zones.
// ---------------------------------------------------------------------------------------------------------
uintptr_t s_nGangWarsUpdateAddress = 0;

void __cdecl CGangWarsUpdate_Hook()
{
    if (!CNetwork::m_bAuthenticated || CLocalPlayer::m_bIsHost)
    {
        plugin::CallDyn(s_nGangWarsUpdateAddress);
    }
}
}  // namespace

void CGangSync::Init()
{
    s_nGangWarsUpdateAddress = injector::GetBranchDestination(0x53C122).as_int();
    patch::RedirectCall(0x53C122, CGangWarsUpdate_Hook);

    Events::gameProcessEvent += [] { CGangSync::Process(); };
}

void CGangSync::Process()
{
    if (!CNetwork::m_bAuthenticated)
    {
        ResetOnDisconnect();
        return;
    }

    if (!s_bWasConnected)
    {
        s_bWasConnected = true;
        // the territories of this session have to be resent once the local game is running
        s_hostZones.initialized = false;
        s_hostWar.valid = false;
    }

    if (CLocalPlayer::m_bIsHost)
    {
        if (s_war.valid || s_war.blip >= 0)
        {
            // we are the authority now: drop the war we were mirroring, our own CGangWars::Update takes over
            ClearWarBlip();
            s_war = SWarState{};
        }

        ProcessHostZones();
        ProcessHostWar();
    }
    else
    {
        ApplyHostZones();
        UpdateWarBlip();
    }
}

// host -> everyone (called when a player connects): full territory snapshot plus the current war
void CGangSync::SyncCurrentState()
{
    if (!CNetwork::m_bAuthenticated || !CLocalPlayer::m_bIsHost)
        return;

    const int count = GetZoneInfoCount();
    const CZoneInfo* zones = GetZoneInfos();

    std::vector<int> indexes;
    for (int i = 0; i < count; i++)
    {
        if (HasGangDensity(zones[i]))
        {
            indexes.push_back(i);
        }
    }

    SendZonesPacket(true, indexes.data(), static_cast<int>(indexes.size()));
    SnapshotHostZones();

    const SWarSnapshot war = BuildWarSnapshot();
    SendWarState(war);
    s_hostWar = war;
}

void CGangSync::HandleZonesSync(Packets::Gangs::GangZonesSync* pPacket)
{
    // we are the authority, the server only forwards packets that come from the host
    if (CLocalPlayer::m_bIsHost)
        return;

    if (pPacket->reset)
    {
        memset(s_abZoneFromHost, 0, sizeof(s_abZoneFromHost));
    }

    CZoneInfo* zones = GetZoneInfos();

    for (int i = 0; i < pPacket->count; i++)
    {
        const Packets::Gangs::GangZoneEntry& entry = pPacket->entries[i];
        if (entry.index < 0 || entry.index >= MAX_ZONE_INFOS)
        {
            continue;
        }

        // The index is written even if the local game has not initialized its zones yet (0 entries): the
        // data is kept in the mirror and ApplyHostZones applies it as soon as the zones exist. This way a
        // full resync that arrives during the loading screen is not lost.
        memcpy(&zones[entry.index], entry.data, ZONE_INFO_SIZE);
        memcpy(s_aHostZoneData[entry.index], entry.data, ZONE_INFO_SIZE);
        s_abZoneFromHost[entry.index] = true;
    }

    // the territories are only drawn while the host has the gang wars active
    CGangWars::bGangWarsActive = pPacket->gangWarsActive;
    CGangWars::NumSpecificZones = ClampInt(pPacket->numSpecificZones, 0, 6);
    for (int i = 0; i < 6; i++)
    {
        CGangWars::aSpecificZones[i] = pPacket->specificZones[i] >= 0 ? pPacket->specificZones[i] : 0;
    }

    s_bHaveWarGlobals = true;
    s_bHostGangWarsActive = CGangWars::bGangWarsActive;
    s_nHostNumSpecificZones = CGangWars::NumSpecificZones;
    for (int i = 0; i < 6; i++)
    {
        s_aHostSpecificZones[i] = static_cast<int16_t>(CGangWars::aSpecificZones[i]);
    }
}

void CGangSync::HandleWarState(Packets::Gangs::GangWarState* pPacket)
{
    // we are the authority, the server only forwards packets that come from the host
    if (CLocalPlayer::m_bIsHost)
        return;

    ApplyWarState(pPacket);
}

// ---------------------------------------------------------------------------------------------------------
// TODO (priority 3): recruited gang members.
//
// Not done yet, the two priorities above (territories and wars) are. What has to be kept in mind:
//   * the attack waves of a war are created by CGangWars::CreateAttackWave on the host only, and they are
//     plain CCivilianPeds, so CNetworkPedManager already syncs them as normal network peds;
//   * a *recruited* member (the gang members following CJ) belongs to the local player's CPedGroup, which
//     every client builds on its own machine, so right now each player sees his own recruits only;
//   * to sync them the group of the local player has to be enumerated
//     (CPedGroup::m_groupMembership -> CountMembers() / GetMember(i); the plugin-sdk does not expose the
//     player-group pointer, it has to be read from the player ped), each member spawned on the other
//     clients with Packets::Peds::PedSpawn and owned by the player that recruited it, and it has to be
//     excluded from the normal population/claim logic (otherwise CNetworkPedManager would release it and
//     the game script would create a second copy). The follow task of the group has to be reproduced on
//     the copies as well (CTaskComplexFollowPed or the group task the host is running).
// ---------------------------------------------------------------------------------------------------------

// packet handlers (registered like the ones in PacketHandlers/*.cpp)
PACKET_HANDLER(ePacketType::GANG_ZONES_SYNC, Packets::Gangs::GangZonesSync* pGangZonesSync)
{
    CGangSync::HandleZonesSync(pGangZonesSync);
}

PACKET_HANDLER(ePacketType::GANG_WAR_STATE, Packets::Gangs::GangWarState* pGangWarState)
{
    CGangSync::HandleWarState(pGangWarState);
}
