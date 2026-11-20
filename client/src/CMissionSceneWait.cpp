#include "stdafx.h"
#include "CMissionSceneWait.h"
#include "CConfigLoader.h"
#include "CCoopMission.h"
#include "CMirrorBlips.h"
#include "CMissionHints.h"
#include "CMissionLock.h"
#include "CMissionMirror.h"
#include "CMissionSyncRules.h"
#include "COpCodeSync.h"
#include "CNetwork.h"
#include "CNetworkPlayer.h"
#include "CNetworkPlayerManager.h"
#include "UI/CChat.h"
#include "network/packets/mission_wait_notice.h"
#include "CCutsceneMgr.h"
#include <game_sa/CTagManager.h>
#include "Commands/Commands/CCommandTeleportPlayersToHostSafely.h"
#include <algorithm>
#include <string>
#include <vector>

// gta-reversed eScriptCommands.h: COMMAND_START_CUTSCENE = 0x02E7, COMMAND_SWITCH_WIDESCREEN = 0x02A3 (its parameter is
// a bool). Both are run by the authority's script only; the guests get them over the opcode sync afterwards.
namespace
{
constexpr uint16_t OP_START_CUTSCENE = 0x02E7;
constexpr uint16_t OP_SWITCH_WIDESCREEN = 0x02A3;
// outside, and inside an interior (the houses of the missions are big): how far a player may be and still count as there
constexpr float RANGE_OUTSIDE = 35.0f;
constexpr float RANGE_INSIDE = 80.0f;
// a scene never waits longer than this (a player stuck behind a wall must not hold the mission for ever)
constexpr DWORD GIVE_UP_MS = 90000;
// prelude commands of a scene (gta-reversed eScriptCommands.h): DO_FADE 016A, SELECT_INTERIOR 04BB, LOAD_CUTSCENE 02E4
constexpr uint16_t OP_DO_FADE = 0x016A;
constexpr uint16_t OP_SELECT_INTERIOR = 0x04BB;
constexpr uint16_t OP_LOAD_CUTSCENE = 0x02E4;
// after a scene was released (everybody there, or given up) the rest of its commands pass for this long (renewed while
// the bars are on)
constexpr DWORD PASS_WINDOW_MS = 15000;
// the host walked into an interior: the script waits this long for the others
constexpr DWORD ENTRY_GIVE_UP_MS = 45000;
// an outside player this close to the door the host went through is "coming"
constexpr float COMING_RANGE = 60.0f;

const uint8_t* s_holdIp = nullptr;
DWORD s_holdStart = 0;
DWORD s_lastLog = 0;
const CRunningScript* s_scene = nullptr;
DWORD s_passUntil = 0;
// interior entry hold
const CRunningScript* s_entryScript = nullptr;
unsigned s_entryLastArea = 0, s_entryArea = 0;
bool s_entryHolding = false;
DWORD s_entryStart = 0, s_entryLastLog = 0, s_entryLastTeleport = 0;
// interiors this mission already gathered the players in: walking in again (Home Invasion's boxes) does not hold
std::vector<unsigned> s_entryDone;
CVector s_doorPos{};
bool s_haveDoor = false;

// a literal parameter at ip (int8/int16/int32); false = a variable or another type
bool ReadLiteral(const uint8_t* ip, int& value, int& size)
{
    switch (ip[0])
    {
        case 0x04: value = static_cast<int8_t>(ip[1]); size = 2; return true;
        case 0x05: value = *reinterpret_cast<const int16_t*>(ip + 1); size = 3; return true;
        case 0x01: value = *reinterpret_cast<const int32_t*>(ip + 1); size = 5; return true;
    }
    return false;
}

// DO_FADE(time, direction): gta-reversed CameraCommands.cpp DoCamFade, direction 0 (eFadeFlag::FADE_IN in its naming) is the
// one that sets bScriptHasFadedOut: a fade to black
bool IsFadeOut(const CRunningScript* script)
{
    const uint8_t* ip = script->m_pCurrentIP;
    int time = 0, direction = 0, st = 0, sd = 0;
    if (!ip || !ReadLiteral(ip + 2, time, st) || !ReadLiteral(ip + 2 + st, direction, sd))
        return false;
    return direction == 0;
}

// SELECT_INTERIOR with a literal other than 0 (outside)
bool IsInteriorSelect(const CRunningScript* script)
{
    const uint8_t* ip = script->m_pCurrentIP;
    int a = 0, sa = 0;
    return ip && ReadLiteral(ip + 2, a, sa) && a != 0;
}

// SWITCH_WIDESCREEN(true) with a literal parameter (a variable is not a scene start we can read here)
bool IsWidescreenOn(const CRunningScript* script)
{
    const uint8_t* ip = script->m_pCurrentIP;
    if (!ip)
        return false;
    switch (ip[2])
    {
        case 0x04: return ip[3] != 0;  // int8
        case 0x05: return *reinterpret_cast<const int16_t*>(ip + 3) != 0;
        case 0x01: return *reinterpret_cast<const int32_t*>(ip + 3) != 0;
    }
    return false;
}

// the mission players that are not where the authority is; missing = names, ", " between them
bool FindMissing(CPlayerPed* host, std::string& missing, bool& inOtherPlace, bool& hostInside)
{
    const unsigned area = host->m_nAreaCode;
    hostInside = area != 0;
    const CVector origin = FindPlayerCoors(0);
    const float range = area != 0 ? RANGE_INSIDE : RANGE_OUTSIDE;
    inOtherPlace = false;
    for (auto player : CNetworkPlayerManager::m_pPlayers)
    {
        CPed* ped = player->m_pPed;
        if (!ped || player->m_bDead || ped->m_fHealth <= 0.0f || !CCoopMission::ShouldIncludeNetworkPlayer(player) ||
            CMissionLock::IsIndependentIntro(player->m_nRunningMission))
            continue;
        bool here = ped->m_nAreaCode == area;
        if (here)
        {
            if (ped->m_nPedFlags.bInVehicle && !ped->m_pVehicle)
                continue;  // between two vehicles of the sync: not known, do not hold for it
            const CVector pos = ped->m_nPedFlags.bInVehicle ? ped->m_pVehicle->GetPosition() : ped->GetPosition();
            const float dx = pos.x - origin.x, dy = pos.y - origin.y;
            here = dx * dx + dy * dy <= range * range;
        }
        else
            inOtherPlace = true;
        if (here)
            continue;
        if (!missing.empty())
            missing += ", ";
        missing += player->GetName();
    }
    return missing.empty();
}
}

