// Adapted from Karimov-Akbar/sa-coop (MIT), features/teleport and anims/Emotes.cpp, Anims.cpp.
#include "stdafx.h"
#include "CSocialActions.h"
#include "CEmoteIfp.h"
#include "CConfigLoader.h"
#include "CMissionMode.h"
#include "CCoopMission.h"
#include "CTestMode.h"
#include "SceneCompat/Anim.h"
#include "SceneCompat/Script.h"
#include "network/packets/scripts.h"
#include "imgui.h"
#include "UI/CModeMenu.h"
#include "UI/CCheatMenu.h"
#include "Debug/CImGui.h"
#include <filesystem>
#include <fstream>
#include <map>
#include <algorithm>
namespace {
using P = Packets::Scripts::SocialAction;
using A = Packets::Scripts::EmoteState;
using scene_game::script::Cmd;
namespace anim = scene_game::anim;
struct Emote { std::string name, block, clip, other; float apart = 0; bool loop = false; };
std::vector<Emote> list;
struct Consent { P p{}; uint32_t began = 0; bool incoming = false, active = false; } consent;
struct Playing { Emote e; uint32_t began = 0, session = 0, sequence = 0, sent = 0; bool started = false;
    float health = 0; CVector pos{}; } playing;
struct Remote { A p; uint32_t heard = 0; };
std::map<uint32_t, Remote> remotes;
bool opened = false, loaded = false, keys[10]{};
int selected = 0;
playerid_t selectedPartner{};
uint32_t serial = 0;
uint32_t Serial() { if (++serial == 0) ++serial; return serial; }
void Message(const char* s) { CChat::AddMessage("{c7a6ff}[Coop]{d6dce8} %s", s); }
bool Connected() { return CNetwork::m_bConnected && CNetwork::m_bAuthenticated; }
bool Ready() {
    auto p = FindPlayerPed(0);
    return Connected() && p && p->m_fHealth > 0 && !p->m_nPedFlags.bInVehicle &&
        !CMissionMode::IsIndividual() && !CCoopMission::IsActive() &&
        !CModeMenu::ms_bOpen && !CCheatMenu::ms_bOpen && !CImGui::ms_bActive &&
        CWorld::Players[0].m_nPlayerState == PLAYERSTATE_PLAYING && !CPad::GetPad(0)->DisablePlayerControls &&
        !FrontEndMenuManager.m_bMenuActive && !CCutsceneMgr::ms_running && !CCutsceneMgr::ms_cutsceneLoadStatus &&
        !*reinterpret_cast<int*>(CTheScripts::ScriptSpace + 1636);
}
bool Near(playerid_t id, bool pair) {
    auto r = CNetworkPlayerManager::GetPlayer(id); auto me = FindPlayerPed(0);
    return r && me && r->m_pPed && !r->m_bDead && !r->m_bIndividualMode && r->m_nRunningMission < 0 &&
        !r->m_pPed->m_nPedFlags.bInVehicle && r->m_pPed->m_nAreaCode == me->m_nAreaCode &&
        (!pair || (r->m_pPed->GetPosition() - me->GetPosition()).Magnitude() <= 4.0f);
}
void SendConsent(uint8_t action) { auto p = consent.p; p.action = action; GetPacketFactory().Send(p); }
bool ValidPed(CPed* ped) {
    return ped && CPools::ms_pPedPool && CPools::ms_pPedPool->IsObjectValid(ped) && ped->IsVTableValid();
}
void Fade(CPed* ped, const A& p) {
    if (!ValidPed(ped) || !ped->m_pRwClump) return;
    for (auto a = anim::First(ped->m_pRwClump); a; a = anim::Next(a)) {
        int b, i;
        if (anim::Locate(anim::Hierarchy(a), b, i) && anim::NameKey(anim::Hierarchy(a)) == p.hash &&
            !_stricmp((const char*)anim::Block(b), p.block)) anim::FadeOut(a);
    }
}
void Stop(bool cancel = true) {
    if (playing.session) {
        A a{}; a.session = playing.session; a.sequence = ++playing.sequence;
        strcpy_s(a.block, playing.e.block.c_str());
        a.hash = CKeyGen::GetUppercaseKey(playing.e.clip.c_str());
        if (Connected()) GetPacketFactory().Send(a);
        Fade(FindPlayerPed(0), a); playing = {};
    }
    if (cancel && consent.p.session) { if (Connected()) SendConsent(P::CANCEL); consent = {}; }
}
void Start(const Emote& e, uint32_t session) {
    Stop(false); auto ped = FindPlayerPed(0); if (!ped) return;
    playing.e = e; playing.session = session; playing.began = GetTickCount();
    playing.health = ped->m_fHealth; playing.pos = ped->GetPosition(); opened = false;
}
void InitList() {
    if (!list.empty()) return;
    list = {
        {"Saludar", "ON_LOOKERS", "wave_loop", "", 0, true},
        {"Bailar", "DANCING", "DAN_Down_A", "", 0, true},
        {"Reir", "RAPPING", "Laugh_01", ""},
        {"Brazos cruzados", "COP_AMBIENT", "Coplook_loop", "", 0, true},
        {"Apreton de manos", "GANGS", "hndshkfa", "hndshkfa", 1},
        {"Saludo de banda 1", "GANGS", "hndshkaa", "hndshkaa", 1},
        {"Saludo de banda 2", "GANGS", "hndshkba", "hndshkba", 1},
        {"Saludo de banda 3", "GANGS", "hndshkca", "hndshkcb", 1},
        {"Saludo de banda 4", "GANGS", "hndshkda", "hndshkda", 1},
        {"Saludo de banda 5", "GANGS", "hndshkea", "hndshkea", 1},
        {"Dar la mano", "GANGS", "prtial_hndshk_01", "prtial_hndshk_01", 1},
        {"Saludo de negocios", "GANGS", "prtial_hndshk_biz_01", "prtial_hndshk_biz_01", .92f},
        {"Mano en el hombro", "SWEET", "plyr_hndshldr_01", "sweet_hndshldr_01", .9f},
        {"Trato", "GANGS", "DEALER_DEAL", "DRUGS_BUY", 1},
        {"Regalo", "KISSING", "gift_give", "gift_get", 1},
        {"Discutir 1", "PAULNMAC", "PnM_Argue1_A", "PnM_Argue1_B", 1.2f},
        {"Discutir 2", "PAULNMAC", "PnM_Argue2_A", "PnM_Argue2_B", 1.2f},
        {"Beso 1", "KISSING", "Playa_Kiss_03", "Grlfrd_Kiss_03", 1},
        {"Beso 2", "KISSING", "Playa_Kiss_01", "Grlfrd_Kiss_01", 1},
        {"Beso 3", "KISSING", "Playa_Kiss_02", "Grlfrd_Kiss_02", 1}
    };
}
// Validated files only; permanent blocks are appended before any temporary cutscene block.
// Normal allocation keeps ms_numAnimations truthful (gta-reversed AnimManager::LoadAnimFile_ANP23).
void LoadCustom() {
    if (loaded || CCutsceneMgr::ms_running || CCutsceneMgr::ms_cutsceneLoadStatus || scene_game::Ref<uint8_t>(0xB5F850)) return;
    loaded = true;
    char exe[MAX_PATH]{}; GetModuleFileNameA(nullptr, exe, MAX_PATH);
    auto folder = std::filesystem::path(exe).parent_path() / "coop" / "anims";
    std::error_code ec;
    size_t total = 0; int count = 0, clips = 0;
    std::vector<std::filesystem::path> files;
    for (std::filesystem::directory_iterator it(folder, ec), end; !ec && it != end; it.increment(ec))
        if (it->is_regular_file(ec) && !_stricmp(it->path().extension().string().c_str(), ".ifp")) files.push_back(it->path());
    std::sort(files.begin(), files.end());
    for (auto& path : files) {
        auto n = std::filesystem::file_size(path, ec);
        if (ec || n > (4u << 20) || n < 36 || total + n > (16u << 20) || ++count > 32) continue;
        total += size_t(n);
        std::ifstream f(path, std::ios::binary); std::vector<uint8_t> bytes(size_t(n), 0);
        if (!f.read((char*)bytes.data(), std::streamsize(n))) continue;
        coop::anims::IfpInfo info;
        if (!coop::anims::CheckIfp(bytes.data(), bytes.size(), info)) { Message("IFP invalido: se omitio un archivo de coop/anims."); continue; }
        // The engine reads fixed strings as C strings. Require terminators and portable names.
        bool valid = info.block.size() < 24;
        for (auto& a : info.anims) {
            if (a.empty() || a.size() >= 24) valid = false;
            for (auto c : a) if (!isalnum((unsigned char)c) && c != '_') valid = false;
        }
        if (!valid || clips + info.anims.size() > 64 || scene_game::Ref<int32_t>(0xB4EA30) + 3 >= 180 ||
            scene_game::Ref<int32_t>(0xB4EA2C) + info.anims.size() >= 2100) continue;
        uint32_t hash = 2166136261u; for (auto b : bytes) hash = (hash ^ b) * 16777619u;
        char block[16]; snprintf(block, sizeof(block), "C_%08X", hash);
        int index = scene_game::Call<0x4D3990, int>(block);
        if (index < 0) {
            memset(bytes.data() + 8, 0, 24); memcpy(bytes.data() + 8, block, strlen(block));
            struct Memory { uint8_t* data; uint32_t size; } mem{bytes.data(), uint32_t(bytes.size())};
            static const char none[1][32] = {""};
            if (auto stream = scene_game::Call<0x7ECEF0, void*>(3, 1, &mem)) {
                scene_game::Call<0x4D47F0>(stream, false, none);
                scene_game::Call<0x7ECE20, int>(stream, &mem);
            }
            index = scene_game::Call<0x4D3990, int>(block);
        }
        if (index < 0 || !anim::BlockLoaded(index)) continue;
        // Permanent file block: never submit this index to the IMG streaming loader.
        scene_game::Call<0x4D3FB0>(index); // AddAnimBlockRef
        clips += int(info.anims.size());
        for (auto& a : info.anims) list.push_back({path.stem().string() + ": " + a, block, a, ""});
    }
}
void Ask(playerid_t partner, uint8_t kind, int emote = 0) {
    if (consent.p.session || playing.session) { Message("Ya tenes un pedido o una animacion en curso. N: cancelar."); return; }
    if (!Ready() || !Near(partner, kind == P::PAIR)) {
        Message("No disponible: ambos deben estar vivos, en coop, a pie, en la misma area y sin mision; animaciones a menos de 4 m."); return;
    }
    consent = {}; consent.p.owner = CNetworkPlayerManager::m_localPlayerId; consent.p.partner = partner;
    consent.p.session = Serial(); consent.p.kind = kind; consent.p.emote = uint8_t(emote);
    consent.began = GetTickCount();
    if (kind == P::PAIR) Cmd(0x04ED).S(list[4 + emote].block.c_str()).Run();
    SendConsent(P::OFFER);
    opened = false; Message("Pedido enviado (15 s). N: cancelar.");
}
void Move(const CVector& pos, float heading, uint8_t area) {
    auto ped = FindPlayerPed(0); if (!ped) return;
    int ref = CPools::GetPedRef(ped);
    if (CGame::currArea != area) Cmd(0x04BB).I(area).Run();
    Cmd(0x0860).I(ref).I(area).Run();
    Cmd(0x03CB).F(pos.x).F(pos.y).F(pos.z).Run();
    Cmd(0x00A1).I(ref).F(pos.x).F(pos.y).F(pos.z - 1.0f).Run();
    Cmd(0x0173).I(ref).F(heading * 180.0f / 3.14159265f).Run();
}
}

