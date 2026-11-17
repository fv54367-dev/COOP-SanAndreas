#include "stdafx.h"
#include "CVsLobby.h"

#include "COpCodeSync.h"
#include "CMissionMode.h"
#include "CMissionMirror.h"
#include <mission_ids.h>
#include "UI/CChat.h"
#include "CServerTime.h"
#include "CConfigLoader.h"
#include "UI/CCheatMenu.h"
#include "CMissionHints.h"
#include "CTestMode.h"
#include <CFont.h>
#include <CSprite2d.h>
#include <CCamera.h>
#include "network/packets/scripts.h"

#include "imgui.h"

#include <algorithm>
#include <string>

namespace
{
constexpr uint32_t DECIDE_MS = 15000;     // Y / N window of the owner
constexpr uint32_t INVITE_MS = 20000;     // time a guest has to answer
constexpr uint32_t START_LEAD_MS = 800;   // from GO to the common start: covers the latency of the slowest guest
constexpr uint32_t START_MAX_MS = 3000;   // sanity cap against a bad clock offset
constexpr uint32_t ARRIVAL_MS = 90000;    // time the owner waits for the accepted players to walk into the marker

std::string PlayerName(playerid_t playerid)
{
    if (playerid == CNetworkPlayerManager::m_localPlayerId)
        return CLocalPlayer::m_Name[0] != '\0' ? CLocalPlayer::m_Name : "Vos";

    if (CNetworkPlayer* pNetworkPlayer = CNetworkPlayerManager::GetPlayer(playerid))
    {
        std::string name = pNetworkPlayer->GetName();
        if (!name.empty())
            return name;
    }

    return "Jugador " + std::to_string(playerid.value());
}

bool IsMenuOrChatOpen()
{
    return FrontEndMenuManager.m_bMenuActive || CCheatMenu::ms_bOpen || CModeMenu::ms_bOpen || CChat::m_bInputActive;
}

bool IsScriptRunning(CRunningScript* pScript)
{
    for (CRunningScript* script = CTheScripts::pActiveScripts; script; script = script->m_pNext)
    {
        if (script == pScript)
            return true;
    }
    return false;
}

// this game's own race script (CPRACE, or CPRSOLO once released)
CRunningScript* FindLocalRaceScript()
{
    for (CRunningScript* script = CTheScripts::pActiveScripts; script; script = script->m_pNext)
    {
        if (script->m_bIsMission &&
            (_strnicmp(script->m_szName, "CPRACE", 6) == 0 || _strnicmp(script->m_szName, "CPRSOLO", 7) == 0))
            return script;
    }
    return nullptr;
}

bool IsLocalPlayerDeadOrArrested()
{
    return CWorld::Players[0].m_nPlayerState == PLAYERSTATE_HASDIED ||
           CWorld::Players[0].m_nPlayerState == PLAYERSTATE_HASBEENARRESTED;
}

bool IsStillInPlayers(playerid_t id)
{
    return CNetworkPlayerManager::GetPlayer(id) != nullptr;
}

// number row key pressed this frame: 1 / 2 / 3 choose (the user, 14-11: no Y / N), 4 leaves the marker
bool KeyJustDown(int key)
{
    static bool down[4]{};
    bool& was = down[key - '1'];
    const bool now = CTestMode::IsGameWindowFocused() && (GetAsyncKeyState(key) & 0x8000) != 0;
    const bool pressed = now && !was;
    was = now;
    return pressed;
}
}  // namespace

void CVsLobby::Init()
{
    Events::gameProcessEvent.after += []
    {
        Process();
    };
}

