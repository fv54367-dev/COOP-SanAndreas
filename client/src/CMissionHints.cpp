#include "stdafx.h"
#include "CMissionHints.h"
#include "CConfigLoader.h"
#include "CCoopMission.h"
#include "CLocalPlayer.h"
#include "CMissionMirror.h"
#include "CMissionMode.h"
#include "CNetwork.h"
#include "CNetworkPlayerManager.h"
#include "CPacketFactory.h"
#include "network/packets/mission_wait_notice.h"
#include <CUserDisplay.h>

namespace
{
// Story missions with an hour window, from the decompiled launchers (scm/scripts/*.txt): each one checks the hour only
// after Player.CanStartMission, and prints MTIMEx (Text.PrintNow, 1000 ms, flag 1) when it is outside. nOffset is the
// byte offset of the launcher's chain counter in ScriptSpace (same table as CMissionMode).
struct SHourGate
{
    const char* szLauncher;
    int nOffset;
    int nCounter;
    int nFrom;  // the mission starts from this hour...
    int nTo;    // ...until this one (exclusive); nFrom > nTo wraps over midnight
    const char* szKey;
    int nExtraOffset;  // a second counter the launcher also requires (-1 = none)
    int nExtraValue;
    int nStatAbove;    // WUZI: the hour is checked only with this stat above 50 (-1 = none)
};

const SHourGate s_aGates[] = {
    {"RYDER", 1812, 0, 12, 20, "MTIME6", -1, 0, -1},   // Home Invasion
    {"STRAP", 1820, 0, 22, 6, "MTIME5", -1, 0, -1},    // Life's a Beach
    {"STRAP", 1820, 2, 12, 5, "MTIME2", -1, 0, -1},    // Management Issues
    {"STRAP", 1820, 4, 20, 6, "MTIME1", -1, 0, -1},    // House Party
    {"SWEET", 1808, 8, 9, 17, "MTIME3", -1, 0, -1},    // Los Sepulcros (the launcher also needs a group member: not checked)
    {"SYND", 2180, 6, 20, 6, "MTIME1", 2184, 2, -1},   // Ice Cold Killa
    {"WUZI", 2172, 3, 20, 6, "MTIME1", -1, 0, 225},    // Amphibious Assault
    {"DESERT", 2372, 7, 20, 6, "MTIME1", -1, 0, -1},   // Black Project
    {"HEIST", 2400, 4, 20, 6, "MTIME1", -1, 0, -1},    // Up, Up and Away!
};

bool IsHelpBoxUp();

int ReadCounter(int offset)
{
    return *reinterpret_cast<int*>(&CTheScripts::ScriptSpace[offset]);
}
}

bool CMissionHints::Enabled()
{
    return CConfigLoader::ms_bNativeHints;
}

void CMissionHints::Help(const std::string& text)
{
    if (!Enabled() || text.empty())
        return;

    static char szHelp[200];
    static std::string lastText;
    static DWORD lastTick = 0;
    const DWORD now = GetTickCount();
    if (text == lastText && now - lastTick < 2000)
        return;
    lastText = text;
    lastTick = now;

    strncpy_s(szHelp, ToGameText(text).c_str(), _TRUNCATE);
    CHud::SetHelpMessage(szHelp, true, false, false);
}

std::string CMissionHints::ToGameText(const std::string& text)
{
    std::string out;
    out.reserve(text.size());
    for (size_t i = 0; i < text.size(); ++i)
    {
        const unsigned char c = static_cast<unsigned char>(text[i]);
        if (c < 0x80)
        {
            out += c == '~' ? '-' : static_cast<char>(c);
            continue;
        }
        // a two-byte UTF-8 sequence covers all of Latin-1; a lone byte is taken as Latin-1 (cp1252 strings)
        unsigned int cp = c;
        if ((c & 0xE0) == 0xC0 && i + 1 < text.size() && (static_cast<unsigned char>(text[i + 1]) & 0xC0) == 0x80)
            cp = ((c & 0x1F) << 6) | (static_cast<unsigned char>(text[++i]) & 0x3F);
        else if (c >= 0xC0)
        {
            while (i + 1 < text.size() && (static_cast<unsigned char>(text[i + 1]) & 0xC0) == 0x80)
                ++i;  // longer UTF-8 sequence: no game glyph, dropped
            continue;
        }
        static const char* const latin = "AAAAAAACEEEEIIIIDNOOOOOxOUUUUYTsaaaaaaaceeeeiiiidnooooo/ouuuuyty";
        if (cp >= 0xC0 && cp <= 0xFF)
            out += latin[cp - 0xC0];
    }
    return out;
}

