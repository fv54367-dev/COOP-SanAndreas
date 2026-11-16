// Adapted from sa-coop (Karimov-Akbar, MIT): features/story/pool/Pool.cpp.
// Native semantics: gta-reversed Scripts/Commands/PadCommands.cpp, RunningScript.cpp.
#include "stdafx.h"
#include "CCoopPool.h"
#include "CConfigLoader.h"
#include "CMissionMode.h"
#include "COpCodeSync.h"
#include "CTestMode.h"
#include "CCoopMission.h"
#include "CMissionMirror.h"
#include "network/packets/scripts.h"
#include <CAnimBlendAssociation.h>
#include <CAnimBlendHierarchy.h>
#include <CAnimBlock.h>
#include <CObject.h>
#include <CGame.h>
#include <CTheScripts.h>
#include <algorithm>
#include <cmath>
#include <map>

namespace {
using P = Packets::Scripts::CoopPool;
class Cmd {
public:
    explicit Cmd(uint16_t opcode);

    Cmd& I(int32_t v);              // integer literal
    Cmd& F(float v);                // float literal
    Cmd& S(const char* str);        // variable-length string literal (type 0x0E)
    Cmd& S8(const char* str);       // fixed 8-byte string literal (type 0x09), e.g. GXT keys
    Cmd& Out(int32_t* result);      // output into a local variable, copied back after Run()
    Cmd& Out(float* result);
    Cmd& End();                     // end of variadic arguments (START_NEW_SCRIPT etc.)

    // Executes the command. Returns the condition result for conditional opcodes.
    bool Run();

private:
    void Put(const void* p, size_t n);
    uint8_t m_code[256];
    size_t m_size = 0;
    struct OutVar {
        void* ptr;
        uint16_t index;
    };
    OutVar m_outs[16];
    int m_numOuts = 0;
    uint16_t m_nextLocal = 0;
    bool m_overflow = false;
};

Cmd::Cmd(uint16_t opcode) { Put(&opcode, 2); }

void Cmd::Put(const void* p, size_t n) {
    if (m_size + n > sizeof(m_code)) {
        m_overflow = true;
        return;
    }
    memcpy(m_code + m_size, p, n);
    m_size += n;
}

Cmd& Cmd::I(int32_t v) {
    uint8_t t = 1;  // SCRIPTPARAM_STATIC_INT_32BITS
    Put(&t, 1);
    Put(&v, 4);
    return *this;
}

Cmd& Cmd::F(float v) {
    uint8_t t = 6;  // SCRIPTPARAM_STATIC_FLOAT
    Put(&t, 1);
    Put(&v, 4);
    return *this;
}

Cmd& Cmd::S(const char* str) {
    uint8_t t = 0x0E;  // SCRIPTPARAM_STATIC_PASCAL_STRING
    // CRunningScript::ReadTextLabelFromScript copies it unbounded into the handler's stack buffer (24 bytes for an
    // animation name, 16 for its block) and takes a length of 128 or more as negative
    size_t n = strlen(str);
    if (n > 23) {
        
        n = 23;
    }
    uint8_t len = (uint8_t)n;
    Put(&t, 1);
    Put(&len, 1);
    Put(str, n);
    return *this;
}

Cmd& Cmd::S8(const char* str) {
    uint8_t t = 0x09;  // SCRIPTPARAM_STATIC_SHORT_STRING
    char buf[8] = {};
    strncpy(buf, str, 7);
    Put(&t, 1);
    Put(buf, 8);
    return *this;
}

Cmd& Cmd::Out(int32_t* result) {
    if (m_numOuts >= 16 || m_nextLocal >= 32) {
        m_overflow = true;
        return *this;
    }
    uint8_t t = 3;  // SCRIPTPARAM_LOCAL_NUMBER_VARIABLE
    Put(&t, 1);
    Put(&m_nextLocal, 2);
    m_outs[m_numOuts++] = {result, m_nextLocal++};
    return *this;
}

Cmd& Cmd::Out(float* result) { return Out(reinterpret_cast<int32_t*>(result)); }

Cmd& Cmd::End() {
    uint8_t t = 0;
    Put(&t, 1);
    return *this;
}

bool Cmd::Run() {
    if (m_overflow) return false;
    int savedParams[32];
    memcpy(savedParams, ScriptParams, sizeof(savedParams));
    CRunningScript script{};
    script.Init();
    script.m_pBaseIP = script.m_pCurrentIP = m_code;
    memcpy(script.m_szName, "t55cmd", 7);
    script.ProcessOneCommand();
    for (int i=0; i<m_numOuts; ++i)
        memcpy(m_outs[i].ptr, &script.m_aLocalVars[m_outs[i].index], 4);
    memcpy(ScriptParams, savedParams, sizeof(savedParams));
    return script.m_bCondResult;
}

uint64_t Now() { return GetTickCount64(); }
int32_t& Local(int n) { return reinterpret_cast<int32_t*>(0xA48960)[n]; }
CRunningScript* Pool() {
    for (auto s = CTheScripts::pActiveScripts; s; s = s->m_pNext)
        if (s->m_bIsMission && CCoopPool::IsPool(s)) return s;
    return nullptr;
}
bool Ready() {
    auto me = FindPlayerPed(0);
    return CCoopPool::Enabled() && CNetwork::m_bConnected && CNetwork::m_bAuthenticated && !CMissionMode::IsIndividual() &&
        !CCoopMission::IsActive() && me && me->m_fHealth > 0 && !me->m_nPedFlags.bInVehicle &&
        CWorld::Players[0].m_nPlayerState == PLAYERSTATE_PLAYING;
}
bool FreeGuest() {
    return !COpCodeSync::IsLocalMissionRunning() && !CCutsceneMgr::ms_running &&
        !CPad::GetPad(0)->DisablePlayerControls && !CTheScripts::bMiniGameInProgress;
}
bool Finite(const CVector& v) { return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z); }
bool Near(playerid_t id) {
    auto r = CNetworkPlayerManager::GetPlayer(id); auto me = FindPlayerPed(0);
    if (!me || !r || !r->m_pPed || r->m_bDead || r->m_bIndividualMode || r->m_bOtherScripts ||
        r->m_pPed->m_nAreaCode != me->m_nAreaCode) return false;
    return (r->m_pPed->GetPosition() - me->GetPosition()).Magnitude() <= 8.0f;
}
struct Session {
    playerid_t owner{}, partner{};
    uint32_t id = 0, seq = 0, lastSeq = 0;
    bool active = false, owning = false, observer = false;
    uint64_t heard = 0, sent = 0;
} g;
struct Offer { P p; uint64_t heard = 0; };
std::map<uint64_t, Offer> offers;
P view{};
bool haveView = false;
bool cameraOwned = false;
CVector shownCam{}, shownFront{};
float shownFov = 70;
uint64_t shownAt = 0;
CVector savedPos{};
bool savedVisible = true;
bool keyWasDown = false;
uint64_t inputAt = 0, pendingUntil = 0;
playerid_t pendingOwner{};
uint32_t pendingSession = 0;
int16_t remoteKeys[20]{};
float mx = 0, my = 0;
bool remoteJoy = false, commandPad = false, commandMouse = false;
CControllerState savedNew{}, savedOld{};
float savedMouse[2]{};
int lastTurn = -1;
uint64_t helpAt = 0;
int avatarNpc = -1, avatarCue = -1;
playerid_t avatarPartner{};
bool avatarVisible = true, resultSent = false;
int pendingResult = -1;
void CueBack() {
    auto r = CNetworkPlayerManager::GetPlayer(avatarPartner);
    if (r && r->m_pPed) {
        if (avatarCue >= 0 && CPools::GetObject(avatarCue) &&
            Cmd(0x0737).I(CPools::GetPedRef(r->m_pPed)).I(avatarCue).Run())
            r->m_pPed->DropEntityThatThisPedIsHolding(false);
        Cmd(0x06AB).I(CPools::GetPedRef(r->m_pPed)).I(0).Run();
    }
    if (avatarNpc >= 0) if (auto npc = CPools::GetPed(avatarNpc)) npc->m_bIsVisible = avatarVisible;
    avatarNpc = avatarCue = -1; avatarPartner = {};
}
uint32_t viewInterval = 50;
P lastGoodCam{};
bool haveGoodCam = false;
uint64_t viewTxLogAt = 0, viewRxLogAt = 0, lastFullSend = 0;
uint32_t viewTxCount = 0, viewRxCount = 0, viewDropped = 0, lastStateHash = 0;
struct Mirror { int handle = -1; uint16_t model = 0; CVector from{}, to{}; uint64_t tick = 0; P::Object state{}; };
std::map<int, Mirror> mirrors;
std::map<int, bool> hidden;
int tableHandle = -1;
CVector tablePos{}, tableRight{}, tableUp{}, tableAt{};
void PlaceTable(CObject* o, const CVector& pos, const CVector& right, const CVector& up, const CVector& at) {
    if (!o || !o->m_matrix) return;
    CWorld::Remove(o); o->SetPosn(pos);
    o->m_matrix->right = right; o->m_matrix->up = up; o->m_matrix->at = at;
    o->UpdateRwMatrix(); o->UpdateRwFrame(); CWorld::Add(o);
}

