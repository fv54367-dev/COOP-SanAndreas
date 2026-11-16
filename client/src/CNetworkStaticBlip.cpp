#include "stdafx.h"
#include "CNetworkStaticBlip.h"
#include "CEntryExit.h"
#include <CEntryExitManager.h>
#include "CMirrorBlips.h"
#include "CMissionMirror.h"
#include "CCoopMission.h"
#include "CMissionSyncRules.h"
#include "CMissionMode.h"
#include "CLocalPlayer.h"
#include <algorithm>

// main.scm globals (CustomVariables.ini index * 4)
static constexpr int INTRO_COUNTER = 1792;    // $flag_intro_mission_counter (448)
static constexpr int INTRO_BLIP = 1756;       // $intro_contact_blip (439)
static constexpr int RYDER_BLIP_ICON = 1772;  // $ryder_blip_icon (443)
static constexpr int INTRO_XYZ = 1836;        // $introX/Y/Z (459)
static constexpr int RYDER_XYZ = 1860;        // $ryderX/Y/Z (465)

void CNetworkStaticBlip::SetIntroCounter(int value)
{
    int& counter = *reinterpret_cast<int*>(&CTheScripts::ScriptSpace[INTRO_COUNTER]);
    int& handle = *reinterpret_cast<int*>(&CTheScripts::ScriptSpace[INTRO_BLIP]);
    if (CRadar::GetActualBlipArrayIndex(handle) != -1)
        CRadar::ClearBlip(handle);
    if (value == 1)
    {
        // what INTROST does when Big Smoke is passed: Ryder's icon in the same variable
        const float* ryder = reinterpret_cast<const float*>(&CTheScripts::ScriptSpace[RYDER_XYZ]);
        handle = CRadar::SetCoordBlip(BLIP_CONTACTPOINT, CVector(ryder[0], ryder[1], ryder[2]), 0, BLIP_DISPLAY_BOTH, nullptr);
        CRadar::SetBlipSprite(handle, *reinterpret_cast<int*>(&CTheScripts::ScriptSpace[RYDER_BLIP_ICON]));
    }
    CMissionMirror::LogEventf("intro-follow-host counter from=%d to=%d", counter, value);
    counter = value;  // at 2 INT terminates itself and its marker goes
}