// no race: the script returns from its body (a gosub) to its own cleanup, as when the race menu is left
void CVsLobby::AbortScript(CRunningScript* pScript)
{
    if (pScript == nullptr || !IsScriptRunning(pScript))
        return;
    ms_pAborted = pScript;
    if (pScript->m_nSP == 0)
    {
        CMissionMirror::LogEvent("vs-lobby-abort-failed reason=no-gosub");
        return;
    }
    // exit_mode (local 53) is not set yet: 0 would send the player to the Race Tournament marker of the city
    // (RaceTour.sc mission_cleanup_cprace); 2 skips that teleport and its fade in
    if (pScript->m_bIsMission)
    {
        reinterpret_cast<int*>(CTheScripts::LocalVariablesForCurrentMission)[53] = 2;
        // stays where the marker is, a step back out of it so the launcher does not start it again at once
        if (CPlayerPed* pPlayer = FindPlayerPed(0); pPlayer != nullptr && !pPlayer->m_nPedFlags.bInVehicle)
            pPlayer->SetPosn(pPlayer->GetPosition() - pPlayer->GetForward() * 3.0f);
        TheCamera.Fade(0.5f, 1);  // the launcher faded the screen out (mini_fade)
    }
    pScript->m_pCurrentIP = pScript->m_apStack[--pScript->m_nSP];
    pScript->m_nWakeTime = 0;
    CMissionMirror::LogEventf("vs-lobby-abort script=%.8s", pScript->m_szName);
}

bool CVsLobby::WithNpcs()
{
    return ms_nMode == 1 && (ms_eRole == ROLE_STARTED || ms_eRole == ROLE_GUEST_GO || ms_eRole == ROLE_OWNER_COUNTDOWN ||
        ms_eRole == ROLE_GUEST_COUNTDOWN);
}

bool CVsLobby::IsOwnerLocal()
{
    return ms_ownerId == CNetworkPlayerManager::m_localPlayerId;
}

void CVsLobby::Send(uint8_t action)
{
    Packets::Scripts::RaceVsLobby packet{};
    packet.action = action;
    CMissionMirror::LogEventf("vs-lobby-tx action=%u role=%d", (unsigned)action, (int)ms_eRole);
    if (action == Packets::Scripts::RaceVsLobby::GO)
        packet.startTime = ms_nStartAt;
    packet.mode = ms_nMode;
    GetPacketFactory().Send(packet);
}

void CVsLobby::Reset()
{
    ms_eRole = ROLE_NONE;
    ms_pScript = nullptr;
    ms_vGuests.clear();
    ms_nMode = 0;
}

// busy in another mission (not the race itself), dead, in a cutscene: cannot take part
bool CVsLobby::CanAnswerInvite()
{
    if (CCutsceneMgr::ms_running || IsLocalPlayerDeadOrArrested())
        return false;

    if (COpCodeSync::IsLocalMissionRunning() && !IsRaceMissionId(CMissionMirror::GetRunningMissionId()))
        return false;

    return true;
}

bool CVsLobby::Choose(CRunningScript* pScript)
{
    if (pScript == nullptr || !CNetwork::m_bAuthenticated || pScript == ms_pAborted)
        return true;

    // a guest that accepted walked into the marker: his race script waits for the owner
    if (ms_eRole == ROLE_GUEST_ACCEPTED && ms_pScript == nullptr)
    {
        ms_pScript = pScript;
        ms_eRole = ROLE_GUEST_ARRIVED;
        Send(Packets::Scripts::RaceVsLobby::ARRIVED);
        CChat::AddMessage("{c7a6ff}[Coop]{d6dce8} Esperando que " + PlayerName(ms_ownerId) + " arranque la carrera...");
        return false;
    }

    // still answering the invitation while he stepped into the marker
    if (ms_eRole == ROLE_GUEST_INVITED)
        return false;

    if (ms_pScript != pScript)
    {
        if (ms_pScript != nullptr && IsScriptRunning(ms_pScript))
            return true;  // another race script of this client holds the lobby

        Reset();

        // nobody to race against
        if (CNetworkPlayerManager::m_pPlayers.empty())
            return true;

        ms_pScript = pScript;
        ms_eRole = ROLE_DECIDING;
        ms_ownerId = CNetworkPlayerManager::m_localPlayerId;
        ms_nDeadline = GetTickCount() + DECIDE_MS;
        return false;
    }

    return ms_eRole == ROLE_STARTED || ms_eRole == ROLE_GUEST_GO;  // the countdown roles keep the script held
}

