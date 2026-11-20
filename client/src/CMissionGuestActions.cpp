#include "stdafx.h"
#include "CMissionGuestActions.h"
#include "CMissionMirror.h"
#include "CCoopMission.h"
#include "CConfigLoader.h"
#include "COpCodeSync.h"
#include "CTestMode.h"
#include "Commands/CommandHooks.h"
#include "network/packets/mission_guest_actions.h"
#include <algorithm>
#include <cmath>
#include <map>

// Adapted from Karimov-Akbar/sa-coop MissionRules.cpp (MIT), see NOTICE.md.
// Actor-specific native sequences replace only the guest path, never $PLAYER_ACTOR.
namespace
{
using P = Packets::Scripts::MissionGuestActions;
struct Spec { const char* name; uint32_t start, length, hash, locate, button; int action; };
constexpr Spec specs[] = {
    {"HEIST2", 0x23822E, 19367, 0xA56B6122, 0x23B69E, 0x23B6FF, 1},
    {"WUZI2", 0x16CF33, 27266, 0x4E273A6C, 0x16ED89, 0x16EDDB, 2},
    {"RYDER3", 0x0C0A67, 20209, 0x00AD32D8, 0, 0, 0},
};
P session;
CRunningScript* active = nullptr;
const Spec* spec = nullptr;
DWORD announced = 0, heard = 0, sent = 0, chargeAt = 0;
uint32_t epoch = 0, sequence = 0, inputSequence = 0;
bool throwing = false;
struct Input { P packet; DWORD at = 0; uint32_t consumed = 0, thrown = 0; DWORD throwAt = 0; };
std::map<playerid_t, Input> inputs;
struct Action { P packet; DWORD at = 0; int slot = -1; bool ready = false, playing = false; } action;
struct Animation { P packet; DWORD at = 0; int handle = -1; bool started = false; };
std::map<playerid_t, Animation> animations;
std::map<playerid_t, uint32_t> lastAnimation;

// Direct native calls must not contaminate the surrounding opcode's network capture.
struct NativeScope
{
    int params[32]; OpcodeParameter buffer[NUM_SYNCED_PARAMS]; bool network;
    NativeScope() : network(COpCodeSync::bProcessingNetworkOpcode)
    {
        memcpy(params, ScriptParams, sizeof(params));
        memcpy(buffer, COpCodeSync::scriptParamsBuffer, sizeof(buffer));
        COpCodeSync::BeginLocalCommands();
        COpCodeSync::bProcessingNetworkOpcode = true;
    }
    ~NativeScope()
    {
        memcpy(ScriptParams, params, sizeof(params));
        memcpy(COpCodeSync::scriptParamsBuffer, buffer, sizeof(buffer));
        COpCodeSync::bProcessingNetworkOpcode = network;
        COpCodeSync::EndLocalCommands();
    }
};
int& L(int i) { return CTheScripts::LocalVariablesForCurrentMission[i].iParam; }
float& F(int i) { return CTheScripts::LocalVariablesForCurrentMission[i].fParam; }
bool Enabled()
{
    auto me = FindPlayerPed(0);
    return CConfigLoader::ms_bMissionGuestActions && CConfigLoader::ms_bVanillaScripts &&
        CNetwork::m_bAuthenticated && me && me->IsAlive() && !CCoopMission::IsLocalIndividualMission() &&
        !CCoopMission::HasLocalLeft();
}
bool Eligible(CNetworkPlayer* p)
{
    auto me = FindPlayerPed(0);
    return me && p && p->m_pPed && p->m_pPed->IsVTableValid() && !p->m_bDead &&
        !p->m_bIndividualMode && p->m_pPed->m_fHealth > 0 &&
        p->m_pPed->m_nAreaCode == me->m_nAreaCode && CCoopMission::ShouldIncludeNetworkPlayer(p);
}
bool Owner(CNetworkPlayer* p)
{
    return p && (CCoopMission::IsRunning() ? p->m_playerId == CCoopMission::GetOwnerId() : p->m_bIsHost);
}
CPed* Actor(playerid_t id)
{
    if (id == CNetworkPlayerManager::m_localPlayerId) return FindPlayerPed(0);
    auto p = CNetworkPlayerManager::GetPlayer(id);
    return Eligible(p) ? p->m_pPed : nullptr;
}
const Spec* Original(CRunningScript* s)
{
    if (!s || s->m_pBaseIP != reinterpret_cast<uint8_t*>(0xA7A6A0)) return nullptr;
    for (const auto& entry : specs)
    {
        if (_strnicmp(s->m_szName, entry.name, 8)) continue;
        uint32_t hash = 2166136261u;
        for (uint32_t i = 0; i < entry.length; ++i) hash = (hash ^ s->m_pBaseIP[i]) * 16777619u;
        if (hash == entry.hash) return &entry;
    }
    return nullptr;
}
CRunningScript* Mission()
{
    for (auto s = CTheScripts::pActiveScripts; s; s = s->m_pNext)
        if (s->m_bIsMission && COpCodeSync::IsScriptSynced(s)) return s;
    return nullptr;
}
bool At(CRunningScript* s, uint32_t offset)
{
    return spec && CommandHooks::GetCurrentCommandIP() == s->m_pBaseIP + offset - spec->start;
}
void StopAnimation(playerid_t id)
{
    auto it = animations.find(id);
    if (it == animations.end()) return;
    auto ped = Actor(id);
    if (ped && CPools::GetPedRef(ped) == it->second.handle && ped->IsAlive())
    {
        NativeScope scope;
        // Only remove our own bomber tasks; never wipe a replacement death/mission task.
        const char* names[] = {"BOM_PLANT_IN", "BOM_PLANT_LOOP", "BOM_PLANT_2IDLE",
            "BOM_PLANT_CROUCH_IN", "BOM_PLANT_CROUCH_OUT"};
        for (auto name : names)
            if (plugin::Command<Commands::IS_CHAR_PLAYING_ANIM>(it->second.handle, name))
            {
                plugin::Command<Commands::CLEAR_CHAR_TASKS>(it->second.handle);
                break;
            }
    }
    animations.erase(it);
}
void BeginAnimation(const P& p)
{
    if (!p.sequence || p.sequence <= lastAnimation[p.actor] || !Actor(p.actor)) return;
    StopAnimation(p.actor);
    lastAnimation[p.actor] = p.sequence;
    animations[p.actor] = {p, GetTickCount(), CPools::GetPedRef(Actor(p.actor)), false};
}
void Animate()
{
    const DWORD now = GetTickCount();
    for (auto it = animations.begin(); it != animations.end(); )
    {
        auto id = it->first;
        auto& a = it->second;
        auto ped = Actor(id);
        if (!ped || !ped->IsAlive() || CPools::GetPedRef(ped) != a.handle || now - a.at > 8000)
        { ++it; StopAnimation(id); continue; }
        if (!a.started)
        {
            NativeScope scope;
            plugin::Command<Commands::REQUEST_ANIMATION>("BOMBER");
            if (plugin::Command<Commands::HAS_ANIMATION_LOADED>("BOMBER"))
            {
                int seq = -1;
                plugin::Command<Commands::OPEN_SEQUENCE_TASK>(&seq);
                if (!a.packet.crouched)
                    plugin::Command<Commands::TASK_TURN_CHAR_TO_FACE_COORD>(-1, a.packet.x, a.packet.y, a.packet.z);
                plugin::Command<Commands::TASK_PLAY_ANIM_NON_INTERRUPTABLE>(-1,
                    a.packet.crouched ? "BOM_PLANT_CROUCH_IN" : "BOM_PLANT_IN", "BOMBER", 4.0f, 0, 0, 0, 0, -1);
                plugin::Command<Commands::TASK_PLAY_ANIM_NON_INTERRUPTABLE>(-1, "BOM_PLANT_LOOP", "BOMBER", 4.0f, 0, 0, 0, 0, -1);
                if (a.packet.action == 1)
                    plugin::Command<Commands::TASK_PLAY_ANIM_NON_INTERRUPTABLE>(-1, "BOM_PLANT_LOOP", "BOMBER", 4.0f, 0, 0, 0, 0, -1);
                plugin::Command<Commands::TASK_PLAY_ANIM_NON_INTERRUPTABLE>(-1,
                    a.packet.crouched ? "BOM_PLANT_CROUCH_OUT" : "BOM_PLANT_2IDLE", "BOMBER", 4.0f, 0, 0, 0, 0, -1);
                if (a.packet.crouched) plugin::Command<Commands::TASK_TOGGLE_DUCK>(-1, 1);
                plugin::Command<Commands::CLOSE_SEQUENCE_TASK>(seq);
                plugin::Command<Commands::PERFORM_SEQUENCE_TASK>(a.handle, seq);
                plugin::Command<Commands::CLEAR_SEQUENCE_TASK>(seq);
                a.started = true;
                if (a.packet.actor == CNetworkPlayerManager::m_localPlayerId && !CCoopMission::IsScriptAuthority())
                {
                    auto ready = a.packet; ready.kind = P::READY;
                    GetPacketFactory().Send(ready);
                }
            }
        }
        ++it;
    }
}
void Cancel(bool release)
{
    if (action.packet.sequence)
    {
        if (release && spec && spec->action == 1 && action.slot >= 0 && action.slot < 5)
            L(108 + action.slot) = 0;
        auto end = action.packet; end.kind = P::END;
        if (CNetwork::m_bAuthenticated) GetPacketFactory().Send(end);
        StopAnimation(end.actor);
        action = Action{};
    }
}
// Catalyst: the script attaches the owner's CJ to the caboose (ATTACH_CHAR_TO_CAR 163@, 0 / -8 / 0.9) so that he stands on
// the moving train; a guest gets the same, a bit further along the caboose, or he is left behind on the track (2026-11-12).
bool trainAttached = false;
DWORD trainTry = 0, trainCheck = 0;
void DetachFromTrain()
{
    if (!trainAttached) return;
    trainAttached = false;
    auto me = FindPlayerPed(0);
    if (me && me->m_pAttachedTo)
    {
        me->DettachPedFromEntity();
        CMissionMirror::LogEventf("guest-action train-detach");
    }
}
void AttachToTrain(CPlayerPed* me)
{
    auto owner = CNetworkPlayerManager::GetPlayer(session.owner);
    CEntity* train = owner && owner->m_pPed ? owner->m_pPed->m_pAttachedTo : nullptr;
    // mission-catalyst-guest-car: the guests drive an extra car of the mission's model instead of riding the train
    const bool want = CConfigLoader::ms_bMissionGuestTrain && !CConfigLoader::ms_bMissionCatalystGuestCar && !session.action &&
        session.pressed && train &&
        train->m_nType == ENTITY_TYPE_VEHICLE && static_cast<CVehicle*>(train)->m_nVehicleSubType == VEHICLE_TRAIN &&
        me->m_fHealth > 0.0f && !me->m_nPedFlags.bInVehicle && me->m_nAreaCode == train->m_nAreaCode;
    if (!want) { DetachFromTrain(); return; }
    const DWORD now = GetTickCount();
    // the owner's own offset on his carriage (the script's 0, -8, 0.9) when his copy holds one; else that default
    CVector base(0.0f, -8.0f, 0.9f);
    if (owner->m_pPed->m_pAttachedTo == train)
    {
        const CVector& o = owner->m_pPed->m_vecTurretOffset;
        if (std::isfinite(o.x) && std::isfinite(o.y) && std::isfinite(o.z) && std::abs(o.x) < 3.0f && std::abs(o.y) < 20.0f && o.z > 0.0f && o.z < 3.0f)
            base = o;
    }
    int rank = 0;
    for (auto p : CNetworkPlayerManager::m_pPlayers)
        if (p->m_pPed && p->m_playerId != session.owner && p->m_playerId < CNetworkPlayerManager::m_localPlayerId && Eligible(p)) ++rank;
    // each guest 1.5 m further along the carriage than the owner and on alternating sides
    const float x = base.x + ((rank % 2 == 0) ? 0.8f : -0.8f), y = base.y + 1.5f * (rank + 1);
    if (me->m_pAttachedTo == train)
    {
        trainAttached = true;
        // the attach must hold: a guest that ends up away from his slot on the copy (the copy was rebuilt or he was
        // moved) is put back; the distance goes to the log as evidence (train-sync-guest)
        if (CConfigLoader::ms_bTrainSyncRail && train->m_matrix && me->m_matrix && now - trainCheck >= 2000)
        {
            trainCheck = now;
            const CVector slot = (*train->GetMatrix()) * CVector(x, y, base.z);
            const float away = (me->GetPosition() - slot).Magnitude();
            CMissionMirror::LogEventf("guest-action train-check away=%.1f train=%.1f,%.1f,%.1f me=%.1f,%.1f,%.1f", away,
                train->GetPosition().x, train->GetPosition().y, train->GetPosition().z, me->GetPosition().x, me->GetPosition().y, me->GetPosition().z);
            if (away > 4.0f) { me->DettachPedFromEntity(); trainTry = 0; }
        }
        if (me->m_pAttachedTo == train) return;
    }
    if (me->m_pAttachedTo || now - trainTry < 1000) return;
    trainTry = now;
    NativeScope scope;
    plugin::Command<Commands::ATTACH_CHAR_TO_CAR>(CPools::GetPedRef(me), CPools::GetVehicleRef(static_cast<CVehicle*>(train)),
        x, y, base.z, 0, 360.0f, 0);
    trainAttached = true;
    CMissionMirror::LogEventf("guest-action train-attach rank=%d at=%.1f,%.1f,%.1f train=%.1f,%.1f,%.1f", rank, x, y, base.z,
        train->GetPosition().x, train->GetPosition().y, train->GetPosition().z);
}
void Reset(bool release)
{
    DetachFromTrain();
    Cancel(release);
    while (!animations.empty()) StopAnimation(animations.begin()->first);
    active = nullptr; spec = nullptr; session = P{}; inputs.clear(); lastAnimation.clear();
    sent = announced = 0; throwing = false; chargeAt = 0;
}
bool GuestSession()
{
    auto owner = CNetworkPlayerManager::GetPlayer(session.owner);
    return Enabled() && !CCoopMission::IsScriptAuthority() && session.epoch &&
        GetTickCount() - heard < 1500 && Eligible(owner) && Owner(owner) && owner->m_nRunningMission == session.mission;
}
void FinishCharge()
{
    const int i = action.slot;
    auto ped = Actor(action.packet.actor);
    if (!ped || i < 0 || i >= 5 || L(108 + i) != 1) { Cancel(true); return; }
    if (!CTheScripts::MissionCleanUp.FindFree()) { Cancel(true); return; }
    NativeScope scope;
    float x, y, z;
    plugin::Command<Commands::GET_OFFSET_FROM_CHAR_IN_WORLD_COORDS>(CPools::GetPedRef(ped), 0.0f, 0.5f, -1.0f, &x, &y, &z);
    int object = -1;
    plugin::Command<Commands::CREATE_OBJECT>(363, x, y, z, &object);
    if (!CPools::GetObject(object)) { Cancel(true); return; }
    CPools::GetObject(object)->m_nObjectType = OBJECT_MISSION;
    CPools::GetObject(object)->m_nAreaCode = ped->m_nAreaCode;
    CTheScripts::MissionCleanUp.AddEntityToList(object, MISSION_CLEANUP_ENTITY_TYPE_OBJECT);
    plugin::Command<Commands::SET_OBJECT_HEADING>(object, 100.0f);
    F(89 + i) = x; F(94 + i) = y; F(99 + i) = z; L(169 + i) = object;
    plugin::Command<Commands::REMOVE_BLIP>(L(140 + i));
    ++L(34);
    CMissionMirror::LogEventf("guest-action complete HEIST2 slot=%d count=%d", i, L(34));
    Cancel(false);
}
bool OnTrain(CPed* ped, CVehicle* train)
{
    if (!ped || !train || ped->m_nPedFlags.bInVehicle || ped->m_nAreaCode != train->m_nAreaCode) return false;
    const CVector d = ped->GetPosition() - train->GetPosition();
    // Reject off-train throws; use the actual caboose's local frame, not world axes.
    const auto& m = train->GetMatrix();
    const float lateral = d.x * m->right.x + d.y * m->right.y + d.z * m->right.z;
    const float along = d.x * m->up.x + d.y * m->up.y + d.z * m->up.z;
    return std::abs(lateral) < 2.0f && std::abs(along) < 10.0f && d.z > 0.0f && d.z < 5.0f;
}
void HostThrow(const P& p)
{
    if (!spec || spec->action || !active || L(89) != 10 || !FindPlayerPed(0)->m_pAttachedTo) return;
    auto train = CPools::GetVehicle(L(163)); auto ped = Actor(p.actor);
    if (!OnTrain(ped, train) || !std::isfinite(p.power) || p.power <= 20 || p.power > 100) return;
    CVector from(p.x, p.y, p.z), dir(p.dx, p.dy, p.dz);
    const float len = dir.Magnitude();
    if (!std::isfinite(len) || len < 0.9f || len > 1.1f || !std::isfinite(from.x) ||
        !std::isfinite(from.y) || !std::isfinite(from.z) || (from - ped->GetPosition()).Magnitude() > 8.0f) return;
    const int i = L(109);
    if (i < 0 || i >= 10) return;
    NativeScope scope;
    if (CPools::GetObject(L(110 + i)))
    {
        CTheScripts::MissionCleanUp.RemoveEntityFromList(L(110 + i), MISSION_CLEANUP_ENTITY_TYPE_OBJECT);
        plugin::Command<Commands::DELETE_OBJECT>(L(110 + i));
    }
    if (!CTheScripts::MissionCleanUp.FindFree()) return;
    CVector to = from + dir;
    int box = -1;
    plugin::Command<Commands::CREATE_OBJECT_NO_OFFSET>(L(127), to.x, to.y, to.z, &box);
    if (!CPools::GetObject(box)) return;
    CPools::GetObject(box)->m_nObjectType = OBJECT_MISSION;
    CPools::GetObject(box)->m_nAreaCode = ped->m_nAreaCode;
    CTheScripts::MissionCleanUp.AddEntityToList(box, MISSION_CLEANUP_ENTITY_TYPE_OBJECT);
    plugin::Command<Commands::SET_OBJECT_RECORDS_COLLISIONS>(box, 1);
    plugin::Command<Commands::SET_OBJECT_DYNAMIC>(box, 1);
    plugin::Command<Commands::SET_OBJECT_COLLISION>(box, 1);
    CVector velocity = dir * (p.power * 0.5f) + train->m_vecMoveSpeed * 50.0f;
    plugin::Command<Commands::SET_OBJECT_VELOCITY>(box, velocity.x, velocity.y, velocity.z);
    L(110 + i) = box; L(166 + i) = L(127); L(109) = (i + 1) % 10;
    CMissionMirror::LogEventf("guest-action throw player=%u slot=%d power=%.1f", p.actor.value(), i, p.power);
}
}