namespace
{
// contacts-follow-host. A contact point (icon and the yellow marker CRadar::Draw3dMarkers puts on it, gta-reversed
// Radar.cpp:1375) is added and removed by the mission script when it passes (SWEET7 removes the contact blip)
// and by the MOBILE script. The mission ran on the host only: the guest kept the icon of a mission passed in coop, and showed
// contacts of his own save ahead of the host's story. His own ones are hidden (not cleared: his scripts keep the handles)
struct SHiddenContact
{
    int index;
    uint16_t counter;
    uint8_t display;
};
struct SMadeContact
{
    int handle;
    CVector position;
    uint8_t sprite;
};
std::vector<SHiddenContact> s_aHiddenContacts;
std::vector<SMadeContact> s_aMadeContacts;
int s_nHostId = -1;  // the host when ms_aHostBlips came
int s_nLastHidden = -1, s_nLastMade = -1;

int CurrentHostId()
{
    for (auto networkPlayer : CNetworkPlayerManager::m_pPlayers)
        if (networkPlayer && networkPlayer->m_bIsHost)
            return static_cast<int>(networkPlayer->m_playerId.value());
    return -1;
}

// where the host's Send puts it
CVector TracePosition(const tRadarTrace& trace)
{
    if (trace.m_pEntryExit)
    {
        auto& rect = trace.m_pEntryExit->m_recEntrance;
        return CVector((rect.right + rect.left) * 0.5f, (rect.bottom + rect.top) * 0.5f, trace.m_pEntryExit->m_fEntranceZ);
    }
    return trace.m_vecPos;
}

bool SamePlace(const CVector& a, const CVector& b)
{
    return fabsf(a.x - b.x) < 3.0f && fabsf(a.y - b.y) < 3.0f;
}

bool IsMadeContact(int index)
{
    for (const auto& made : s_aMadeContacts)
        if (CRadar::GetActualBlipArrayIndex(made.handle) == index)
            return true;
    for (int handle : CNetworkStaticBlip::ms_aSnapshotHandles)
        if (CRadar::GetActualBlipArrayIndex(handle) == index)
            return true;
    return false;
}

// a contact point of this game's own scripts
bool IsOwnContact(int index)
{
    const auto& trace = CRadar::ms_RadarTrace[index];
    return trace.m_bInUse && trace.m_nBlipType == BLIP_CONTACTPOINT &&
        CNetworkStaticBlip::IsAllowedSyncingRadarSprite(static_cast<eRadarSprite>(trace.m_nRadarSprite)) &&
        !CNetworkStaticBlip::IsMissionBlip(index) && !CMirrorBlips::IsMirrorTrace(index) && !IsMadeContact(index);
}

void RestoreHiddenContacts()
{
    for (const auto& hidden : s_aHiddenContacts)
    {
        auto& trace = CRadar::ms_RadarTrace[hidden.index];
        if (trace.m_bInUse && trace.m_nCounter == hidden.counter && trace.m_nBlipDisplay == BLIP_DISPLAY_NEITHER)
            trace.m_nBlipDisplay = hidden.display;
    }
    s_aHiddenContacts.clear();
}

void ClearMadeContacts()
{
    for (const auto& made : s_aMadeContacts)
        if (CRadar::GetActualBlipArrayIndex(made.handle) != -1)
            CRadar::ClearBlip(made.handle);
    s_aMadeContacts.clear();
}

bool FollowsHost()
{
    if (!CConfigLoader::ms_bContactsFollowHost || !CConfigLoader::ms_bOwnStaticBlips || !CConfigLoader::ms_bVanillaScripts ||
        !CNetwork::m_bConnected || CLocalPlayer::m_bIsHost)
    {
        CNetworkStaticBlip::ms_bHaveHostBlips = false;
        return false;
    }
    // another host has not sent his yet
    const int host = CurrentHostId();
    if (s_nHostId == -1)
        s_nHostId = host;
    else if (host != -1 && host != s_nHostId)
        CNetworkStaticBlip::ms_bHaveHostBlips = false;
    return CNetworkStaticBlip::ms_bHaveHostBlips && !CMissionMode::IsIndividual();
}

void MakeHostMissionBlips()
{
    for (int handle : CNetworkStaticBlip::ms_aSnapshotHandles)
        CRadar::ClearBlip(handle);
    CNetworkStaticBlip::ms_aSnapshotHandles.clear();
    for (const auto& blipState : CNetworkStaticBlip::ms_aHostBlips)
    {
        if (!blipState.missionBlip)
            continue;
        const int blip = CRadar::SetCoordBlip(
            static_cast<eBlipType>(blipState.type ? eBlipType::BLIP_COORD : eBlipType::BLIP_CONTACTPOINT),
            blipState.position, 0, static_cast<eBlipDisplay>(blipState.display), nullptr);
        const auto index = CRadar::GetActualBlipArrayIndex(blip);
        if (index == -1)
            continue;
        CRadar::SetBlipSprite(blip, blipState.sprite);
        CNetworkStaticBlip::MarkMissionBlip(index, false);
        auto& trace = CRadar::ms_RadarTrace[index];
        trace.m_bShortRange = blipState.shortRange;
        trace.m_bFriendly = blipState.friendly;
        trace.m_nCoordBlipAppearance = blipState.coordBlipAppearance;
        trace.m_nBlipSize = blipState.size;
        trace.m_nColour = blipState.color;
        CNetworkStaticBlip::ms_aSnapshotHandles.push_back(blip);
    }
}

void FollowHostContacts()
{
    if (!FollowsHost())
    {
        if (!s_aHiddenContacts.empty() || !s_aMadeContacts.empty())
            CMissionMirror::LogEventf("contacts-follow-host off hidden=%d made=%d", (int)s_aHiddenContacts.size(),
                (int)s_aMadeContacts.size());
        RestoreHiddenContacts();
        ClearMadeContacts();
        s_nLastHidden = s_nLastMade = -1;
        return;
    }

    s_aMadeContacts.erase(std::remove_if(s_aMadeContacts.begin(), s_aMadeContacts.end(),
        [](const SMadeContact& made) { return CRadar::GetActualBlipArrayIndex(made.handle) == -1; }), s_aMadeContacts.end());
    std::vector<int> own;
    for (int i = 0; i < MAX_RADAR_TRACES; i++)
        if (IsOwnContact(i))
            own.push_back(i);
    std::vector<bool> ownMatched(own.size(), false);
    std::vector<bool> madeUsed(s_aMadeContacts.size(), false);

    for (const auto& blipState : CNetworkStaticBlip::ms_aHostBlips)
    {
        if (blipState.type || blipState.missionBlip)
            continue;
        const CVector position = blipState.position;
        bool found = false;
        for (size_t k = 0; k < own.size() && !found; k++)
        {
            const auto& trace = CRadar::ms_RadarTrace[own[k]];
            if (!ownMatched[k] && trace.m_nRadarSprite == blipState.sprite && SamePlace(TracePosition(trace), position))
                ownMatched[k] = found = true;
        }
        for (size_t m = 0; m < s_aMadeContacts.size() && !found; m++)
        {
            if (!madeUsed[m] && s_aMadeContacts[m].sprite == blipState.sprite && SamePlace(s_aMadeContacts[m].position, position))
                madeUsed[m] = found = true;
        }
        if (found)
            continue;
        // the host's contact this game lacks
        const int blip = CRadar::SetCoordBlip(BLIP_CONTACTPOINT, position, 0, static_cast<eBlipDisplay>(blipState.display), nullptr);
        const auto index = CRadar::GetActualBlipArrayIndex(blip);
        if (index == -1)
            continue;
        CRadar::SetBlipSprite(blip, blipState.sprite);
        CNetworkStaticBlip::MarkMissionBlip(index, false);
        auto& trace = CRadar::ms_RadarTrace[index];
        trace.m_bShortRange = blipState.shortRange;
        trace.m_bFriendly = blipState.friendly;
        trace.m_nBlipSize = blipState.size;
        trace.m_nColour = blipState.color;
        s_aMadeContacts.push_back({blip, position, blipState.sprite});
        madeUsed.push_back(true);
    }
    for (size_t m = s_aMadeContacts.size(); m-- > 0;)
    {
        if (madeUsed[m])
            continue;
        CRadar::ClearBlip(s_aMadeContacts[m].handle);
        s_aMadeContacts.erase(s_aMadeContacts.begin() + m);
    }

    // the own ones: hidden while the host has none there, shown again when he does
    std::vector<SHiddenContact> hidden;
    for (size_t k = 0; k < own.size(); k++)
    {
        auto& trace = CRadar::ms_RadarTrace[own[k]];
        auto it = std::find_if(s_aHiddenContacts.begin(), s_aHiddenContacts.end(),
            [&](const SHiddenContact& entry) { return entry.index == own[k] && entry.counter == trace.m_nCounter; });
        if (ownMatched[k])
        {
            if (it != s_aHiddenContacts.end() && trace.m_nBlipDisplay == BLIP_DISPLAY_NEITHER)
                trace.m_nBlipDisplay = it->display;
            continue;
        }
        SHiddenContact entry{own[k], trace.m_nCounter, static_cast<uint8_t>(trace.m_nBlipDisplay)};
        // his script changed the display meanwhile: that is the one to give back
        if (it != s_aHiddenContacts.end() && trace.m_nBlipDisplay == BLIP_DISPLAY_NEITHER)
            entry.display = it->display;
        trace.m_nBlipDisplay = BLIP_DISPLAY_NEITHER;
        hidden.push_back(entry);
    }
    s_aHiddenContacts = std::move(hidden);

    if ((int)s_aHiddenContacts.size() != s_nLastHidden || (int)s_aMadeContacts.size() != s_nLastMade)
    {
        s_nLastHidden = (int)s_aHiddenContacts.size();
        s_nLastMade = (int)s_aMadeContacts.size();
        CMissionMirror::LogEventf("contacts-follow-host own=%d hidden=%d made=%d host=%d", (int)own.size(), s_nLastHidden,
            s_nLastMade, s_nHostId);
    }
}
}