void CMissionHints::OnLauncherAsked(CRunningScript* pLauncher)
{
    if (!Enabled() || pLauncher == nullptr)
        return;

    for (const SHourGate& gate : s_aGates)
    {
        if (_strnicmp(pLauncher->m_szName, gate.szLauncher, 8) != 0 || ReadCounter(gate.nOffset) != gate.nCounter)
            continue;
        if (gate.nExtraOffset >= 0 && ReadCounter(gate.nExtraOffset) != gate.nExtraValue)
            continue;
        if (gate.nStatAbove >= 0 && CStats::GetStatValue(static_cast<eStats>(gate.nStatAbove)) <= 50.0f)
            continue;

        const int hour = CClock::ms_nGameClockHours;
        const bool bOpen = gate.nFrom < gate.nTo ? (hour >= gate.nFrom && hour < gate.nTo) : (hour >= gate.nFrom || hour < gate.nTo);
        if (bOpen)
            return;

        // the launcher prints it every loop while the player stands in the marker (1 s on screen)
        static DWORD nLastTick = 0;
        const DWORD now = GetTickCount();
        if (now - nLastTick < 300)
            return;
        nLastTick = now;

        char* szText = TheText.Get(gate.szKey);
        if (szText != nullptr)
            CMessages::AddMessageJumpQ(szText, 1000, 1, false);
        // only the game's own bottom text, like vanilla: no copy in the top-left help box (testeo 15-11)
        return;
    }
}

namespace
{
using WaitNotice = Packets::Scripts::MissionWaitNotice;

std::string s_waitShown;    // game text of the wait notice this game put in the help box
DWORD s_waitRenewed = 0;    // last time somebody asked for it
DWORD s_waitDrawn = 0;      // last SetHelpMessage of it
DWORD s_networkWait = 0;    // last notice from the authority
DWORD s_authorityWait = 0;  // last time this game (the authority) waited for players inside the mission
bool s_timerPausedByUs = false;

std::string WaitingFor(uint8_t kind, const std::string& missing)
{
    switch (kind)
    {
        case WaitNotice::START: return "Esperando a " + missing + " en el marcador para empezar la mision";
        case WaitNotice::LEAVE_INTERIOR: return "Esperando a que " + missing + " salga para seguir la mision";
        case WaitNotice::ENTER_INTERIOR: return "Esperando a que " + missing + " entre para seguir la mision";
        case WaitNotice::RACE_FINISH: return "Esperando a " + missing + " en la meta para terminar la carrera";
        default: return "Esperando a " + missing + " para seguir la mision";
    }
}

std::string WaitedText(uint8_t kind)
{
    switch (kind)
    {
        case WaitNotice::START: return "Te esperan en el marcador para empezar la mision";
        case WaitNotice::LEAVE_INTERIOR: return "Te esperan afuera para seguir la mision";
        case WaitNotice::ENTER_INTERIOR: return "Te esperan adentro para seguir la mision";
        case WaitNotice::BEACH: return "Te esperan en la fiesta de la playa para seguir la mision";
        case WaitNotice::RACE_FINISH: return "Te esperan en la meta para terminar la carrera";
        default: return "Te esperan en el marcador para seguir la mision";
    }
}

// the help box is up (0x58B6E0: state 0 = none) or a new one waits for the next draw
bool IsHelpBoxUp()
{
    return CHud::m_nHelpMessageState != 0 ||
        (CHud::m_pHelpMessage[0] != '\0' && strncmp(CHud::m_pHelpMessage, CHud::m_pLastHelpMessage, 400) != 0);
}
}

bool CMissionHints::WaitNoticesEnabled()
{
    return Enabled() && CConfigLoader::ms_bMissionWaitNotices;
}

void CMissionHints::ShowWait(const std::string& text)
{
    const DWORD now = GetTickCount();
    const std::string game = ToGameText(text);
    s_waitRenewed = now;
    if (IsHelpBoxUp())
    {
        if (strncmp(CHud::m_pHelpMessageToPrint, game.c_str(), 400) == 0)
            return;  // already up
        // another help box (the mission's): never over it, the notice comes back when it is gone
        if (s_waitShown.empty() || strncmp(CHud::m_pHelpMessageToPrint, s_waitShown.c_str(), 400) != 0)
            return;
    }
    else if (game == s_waitShown && now - s_waitDrawn < 500)
        return;  // the game refused it (cutscene, garage text): try again a bit later

    static char szWait[200];
    strncpy_s(szWait, game.c_str(), _TRUNCATE);
    // permanent: it stays while the wait lasts and the box never replays its sound (Process takes it away)
    CHud::SetHelpMessage(szWait, false, true, false);
    s_waitShown = game;
    s_waitDrawn = now;
}

void CMissionHints::AuthorityWait(uint8_t kind, const std::string& missing)
{
    if (!missing.empty() && kind != WaitNotice::START)
        s_authorityWait = GetTickCount();
    if (!WaitNoticesEnabled() || missing.empty())
        return;
    ShowWait(WaitingFor(kind, missing));
    SendWait(kind, missing);
}