bool CSocialActions::Command(const std::wstring& name, const std::wstring& args) {
    if (name == L"anims") {
        if (!CConfigLoader::ms_bEmotes) { Message("Animaciones desactivadas (emotes=false)."); return true; }
        if (!Ready()) { Message("Animaciones: sali de la mision, vehiculo o menu; modo coop requerido."); return true; }
        InitList(); opened = !opened; return true;
    }
    if (name != L"tp" && name != L"ir" && name != L"traer") return false;
    if (CTestMode::ms_bEnabled) return false; // retain the old coordinate / nearest-player test commands
    char target[128]{}; WideCharToMultiByte(CP_ACP, 0, args.c_str(), -1, target, sizeof(target), nullptr, nullptr);
    std::string query = target; while (!query.empty() && query.back() == ' ') query.pop_back();
    CNetworkPlayer* found = nullptr;
    for (auto r : CNetworkPlayerManager::m_pPlayers) if (!_stricmp(query.c_str(), r->GetName().c_str())) {
        if (found) { Message("Nombre repetido: elegi nombres distintos para dirigir el pedido."); return true; } found = r;
    }
    if (!found) { Message("Uso: /tp <nombre exacto>, /ir <nombre exacto> o /traer <nombre exacto>."); return true; }
    Ask(found->m_playerId, name == L"traer" ? P::BRING : P::GO); return true;
}
void CSocialActions::OnPacket(const P& p) {
    auto me = CNetworkPlayerManager::m_localPlayerId;
    if (!Connected() || !std::isfinite(p.pos.x) || !std::isfinite(p.pos.y) ||
        !std::isfinite(p.pos.z) || !std::isfinite(p.heading) || (p.owner != me && p.partner != me)) return;
    if (p.action == P::OFFER) {
        if (p.partner != me || consent.p.session || playing.session || !Ready() || !Near(p.owner, p.kind == P::PAIR) ||
            (p.kind == P::PAIR && !CConfigLoader::ms_bEmotes)) {
            auto out = p; out.action = P::CANCEL; GetPacketFactory().Send(out); return;
        }
        consent = {}; consent.p = p; consent.incoming = true; consent.began = GetTickCount();
        auto r = CNetworkPlayerManager::GetPlayer(p.owner);
        CChat::AddMessage("{c7a6ff}[Coop]{d6dce8} %s %s: Y/N (15 s)", r ? r->GetName().c_str() : "Jugador",
            p.kind == P::GO ? "quiere ir hacia vos" : p.kind == P::BRING ? "quiere traerte" : "te invita a una animacion de a dos");
        if (p.kind == P::PAIR) { InitList(); Cmd(0x04ED).S(list[4 + p.emote].block.c_str()).Run(); }
        opened = false; return;
    }
    if (!consent.p.session || p.session != consent.p.session || p.owner != consent.p.owner || p.partner != consent.p.partner ||
        p.kind != consent.p.kind || p.emote != consent.p.emote) return;
    if (p.action == P::CANCEL) {
        Stop(false); consent = {};
        Message(p.reason == 2 ? "Pedidos TP desactivados en el servidor (tp-request=false)." :
            p.reason == 3 ? "Uno de los dos ya tiene un pedido en curso." :
            p.reason == 5 ? "El servidor te ve ocupado (mision, vehiculo, controles bloqueados o sin posicion reciente)." :
            p.reason == 6 ? "El otro jugador esta ocupado (mision, vehiculo, controles bloqueados o sin posicion reciente)." :
            p.reason == 7 ? "Estan lejos (animaciones de a dos: menos de 4 m) o en distinta area." : "Pedido terminado: rechazo, vencimiento o jugador no disponible/mision en curso."); return;
    }
    if (p.action != P::START || consent.active || !Ready() || !Near(p.owner == me ? p.partner : p.owner, p.kind == P::PAIR)) return;
    if (p.kind == P::PAIR) {
        if (!CConfigLoader::ms_bEmotes) return;
        InitList(); auto e = list[4 + p.emote];
        if (p.partner == me) {
            e.clip = e.other;
            CVector pos = p.pos; pos.x -= sinf(p.heading) * e.apart; pos.y += cosf(p.heading) * e.apart;
            Move(pos, p.heading + 3.14159265f, p.area);
        } else Move(p.pos, p.heading, p.area);
        consent.active = true; consent.began = GetTickCount(); Start(e, p.session);
    } else {
        auto mover = p.kind == P::GO ? p.owner : p.partner;
        if (mover == me) { CVector pos = p.pos; pos.x += 2.0f; Move(pos, p.heading, p.area); }
        consent = {}; Message("Teletransporte aceptado.");
    }
}
void CSocialActions::OnAnimation(const A& p) {
    if (!CConfigLoader::ms_bEmotes || !Connected() || CMissionMode::IsIndividual() ||
        p.player == CNetworkPlayerManager::m_localPlayerId || !std::isfinite(p.time) || p.time < 0 || p.time > 3600) return;
    auto r = CNetworkPlayerManager::GetPlayer(p.player); if (!r || r->m_bIndividualMode) return;
    auto it = remotes.find(p.player.value());
    if (it != remotes.end() && it->second.p.session == p.session && p.sequence <= it->second.p.sequence) return;
    if (it != remotes.end() && (it->second.p.session != p.session || !p.active)) Fade(r->m_pPed, it->second.p);
    remotes[p.player.value()] = {p, GetTickCount()};
    if (!p.active && consent.active && consent.p.session == p.session &&
        (consent.p.owner == p.player || consent.p.partner == p.player)) Stop();
}
void CSocialActions::Reset() {
    Stop(); opened = false;
    for (auto& [id, s] : remotes) if (auto r = CNetworkPlayerManager::GetPlayer(playerid_t(id))) Fade(r->m_pPed, s.p);
    remotes.clear();
}
bool CSocialActions::Visible() { return opened || consent.p.session != 0; }
void CSocialActions::Draw() {
    if (!Visible()) return;
    ImGui::SetNextWindowPos(ImVec2(30, 90), ImGuiCond_FirstUseEver);
    ImGui::Begin("Animaciones y pedidos", nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoSavedSettings);
    if (consent.p.session) {
        auto r = CNetworkPlayerManager::GetPlayer(consent.incoming ? consent.p.owner : consent.p.partner);
        ImGui::Text("%s: %s", consent.p.kind == P::PAIR ? "Animacion de a dos" : "Teletransporte", r ? r->GetName().c_str() : "Jugador");
        if (consent.p.kind == P::PAIR) { InitList(); ImGui::TextUnformatted(list[4 + consent.p.emote].name.c_str()); }
        ImGui::TextUnformatted(consent.active ? "N: terminar" : consent.incoming ? "Y: aceptar / N: rechazar (15 s)" : "Esperando respuesta (15 s). N: cancelar");
    } else {
        InitList();
        ImGui::TextUnformatted("F9: cerrar. Flechas: elegir. Enter: reproducir. N: parar.");
        auto r = CNetworkPlayerManager::GetPlayer(selectedPartner);
        ImGui::Text("Izq/Der: companero: %s", r ? r->GetName().c_str() : "ninguno cercano");
        int begin = (std::max)(0, selected - 6), end = (std::min)(int(list.size()), begin + 13);
        for (int i = begin; i < end; ++i) ImGui::Text("%s%s%s", i == selected ? "> " : "  ", list[i].name.c_str(), list[i].apart ? " (de a dos)" : "");
        ImGui::TextUnformatted("Propias: coop/anims/*.ifp (ANP2/ANP3, mismos archivos en todos).");
    }
    ImGui::End();
}
void CSocialActions::Process() {
    const auto now = GetTickCount();
    const bool wasOpen = opened;
    const int codes[] = {VK_F9, 'Y', 'N', VK_UP, VK_DOWN, VK_RETURN, VK_LEFT, VK_RIGHT, VK_SPACE, 'F'};
    bool press[10]{};
    for (int i = 0; i < 10; ++i) {
        bool down = CTestMode::IsGameWindowFocused() && !CChat::m_bInputActive && !FrontEndMenuManager.m_bMenuActive && (GetAsyncKeyState(codes[i]) & 0x8000);
        press[i] = down && !keys[i]; keys[i] = down;
    }
    if (!Connected()) { Stop(); opened = false; remotes.clear(); return; }
    if (CConfigLoader::ms_bEmotes && FindPlayerPed(0)) { InitList(); LoadCustom(); }
    if (press[0]) Command(L"anims", L"");
    if (!Ready()) { if (consent.p.session || playing.session) Stop(); opened = false; }
    if (consent.p.session) {
        auto rival = consent.p.owner == CNetworkPlayerManager::m_localPlayerId ? consent.p.partner : consent.p.owner;
        if ((!consent.active && now - consent.began >= 15000) || !Near(rival, consent.p.kind == P::PAIR) || press[2]) {
            Stop(); Message("Pedido o animacion cancelado.");
        } else if (consent.incoming && !consent.active && press[1]) {
            if (consent.p.kind == P::PAIR && !Cmd(0x04EE).S(list[4 + consent.p.emote].block.c_str()).Run()) Message("Cargando animacion: volve a pulsar Y.");
            else { SendConsent(P::ACCEPT); consent.incoming = false; }
        }
    }
    if (!CConfigLoader::ms_bEmotes) { Stop(); opened = false; }
    if (opened) {
        std::vector<playerid_t> targets;
        for (auto r : CNetworkPlayerManager::m_pPlayers) if (Near(r->m_playerId, true)) targets.push_back(r->m_playerId);
        std::sort(targets.begin(), targets.end(), [](auto a, auto b) { return a.value() < b.value(); });
        auto it = std::find(targets.begin(), targets.end(), selectedPartner);
        int at = it == targets.end() ? 0 : int(it - targets.begin());
        if (!targets.empty()) { if (press[6]) at = (at + int(targets.size()) - 1) % targets.size(); if (press[7]) at = (at + 1) % targets.size(); selectedPartner = targets[at]; }
        else selectedPartner = {};
        if (press[3]) selected = (selected + int(list.size()) - 1) % list.size();
        if (press[4]) selected = (selected + 1) % list.size();
        if (press[5]) {
            auto e = list[selected];
            if (e.apart) Ask(selectedPartner, P::PAIR, selected - 4);
            else if (Ready()) Start(e, Serial());
        }
    }
    if (opened || wasOpen) {
        // Consume menu navigation without changing script-owned DisablePlayerControls flags.
        CPad::GetPad(0)->NewState = {}; CPad::GetPad(0)->OldState = {};
    }
    if (playing.session) {
        auto ped = FindPlayerPed(0);
        bool moving = press[2] || press[8] || press[9] || abs(CPad::GetPad(0)->NewState.LeftStickX) > 20 || abs(CPad::GetPad(0)->NewState.LeftStickY) > 20;
        if (!ped || !Ready() || moving || ped->m_fHealth < playing.health || (ped->GetPosition() - playing.pos).Magnitude() > 1.8f) Stop();
        else {
            auto& e = playing.e;
            int b = scene_game::Call<0x4D3990, int>(e.block.c_str());
            void* h = b < 0 ? nullptr : anim::Find(b, CKeyGen::GetUppercaseKey(e.clip.c_str()));
            if (!playing.started) {
                if (b < 0) Cmd(0x04ED).S(e.block.c_str()).Run();
                if (h) {
                    Cmd(0x0605).I(CPools::GetPedRef(ped)).S(e.clip.c_str()).S(e.block.c_str()).F(4).I(e.loop).I(0).I(0).I(0).I(-1).Run();
                    playing.started = true; playing.began = now;
                } else if (now - playing.began > 4000) { Stop(); Message("No se pudo cargar la animacion."); }
            } else {
                void* a = anim::Main(ped->m_pRwClump);
                if (now - playing.began > 500 && (!a || anim::NameKey(anim::Hierarchy(a)) != CKeyGen::GetUppercaseKey(e.clip.c_str()) || anim::BlendDelta(a) < 0)) Stop();
                else if (a && now - playing.sent >= 200) {
                    A p{}; p.session = playing.session; p.sequence = ++playing.sequence; p.active = true;
                    strcpy_s(p.block, e.block.c_str()); p.hash = CKeyGen::GetUppercaseKey(e.clip.c_str()); p.time = anim::CurrentTime(a);
                    GetPacketFactory().Send(p); playing.sent = now;
                }
            }
        }
    }
    for (auto it = remotes.begin(); it != remotes.end();) {
        auto r = CNetworkPlayerManager::GetPlayer(playerid_t(it->first)); auto& s = it->second; auto& p = s.p;
        if (!r || !CConfigLoader::ms_bEmotes || CMissionMode::IsIndividual() || !p.active || now - s.heard > 1500 || r->m_bDead || r->m_bIndividualMode) {
            if (r) Fade(r->m_pPed, p);
            it = remotes.erase(it); continue;
        }
        auto ped = r->m_pPed;
        if (ValidPed(ped) && ped->m_pRwClump && !ped->m_nPedFlags.bInVehicle) {
            int b = scene_game::Call<0x4D3990, int>(p.block);
            if (b < 0 && strncmp(p.block, "C_", 2)) Cmd(0x04ED).S(p.block).Run();
            void* h = b < 0 ? nullptr : anim::Find(b, p.hash);
            if (h) {
                void* a = nullptr;
                for (auto x = anim::First(ped->m_pRwClump); x; x = anim::Next(x)) if (anim::Hierarchy(x) == h && anim::BlendDelta(x) >= 0) { a = x; break; }
                if (!a) a = anim::Blend(ped->m_pRwClump, h, anim::F_AUTO_REMOVE, 8.0f);
                float time = p.time + (now - s.heard) * .001f;
                if (a) anim::SetCurrentTime(a, (std::min)(time, (std::max)(0.0f, anim::TotalTime(h) - .01f)));
            }
        }
        ++it;
    }
}