void CNetworkStaticBlip::Process()
{
    static unsigned int lastFrame = 0;
    static DWORD lastRun = 0;
    // a game loaded or started: CRadar was cleared, nothing of the old lists is there
    if (CTimer::m_FrameCounter < lastFrame)
    {
        s_aHiddenContacts.clear();
        s_aMadeContacts.clear();
        ms_aSnapshotHandles.clear();
        ms_bHaveHostBlips = false;
    }
    lastFrame = CTimer::m_FrameCounter;
    const DWORD now = GetTickCount();
    if (now - lastRun < 250)
        return;
    lastRun = now;
    FollowHostContacts();
}

void CNetworkStaticBlip::BeforeSave()
{
    RestoreHiddenContacts();
    ClearMadeContacts();
    if (CConfigLoader::ms_bOwnStaticBlips && CConfigLoader::ms_bVanillaScripts)
    {
        // the host's mission blips would stay in this save as icons nobody clears
        for (int handle : ms_aSnapshotHandles)
            CRadar::ClearBlip(handle);
        ms_aSnapshotHandles.clear();
    }
}

void CNetworkStaticBlip::AfterSave()
{
    if (CConfigLoader::ms_bOwnStaticBlips && CConfigLoader::ms_bVanillaScripts && ms_bHaveHostBlips)
        MakeHostMissionBlips();
    s_nLastHidden = s_nLastMade = -1;
    FollowHostContacts();
}