void CMissionGuestActions::OnCommandStart(CRunningScript* s, uint16_t op)
{
    if (s && s->m_bIsMission && op == 0x03A4) Reset(false);
}
void CMissionGuestActions::Process()
{
    const DWORD now = GetTickCount();
    for (auto it = inputs.begin(); it != inputs.end();)
        if (!CNetworkPlayerManager::GetPlayer(it->first)) it = inputs.erase(it); else ++it;
    for (auto it = lastAnimation.begin(); it != lastAnimation.end();)
        if (!CNetworkPlayerManager::GetPlayer(it->first)) it = lastAnimation.erase(it); else ++it;
    if (!Enabled()) { Reset(active && Mission() == active); return; }
    if (CCoopMission::IsScriptAuthority())
    {
        auto s = Mission();
        if (s != active)
        {
            Reset(false);
            if (!s || !(spec = Original(s))) return;
            // Catalyst with the guests' Picador (mission-catalyst-ryder-car): only the owner throws crates; the guests
            // drive Ryder's car instead of riding the caboose, so there is no session for them
            if (spec && spec->action == 0 && CConfigLoader::ms_bMissionCatalystRyderCar)
            {
                active = s;
                spec = nullptr;
                return;
            }
            active = s; session.owner = CNetworkPlayerManager::m_localPlayerId;
            session.mission = CMissionMirror::GetRunningMissionId(); session.epoch = ++epoch;
            if (!session.epoch) session.epoch = ++epoch;
            session.action = spec->action;
        }
        if (!active || !spec) return;
        if (now - announced >= 400)
        {
            announced = now;
            session.pressed = !spec->action && L(89) == 10 && FindPlayerPed(0)->m_pAttachedTo;
            GetPacketFactory().Send(session);
        }
        if (action.packet.sequence)
        {
            auto p = CNetworkPlayerManager::GetPlayer(action.packet.actor);
            if (!Eligible(p) || (p->m_pPed->GetPosition() - CVector(action.packet.x, action.packet.y, action.packet.z)).Magnitude() > 8.0f)
                Cancel(true);
            else if (spec->action == 2 && L(48) != 0) Cancel(false);
            else if (!action.playing && now - action.at >= 3000) Cancel(true);
            else if (action.playing && now - action.at >= 6000)
            {
                if (spec->action == 1) FinishCharge();
                else action.ready = true;
            }
        }
    }
    else
    {
        if (!GuestSession()) { Reset(false); return; }
        auto me = FindPlayerPed(0);
        AttachToTrain(me);
        auto pad = CPad::GetPad(0);
        const bool focused = CTestMode::IsGameWindowFocused() && !CChat::m_bInputActive && !pad->DisablePlayerControls;
        if (now - sent >= 100)
        {
            sent = now; P input = session; input.kind = P::INPUT; input.sequence = ++inputSequence;
            input.pressed = focused && pad->NewState.ButtonTriangle;
            { NativeScope scope; input.crouched = plugin::Command<Commands::IS_CHAR_DUCKING>(CPools::GetPedRef(me)); }
            GetPacketFactory().Send(input);
        }
        const bool hold = !session.action && session.pressed && focused && (pad->NewState.LeftShoulder1 || pad->NewState.ButtonCircle);
        if (hold && !throwing) chargeAt = now;
        if (hold)
        {
            char help[96];
            sprintf_s(help, "Lanzar caja: potencia %d%%. Solta disparo para lanzar.", (std::min)(100, int((now - chargeAt) / 20)));
            CHud::SetHelpMessage(help, true, false, false);
        }
        if (!hold && throwing && focused)
        {
            const auto& camera = TheCamera.m_aCams[TheCamera.m_nActiveCam];
            P p = session; p.kind = P::THROW; p.sequence = ++inputSequence;
            p.x = camera.m_vecSource.x; p.y = camera.m_vecSource.y; p.z = camera.m_vecSource.z;
            p.dx = camera.m_vecFront.x; p.dy = camera.m_vecFront.y; p.dz = camera.m_vecFront.z;
            p.power = (std::min)(100.0f, (now - chargeAt) * 0.05f);
            GetPacketFactory().Send(p);
        }
        throwing = hold;
    }
    Animate();
}