void CVsLobby::StartOwnerRace(bool bWithGuests)
{
    ms_eRole = ROLE_STARTED;

    if (bWithGuests)
    {
        // everybody (owner included) starts the race at the same server instant instead of when the packet arrives
        ms_nStartAt = CConfigLoader::ms_bVsRaceSync ? g_serverTime + START_LEAD_MS : 0;
        Send(Packets::Scripts::RaceVsLobby::GO);
        if (ms_nStartAt != 0)
            ms_eRole = ROLE_OWNER_COUNTDOWN;
        CChat::AddMessage("{c7a6ff}[Coop]{d6dce8} Carrera VS: " "arranca!");
    }
    else
    {
        Send(Packets::Scripts::RaceVsLobby::CANCEL);
    }
}

bool CVsLobby::StartReached()
{
    const int32_t remaining = (int32_t)(ms_nStartAt - g_serverTime);
    return remaining <= 0 || remaining > (int32_t)START_MAX_MS;
}

void CVsLobby::Process()
{
    // the aborted script ended: a race launched later may get the same script slot and must be held again
    if (ms_pAborted != nullptr && !IsScriptRunning(ms_pAborted))
        ms_pAborted = nullptr;

    if (ms_eRole == ROLE_NONE)
        return;

    const uint32_t now = GetTickCount();

    if (ms_eRole == ROLE_ABORT)
    {
        AbortScript(ms_pScript);
        Reset();
        return;
    }

    // the script that held the lobby ended (the owner died, left, the race finished)
    if (ms_pScript != nullptr && !IsScriptRunning(ms_pScript))
    {
        if (ms_eRole == ROLE_DECIDING || ms_eRole == ROLE_WAITING)
            Send(Packets::Scripts::RaceVsLobby::CANCEL);
        Reset();
        return;
    }

    if (!CNetwork::m_bAuthenticated)
    {
        Reset();
        return;
    }

    switch (ms_eRole)
    {
    case ROLE_DECIDING:
    {
        if (CNetworkPlayerManager::m_pPlayers.empty() || (int32_t)(now - ms_nDeadline) >= 0)
        {
            ms_eRole = ROLE_STARTED;  // alone
            break;
        }

        if (IsMenuOrChatOpen())
            break;

        // 1: the players and the owner's NPC racers, 2: VS against the players only, 3: alone against the NPCs
        // (order asked by the players, 2026-11-15)
        const bool bNpcs = KeyJustDown('1');
        const bool bPlayers = KeyJustDown('2');
        const bool bAlone = KeyJustDown('3');
        // 4: walked into the marker by mistake, the race script goes back to its own exit (the user, 16-11)
        if (KeyJustDown('4'))
        {
            CChat::AddMessage("{c7a6ff}[Coop]{d6dce8} Saliste del marcador de la carrera.");
            ms_eRole = ROLE_ABORT;
            break;
        }
        if (bPlayers || bNpcs)
        {
            ms_nMode = bNpcs ? 1 : 0;
            ms_vGuests.clear();
            for (auto pNetworkPlayer : CNetworkPlayerManager::m_pPlayers)
            {
                if (pNetworkPlayer)
                {
                    SGuest guest{};
                    guest.id = pNetworkPlayer->m_playerId;
                    ms_vGuests.push_back(guest);
                }
            }

            Send(Packets::Scripts::RaceVsLobby::INVITE);
            ms_eRole = ROLE_WAITING;
            ms_nDeadline = now + INVITE_MS;
            ms_nArrivalDeadline = now + ARRIVAL_MS;
            CChat::AddMessage(ms_nMode ? "{c7a6ff}[Coop]{d6dce8} Invitaste a los demas jugadores a una carrera VS con NPC."
                                       : "{c7a6ff}[Coop]{d6dce8} Invitaste a los demas jugadores a una carrera VS.");
        }
        else if (bAlone)
        {
            ms_eRole = ROLE_STARTED;  // alone
        }
        break;
    }

    case ROLE_WAITING:
    {
        if (!IsMenuOrChatOpen() && KeyJustDown('4'))
        {
            CChat::AddMessage("{c7a6ff}[Coop]{d6dce8} Cancelaste la carrera VS y saliste del marcador.");
            Send(Packets::Scripts::RaceVsLobby::CANCEL);
            ms_eRole = ROLE_ABORT;
            break;
        }
        int nInvited = 0, nAccepted = 0, nArrived = 0;
        for (auto& guest : ms_vGuests)
        {
            // disconnected, or ran out of time answering
            if (guest.state != GUEST_DECLINED && !IsStillInPlayers(guest.id))
                guest.state = GUEST_DECLINED;
            if (guest.state == GUEST_INVITED && (int32_t)(now - ms_nDeadline) >= 0)
                guest.state = GUEST_DECLINED;

            if (guest.state == GUEST_INVITED)
                nInvited++;
            else if (guest.state == GUEST_ACCEPTED)
                nAccepted++;
            else if (guest.state == GUEST_ARRIVED)
                nArrived++;
        }

        const bool bTimeout = (int32_t)(now - ms_nArrivalDeadline) >= 0;

        if (nInvited == 0 && nAccepted == 0)
        {
            if (nArrived == 0)
            {
                // an offer nobody took starts no race at all (the user, 14-11: not one alone with the NPCs)
                CChat::AddMessage("{c7a6ff}[Coop]{d6dce8} Nadie acepto la carrera VS: no arranca.");
                Send(Packets::Scripts::RaceVsLobby::CANCEL);
                ms_eRole = ROLE_ABORT;
            }
            else
            {
                StartOwnerRace(true);
            }
        }
        else if (bTimeout)
        {
            if (nArrived == 0)
            {
                CChat::AddMessage("{c7a6ff}[Coop]{d6dce8} Nadie llego al marcador: la carrera VS no arranca.");
                Send(Packets::Scripts::RaceVsLobby::CANCEL);
                ms_eRole = ROLE_ABORT;
            }
            else
            {
                StartOwnerRace(true);
            }
        }
        break;
    }

    case ROLE_GUEST_INVITED:
    {
        if ((int32_t)(now - ms_nDeadline) >= 0 || !CanAnswerInvite())
        {
            Send(Packets::Scripts::RaceVsLobby::DECLINE);
            Reset();
            break;
        }

        if (IsMenuOrChatOpen())
            break;

        const bool bYes = KeyJustDown('1');
        const bool bNo = KeyJustDown('2');
        if (bYes)
        {
            Send(Packets::Scripts::RaceVsLobby::ACCEPT);
            ms_eRole = ROLE_GUEST_ACCEPTED;
            ms_nDeadline = now + ARRIVAL_MS + INVITE_MS;
            CChat::AddMessage("{c7a6ff}[Coop]{d6dce8} Aceptaste la carrera VS: entra al marcador de la carrera.");
        }
        else if (bNo)
        {
            Send(Packets::Scripts::RaceVsLobby::DECLINE);
            Reset();
        }
        break;
    }

    case ROLE_GUEST_ACCEPTED:
        if ((int32_t)(now - ms_nDeadline) >= 0 || !IsStillInPlayers(ms_ownerId))
        {
            CChat::AddMessage("{c7a6ff}[Coop]{d6dce8} La carrera VS ya no te espera.");
            Reset();
            break;
        }
        // the race script started without passing through the side mission hold (8-Track, testeo 15-11: the guest raced
        // alone and the owner waited in black for an ARRIVED that never came): it arrives from here and is held below
        if (CRunningScript* pRace = FindLocalRaceScript(); pRace != nullptr && pRace != ms_pAborted)
        {
            CMissionMirror::LogEventf("vs-lobby-arrived-late script=%.8s", pRace->m_szName);
            Choose(pRace);
        }
        break;

    case ROLE_GUEST_ARRIVED:
        if (!IsStillInPlayers(ms_ownerId))
        {
            ms_eRole = ROLE_GUEST_GO;  // the owner left: the race goes on alone
            break;
        }
        if (!IsMenuOrChatOpen() && KeyJustDown('4'))
        {
            // the owner counts him out as if he had said no
            CChat::AddMessage("{c7a6ff}[Coop]{d6dce8} Saliste del marcador de la carrera.");
            Send(Packets::Scripts::RaceVsLobby::DECLINE);
            ms_eRole = ROLE_ABORT;
            break;
        }
        if (ms_pScript != nullptr)
            ms_pScript->m_nWakeTime = CTimer::m_snTimeInMilliseconds + 100;  // waits for the owner's GO
        break;

    case ROLE_OWNER_COUNTDOWN:
        if (StartReached())
            ms_eRole = ROLE_STARTED;
        break;

    case ROLE_GUEST_COUNTDOWN:
        if (StartReached() || !IsStillInPlayers(ms_ownerId))
            ms_eRole = ROLE_GUEST_GO;
        else if (ms_pScript != nullptr)
            ms_pScript->m_nWakeTime = CTimer::m_snTimeInMilliseconds + 100;
        break;

    default:
        break;
    }
}