void CMissionSceneWait::Reset()
{
    s_holdIp = nullptr;
    s_holdStart = 0;
}

bool CMissionSceneWait::MissingAreComing()
{
    CPlayerPed* host = FindPlayerPed(0);
    if (!s_haveDoor || !host)
        return false;
    bool any = false;
    for (auto player : CNetworkPlayerManager::m_pPlayers)
    {
        CPed* ped = player->m_pPed;
        if (!ped || player->m_bDead || ped->m_fHealth <= 0.0f || !CCoopMission::ShouldIncludeNetworkPlayer(player) ||
            CMissionLock::IsIndependentIntro(player->m_nRunningMission) || ped->m_nAreaCode == host->m_nAreaCode)
            continue;
        any = true;
        if (ped->m_nAreaCode != 0)
            return false;  // inside another interior: not coming
        const CVector pos = ped->m_nPedFlags.bInVehicle && ped->m_pVehicle ? ped->m_pVehicle->GetPosition() : ped->GetPosition();
        const float dx = pos.x - s_doorPos.x, dy = pos.y - s_doorPos.y;
        if (dx * dx + dy * dy > COMING_RANGE * COMING_RANGE)
            return false;
    }
    return any;
}

namespace
{
// The common conditions: the authority of a synced story mission runs this script, nobody is in a cutscene, he is alive
bool EligibleScript(CRunningScript* script)
{
    if (!script || !script->m_bIsMission || !CNetwork::m_bConnected || COpCodeSync::bProcessingNetworkOpcode ||
        !CCoopMission::IsScriptAuthority())
        return false;
    if (CCutsceneMgr::ms_running || !COpCodeSync::IsScriptSynced(script) || !CMissionSyncRules::IsStoryScript(script) ||
        CMirrorBlips::IsPerPlayerMissionRunning())
        return false;
    CPlayerPed* host = FindPlayerPed(0);
    return host && host->m_fHealth > 0.0f && CWorld::Players[0].m_nPlayerState != PLAYERSTATE_HASDIED &&
        CWorld::Players[0].m_nPlayerState != PLAYERSTATE_HASBEENARRESTED;
}

// the mission players outside the authority's interior go in next to him (a script-made entrance like Home Invasion's house
// has no door a guest can use; the owner asked for the teleport, 2026-11-12)
void TeleportOutsiders(unsigned area)
{
    CNetworkPlayer* players[8];
    int count = 0;
    for (auto player : CNetworkPlayerManager::m_pPlayers)
    {
        CPed* ped = player->m_pPed;
        if (count < 8 && ped && !player->m_bDead && ped->m_fHealth > 0.0f && CCoopMission::ShouldIncludeNetworkPlayer(player) &&
            !CMissionLock::IsIndependentIntro(player->m_nRunningMission) && ped->m_nAreaCode != area)
            players[count++] = player;
    }
    if (count > 0)
        CCommandTeleportPlayersToHostSafely::TeleportToHost(players, count);
    CMissionMirror::LogEventf("rule-hold-entry-teleport area=%u players=%d", area, count);
}

// The authority walked into an interior: his script stops (every command) and the players of the mission are teleported in
// next to him; it goes on once they are inside, so no text, camera or scene starts while a guest stands outside looking at
// an empty world (Home Invasion, 2026-11-12). Only the first entry of each interior per mission: walking in again later
// (carrying the boxes out and back) never holds.
bool HoldInteriorEntry(CRunningScript* script)
{
    CPlayerPed* host = FindPlayerPed(0);
    const unsigned area = host->m_nAreaCode;
    const DWORD now = GetTickCount();
    if (s_entryScript != script)
    {
        s_entryScript = script;
        s_entryLastArea = area;
        s_entryHolding = false;
        s_haveDoor = false;
        s_entryDone.clear();
        return false;
    }
    if (!s_entryHolding)
    {
        if (area == 0)
        {
            s_doorPos = host->GetPosition();  // the last place outside = the door he is about to go through
            s_haveDoor = true;
        }
        else if (area != s_entryLastArea &&
                 std::find(s_entryDone.begin(), s_entryDone.end(), area) == s_entryDone.end())
        {
            // the script's own way in first (interior shown, faded back in): held in the middle of it, the host stayed
            // black until the give-up (Home Invasion, testeo 17-11)
            if (CGame::currArea != area || TheCamera.m_bFading || CDraw::FadeValue > 20)
                return false;
            s_entryDone.push_back(area);
            s_entryHolding = true;
            s_entryArea = area;
            s_entryStart = now;
            s_entryLastTeleport = 0;
            CMissionMirror::LogEventf("rule-hold-entry-open area=%u", area);
        }
        s_entryLastArea = area;
        if (!s_entryHolding)
            return false;
    }
    if (area != s_entryArea)
    {
        s_entryHolding = false;  // he left or went on to another interior
        s_entryLastArea = area;
        return false;
    }
    std::string missing;
    bool inOtherPlace = false, hostInside = false;
    if (FindMissing(host, missing, inOtherPlace, hostInside))
    {
        CMissionMirror::LogEventf("rule-hold-entry-go area=%u waited=%u ms", area, now - s_entryStart);
        s_entryHolding = false;
        return false;
    }
    if (now - s_entryStart >= ENTRY_GIVE_UP_MS)
    {
        CMissionMirror::LogEventf("rule-hold-entry-giveup area=%u missing=%s", area, missing.c_str());
        s_entryHolding = false;
        return false;
    }
    // after the interior streamed in around the host; again every 10 s for the ones still outside
    // a real door: they walk in by themselves (a teleport left them at another door on the way out, testeo 13-11)
    // Home Invasion too: its house door (burhous) is opened by the script and synced; the teleport broke the guest (15-11)
    const bool teleport = !CConfigLoader::ms_bMissionEntryNoTeleport;
    if (teleport && inOtherPlace && now - s_entryStart >= 1000 &&
        (s_entryLastTeleport == 0 || now - s_entryLastTeleport >= 10000))
    {
        s_entryLastTeleport = now;
        TeleportOutsiders(area);
    }
    using Notice = Packets::Scripts::MissionWaitNotice;
    CMissionHints::AuthorityWait(Notice::ENTER_INTERIOR, missing);
    if (now - s_entryLastLog > 5000)
    {
        s_entryLastLog = now;
        if (!CMissionHints::WaitNoticesEnabled())
            CChat::AddMessage("{c7a6ff}[Coop]{d6dce8} Esperando a que %s entre para seguir la mision.", missing.c_str());
        CMissionMirror::LogEventf("rule-hold-entry area=%u missing=%s", area, missing.c_str());
    }
    return true;
}
}