// INT, INTRO1 and INTRO2 ran on the host only: here INT kept the CJ blip and its red marker (testeo 13-11). The host's
// contacts tell how far he is: a blip at introX = 0, at ryderX = 1, neither = 2
static void FollowHostIntro(const Packets::Blips::StaticBlipsSnapshot& packet)
{
    if (!CConfigLoader::ms_bIntroFollowHost || CCoopMission::IsScriptAuthority() || CMissionMirror::GetRunningMissionId() >= 0 ||
        *reinterpret_cast<int*>(&CTheScripts::ScriptSpace[1636]) != 0)
        return;
    const int counter = *reinterpret_cast<int*>(&CTheScripts::ScriptSpace[INTRO_COUNTER]);
    if (counter < 0 || counter >= 2)
        return;
    const float* intro = reinterpret_cast<const float*>(&CTheScripts::ScriptSpace[INTRO_XYZ]);
    const float* ryder = reinterpret_cast<const float*>(&CTheScripts::ScriptSpace[RYDER_XYZ]);
    bool atIntro = false, atRyder = false, anyContact = false;
    // inside INTRO1/INTRO2 the host's mission clears its own contact blip: he would look past Ryder (testeo 14-11, the
    // guest's INT terminated at 2 and Ryder's marker never asked again). A coop mission or any mission blip of his: wait
    static DWORD stableSince = 0;
    static int lastSeen = -1;
    if (CCoopMission::IsRunning())
    {
        lastSeen = -1;
        return;
    }
    for (size_t i = 0; i < packet.countBlips; i++)
    {
        if (packet.blips[i].missionBlip)
        {
            lastSeen = -1;
            return;
        }
    }
    for (size_t i = 0; i < packet.countBlips; i++)
    {
        const auto& blip = packet.blips[i];
        if (blip.missionBlip || blip.type)
            continue;
        anyContact = true;
        atIntro |= fabsf(blip.position.x - intro[0]) < 3.0f && fabsf(blip.position.y - intro[1]) < 3.0f;
        atRyder |= fabsf(blip.position.x - ryder[0]) < 3.0f && fabsf(blip.position.y - ryder[1]) < 3.0f;
    }
    const int hostCounter = atIntro ? 0 : atRyder ? 1 : 2;
    if (!anyContact || hostCounter <= counter)
    {
        lastSeen = -1;
        return;
    }
    // the same reading for 10 s: a mission start or end in between changes the blips for a moment
    const DWORD now = GetTickCount();
    if (hostCounter != lastSeen)
    {
        lastSeen = hostCounter;
        stableSince = now;
        return;
    }
    if (now - stableSince < 10000)
        return;
    if (CNetworkStaticBlip::ms_nIntroOwnCounter < 0)
        CNetworkStaticBlip::ms_nIntroOwnCounter = counter;
    CNetworkStaticBlip::ms_nIntroCopiedCounter = hostCounter;
    CNetworkStaticBlip::SetIntroCounter(hostCounter);
}