void CVsLobby::OnPacket(const Packets::Scripts::RaceVsLobby& packet)
{
    using Action = Packets::Scripts::RaceVsLobby;
    const playerid_t sender = packet.playerid;
    CMissionMirror::LogEventf("vs-lobby-rx action=%u from=%u role=%d", (unsigned)packet.action, (unsigned)sender.value(),
        (int)ms_eRole);

    switch (packet.action)
    {
    case Action::INVITE:
        if (ms_eRole == ROLE_NONE && CanAnswerInvite())
        {
            ms_eRole = ROLE_GUEST_INVITED;
            ms_ownerId = sender;
            ms_nMode = packet.mode;
            ms_nDeadline = GetTickCount() + INVITE_MS;
            CChat::AddMessage("{c7a6ff}[Coop]{d6dce8} " + PlayerName(sender) + " te invita a una carrera VS" + std::string(packet.mode ? " con NPC" : "") + " (1 = si / 2 = no).");
        }
        else
        {
            Send(Action::DECLINE);
        }
        break;

    case Action::ACCEPT:
    case Action::DECLINE:
    case Action::ARRIVED:
        if (ms_eRole == ROLE_WAITING)
        {
            for (auto& guest : ms_vGuests)
            {
                if (guest.id != sender)
                    continue;

                if (packet.action == Action::ACCEPT && guest.state == GUEST_INVITED)
                    guest.state = GUEST_ACCEPTED;
                else if (packet.action == Action::DECLINE)
                    guest.state = GUEST_DECLINED;
                else if (packet.action == Action::ARRIVED && guest.state != GUEST_DECLINED)
                    guest.state = GUEST_ARRIVED;
            }
        }
        break;

    case Action::GO:
        if (sender == ms_ownerId)
        {
            if (ms_eRole == ROLE_GUEST_ARRIVED)
            {
                ms_nStartAt = CConfigLoader::ms_bVsRaceSync ? packet.startTime : 0;
                ms_eRole = (ms_nStartAt != 0 && !StartReached()) ? ROLE_GUEST_COUNTDOWN : ROLE_GUEST_GO;
            }
            else if (ms_eRole == ROLE_GUEST_ACCEPTED || ms_eRole == ROLE_GUEST_INVITED)
            {
                CChat::AddMessage("{c7a6ff}[Coop]{d6dce8} La carrera VS arranco sin vos.");
                Reset();
            }
        }
        break;

    case Action::CANCEL:
        if (sender == ms_ownerId)
        {
            if (ms_eRole == ROLE_GUEST_ARRIVED)
            {
                CChat::AddMessage("{c7a6ff}[Coop]{d6dce8} " + PlayerName(sender) + " cancelo la carrera VS: no arranca.");
                ms_eRole = ROLE_ABORT;
            }
            else if (ms_eRole == ROLE_GUEST_ACCEPTED || ms_eRole == ROLE_GUEST_INVITED)
            {
                CChat::AddMessage("{c7a6ff}[Coop]{d6dce8} " + PlayerName(sender) + " cancelo la carrera VS.");
                Reset();
            }
        }
        break;

    default:
        break;
    }
}