void CMissionHints::SendWait(uint8_t kind, const std::string& missing)
{
    static DWORD nSent = 0;
    static uint8_t nLastKind = 0xFF;
    static std::string lastMissing;
    const DWORD now = GetTickCount();
    if (!CNetwork::m_bAuthenticated || (now - nSent < 1000 && kind == nLastKind && missing == lastMissing))
        return;
    nSent = now;
    nLastKind = kind;
    lastMissing = missing;
    WaitNotice packet{};
    packet.kind = kind;
    strncpy_s(packet.missing, missing.c_str(), _TRUNCATE);
    GetPacketFactory().Send(packet);
}

void CMissionHints::LocalStartWait(const std::string& missing)
{
    // the authority's notice says it better (it knows who is in its marker)
    if (!WaitNoticesEnabled() || missing.empty() || GetTickCount() - s_networkWait < 2000)
        return;
    ShowWait(WaitingFor(WaitNotice::START, missing));
    // the authority is not in a marker, so its launcher never asks: he learns from this that he is awaited
    SendWait(WaitNotice::START, missing);
}

void CMissionHints::AuthorityAwaited(uint8_t kind)
{
    if (!WaitNoticesEnabled())
        return;
    ShowWait(WaitedText(kind));
    SendWait(kind, CLocalPlayer::m_Name);
}

void CMissionHints::ReceiveWaitNotice(playerid_t sender, uint8_t kind, const char* missing)
{
    if (!WaitNoticesEnabled() || !CNetwork::m_bAuthenticated || kind >= WaitNotice::KIND_COUNT || missing == nullptr ||
        CMissionMode::IsIndividual() || CCoopMission::IsLocalIndividualMission() || CCoopMission::HasLocalLeft())
        return;
    // a guest's start notice reaches the authority too: he only cares when he is the one awaited
    const bool bAuthority = CCoopMission::IsScriptAuthority();

    // this player among the missing ones, and the others
    bool bMine = false;
    std::string others;
    const std::string list(missing, strnlen(missing, 128));
    size_t from = 0;
    while (from <= list.size())
    {
        size_t to = list.find(", ", from);
        if (to == std::string::npos)
            to = list.size();
        const std::string name = list.substr(from, to - from);
        if (!name.empty())
        {
            if (name == CLocalPlayer::m_Name)
                bMine = true;
            else
                others += (others.empty() ? "" : ", ") + name;
        }
        from = to + 2;
    }
    if (!bMine && (others.empty() || bAuthority))
        return;

    s_networkWait = GetTickCount();
    ShowWait(bMine ? WaitedText(kind) : WaitingFor(kind, others));

    static DWORD nLastLog = 0;
    if (GetTickCount() - nLastLog > 5000)
    {
        nLastLog = GetTickCount();
        CMissionMirror::LogEventf("rx-wait kind=%d mine=%d missing=%s", kind, bMine ? 1 : 0, list.c_str());
    }
}

void CMissionHints::Process()
{
    // mission-wait-pause-timer: the mission's countdown (Los Sepulcros' 240 s) stands still while the players wait for
    // each other; a freeze of the script's own (FREEZE_ONSCREEN_TIMER) is left alone
    const DWORD now = GetTickCount();
    const bool waiting = CConfigLoader::ms_bMissionWaitPauseTimer && CTheScripts::OnAMissionFlag &&
        CTheScripts::ScriptSpace[CTheScripts::OnAMissionFlag] &&
        (now - s_authorityWait < 1500 || (s_networkWait && now - s_networkWait < 2000));
    COnscreenTimer& timer = CUserDisplay::OnscnTimer;
    if (waiting && !timer.m_bPaused)
    {
        timer.m_bPaused = true;
        s_timerPausedByUs = true;
        CMissionMirror::LogEventf("wait-timer pause");
    }
    else if (!waiting && s_timerPausedByUs)
    {
        timer.m_bPaused = false;
        s_timerPausedByUs = false;
        CMissionMirror::LogEventf("wait-timer resume");
    }
    // a start notice goes at once with the mission begun; otherwise a short gap in the asking (the player at the edge of
    // the marker: its LOCATE false for a moment) made it blink on and off (testeo 14-11, the host at a marker)
    const bool started = CTheScripts::OnAMissionFlag && CTheScripts::ScriptSpace[CTheScripts::OnAMissionFlag] &&
        s_waitShown.find("empezar") != std::string::npos;
    if (s_waitShown.empty() || (!started && GetTickCount() - s_waitRenewed < 4000))
        return;
    // nobody renews it: the wait is over (a mission title or another help box may have taken it away already)
    if (IsHelpBoxUp() && strncmp(CHud::m_pHelpMessageToPrint, s_waitShown.c_str(), 400) == 0)
        CHud::SetHelpMessage(nullptr, true, false, false);
    s_waitShown.clear();
}