void CNetworkStaticBlip::Create(const Packets::Blips::StaticBlipsSnapshot& packet)
{
    FollowHostIntro(packet);
    if (CConfigLoader::ms_bOwnStaticBlips && CConfigLoader::ms_bVanillaScripts)
    {
        // every game runs its own main.scm: its blips are its own progress (the host may have loaded another save). Only
        // the host's synced mission blips come, replacing the ones of the previous snapshot; his contact points are
        // shown by Process while they play coop
        ms_aHostBlips.assign(packet.blips, packet.blips + packet.countBlips);
        ms_bHaveHostBlips = true;
        s_nHostId = CurrentHostId();
        MakeHostMissionBlips();
        FollowHostContacts();
        return;
    }

    for (int i = 0; i < MAX_RADAR_TRACES; i++)
    {
        auto& trace = CRadar::ms_RadarTrace[i];

        if (!trace.m_bInUse)
        {
            MarkMissionBlip(i, false);
            continue;
        }

        // a blip of the local player's own mission is not part of the world snapshot: keep it
        if (CConfigLoader::ms_bCoopSideMissions && IsMissionBlip(i))
            continue;

        // a blip of the authority's mission that the mission mirror layer made stays (M2, M-D1)
        if (CMirrorBlips::IsMirrorTrace(i))
            continue;

        if ((trace.m_nBlipType == BLIP_CONTACTPOINT || trace.m_nBlipType == BLIP_COORD) &&
            IsAllowedSyncingRadarSprite(static_cast<eRadarSprite>(trace.m_nRadarSprite)))
        {
            CRadar::ClearActualBlip(i);
        }
    }

    for (size_t i = 0; i < packet.countBlips; i++)
    {
        const Packets::Blips::_StaticBlipPayload& blipState = packet.blips[i];

        assert(IsAllowedSyncingRadarSprite(static_cast<eRadarSprite>(blipState.sprite)));

        // CChat::AddMessage("CNetworkMissionMarker::Create({%.2f %.2f %.2f}, %d, %d)", position.x, position.y,
        // position.z, sprite, display);

        int blip = CRadar::SetCoordBlip(
            static_cast<eBlipType>(blipState.type ? eBlipType::BLIP_COORD : eBlipType::BLIP_CONTACTPOINT),
            blipState.position, 0, static_cast<eBlipDisplay>(blipState.display), nullptr);
        CRadar::SetBlipSprite(blip, blipState.sprite);
        CRadar::ChangeBlipDisplay(blip, static_cast<eBlipDisplay>(blipState.display));

        if (const auto index = CRadar::GetActualBlipArrayIndex(blip); index != -1)
        {
            // this blip comes from the world snapshot, so the slot is not a mission one anymore
            MarkMissionBlip(index, false);

            CRadar::ms_RadarTrace[index].m_bShortRange = blipState.shortRange;
            CRadar::ms_RadarTrace[index].m_bFriendly = blipState.friendly;
            CRadar::ms_RadarTrace[index].m_nCoordBlipAppearance = blipState.coordBlipAppearance;
            CRadar::ms_RadarTrace[index].m_nBlipSize = blipState.size;
            CRadar::ms_RadarTrace[index].m_nColour = blipState.color;
        }
    }
}

void CNetworkStaticBlip::Send()
{
    Packets::Blips::StaticBlipsSnapshot packet{};
    packet.countBlips = 0;
    for (int i = 0; i < MAX_RADAR_TRACES; i++)
    {
        auto& trace = CRadar::ms_RadarTrace[i];

        if (!trace.m_bInUse)
        {
            MarkMissionBlip(i, false);
            continue;
        }

        // blips of a mission script belong to one player's mission, not to the world: they never travel in the snapshot
        if (CConfigLoader::ms_bCoopSideMissions && IsMissionBlip(i))
            continue;

        if ((trace.m_nBlipType != eBlipType::BLIP_CONTACTPOINT && trace.m_nBlipType != eBlipType::BLIP_COORD) ||
            !IsAllowedSyncingRadarSprite(static_cast<eRadarSprite>(trace.m_nRadarSprite)))
        {
            continue;
        }

        // a blip of the authority's mission goes through the mission mirror layer instead (M2, M-D1)
        if (CMirrorBlips::IsOwnedTrace(i))
            continue;

        Packets::Blips::_StaticBlipPayload& blipState = packet.blips[packet.countBlips];

        if (trace.m_pEntryExit)
        {
            auto& rect = trace.m_pEntryExit->m_recEntrance;
            blipState.position = CVector(
                (rect.right + rect.left) * 0.5f, (rect.bottom + rect.top) * 0.5f, trace.m_pEntryExit->m_fEntranceZ);
        }
        else
        {
            blipState.position = trace.m_vecPos;
        }

        blipState.display = trace.m_nBlipDisplay;
        blipState.sprite = trace.m_nRadarSprite;
        blipState.type = trace.m_nBlipType == eBlipType::BLIP_COORD;
        blipState.missionBlip = IsSyncedMissionBlip(i);
        blipState.shortRange = trace.m_bShortRange;
        blipState.friendly = trace.m_bFriendly;
        blipState.coordBlipAppearance = trace.m_nCoordBlipAppearance;
        blipState.size = trace.m_nBlipSize;
        blipState.color = trace.m_nColour;

        ++packet.countBlips;
    }
    GetPacketFactory().Send(packet);
}