// The help box is drawn with the HUD, under the screen fade. The stadium races (8-Track, Dirt Track...) ask while the
// screen is still black from the entrance (2026-11-15: black screen, no options): then the ImGui box, drawn on top.
static bool ScreenFaded()
{
    return TheCamera.m_fFadeAlpha > 10.0f;
}

bool CVsLobby::ShouldDraw()
{
    return ms_eRole == ROLE_DECIDING || ms_eRole == ROLE_WAITING || ms_eRole == ROLE_GUEST_INVITED ||
           ms_eRole == ROLE_GUEST_ACCEPTED || ms_eRole == ROLE_GUEST_ARRIVED ||
           ms_eRole == ROLE_OWNER_COUNTDOWN || ms_eRole == ROLE_GUEST_COUNTDOWN;
}

// the offer/wait text as the game's help box: top left, black box, 640x448 virtual screen (CHud::DrawHelpText layout)
void CVsLobby::DrawHelpBox()
{
    // retired 2026-11-15: the players want the old centred window back (CVsLobby::Draw); vs-lobby-helpbox is ignored
    return;

    std::string text;
    switch (ms_eRole)
    {
    case ROLE_DECIDING:
        text = "~p~Carrera~w~~n~1: VS contra jugadores y NPC~n~2: VS solo contra jugadores~n~3: solo contra NPC";
        break;
    case ROLE_WAITING:
        text = "~p~Carrera VS: esperando jugadores~w~";
        for (const auto& guest : ms_vGuests)
        {
            const char* szState = guest.state == GUEST_INVITED    ? "pensando..."
                                  : guest.state == GUEST_ACCEPTED ? "acepto, falta que entre al marcador"
                                  : guest.state == GUEST_ARRIVED  ? "listo"
                                                                  : "no viene";
            text += "~n~" + CMissionHints::ToGameText(PlayerName(guest.id)) + ": " + szState;
        }
        break;
    case ROLE_GUEST_INVITED:
        text = "~p~" + CMissionHints::ToGameText(PlayerName(ms_ownerId)) + " te invita a una carrera VS" +
            (ms_nMode ? " con NPC" : "") + "~w~~n~1: Si~n~2: No";
        break;
    case ROLE_GUEST_ACCEPTED:
        text = "~p~Aceptaste la carrera VS de " + CMissionHints::ToGameText(PlayerName(ms_ownerId)) + "~w~~n~Entra al marcador de la carrera";
        break;
    case ROLE_GUEST_ARRIVED:
        text = "~p~Esperando que " + CMissionHints::ToGameText(PlayerName(ms_ownerId)) + " arranque la carrera...";
        break;
    case ROLE_OWNER_COUNTDOWN:
    case ROLE_GUEST_COUNTDOWN:
        text = "~p~Carrera VS: todos listos~w~~n~Arranca en un instante...";
        break;
    default:
        return;
    }
    const float sx = RsGlobal.maximumWidth / 640.0f, sy = RsGlobal.maximumHeight / 448.0f;
    const float x1 = 20.0f * sx, y1 = 20.0f * sy, w = 200.0f * sx, pad = 8.0f * sx;

    CFont::SetBackground(false, false);
    CFont::SetProportional(true);
    CFont::SetJustify(false);
    CFont::SetOrientation(ALIGN_LEFT);
    CFont::SetWrapx(x1 + w - pad);
    CFont::SetEdge(0);
    CFont::SetFontStyle(FONT_SUBTITLES);
    CFont::SetScale(0.5f * sx, 1.0f * sy);
    CFont::SetColor(CRGBA(225, 225, 225, 255));
    const float lineH = 16.0f * sy;
    const float h = CFont::GetNumberLines(x1 + pad, y1 + 6.0f * sy, const_cast<char*>(text.c_str())) * lineH + 12.0f * sy;
    CSprite2d::DrawRect(CRect(x1, y1, x1 + w, y1 + h), CRGBA(0, 0, 0, 190));
    CFont::PrintString(x1 + pad, y1 + 6.0f * sy, const_cast<char*>(text.c_str()));
    CFont::RenderFontBuffer();
}