bool CMissionGuestActions::FilterCompare(CRunningScript* s, uint16_t op, bool state, bool& handled)
{
    handled = false;
    if (!Enabled() || COpCodeSync::bProcessingNetworkOpcode || !CCoopMission::IsScriptAuthority() ||
        s != active || !spec || !spec->action || !s->m_bIsMission) return state;
    if (op == 0x00E1 && spec->action == 2 && At(s, spec->button) && action.ready)
    { handled = true; return true; }
    if (op != 0x00FF || !At(s, spec->locate)) return state;
    handled = true;
    // The owner retains the native path. A single reserved guest path serializes placements.
    if (state) return state;
    if (spec->action == 2 && action.ready) return true;
    if (action.packet.sequence) return false;
    const int slot = spec->action == 1 ? L(37) : -1;
    if (spec->action == 1 && (slot < 0 || slot >= 5 || L(108 + slot) != 0 || L(34) >= 5)) return false;
    if (spec->action == 2 && (L(48) != 0 || L(522) == 2)) return false;
    int params[8]; memcpy(params, ScriptParams, sizeof(params));
    if (params[0] != CPools::GetPedRef(FindPlayerPed(0))) return false;
    for (auto& [id, input] : inputs)
    {
        auto p = CNetworkPlayerManager::GetPlayer(id);
        if (!Eligible(p) || GetTickCount() - input.at > 350 || !input.packet.pressed || input.packet.sequence <= input.consumed) continue;
        params[0] = CPools::GetPedRef(p->m_pPed); params[7] = 0;
        if (!COpCodeSync::ProbeCondition(s, op, params, 8)) continue;
        input.consumed = input.packet.sequence;
        P begin = session; begin.kind = P::BEGIN; begin.actor = id; begin.sequence = ++sequence;
        if (!begin.sequence) begin.sequence = ++sequence;
        begin.x = spec->action == 1 ? F(89 + slot) : -1372.733f;
        begin.y = spec->action == 1 ? F(94 + slot) : 1495.55f;
        begin.z = spec->action == 1 ? F(99 + slot) : 0.8578f;
        begin.crouched = input.packet.crouched;
        action = {begin, GetTickCount(), slot, false};
        if (slot >= 0) L(108 + slot) = 1;
        GetPacketFactory().Send(begin); BeginAnimation(begin);
        CMissionMirror::LogEventf("guest-action begin %.8s player=%u slot=%d", s->m_szName, id.value(), slot);
        return false;
    }
    return false;
}