void Send(uint8_t action) {
    P p{}; p.action = action; p.owner = g.owner; p.partner = g.partner; p.session = g.id;
    p.pos = FindPlayerPed(0) ? FindPlayerPed(0)->GetPosition() : CVector{};
    GetPacketFactory().Send(p);
}
void Help(const char* text) {
    if (Now() - helpAt < 2500) return;
    helpAt = Now();
    CChat::AddMessage("{c7a6ff}[Billar]{d6dce8} %s", text);
    CHud::SetHelpMessage(text, true, false, false);
}
void Delete(Mirror& m) {
    if (m.handle >= 0 && CPools::GetObject(m.handle)) Cmd(0x0108).I(m.handle).Run();
    m.handle = -1;
}
void RestoreHidden() {
    for (auto [h, visible] : hidden) if (auto ped = CPools::GetPed(h)) ped->m_bIsVisible = visible;
    hidden.clear();
}
void Stop(bool worldGone = false) {
    if (!worldGone) {
        CueBack();
        if (tableHandle >= 0) PlaceTable(CPools::GetObject(tableHandle), tablePos, tableRight, tableUp, tableAt);
        for (auto& [slot, m] : mirrors) Delete(m);
        RestoreHidden();
        if (g.active && !g.owning && !g.observer) {
            if (auto me = FindPlayerPed(0)) {
                Cmd(0x0792).I(CPools::GetPedRef(me)).Run();
                if (me->m_fHealth > 0 && CWorld::Players[0].m_nPlayerState == PLAYERSTATE_PLAYING) me->SetPosn(savedPos);
                me->m_bIsVisible = savedVisible;
                Cmd(0x04D7).I(CPools::GetPedRef(me)).I(0).Run();
            }
            Cmd(0x01B4).I(0).I(1).Run(); Cmd(0x0581).I(1).Run(); Cmd(0x0826).I(1).Run();
            if (auto me = FindPlayerPed(0)) Cmd(0x06AB).I(CPools::GetPedRef(me)).I(0).Run();
            if (cameraOwned) {
                Cmd(0x0930).I(0).Run(); Cmd(0x092F).I(0).Run(); Cmd(0x0931).I(0).Run();
            }
            Cmd(0x0925).Run(); Cmd(0x02EB).Run(); Cmd(0x0373).Run();
        }
    }
    avatarNpc = avatarCue = -1; avatarPartner = {}; resultSent = false; pendingResult = -1;
    tableHandle = -1; cameraOwned = false;
    mirrors.clear(); hidden.clear(); g = {}; haveView = false; mx = my = 0;
    viewTxCount = viewRxCount = viewDropped = 0; lastStateHash = 0; haveGoodCam = false; viewInterval = 50;
    memset(remoteKeys, 0, sizeof(remoteKeys)); lastTurn = -1; inputAt = 0;
}
void CancelOwner() {
    if (g.active && g.partner.valid()) Send(P::END);
    CueBack();
    // POOL2's own main loop returns to its registered cleanup when 108@ is nonzero.
    if (Pool()) Local(108) = 1;
    g.partner = {}; g.heard = Now(); inputAt = 0;
}
void CaptureAnim(RpClump* clump, P::Anim& out) {
    if (!clump || RwObjectGetType(clump) != rpCLUMP || !RpAnimBlendClumpIsInitialized(clump)) return;
    auto a = RpAnimBlendClumpGetMainAssociation(clump, nullptr, nullptr);
    if (!a || !a->m_pHierarchy) return;
    int b = a->m_pHierarchy->m_nAnimBlockId;
    if (b < 0 || b >= CAnimManager::ms_numAnimBlocks) return;
    const auto name = CAnimManager::ms_aAnimBlocks[b].szName;
    if (_stricmp(name, "POOL") && _stricmp(name, "PED")) return;
    strncpy_s(out.block, name, 15);
    out.hash = a->m_pHierarchy->m_hashKey; out.time = a->m_fCurrentTime;
    out.speed = a->m_fSpeed; out.flags = a->m_nFlags;
}
void ApplyAnim(RpClump* clump, const P::Anim& a) {
    if (!a.hash || !clump || RwObjectGetType(clump) != rpCLUMP || !RpAnimBlendClumpIsInitialized(clump)) return;
    auto block = CAnimManager::GetAnimationBlock(a.block);
    if (!block || !block->bLoaded) { Cmd(0x04ED).S(a.block).Run(); return; }
    auto h = CAnimManager::GetAnimation(a.hash, block);
    if (!h) return;
    auto current = RpAnimBlendClumpGetMainAssociation(clump, nullptr, nullptr);
    if (!current || current->m_pHierarchy != h) current = CAnimManager::BlendAnimation(clump, h, a.flags, 8.0f);
    if (!current) return;
    if (std::abs(current->m_fCurrentTime - a.time) > 0.12f) current->SetCurrentTime(std::clamp(a.time, 0.0f, h->m_fTotalTime));
    current->m_fSpeed = a.speed;
}
bool ValidAnim(const P::Anim& a) {
    return a.block[15] == 0 && (!a.hash || !_stricmp(a.block, "POOL") || !_stricmp(a.block, "PED")) && std::isfinite(a.time) && a.time >= 0 && a.time < 3600 &&
        std::isfinite(a.speed) && a.speed >= 0 && a.speed <= 10;
}
bool ValidView(const P& p) {
    if (!Finite(p.pos) || !Finite(p.camera) || !Finite(p.front) || !std::isfinite(p.heading) ||
        !(p.fov > 5 && p.fov < 170) || CVector(p.front).Magnitude() < 0.5f || CVector(p.front).Magnitude() > 1.5f || !ValidAnim(p.anim)) return false;
    if ((p.tableModel != 2964 && p.tableModel != 3094) || !Finite(p.tablePos) || !Finite(p.tableRight) ||
        !Finite(p.tableUp) || !Finite(p.tableAt) || (p.tablePos - p.pos).Magnitude() > 8.0f) return false;
    auto basisValid = [](const CVector& r, const CVector& u, const CVector& a) {
        auto dot = [](const CVector& x, const CVector& y) { return x.x*y.x + x.y*y.y + x.z*y.z; };
        return std::abs(dot(r,r)-1) < 0.05f && std::abs(dot(u,u)-1) < 0.05f && std::abs(dot(a,a)-1) < 0.05f &&
            std::abs(dot(r,u)) < 0.05f && std::abs(dot(r,a)) < 0.05f && std::abs(dot(u,a)) < 0.05f;
    };
    if (!basisValid(p.tableRight, p.tableUp, p.tableAt)) return false;
    bool slots[75]{};
    for (int i = 0; i < p.count; ++i) {
        const auto& o = p.objects[i];
        if (slots[o.slot] || !Finite(o.pos) || !Finite(o.right) || !Finite(o.up) || !Finite(o.at) ||
            !std::isfinite(o.scale) || o.scale <= 0 || o.scale > 4 || !ValidAnim(o.anim)) return false;
        if (!basisValid(o.right, o.up, o.at)) return false;
        auto length = [](const CVector& v) { return v.x*v.x + v.y*v.y + v.z*v.z; };
        if (length(o.right) < 0.8f || length(o.right) > 1.2f || length(o.up) < 0.8f || length(o.up) > 1.2f ||
            length(o.at) < 0.8f || length(o.at) > 1.2f) return false;
        slots[o.slot] = true;
        // Only POOL2 props; never request arbitrary models sent by a peer.
        if (o.model != 2965 && o.model != 3122 && o.model != 3004 &&
            (o.model < 2995 || o.model > 3003) && (o.model < 3100 || o.model > 3106)) return false;
        if ((o.pos - p.pos).Magnitude() > 25) return false;
    }
    return true;
}
void OwnAvatar() {
    if (!g.partner.valid() || Local(34) < 3 || Local(108) != 0 || resultSent) return;
    auto r = CNetworkPlayerManager::GetPlayer(g.partner);
    auto npc = CPools::GetPed(Local(214));
    if (!r || !r->m_pPed || !npc) return;
    if (avatarNpc < 0) { avatarNpc = Local(214); avatarVisible = npc->m_bIsVisible; avatarPartner = g.partner; }
    npc->m_bIsVisible = false;
    auto rp = r->m_pPed;
    rp->SetPosn(npc->GetPosition()); rp->m_fCurrentRotation = rp->m_fAimingRotation = npc->m_fCurrentRotation;
    P::Anim anim{}; CaptureAnim(npc->m_pRwClump, anim); ApplyAnim(rp->m_pRwClump, anim);
    int cue = Local(107); if (!CPools::GetObject(cue)) return;
    if (Cmd(0x0737).I(Local(214)).I(cue).Run()) npc->DropEntityThatThisPedIsHolding(false);
    int ref = CPools::GetPedRef(rp);
    Cmd(0x06AB).I(ref).I(1).Run();
    if (!Cmd(0x0737).I(ref).I(cue).Run()) {
        rp->DropEntityThatThisPedIsHolding(false);
        Cmd(0x070A).I(ref).I(cue).F(0).F(0).F(0).I(6).I(16).S("NULL").S("NULL").I(1).Run();
    }
    avatarCue = cue;
}
// Cheap hash of everything that moves, so a quiet table is sent at a low rate and a change goes out at once.
uint32_t StateHash(const P& p) {
    uint32_t h = 2166136261u;
    auto mix = [&](float f) { uint32_t u; memcpy(&u, &f, 4); h = (h ^ u) * 16777619u; };
    for (float f : {p.pos.x, p.pos.y, p.pos.z, p.heading, p.camera.x, p.camera.y, p.camera.z, p.front.x, p.front.y, p.front.z, p.anim.time}) mix(f);
    h = (h ^ p.turn) * 16777619u;
    for (int i = 0; i < p.count; ++i) {
        const auto& o = p.objects[i];
        h = (h ^ o.slot) * 16777619u; h = (h ^ o.visible) * 16777619u;
        for (float f : {o.pos.x, o.pos.y, o.pos.z,
            o.right.x, o.right.y, o.right.z, o.up.x, o.up.y, o.up.z, o.at.x, o.at.y, o.at.z,
            o.scale, o.anim.time, o.anim.speed}) mix(f);
    }
    return h;
}
void Snapshot() {
    P p{}; p.action = P::VIEW; p.owner = g.owner; p.partner = g.partner; p.session = g.id; p.sequence = ++g.seq;
    auto npc = CPools::GetPed(Local(214));
    if (!npc) return;
    p.pos = npc->GetPosition(); p.heading = npc->m_fCurrentRotation; p.turn = Local(180) == 1;
    auto& cam = TheCamera.m_aCams[TheCamera.m_nActiveCam];
    if (TheCamera.m_bTransitionState) {
        p.camera = TheCamera.m_vecGameCamPos;
        p.front = TheCamera.m_vecTargetDuringInter - TheCamera.m_vecSourceDuringInter;
        if (p.front.Magnitude() > 0.001f) p.front.Normalise();
        p.fov = TheCamera.m_fFOVDuringInter;
    } else {
        p.camera = cam.m_vecSource; p.front = cam.m_vecFront; p.fov = cam.m_fFOV;
    }
    // A bad camera frame must not drop the whole snapshot (balls included): reuse the last good camera.
    if (!Finite(p.camera) || !Finite(p.front) || !(p.fov > 5 && p.fov < 170) ||
        CVector(p.front).Magnitude() < 0.5f || CVector(p.front).Magnitude() > 1.5f) {
        if (haveGoodCam) { p.camera = lastGoodCam.camera; p.front = lastGoodCam.front; p.fov = lastGoodCam.fov; }
    } else { lastGoodCam.camera = p.camera; lastGoodCam.front = p.front; lastGoodCam.fov = p.fov; haveGoodCam = true; }
    CaptureAnim(npc->m_pRwClump, p.anim);
    auto table = CPools::GetObject(Local(163));
    if (!table || !table->m_matrix) return;
    p.tableModel = table->m_nModelIndex; p.tablePos = table->GetPosition();
    p.tableRight = table->m_matrix->right; p.tableUp = table->m_matrix->up; p.tableAt = table->m_matrix->at;
    for (int i = 0; i < 75; ++i) {
        auto entry = reinterpret_cast<uint8_t*>(0xA90850) + i * 8;
        int h = *reinterpret_cast<int*>(entry + 4);
        auto o = *entry == 3 ? CPools::GetObject(h) : nullptr;
        if (!o || (o->GetPosition() - p.pos).Magnitude() > 25.0f) continue;
        const int model = o->m_nModelIndex;
        if (model != 2965 && model != 3122 && model != 3004 &&
            (model < 2995 || model > 3003) && (model < 3100 || model > 3106)) continue;
        auto& v = p.objects[p.count++]; v.slot = i; v.model = o->m_nModelIndex;
        v.pos = o->GetPosition(); v.visible = o->m_bIsVisible; v.scale = o->m_fScale;
        if (auto m = o->m_matrix) { v.right = m->right; v.up = m->up; v.at = m->at; }
        else { v.right = {1,0,0}; v.up = {0,1,0}; v.at = {0,0,1}; }
        CaptureAnim(o->m_pRwClump, v.anim);
    }
    const uint32_t hash = StateHash(p);
    if (hash == lastStateHash && Now() - lastFullSend < 500) return; // table at rest: keep-alive only
    if (!ValidView(p)) {
        ++viewDropped;
        CMissionMirror::LogEventf("pool-view-invalid cam=%d front=%d fov=%.1f table=%u anim=%d dist=%.1f count=%d", Finite(p.camera),
            Finite(p.front), p.fov, p.tableModel, ValidAnim(p.anim), (p.tablePos - p.pos).Magnitude(), p.count);
        return;
    }
    lastStateHash = hash; lastFullSend = Now(); ++viewTxCount;
    GetPacketFactory().Send(p);
    if (Now() - viewTxLogAt >= 2000) {
        viewTxLogAt = Now();
        CMissionMirror::LogEventf("pool-view-tx n=%u dropped=%u balls=%d turn=%d", viewTxCount, viewDropped, p.count, p.turn);
    }
}
void MirrorView() {
    if (!haveView) return;
    if (Now() - viewRxLogAt >= 2000) {
        viewRxLogAt = Now();
        int made = 0; for (auto& [slot, m] : mirrors) made += m.handle >= 0;
        CMissionMirror::LogEventf("pool-view-rx n=%u balls=%d mirrors=%d table=%d", viewRxCount, view.count, made, tableHandle >= 0);
    }
    if (tableHandle < 0 && CPools::ms_pObjectPool) {
        auto pool = CPools::ms_pObjectPool;
        for (int i = 0; i < pool->m_nSize; ++i) {
            auto o = pool->GetAt(i);
            if (!o || o->m_nModelIndex != view.tableModel || !o->m_matrix || (o->GetPosition() - view.tablePos).Magnitude() > 1.0f) continue;
            tableHandle = CPools::GetObjectRef(o); tablePos = o->GetPosition();
            tableRight = o->m_matrix->right; tableUp = o->m_matrix->up; tableAt = o->m_matrix->at;
            break;
        }
    }
    if (tableHandle >= 0) PlaceTable(CPools::GetObject(tableHandle), view.tablePos, view.tableRight, view.tableUp, view.tableAt);
    bool seen[75]{};
    for (int i = 0; i < view.count; ++i) {
        const auto& v = view.objects[i]; seen[v.slot] = true;
        auto& m = mirrors[v.slot];
        if (m.model != v.model) Delete(m);
        if (m.handle >= 0 && !CPools::GetObject(m.handle)) m.handle = -1;
        if (m.handle < 0) {
            if (CStreaming::ms_aInfoForModel[v.model].m_nLoadState != LOADSTATE_LOADED) {
                CStreaming::RequestModel(v.model, GAME_REQUIRED | PRIORITY_REQUEST);
                CMissionMirror::LogEventf("pool-mirror-wait model=%u", v.model); continue;
            }
            Cmd(0x0107).I(v.model).F(v.pos.x).F(v.pos.y).F(v.pos.z).Out(&m.handle).Run();
            auto o = m.handle >= 0 ? CPools::GetObject(m.handle) : nullptr;
            if (!o) continue;
            m.model = v.model; m.from = v.pos; m.to = v.pos;
            Cmd(0x0382).I(m.handle).I(0).Run(); Cmd(0x0550).I(m.handle).I(1).Run();
        }
        auto o = CPools::GetObject(m.handle); if (!o) continue;
        // The visible area, not our ped's (a script ped can keep area 0): balls made but never seen (sa-coop Pool.cpp).
        o->m_nAreaCode = CGame::currArea;
        if (!o->m_pRwObject) o->CreateRwObject();
        if (m.tick != g.heard) {
            m.from = o->GetPosition(); m.to = v.pos;
            if ((m.to - m.from).Magnitude() > 1) m.from = m.to;
            m.tick = g.heard; m.state = v;
        }
        float t = std::clamp(float(Now() - m.tick) / float(viewInterval), 0.0f, 1.0f);
        CWorld::Remove(o); o->SetPosn(m.from + (m.to - m.from) * t);
        if (o->m_matrix) { o->m_matrix->right = v.right; o->m_matrix->up = v.up; o->m_matrix->at = v.at; }
        o->m_fScale = v.scale; o->m_bIsVisible = v.visible;
        o->UpdateRwMatrix(); o->UpdateRwFrame(); CWorld::Add(o); ApplyAnim(o->m_pRwClump, v.anim);
    }
    for (auto it = mirrors.begin(); it != mirrors.end();) {
        if (!seen[it->first]) { Delete(it->second); it = mirrors.erase(it); } else ++it;
    }
    if (g.observer) return;
    auto me = FindPlayerPed(0); if (!me) return;
    // The native opponent remains authoritative. Our CJ takes its pose; cue motion comes in the snapshot.
    me->SetPosn(view.pos); me->SetHeading(view.heading);
    me->m_fCurrentRotation = me->m_fAimingRotation = view.heading; me->m_vecMoveSpeed = {};
    ApplyAnim(me->m_pRwClump, view.anim);
    // As sa-coop Pool.cpp (MIT): one scripted fixed camera, then CAMERA_PERSIST_* makes CCamera take the move/track/FOV
    // values every frame. They must be written each frame (stale vectors otherwise), smoothed between views.
    const uint64_t now = Now();
    if (!cameraOwned) {
        auto look = view.camera + view.front * 10.0f;
        Cmd(0x015F).F(view.camera.x).F(view.camera.y).F(view.camera.z).F(0).F(0).F(0).Run();
        Cmd(0x0160).F(look.x).F(look.y).F(look.z).I(2).Run();
        Cmd(0x0925).Run(); Cmd(0x0930).I(1).Run(); Cmd(0x092F).I(1).Run(); Cmd(0x0931).I(1).Run();
        shownCam = view.camera; shownFront = view.front; shownFov = view.fov; shownAt = now;
        cameraOwned = true;
    }
    const float frame = std::min(0.1f, float(now - shownAt) / 1000.0f); shownAt = now;
    if ((view.camera - shownCam).Magnitude() > 3.0f) { shownCam = view.camera; shownFront = view.front; shownFov = view.fov; }
    else {
        const float k = 1.0f - std::exp(-12.0f * frame);
        shownCam += (view.camera - shownCam) * k; shownFront += (view.front - shownFront) * k; shownFov += (view.fov - shownFov) * k;
    }
    if (shownFront.Magnitude() < 0.001f) shownFront = view.front;
    shownFront.Normalise();
    TheCamera.m_vecMoveLinear = shownCam;
    TheCamera.m_vecTrackLinear = shownCam + shownFront * 10.0f;
    TheCamera.m_fFOVNew = std::clamp(shownFov, 5.0f, 170.0f);
    // Hide only the owner's matching pool opponent, identified by proximity, preserving visibility by handle.
    for (int i = 0; CPools::ms_pPedPool && i < CPools::ms_pPedPool->m_nSize; ++i) {
        auto ped = CPools::ms_pPedPool->GetAt(i);
        if (ped && !ped->IsPlayer() && (ped->GetPosition() - view.pos).Magnitude() < 0.6f) {
            int h = CPools::GetPedRef(ped);
            hidden.emplace(h, bool(ped->m_bIsVisible)); ped->m_bIsVisible = false;
        }
    }
    // Our own bar's opponent, kept by the table attractor (POOL, 1@), waits by the table in the owner's shot (sa-coop Pool.cpp).
    for (auto s = CTheScripts::pActiveScripts; s; s = s->m_pNext) {
        if (_strnicmp(s->m_szName, "pool", 8)) continue;
        int h = s->m_aLocalVars[1].iParam;
        auto ped = h > 0 ? CPools::GetPed(h) : nullptr;
        if (ped && !ped->IsPlayer() && ped->m_bIsVisible) { hidden.emplace(h, true); ped->m_bIsVisible = false; }
    }
}
} // namespace