void CVsLobby::Draw()
{
    if (!ShouldDraw())
        return;

    const float scale = (std::max)(1.0f, ImGui::GetIO().DisplaySize.y / 720.0f) * 1.1f;
    const ImGuiWindowFlags flags = ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse |
                                   ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings |
                                   ImGuiWindowFlags_NoInputs;

    ImGui::SetNextWindowPos(ImVec2(ImGui::GetIO().DisplaySize.x * 0.5f, 80.0f * scale), ImGuiCond_Always, ImVec2(0.5f, 0.0f));
    ImGui::Begin("Carrera VS", nullptr, flags);
    ImGui::SetWindowFontScale(scale);

    const ImVec4 accent(0.79f, 0.65f, 1.0f, 1.0f);

    switch (ms_eRole)
    {
    case ROLE_DECIDING:
        ImGui::TextColored(accent, "Carrera");
        ImGui::TextUnformatted("1 = VS contra jugadores y NPC");
        ImGui::TextUnformatted("2 = VS solo contra jugadores");
        ImGui::TextUnformatted("3 = solo contra NPC");
        ImGui::TextUnformatted("4 = salir del marcador");
        break;

    case ROLE_WAITING:
    {
        ImGui::TextColored(accent, "Carrera VS: esperando jugadores");
        for (const auto& guest : ms_vGuests)
        {
            const char* szState = guest.state == GUEST_INVITED    ? "pensando..."
                                  : guest.state == GUEST_ACCEPTED ? "acepto, falta que entre al marcador"
                                  : guest.state == GUEST_ARRIVED  ? "listo"
                                                                  : "no viene";
            ImGui::Text("%s: %s", PlayerName(guest.id).c_str(), szState);
        }
        ImGui::TextUnformatted("4 = cancelar y salir del marcador");
        break;
    }

    case ROLE_GUEST_INVITED:
        ImGui::TextColored(accent, "%s te invita a una carrera VS", PlayerName(ms_ownerId).c_str());
        ImGui::TextUnformatted("1 (Si) / 2 (No)");
        break;

    case ROLE_GUEST_ACCEPTED:
        ImGui::TextColored(accent, "Aceptaste la carrera VS de %s", PlayerName(ms_ownerId).c_str());
        ImGui::TextUnformatted("Entra al marcador de la carrera");
        break;

    case ROLE_GUEST_ARRIVED:
        ImGui::TextColored(accent, "Esperando que %s arranque la carrera...", PlayerName(ms_ownerId).c_str());
        ImGui::TextUnformatted("4 = salir del marcador");
        break;

    case ROLE_OWNER_COUNTDOWN:
    case ROLE_GUEST_COUNTDOWN:
        ImGui::TextColored(accent, "Carrera VS: todos listos");
        ImGui::TextUnformatted("Arranca en un instante...");
        break;

    default:
        break;
    }

    ImGui::End();
}
