// Adapted from sa-coop (Karimov-Akbar, MIT): features/story/contest/Contest.cpp.
// Score reads and native entry conditions; rewards and launch variants remain local.
#include "stdafx.h"
#include "CActivityChallenge.h"
#include "CConfigLoader.h"
#include "CMissionMode.h"
#include "CCoopMission.h"
#include "COpCodeSync.h"
#include "CTestMode.h"
#include "network/packets/scripts.h"
#include "imgui.h"
#include <algorithm>
namespace {
using P = Packets::Scripts::ActivityChallenge;
struct Run { uint8_t kind = 0; int score = 0, money = 0; uint32_t began = 0, clock = 0; bool success = false; } run;
struct Match {
    P packet{};
    uint8_t phase = 0, local = 0, remote = 0;
    int mine = 0, theirs = 0;
    uint32_t began = 0, heard = 0, sent = 0, result = 0;
    std::string name, text;
} g;
uint32_t serial = 0;
bool keys[3]{};
bool launchPending = false, triFinish = false, basketFinish = false;
const char* Title(int k) {
    const char* names[] = {"", "Basquet", "Blackjack", "Ruleta", "Rueda de la fortuna", "Kickstart", "Blood Ring", "Triatlon"};
    return k >= 1 && k <= 7 ? names[k] : "Actividad";
}
int Global(int off) { return *reinterpret_cast<int*>(CTheScripts::ScriptSpace + off); }
bool Named(CRunningScript* s, const char* name) { return s && !_strnicmp(s->m_szName, name, 8); }
uint8_t Playing() {
    for (auto s = CTheScripts::pActiveScripts; s; s = s->m_pNext) {
        if (Named(s, "bbchal")) return 1;
        if (Named(s, "blackj") && Global(36868)) return 2;
        if (Named(s, "roulete") && Global(13956)) return 3;
        if (Named(s, "wof") && Global(37760)) return 4;
        if (Named(s, "blood") || Named(s, "blosolo")) return 6;
        if (Named(s, "tria") || Named(s, "trisolo")) return 7;
        if ((Named(s, "kicksta") || Named(s, "kicsolo")) && s->m_bIsMission) return 5;
    }
    return 0;
}
playerid_t Rival() {
    return g.packet.owner == CNetworkPlayerManager::m_localPlayerId ? g.packet.partner : g.packet.owner;
}
bool Ready() {
    auto ped = FindPlayerPed(0);
    return CConfigLoader::ms_bActivityChallenge && CConfigLoader::ms_bVanillaScripts &&
        CNetwork::m_bConnected && CNetwork::m_bAuthenticated && ped && ped->m_fHealth > 0 &&
        CWorld::Players[0].m_nPlayerState != PLAYERSTATE_HASDIED &&
        CWorld::Players[0].m_nPlayerState != PLAYERSTATE_HASBEENARRESTED &&
        !CMissionMode::IsIndividual() && !CCoopMission::IsActive();
}
bool Near(playerid_t id, bool idle) {
    auto r = CNetworkPlayerManager::GetPlayer(id); auto me = FindPlayerPed(0);
    return r && me && r->m_pPed && !r->m_bDead && !r->m_bIndividualMode && !r->m_bOtherScripts &&
        (!idle || r->m_nRunningMission < 0) && r->m_pPed->m_nAreaCode == me->m_nAreaCode &&
        (r->m_pPed->GetPosition() - me->GetPosition()).Magnitude() <= 12.0f;
}
bool MissionBusy() {
    for (auto s = CTheScripts::pActiveScripts; s; s = s->m_pNext)
        if (s->m_bIsMission && !Named(s, "kicksta") && !Named(s, "kicsolo") && !Named(s, "blood") && !Named(s, "blosolo") &&
            !Named(s, "tria") && !Named(s, "trisolo")) return true;
    return false;
}
void Send(uint8_t action) {
    auto p = g.packet; p.action = action; p.state = g.local; p.score = g.mine;
    GetPacketFactory().Send(p); g.sent = GetTickCount();
}
void Result() {
    if (g.result) return;
    const char* winner = g.local == 3 ? g.name.c_str() : g.remote == 3 ? "vos" :
        g.mine == g.theirs ? "empate" : g.mine > g.theirs ? "vos" : g.name.c_str();
    char text[240];
    snprintf(text, sizeof(text), "%s: vos %d | %.24s %d. %s%s", Title(g.packet.kind),
        g.packet.kind >= 6 ? -g.mine : g.mine, g.name.c_str(), g.packet.kind >= 6 ? -g.theirs : g.theirs, (g.local == 3 || g.remote == 3) ? "Abandono: " : "Resultado: ", winner);
    g.text = text; g.result = GetTickCount();
    CChat::AddMessage("{c7a6ff}[Desafio]{d6dce8} %s", text);
    CHud::SetHelpMessage(text, true, false, false);
}
}
void CActivityChallenge::OnPacket(const P& p) {
    const auto now = GetTickCount();
    if (!Ready()) return;
    auto me = CNetworkPlayerManager::m_localPlayerId;
    if (p.owner != me && p.partner != me) return;
    if (p.action == P::OFFER) {
        if (g.phase || MissionBusy() || run.kind || !Near(p.owner, false)) return;
        g = {}; g.packet = p; g.phase = 2; g.began = g.heard = now;
        if (auto r = CNetworkPlayerManager::GetPlayer(p.owner)) g.name = r->GetName();
        char text[160]; snprintf(text, sizeof(text), "%.24s te desafia a %s: Y/N", g.name.c_str(), Title(p.kind));
        g.text = text; CHud::SetHelpMessage(text, true, false, false); return;
    }
    if (!g.phase || p.session != g.packet.session || p.owner != g.packet.owner || p.partner != g.packet.partner ||
        p.kind != g.packet.kind || p.variant != g.packet.variant ||
        (p.sender != Rival() && !(p.action == P::ACCEPT && g.phase == 4 && p.sender == me))) return;
    g.heard = now;
    if (p.action == P::DECLINE) { g = {}; return; }
    if (p.action == P::ACCEPT && (g.phase == 1 || g.phase == 4)) {
        launchPending = g.phase == 4;
        g.phase = 3; g.began = now; g.local = run.kind == p.kind ? 1 : 0; g.mine = run.score; Send(P::STATE);
    }
    if (g.phase != 3 || g.result) return;
    if (p.action == P::LEAVE) { g.remote = 3; Result(); }
    if (p.action == P::STATE && g.remote < 2) {
        g.remote = p.state; g.theirs = p.score;
        if (g.remote == 3 || (g.local == 2 && g.remote == 2)) Result();
    }
}
void CActivityChallenge::Process() {
    const auto now = GetTickCount();
    bool pressed[3]{}; const int codes[] = {'U','Y','N'};
    for (int i = 0; i < 3; ++i) {
        bool down = CTestMode::IsGameWindowFocused() && !CChat::m_bInputActive && (GetAsyncKeyState(codes[i]) & 0x8000);
        pressed[i] = down && !keys[i]; keys[i] = down;
    }
    if (!Ready()) {
        if (g.phase == 3 && !g.result && CNetwork::m_bAuthenticated) { g.local = 3; Send(P::LEAVE); }
        g = {}; run = {}; launchPending = false; return;
    }
    if (launchPending) {
        launchPending = false;
        if (MissionBusy() || run.kind || !Near(Rival(), false)) { g.local = 3; Send(P::LEAVE); g = {}; return; }
        if (g.packet.kind >= 5) {
            if (g.packet.kind == 7) *reinterpret_cast<int*>(CTheScripts::ScriptSpace + 7248) = g.packet.variant;
            Command<Commands::LOAD_AND_LAUNCH_MISSION_INTERNAL>(g.packet.kind == 5 ? 129 : g.packet.kind == 6 ? 128 : 130);
        }
        g.text = "Desafio aceptado: toma la pelota o acercate al puesto de la mesa. N: abandonar";
        CHud::SetHelpMessage(g.text.c_str(), true, false, false);
        if (g.packet.kind >= 5) return; // let the new native thread execute SCRIPT_NAME first
    }
    auto kind = Playing();
    if (run.kind && kind != run.kind) {
        if (run.kind >= 2 && run.kind <= 4) run.score = int((std::clamp)(
            int64_t(CWorld::Players[0].m_nMoney) - run.money, int64_t(-100000000), int64_t(100000000)));
        if (g.phase == 3 && g.local == 1) { g.mine = run.score;
            g.local = (run.kind == 1 || run.kind >= 6) && !run.success ? 3 : 2; Send(P::STATE);
            if (g.local == 3) Result(); }
        run = {};
    }
    if (kind && !run.kind) {
        run.kind = kind; run.began = now; run.money = CWorld::Players[0].m_nMoney; run.clock = CTimer::m_snTimeInMilliseconds;
        if (!g.phase) CHud::SetHelpMessage("U: desafiar", true, false, false);
    }
    if (run.kind) {
        if (kind <= 5) run.score = kind == 1 ? Global(7696) : kind == 5 ? Global(33048) :
            int((std::clamp)(int64_t(CWorld::Players[0].m_nMoney) - run.money, int64_t(-100000000), int64_t(100000000)));
        if (kind == 6 && !run.success && CTheScripts::LocalVariablesForCurrentMission[91].iParam == 1) {
            run.success = true; run.score = -int(CTimer::m_snTimeInMilliseconds - run.clock);
        }
        if (g.phase == 3 && g.local == 0 && kind == g.packet.kind) g.local = 1;
        if (g.phase == 3 && g.local == 1) g.mine = run.score;
    }
    if (g.result) { if (now - g.result > 10000) g = {}; return; }
    if (g.phase == 1 || g.phase == 2 || g.phase == 4) {
        if (now - g.began > 20000 || !Near(Rival(), false) || (g.phase == 1 && run.kind != g.packet.kind)) { Send(P::DECLINE); g = {}; return; }
        if ((g.phase == 2 || g.phase == 4) && pressed[2]) { Send(P::DECLINE); g = {}; return; }
        if (g.phase == 2 && pressed[1] && !MissionBusy() && !run.kind && Near(Rival(), false)) {
            g.phase = 4; Send(P::ACCEPT); g.text = "Esperando confirmacion del desafio";
        }
    }
    if (g.phase == 3) {
        auto r = CNetworkPlayerManager::GetPlayer(Rival());
        if (!r || r->m_bDead || r->m_bIndividualMode || now - g.heard > 8000) {
            g.remote = 3; Result(); return;
        }
        if (pressed[2] || MissionBusy() || (g.local == 0 && now - g.began > 180000) ||
            (g.local == 1 && kind != g.packet.kind)) { g.local = 3; Send(P::LEAVE); Result(); return; }
        if (now - g.sent >= 1000) Send(P::STATE);
        if (g.local == 2 && g.remote == 2) Result();
        return;
    }
    if (!g.phase && run.kind && !MissionBusy() && now - run.began <= 20000 && pressed[0]) {
        for (auto r : CNetworkPlayerManager::m_pPlayers) if (Near(r->m_playerId, true)) {
            g = {}; g.packet.owner = CNetworkPlayerManager::m_localPlayerId; g.packet.partner = r->m_playerId;
            if (++serial == 0) ++serial;
            g.packet.session = serial; g.packet.kind = run.kind;
            g.packet.variant = run.kind == 7 ? Global(7248) : 0; g.phase = 1;
            g.began = g.heard = now; g.name = r->GetName(); Send(P::OFFER);
            g.text = "Esperando respuesta de " + g.name; break;
        }
    }
}
bool CActivityChallenge::FilterCompare(CRunningScript* s, unsigned short op, bool state) {
    if (!Ready() || COpCodeSync::bProcessingNetworkOpcode || !s || s->m_bIsMission) return state;
    if (g.phase == 3 && !g.result && g.local == 0 && op == 0x00E1 && ScriptParams[0] == 0 &&
        ScriptParams[1] == 15 && ((g.packet.kind == 2 && Named(s, "blackj")) ||
        (g.packet.kind == 3 && Named(s, "roulete")) || (g.packet.kind == 4 && Named(s, "wof")))) return true;
    if (g.phase == 3 && !g.result && g.packet.kind == 1 && Named(s, "bball")) {
        if (op == 0x00C2) return false;
        if (op == 0x00E1 && g.local == 0 && ScriptParams[0] == 0 && ScriptParams[1] == 19) return true;
    }
    return state;
}
void CActivityChallenge::Reset() {
    if (g.phase && CNetwork::m_bAuthenticated) { g.local = 3; Send(P::LEAVE); }
    g = {}; run = {}; launchPending = false;
}
bool CActivityChallenge::Independent(CRunningScript* s) {
    return CConfigLoader::ms_bActivityChallenge && CConfigLoader::ms_bVanillaScripts &&
        s && (Named(s, "kicksta") || Named(s, "kicsolo") || Named(s, "blood") ||
            Named(s, "blosolo") || Named(s, "tria") || Named(s, "trisolo"));
}
void CActivityChallenge::BeforeCommand(CRunningScript* s, unsigned short op) {
    triFinish = basketFinish = false;
    if (Ready() && s && !COpCodeSync::bProcessingNetworkOpcode && run.kind == 1 && Named(s, "bbchal") && op == 0x00BC) {
        auto ip = s->m_pCurrentIP;
        basketFinish = ip && ip[0] == 9 && !memcmp(ip + 1, "BB_17", 6);
    }
    if (!Ready() || !s || COpCodeSync::bProcessingNetworkOpcode || run.kind != 7 ||
        !(Named(s, "tria") || Named(s, "trisolo")) || op != 0x00BC) return;
    auto ip = s->m_pCurrentIP;
    triFinish = ip && ip[0] == 9 && !memcmp(ip + 1, "RACES_8", 8);
}
void CActivityChallenge::AfterCommand(CRunningScript* s, unsigned short op) {
    if (!Ready() || !s || COpCodeSync::bProcessingNetworkOpcode) return;
    if (basketFinish && Named(s, "bbchal") && !run.success) {
        run.success = true; run.score = Global(7696);
        if (g.phase == 3 && g.local == 1) { g.local = 2; g.mine = run.score; Send(P::STATE); }
    }
    if (!s->m_bIsMission) return;
    if (run.kind == 6 && op == 0x014E) run.clock = CTimer::m_snTimeInMilliseconds;
    if (run.kind == 7 && (Named(s, "tria") || Named(s, "trisolo")) && (op == 0x0394 || triFinish) && !run.success) {
        run.success = true;
        run.score = -int(CTimer::m_snTimeInMilliseconds - uint32_t(CTheScripts::LocalVariablesForCurrentMission[767].iParam));
    }
}
bool CActivityChallenge::Visible() { return CConfigLoader::ms_bActivityChallenge && g.phase != 0; }
void CActivityChallenge::Draw() {
    if (!Visible()) return;
    // Same panel placement and scaling as CMinigameVs / CVsRace.
    float scale = (std::max)(1.0f, ImGui::GetIO().DisplaySize.y / 720.0f) * 1.1f;
    ImGui::SetNextWindowPos(ImVec2(ImGui::GetIO().DisplaySize.x - 10 * scale, 50 * scale), ImGuiCond_Always, ImVec2(1,0));
    ImGui::Begin("Desafio de a dos", nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoInputs |
        ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoCollapse);
    ImGui::SetWindowFontScale(scale);
    ImGui::TextUnformatted(Title(g.packet.kind));
    if (g.phase == 3 && !g.result) {
        ImGui::Text("Vos: %d%s", g.packet.kind >= 6 ? -g.mine : g.mine, g.local == 2 ? " (termino)" : g.local == 0 ? " (esperando inicio)" : "");
        ImGui::Text("%s: %d%s", g.name.c_str(), g.packet.kind >= 6 ? -g.theirs : g.theirs, g.remote == 2 ? " (termino)" : "");
    } else ImGui::TextWrapped("%s", g.text.c_str());
    ImGui::End();
}