bool CCoopPool::Enabled() { return CConfigLoader::ms_bPoolTwoPlayers && CConfigLoader::ms_bVanillaScripts; }
bool CCoopPool::OwnView() { return g.active && !g.owning && !g.observer; }
bool CCoopPool::IsPool(CRunningScript* s) { return s && _strnicmp(s->m_szName, "pool2", 8) == 0; }

bool CCoopPool::FilterCompare(CRunningScript* s, uint16_t op, bool state) {
    if ((!Enabled() && !(g.active && g.owning && !g.partner.valid())) || COpCodeSync::bProcessingNetworkOpcode) return state;
    if (_strnicmp(s->m_szName, "pool", 8) == 0 && op == 0x0105 &&
        ((g.active && !g.owning) || pendingUntil > Now())) return false;
    if (!IsPool(s) || !g.id || !g.owning) return state;
    if (op == 0x0737 && avatarCue >= 0 && ScriptParams[0] == avatarNpc && ScriptParams[1] == avatarCue) return true;
    if (op == 0x00E1) {
        if (!g.active && Local(35) == 3 && ScriptParams[0] == 1 && ScriptParams[1] == 12 && g.partner.value()) return true;
        if (g.active && !g.partner.value()) return false; // cleanup already requested
        if (g.active && ScriptParams[0] == 1) {
            int b = ScriptParams[1];
            return Local(180) == 1 && b >= 0 && b < 20 && Now() - inputAt < 1000 && remoteKeys[b] != 0;
        }
    }
    if (op == 0x0A4B && g.active && Local(180) == 1) return remoteJoy;
    return state;
}
void CCoopPool::BeforeCommand(CRunningScript* s, uint16_t op) {
    op &= 0x7FFF;
    if (!Enabled() || !g.active || !g.owning || !IsPool(s)) return;
    if (op == 0x00BA && !resultSent) {
        const auto ip = s->m_pCurrentIP;
        if (ip[0] == 9 && (!memcmp(ip + 1, "PL_08", 6) || !memcmp(ip + 1, "PL_09", 6))) pendingResult = ip[5] == '9';
    }
    // Release the borrowed cue before POOL2 deletes it in its normal cleanup.
    if (op == 0x0A3F && Local(108) != 0) CueBack();
    // Scoped to one handler: pad/mouse globals of unrelated scripts are never replaced.
    if (op == 0x0494) {
        auto pad = CPad::GetPad(1); savedNew = pad->NewState; savedOld = pad->OldState;
        pad->NewState = {}; pad->OldState = {};
        if (Now() - inputAt < 1000) memcpy(&pad->NewState, remoteKeys, sizeof(remoteKeys));
        commandPad = true;
    }
    if (op == 0x0A4A && Local(180) == 1) {
        auto ptr = reinterpret_cast<float*>(0xB73424);
        savedMouse[0] = ptr[0]; savedMouse[1] = ptr[1];
        ptr[0] = mx; ptr[1] = my; mx = my = 0; commandMouse = true;
    }
}
void CCoopPool::AfterCommand(CRunningScript* s, uint16_t op) {
    if (commandPad) { auto p = CPad::GetPad(1); p->NewState = savedNew; p->OldState = savedOld; commandPad = false; }
    if (commandMouse) { memcpy(reinterpret_cast<void*>(0xB73424), savedMouse, sizeof(savedMouse)); commandMouse = false; }
    if (!Enabled() || !g.owning || !g.id || !IsPool(s)) return;
    if (g.active && pendingResult >= 0 && !resultSent) {
        P p{}; p.action = P::RESULT; p.owner = g.owner; p.partner = g.partner; p.session = g.id; p.turn = pendingResult;
        GetPacketFactory().Send(p); resultSent = true; pendingResult = -1; CueBack();
        CChat::AddMessage("{c7a6ff}[Billar]{d6dce8} Gano %s. Sin apuesta.", p.turn ? "el segundo jugador" : "el primer jugador");
    }
    if (g.active && Local(108) != 0 && g.partner.valid()) CancelOwner();
    if (!g.active && Local(178) == 1 && g.partner.value()) {
        g.active = true; g.heard = Now(); Send(P::START);
        Help("Partida de a dos sin apuesta. Turnos del juego; F7 para salir.");
        CMissionMirror::LogEventf("pool-start session=%u partner=%u", g.id, g.partner.value());
    }
}
void CCoopPool::OnPacket(const P& p) {
    if (!Ready() || p.sender == CNetworkPlayerManager::m_localPlayerId) return;
    if (p.action == P::OFFER && p.sender == p.owner && Near(p.owner) && !g.active && FreeGuest()) {
        offers[p.owner.value()] = {p, Now()}; return;
    }
    if (p.action == P::JOIN) {
        const bool ok = g.owning && !g.active && Pool() && Local(35) == 3 && !g.partner.value() &&
            p.owner == g.owner && p.session == g.id && Near(p.sender);
        if (ok) { g.partner = p.sender; g.heard = Now(); }
        CMissionMirror::LogEventf("pool-join-rx ok=%d owning=%d active=%d pool=%d l35=%d partner=%u sessionOk=%d near=%d", ok,
            g.owning, g.active, Pool() != nullptr, Local(35), g.partner.value(), p.session == g.id, Near(p.sender));
        return;
    }
    if (p.action == P::START) {
        CMissionMirror::LogEventf("pool-start-rx near=%d free=%d active=%d pending=%d", Near(p.owner), FreeGuest(), g.active,
            pendingUntil >= Now());
    }
    if (p.action == P::START && p.sender == p.owner && Near(p.owner) && FreeGuest() && !g.active) {
        bool joined = p.partner == CNetworkPlayerManager::m_localPlayerId;
        if (joined && (pendingUntil < Now() || pendingOwner != p.owner || pendingSession != p.session)) {
            CMissionMirror::LogEvent("pool-start-skip reason=pending-expired-or-mismatch");
            return;
        }
        g.owner = p.owner; g.partner = p.partner; g.id = p.session; g.active = true;
        g.observer = !joined; g.owning = false; g.heard = Now();
        pendingUntil = 0;
        if (joined) {
            auto me = FindPlayerPed(0); savedPos = me->GetPosition(); savedVisible = me->m_bIsVisible;
            Cmd(0x01B4).I(0).I(0).Run(); Cmd(0x04D7).I(CPools::GetPedRef(me)).I(1).Run();
            // As at a table of our own: no radar/HUD, weapon hidden while the cue is in hand.
            Cmd(0x0581).I(0).Run(); Cmd(0x0826).I(0).Run(); Cmd(0x06AB).I(CPools::GetPedRef(me)).I(1).Run();
            Help("Billar de a dos sin apuesta. Usa tus controles en tu turno; F7 para salir.");
        }
        return;
    }
    if (!g.active || p.session != g.id || p.owner != g.owner) return;
    if (p.action == P::END) {
        if (g.owning && p.sender == g.partner) CancelOwner();
        else if (!g.owning && p.sender == g.owner) Stop();
        return;
    }
    if (p.action == P::RESULT && !g.owning && p.sender == g.owner) {
        bool won = (p.turn == 1 && CNetworkPlayerManager::m_localPlayerId == g.partner) ||
            (p.turn == 0 && CNetworkPlayerManager::m_localPlayerId == g.owner);
        CChat::AddMessage("{c7a6ff}[Billar]{d6dce8} %s. Partida sin apuesta.", g.observer ?
            (p.turn ? "Gano el segundo jugador" : "Gano el primer jugador") : (won ? "Ganaste" : "Perdiste"));
        return;
    }
    if (p.action == P::INPUT && g.owning && p.sender == g.partner) {
        if (p.sequence <= g.lastSeq || !std::isfinite(p.mouseX) || !std::isfinite(p.mouseY)) return;
        g.lastSeq = p.sequence; inputAt = g.heard = Now();
        memcpy(remoteKeys, p.keys, sizeof(remoteKeys)); remoteJoy = p.joypad;
        if (Local(180) == 1) { mx += std::clamp(p.mouseX, -200.0f, 200.0f); my += std::clamp(p.mouseY, -200.0f, 200.0f); }
        return;
    }
    if (p.action == P::VIEW && !g.owning && p.sender == g.owner && p.sequence > g.lastSeq && ValidView(p)) {
        const uint64_t t = Now();
        if (g.heard && viewRxCount > 0) viewInterval = (uint32_t)std::clamp<uint64_t>(t - g.heard, 30, 100);
        g.lastSeq = p.sequence; g.heard = t; view = p; haveView = true; ++viewRxCount;
    }
}
void CCoopPool::Reset() { Stop(true); offers.clear(); pendingUntil = 0; }
void CCoopPool::Process() {
    bool key = CTestMode::IsGameWindowFocused() && (GetAsyncKeyState(VK_F7) & 0x8000);
    bool pressed = key && !keyWasDown; keyWasDown = key;
    if (pressed && Enabled())
        CMissionMirror::LogEventf("pool-f7 ready=%d owning=%d active=%d offers=%d pool=%d free=%d l35=%d", Ready(), g.owning, g.active,
            int(offers.size()), Pool() != nullptr, FreeGuest(), Local(35));
    if (!Ready()) {
        if (g.active && !g.owning && !g.observer && CNetwork::m_bAuthenticated) Send(P::END);
        if (g.active && g.owning && Pool()) CancelOwner(); else Stop(!FindPlayerPed(0));
        offers.clear(); pendingUntil = 0; return;
    }
    auto pool = Pool();
    if (g.owning) {
        if (!g.active && g.partner.valid() && Now() - g.heard >= 5000) g.partner = {};
        if (!pool || (g.active && Local(178) != 1)) { Send(P::END); Stop(); return; }
        if (!g.active && Local(35) != 3) { Send(P::END); Stop(); return; }
        if (g.active) {
            if (pressed || (g.partner.value() && (!Near(g.partner) || Now() - g.heard > 5000))) CancelOwner();
            OwnAvatar();
            if (lastTurn != Local(180)) { mx = my = 0; lastTurn = Local(180); }
            if (Now() - g.sent >= 50) { g.sent = Now(); Snapshot(); }
        } else if (Now() - g.sent >= 750) { g.sent = Now(); Send(P::OFFER); }
        return;
    }
    if (g.active) {
        if (!Near(g.owner) || Now() - g.heard > 5000 || pressed || pool) { if (!g.observer) Send(P::END); Stop(); return; }
        MirrorView();
        if (!g.observer) {
            // Full pad state each frame on the state channel (as sa-coop): a lost one is replaced by the next; mouse deltas add up.
            P p{}; p.owner = g.owner; p.partner = g.partner; p.session = g.id; p.action = P::INPUT; p.sequence = ++g.seq;
            if (CTestMode::IsGameWindowFocused()) {
                memcpy(p.keys, &CPad::GetPad(0)->NewState, sizeof(p.keys));
                auto mouse = reinterpret_cast<float*>(0xB73424); p.mouseX = mouse[0]; p.mouseY = mouse[1];
            }
            p.joypad = FrontEndMenuManager.m_nController == 1;
            p.pos = FindPlayerPed(0)->GetPosition(); GetPacketFactory().Send(p);
        }
        return;
    }
    if (pool && Local(35) == 3 && Local(178) == 0) {
        g.owning = true; g.owner = CNetworkPlayerManager::m_localPlayerId;
        g.id = uint32_t(Now()) | 1u; g.heard = Now();
        CMissionMirror::LogEventf("pool-offer-open session=%u", g.id);
        Help("Billar listo: el OTRO jugador (a menos de 8 m) aprieta F7 para unirse; vos segui en la pantalla de apuesta sin elegir nada."); return;
    }
    for (auto it = offers.begin(); it != offers.end();) {
        if (Now() - it->second.heard > 2000 || !Near(it->second.p.owner)) it = offers.erase(it); else ++it;
    }
    if (!offers.empty() && !pool && FreeGuest()) {
        Help("F7: unirse al billar cercano (sin apuesta). Solo dos jugadores por mesa.");
        if (pressed && pendingUntil < Now()) {
            auto p = offers.begin()->second.p; p.action = P::JOIN;
            p.pos = FindPlayerPed(0)->GetPosition(); GetPacketFactory().Send(p);
            CMissionMirror::LogEventf("pool-join-tx owner=%u session=%u", p.owner.value(), p.session);
            pendingOwner = p.owner; pendingSession = p.session; pendingUntil = Now() + 5000;
        }
    }
}