// mission-hold-keep-tags: a held script does not run, but its player walks free (the hold of the Ballas scene in
// SWEET1 lasted 58 s while the host sprayed the roof tag). The script then un-sprays that area with
// SET_TAG_STATUS_IN_AREA (0x0703, FALSE) right before it asks for the tag, and the player had to spray it again.
// The tags sprayed during a hold are remembered and put back right after such a reset.
namespace
{
constexpr int NUM_TAG_SLOTS = 150;
constexpr uint8_t ALPHA_TAGGED = 228;
constexpr uint16_t OP_SET_TAG_STATUS_IN_AREA = 0x0703;
constexpr uint16_t OP_SCRIPT_NAME = 0x03A4;
const CRunningScript* s_trackScript = nullptr;
uint8_t s_tagSnapshot[NUM_TAG_SLOTS];
uint8_t s_tagKeep[NUM_TAG_SLOTS];     // alpha to put back, 0 = nothing to keep
uint8_t s_tagRestore[NUM_TAG_SLOTS];  // alpha to put back before the next command
bool s_restorePending = false;
unsigned int s_releasedAt = 0;      // a fail/cleanup long after the hold must not repaint
constexpr unsigned int KEEP_AFTER_RELEASE_MS = 30000;

void ClearKeptTags()
{
    s_trackScript = nullptr;
    s_restorePending = false;
    memset(s_tagKeep, 0, sizeof(s_tagKeep));
    memset(s_tagRestore, 0, sizeof(s_tagRestore));
}

void TrackHeldScript(const CRunningScript* script)
{
    const int numTags = std::clamp(CTagManager::ms_numTags, 0, NUM_TAG_SLOTS);
    if (s_trackScript != script)
    {
        s_trackScript = script;
        for (int i = 0; i < numTags; i++)
            s_tagSnapshot[i] = CTagManager::ms_tagDesc[i].m_nAlpha;
        return;
    }
    for (int i = 0; i < numTags; i++)
    {
        const uint8_t alpha = CTagManager::ms_tagDesc[i].m_nAlpha;
        if (alpha > ALPHA_TAGGED && s_tagSnapshot[i] <= ALPHA_TAGGED && s_tagKeep[i] == 0)
        {
            s_tagKeep[i] = alpha;
            CMissionMirror::LogEventf("hold-tag-sprayed tag=%d alpha=%d", i, (int)alpha);
        }
    }
}
}