void CMissionGuestActions::Receive(const P& p)
{
    if (!Enabled() || !p.epoch) return;
    auto sender = CNetworkPlayerManager::GetPlayer(p.senderPlayerid);
    if (!Eligible(sender)) return;
    if (p.kind == P::SESSION)
    {
        if (CCoopMission::IsScriptAuthority() || !Owner(sender) || p.owner != sender->m_playerId ||
            p.mission != sender->m_nRunningMission) return;
        if (session.epoch != p.epoch || session.owner != p.owner || session.mission != p.mission) Reset(false);
        session = p; heard = GetTickCount();
    }
    else if (p.owner == session.owner && p.epoch == session.epoch && p.mission == session.mission)
    {
        if ((p.kind == P::INPUT || p.kind == P::THROW || p.kind == P::READY) && CCoopMission::IsScriptAuthority() && active && p.actor == sender->m_playerId)
        {
            if (p.kind == P::READY && !action.playing && p.actor == action.packet.actor &&
                p.sequence == action.packet.sequence)
            { action.playing = true; action.at = GetTickCount(); }
            auto& input = inputs[p.actor];
            if (p.kind == P::INPUT && p.sequence > input.packet.sequence)
            { input.packet = p; input.at = GetTickCount(); }
            if (p.kind == P::THROW && p.sequence > input.thrown)
            {
                input.thrown = p.sequence;
                if (GetTickCount() - input.throwAt >= 500)
                { input.throwAt = GetTickCount(); HostThrow(p); }
            }
        }
        else if (!CCoopMission::IsScriptAuthority() && Owner(sender) && p.owner == sender->m_playerId)
        {
            if (p.kind == P::BEGIN && p.action == session.action && p.action) BeginAnimation(p);
            if (p.kind == P::END && p.sequence >= lastAnimation[p.actor])
            { lastAnimation[p.actor] = p.sequence; StopAnimation(p.actor); }
        }
    }
}