bool CMissionSceneWait::ShouldHold(CRunningScript* script, unsigned short opcode)
{
    const bool hold = ShouldHoldCore(script, opcode);
    if (CConfigLoader::ms_bMissionHoldKeepTags && script && script->m_bIsMission)
    {
        if (hold)
            TrackHeldScript(script);
        else if (s_trackScript == script)
        {
            s_trackScript = nullptr;  // released: what was sprayed meanwhile stays remembered in s_tagKeep
            s_releasedAt = CTimer::m_snTimeInMilliseconds;
        }
    }
    return hold;
}

void CMissionSceneWait::OnCommandStart(CRunningScript* script, unsigned short opcode)
{
    if (!CConfigLoader::ms_bMissionHoldKeepTags || !script)
        return;
    // SCRIPT_NAME runs on each new mission, even when GTA reuses the same script slot
    if (opcode == OP_SCRIPT_NAME && script->m_bIsMission)
    {
        ClearKeptTags();
        return;
    }
    if (s_restorePending)
    {
        s_restorePending = false;
        for (int i = 0; i < NUM_TAG_SLOTS; i++)
        {
            if (!s_tagRestore[i])
                continue;
            auto& tagDesc = CTagManager::ms_tagDesc[i];
            if (tagDesc.m_pEntity && tagDesc.m_nAlpha <= ALPHA_TAGGED)
            {
                CTagManager::SetAlpha(tagDesc.m_pEntity, s_tagRestore[i]);
                CMissionMirror::LogEventf("hold-tag-restored tag=%d alpha=%d", i, (int)s_tagRestore[i]);
            }
            s_tagRestore[i] = 0;
        }
    }
    if (opcode != OP_SET_TAG_STATUS_IN_AREA || !script->m_bIsMission)
        return;
    bool any = false;
    for (int i = 0; i < NUM_TAG_SLOTS && !any; i++)
        any = s_tagKeep[i] != 0;
    if (!any)
        return;
    if (s_trackScript == nullptr && CTimer::m_snTimeInMilliseconds - s_releasedAt > KEEP_AFTER_RELEASE_MS)
    {
        ClearKeptTags();
        return;
    }
    // read the parameters (x1 y1 x2 y2 bool) without consuming them: the command reads them again. The capture hook
    // on CollectParameters must not see this read (the synced 0x0703 would carry the parameters twice)
    float f[4];
    int tagged;
    {
        int params[5];
        OpcodeParameter buffer[NUM_SYNCED_PARAMS];
        const bool network = COpCodeSync::bProcessingNetworkOpcode;
        memcpy(params, ScriptParams, sizeof(params));
        memcpy(buffer, COpCodeSync::scriptParamsBuffer, sizeof(buffer));
        COpCodeSync::BeginLocalCommands();
        COpCodeSync::bProcessingNetworkOpcode = true;
        const uint8_t* ip = script->m_pCurrentIP;
        script->m_pCurrentIP += 2;
        script->CollectParameters(5);
        script->m_pCurrentIP = const_cast<uint8_t*>(ip);
        memcpy(f, &ScriptParams[0], sizeof(f));
        tagged = ScriptParams[4];
        memcpy(ScriptParams, params, sizeof(params));
        memcpy(COpCodeSync::scriptParamsBuffer, buffer, sizeof(buffer));
        COpCodeSync::bProcessingNetworkOpcode = network;
        COpCodeSync::EndLocalCommands();
    }
    if (tagged != 0)
        return;
    const float minX = (std::min)(f[0], f[2]), maxX = (std::max)(f[0], f[2]);
    const float minY = (std::min)(f[1], f[3]), maxY = (std::max)(f[1], f[3]);
    for (int i = 0; i < NUM_TAG_SLOTS; i++)
    {
        auto& tagDesc = CTagManager::ms_tagDesc[i];
        if (!s_tagKeep[i] || !tagDesc.m_pEntity)
            continue;
        const CVector pos = tagDesc.m_pEntity->GetPosition();
        if (pos.x < minX || pos.x > maxX || pos.y < minY || pos.y > maxY)
            continue;
        s_tagRestore[i] = s_tagKeep[i];
        s_tagKeep[i] = 0;
        s_restorePending = true;
    }
}

bool CMissionSceneWait::ShouldHoldCore(CRunningScript* script, unsigned short opcode)
{
    if (CConfigLoader::ms_bMissionHoldOnEntry && script && script->m_bIsMission && EligibleScript(script) &&
        HoldInteriorEntry(script))
        return true;

    // the first commands of a scene reach the guests before the scene itself (fade to black, the interior, the cutscene
    // load): the hold starts there, so the guest keeps his control and sees the world until everybody is together
    const bool prelude = CConfigLoader::ms_bMissionScenePrelude && script &&
        ((opcode == OP_DO_FADE && IsFadeOut(script)) || (opcode == OP_SELECT_INTERIOR && IsInteriorSelect(script)) ||
         opcode == OP_LOAD_CUTSCENE);
    if (opcode != OP_START_CUTSCENE && opcode != OP_SWITCH_WIDESCREEN && !prelude)
        return false;
    if (!CConfigLoader::ms_bMissionWaitScenes || !EligibleScript(script))
        return false;
    if (opcode == OP_SWITCH_WIDESCREEN && (!IsWidescreenOn(script) || TheCamera.m_bWideScreenOn))
        return false;
    const DWORD now = GetTickCount();
    if (s_scene != script)
    {
        s_scene = script;
        s_passUntil = 0;
        Reset();
    }
    // the scene was released a moment ago: its own commands go through (the fade, the load, the start, the bars)
    if (now < s_passUntil)
    {
        if (TheCamera.m_bWideScreenOn)
            s_passUntil = now + PASS_WINDOW_MS;
        return false;
    }
    // SWEET1 (Tagging Up Turf, 2026-11-18): the tag dialogs' bars held the script for 80 s+ while the players spray
    // apart; the host's mission must simply go on, the scene reaches the others mirrored wherever they are
    if (opcode == OP_SWITCH_WIDESCREEN && script && !_strnicmp(script->m_szName, "SWEET1", 8))
    {
        CMissionMirror::LogEventf("rule-wait-scene-skip opcode=%04X reason=sweet1-free", opcode);
        Reset();
        s_passUntil = now + PASS_WINDOW_MS;
        return false;
    }
    // SET_PLAYER_CONTROL off came first (CPad bPlayerSafe, replicated to the guests too): nobody can walk to the scene and
    // a hold froze everybody until the give-up (SWEET1, Sweet arriving after the last tag). The far ones are gathered after it.
    if (CConfigLoader::ms_bMissionSceneSkipNoControl && CPad::GetPad(0)->bPlayerSafe)
    {
        CMissionMirror::LogEventf("rule-wait-scene-skip opcode=%04X reason=no-control", opcode);
        Reset();
        s_passUntil = now + PASS_WINDOW_MS;
        return false;
    }
    CPlayerPed* host = FindPlayerPed(0);

    // bars that come on during a chase (the train got away in Wrong Side, a target escaping): the scene is the
    // outcome of the chase, nobody walks to a marker for it. Story scenes start with the host stopped at the marker.
    if (opcode == OP_SWITCH_WIDESCREEN && host && host->m_nPedFlags.bInVehicle && host->m_pVehicle &&
        host->m_pVehicle->m_vecMoveSpeed.Magnitude() > 0.15f)
    {
        CMissionMirror::LogEventf("rule-wait-scene-skip opcode=%04X reason=host-moving speed=%.2f", opcode,
            host->m_pVehicle->m_vecMoveSpeed.Magnitude());
        Reset();
        s_passUntil = now + PASS_WINDOW_MS;
        return false;
    }

    std::string missing;
    bool inOtherPlace = false, hostInside = false;
    if (FindMissing(host, missing, inOtherPlace, hostInside))
    {
        if (s_holdIp)
            CMissionMirror::LogEventf("rule-wait-scene-go opcode=%04X waited=%u ms", opcode, now - s_holdStart);
        Reset();
        s_passUntil = now + PASS_WINDOW_MS;
        return false;
    }
    if (s_holdIp != script->m_pCurrentIP)
    {
        s_holdIp = script->m_pCurrentIP;
        s_holdStart = now;
    }
    if (now - s_holdStart >= GIVE_UP_MS)
    {
        CMissionMirror::LogEventf("rule-wait-scene-giveup opcode=%04X missing=%s", opcode, missing.c_str());
        Reset();
        s_passUntil = now + PASS_WINDOW_MS;
        return false;
    }

    using Notice = Packets::Scripts::MissionWaitNotice;
    CMissionHints::AuthorityWait(!inOtherPlace ? Notice::MARKER : (hostInside ? Notice::ENTER_INTERIOR : Notice::LEAVE_INTERIOR),
        missing);
    if (now - s_lastLog > 5000)
    {
        s_lastLog = now;
        if (!CMissionHints::WaitNoticesEnabled())
            CChat::AddMessage("{c7a6ff}[Coop]{d6dce8} Esperando a %s para empezar la escena.", missing.c_str());
        CMissionMirror::LogEventf("rule-wait-scene opcode=%04X area=%d missing=%s", opcode, (int)host->m_nAreaCode,
            missing.c_str());
    }
    return true;
}
