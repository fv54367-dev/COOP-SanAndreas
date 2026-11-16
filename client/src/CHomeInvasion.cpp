#include "stdafx.h"
#include "CHomeInvasion.h"
#include "CObjectSync.h"
#include "CTestMode.h"
#include "CCoopMission.h"
#include "CMissionMode.h"
#include "CMissionMirror.h"
#include "COpCodeSync.h"
#include "Commands/CommandHooks.h"
#include "network/packets/home_invasion.h"
#include <CUserDisplay.h>
#include <algorithm>
#include <cmath>
#include <map>

// Adapted from Karimov-Akbar/sa-coop Missions.cpp (MIT): mission look and UpdateOwnNoise.
// gta-reversed: PedClothesDesc::GetIsWearingBalaclava, CClothes::RebuildPlayer,
// CPlayerPed::ProcessAnimGroups; Events/EventGlobalGroup.cpp::GetSoundLevel.
namespace
{
using P = Packets::Scripts::HomeInvasion;
constexpr uint32_t noiseVar = 26820;
P session;
CRunningScript* active = nullptr;
uint32_t nextEpoch = 0, inputSequence = 0;
DWORD announced = 0, heard = 0, sent = 0, lastFrame = 0;
uint32_t noiseTotal = 0;
bool ownLook = false;
uint32_t ownModel = 0, ownTexture = 0;
// the mask was put on this guest by the mission: it is taken off (with the stealth walk it brings) even if ownLook got lost
bool maskGiven = false;
DWORD maskCleanAt = 0;
int dressedPed = -1;
struct Input { uint32_t sequence = 0; DWORD at = 0; uint32_t noiseTotal = 0; float pendingNoise = 0; bool holding = false; DWORD enterAt = 0; };
// One slot per crate of the script: every player carries his own crate at the same time.
struct Applied { uint32_t crate = 0; playerid_t holder; DWORD liftedAt = 0; bool liftable = false; CVector position; uint8_t area = 0; };
Applied applied[7];
bool wasEnter = false, atTruck = false, woken = false;
int barSlot = -1;
float outsideNoise = 0;
std::map<playerid_t, Input> inputs;

// Local commands must not replace the captured parameters of a mission opcode.
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
bool Enabled()
{
    return CConfigLoader::ms_bCoopHomeInvasion && CConfigLoader::ms_bVanillaScripts &&
        CNetwork::m_bAuthenticated && !CMissionMode::IsIndividual() &&
        !CCoopMission::IsLocalIndividualMission() && !CCoopMission::HasLocalLeft();
}
bool Eligible(CNetworkPlayer* p)
{
    return p && !p->m_bIndividualMode && !p->m_bDead && p->m_pPed &&
        p->m_pPed->IsVTableValid() && p->m_pPed->m_fHealth > 0 &&
        CCoopMission::ShouldIncludeNetworkPlayer(p);
}
bool Owner(CNetworkPlayer* p)
{
    return p && (CCoopMission::IsRunning() ? p->m_playerId == CCoopMission::GetOwnerId() : p->m_bIsHost);
}
bool GuestSession()
{
    auto owner = CNetworkPlayerManager::GetPlayer(session.owner);
    return Enabled() && !CCoopMission::IsScriptAuthority() && !COpCodeSync::IsLocalMissionRunning() &&
        session.active && session.epoch && GetTickCount() - heard < 1500 &&
        Owner(owner) && Eligible(owner) && owner->m_nRunningMission == 24;
}
bool House(CPed* me);
bool HouseStage();
// Adapted from Karimov-Akbar/sa-coop Objects.cpp (MIT): Held/TakeUp/FollowHolder.
// gta-reversed TaskManager::FindActiveTaskByType; Ped::DropEntityThatThisPedIsHolding.
CEntity* Held(CPed* ped)
{
    if (!ped || !ped->m_pIntelligence) return nullptr;
    for (int type : {307, 308, 309})
        if (auto task = plugin::CallMethodAndReturn<void*, 0x681740>(
            reinterpret_cast<uint8_t*>(ped->m_pIntelligence) + 4, type))
            return *reinterpret_cast<CEntity**>(reinterpret_cast<uint8_t*>(task) + 8);
    return nullptr;
}
CEntity* Taking(CPed* ped)
{
    if (auto held = Held(ped)) return held;
    if (!ped || !ped->m_pIntelligence) return nullptr;
    auto task = ped->m_pIntelligence->m_TaskMgr.FindActiveTaskByType(310);
    return task ? *reinterpret_cast<CEntity**>(reinterpret_cast<uint8_t*>(task) + 12) : nullptr;
}
CPed* HolderPed(playerid_t id)
{
    if (id == CNetworkPlayerManager::m_localPlayerId) return FindPlayerPed(0);
    auto player = CNetworkPlayerManager::GetPlayer(id);
    return Eligible(player) ? player->m_pPed : nullptr;
}
CObject* Object(uint32_t id)
{
    if (!id) return nullptr;
    if (!CCoopMission::IsScriptAuthority()) return CObjectSync::Resolve(id);
    for (auto [net, object] : CObjectSync::GetPhotoTargets(true))
        if (net == id) return object;
    return nullptr;
}
int CrateIndex(uint32_t id)
{
    if (id)
        for (int i = 0; i < 7; ++i) if (session.crates[i] == id) return i;
    return -1;
}
bool HoldsAny(playerid_t id)
{
    for (auto& holder : session.holders) if (holder == id) return true;
    return false;
}
uint32_t OwnCrate()
{
    for (int i = 0; i < 7; ++i)
        if (session.holders[i] == CNetworkPlayerManager::m_localPlayerId) return session.crates[i];
    return 0;
}
void Drop(CPed* ped, CObject* object)
{
    if (!ped || !object || !ped->m_pIntelligence) return;
    if (Held(ped) == object) plugin::CallMethod<0x5E0360>(ped, false);
    // A revoked pickup may still be approaching its target, before Held exists.
    auto& manager = ped->m_pIntelligence->m_TaskMgr;
    for (int i = 0; i < 5; ++i)
    {
        auto root = manager.m_aPrimaryTasks[i];
        for (auto task = root; task; task = task->GetSubTask())
            if (task->GetTaskType() == 310 &&
                *reinterpret_cast<CEntity**>(reinterpret_cast<uint8_t*>(task) + 12) == object)
            {
                if (root->MakeAbortable(ped, ABORT_PRIORITY_IMMEDIATE, nullptr)) manager.SetTask(nullptr, i, false);
                break;
            }
    }
}
void Release(int i)
{
    auto& a = applied[i];
    auto object = Object(a.crate);
    if (object)
    {
        Drop(FindPlayerPed(0), object);
        for (auto player : CNetworkPlayerManager::m_pPlayers)
            if (player && player->m_pPed && player->m_pPed->IsVTableValid()) Drop(player->m_pPed, object);
        if (CCoopMission::IsScriptAuthority() && a.holder != session.owner)
        {
            CWorld::Remove(object);
            auto position = a.position;
            if (auto collision = object->GetColModel()) position.z -= 1.0f + collision->m_boundBox.m_vecMin.z;
            object->SetPosn(position);
            object->m_nAreaCode = a.area;
            object->UpdateRwMatrix(); object->UpdateRwFrame(); CWorld::Add(object);
            object->m_nObjectFlags.bIsLiftable = true;
            object->m_bUsesCollision = true;
        }
        object->m_bUsesCollision = true;
        if (!CCoopMission::IsScriptAuthority())
        {
            object->SetIsStatic(true);
            object->m_nPhysicalFlags.bDisableMoveForce = true;
            object->m_nPhysicalFlags.bInfiniteMass = true;
        }
        else if (a.liftable) object->m_nObjectFlags.bIsLiftable = true;
    }
    a = Applied{};
}
void ReleaseAll()
{
    for (int i = 0; i < 7; ++i) Release(i);
    atTruck = false;
}
void TakeUp(CPed* ped, CObject* object)
{
    if (!ped || !ped->m_pIntelligence) return;
    auto task = plugin::CallAndReturn<void*, 0x61A5A0>(0x34);
    if (!task) return;
    plugin::CallMethod<0x6919C0>(task, static_cast<CEntity*>(object), 81);
    alignas(4) uint8_t event[0x18];
    plugin::CallMethod<0x4B0A00>(event, 3, task, false);
    plugin::CallMethodAndReturn<void*, 0x4AB420>(reinterpret_cast<uint8_t*>(ped->m_pIntelligence) + 0x68, event, false);
    plugin::CallMethod<0x4B0A50>(event);
}
void Follow(int i)
{
    auto& a = applied[i];
    const uint32_t crate = session.crates[i];
    const playerid_t holder = session.holders[i];
    if (a.crate && (a.crate != crate || a.holder != holder)) Release(i);
    if (!holder.valid()) return;
    auto object = Object(crate);
    auto ped = HolderPed(holder);
    if (!object || !ped) return;
    a.position = ped->GetPosition(); a.area = ped->m_nAreaCode;
    if (!a.crate)
    {
        a.crate = crate; a.holder = holder; a.liftedAt = GetTickCount();
        a.liftable = object->m_nObjectFlags.bIsLiftable != 0;
        object->m_nObjectFlags.bIsLiftable = false;
        object->m_nPhysicalFlags.bDisableMoveForce = false;
        object->m_nPhysicalFlags.bInfiniteMass = false;
        object->SetIsStatic(false);
        object->m_bUsesCollision = false;
        if (holder != CNetworkPlayerManager::m_localPlayerId) TakeUp(ped, object);
        else if (!CCoopMission::IsScriptAuthority()) TakeUp(ped, object);
    }
    object->m_nAreaCode = ped->m_nAreaCode;
    // Remote copies can be busy; the reference's fallback follows the carrier's chest.
    if (holder != CNetworkPlayerManager::m_localPlayerId &&
        Held(ped) != object && GetTickCount() - a.liftedAt > 2000)
    {
        RwV3d bone;
        ped->GetBonePosition(bone, 24, true);
        CVector pos(bone.x, bone.y, bone.z);
        CWorld::Remove(object); object->SetPosn(pos);
        object->UpdateRwMatrix(); object->UpdateRwFrame(); CWorld::Add(object);
    }
}
void FollowAll()
{
    for (int i = 0; i < 7; ++i) Follow(i);
}
void AnnounceCrate()
{
    ++session.sequence; GetPacketFactory().Send(session); announced = GetTickCount();
}
void FreeCrate(int i)
{
    CMissionMirror::LogEventf("home-invasion release epoch=%u crate=%u holder=%u", session.epoch, session.crates[i], session.holders[i].value());
    Release(i); session.holders[i] = playerid_t{};
    if (session.active && CNetwork::m_bAuthenticated) AnnounceCrate();
}
void OwnerCrates()
{
    auto locals = CTheScripts::LocalVariablesForCurrentMission;
    auto me = FindPlayerPed(0);
    session.liftable = 0;
    for (int i = 0; i < 7; ++i)
    {
        auto object = CPools::GetObject(locals[117 + i].iParam);
        session.crates[i] = object ? CObjectSync::GetNetworkId(locals[117 + i].iParam) : 0;
        if (object && object->m_nObjectFlags.bIsLiftable) session.liftable |= 1 << i;
        // The owner's own native pickup claims the crate unless a guest already holds it.
        if (object && session.crates[i] && !session.holders[i].valid() && Taking(me) == object)
            session.holders[i] = session.owner;
    }
    for (int i = 0; i < 7; ++i)
    {
        const auto holder = session.holders[i];
        if (!holder.valid()) continue;
        auto object = Object(session.crates[i]);
        if (!object) { FreeCrate(i); continue; }
        if (holder == session.owner)
        {
            if (Taking(me) != object) FreeCrate(i);
            continue;
        }
        auto player = CNetworkPlayerManager::GetPlayer(holder);
        auto it = inputs.find(holder);
        if (!Eligible(player) || it == inputs.end() || GetTickCount() - it->second.at > 1500 ||
            (!it->second.holding && GetTickCount() - applied[i].liftedAt > 3000)) FreeCrate(i);
    }
    FollowAll();
}
// The script drops the bar and its noise block when the owner leaves the house; keep the ONE shared bar
// for the guests still inside (a counter of the owner's HUD, mirrored to everybody).
void ShowBar()
{
    auto& timer = CUserDisplay::OnscnTimer;
    for (auto& counter : timer.m_aCounters)
        if (counter.m_nVarId == noiseVar) return;
    for (int slot = 0; slot < 4; ++slot)
        if (!timer.m_aCounters[slot].m_nVarId)
        {
            char key[] = "GUNS1_5";
            const uint8_t type = 1; // line (bar)
            timer.AddCounter(noiseVar, type, key, static_cast<unsigned short>(slot));
            timer.m_aCounters[slot].m_bFlashWhenFirstDisplayed = false;
            barSlot = slot;
            return;
        }
}
void ClearBar()
{
    if (barSlot < 0) return;
    auto& counter = CUserDisplay::OnscnTimer.m_aCounters[barSlot];
    if (counter.m_nVarId == noiseVar)
    {
        // Same fields as COnscreenCounterEntry::Clear (gta-reversed), only our slot.
        counter.m_nVarId = 0; counter.m_nMaxVarValue = 0; counter.m_nType = 0; counter.m_bEnabled = false;
        memset(counter.m_szDescriptionTextKey, 0, sizeof(counter.m_szDescriptionTextKey));
    }
    barSlot = -1;
}
bool GuestInHouse(CNetworkPlayer* player, const Input& input)
{
    return GetTickCount() - input.at < 600 && Eligible(player) && House(player->m_pPed);
}
void OutsideNoise(CPed* me)
{
    bool inside = false;
    if (HouseStage() && CTheScripts::LocalVariablesForCurrentMission[94].iParam == 0 && me->m_nAreaCode == 0)
        for (auto& [id, input] : inputs)
            if (GuestInHouse(CNetworkPlayerManager::GetPlayer(id), input)) inside = true;
    if (!inside) { ClearBar(); outsideNoise = 0; return; }
    if (barSlot < 0) { ShowBar(); outsideNoise = 0; }
    // Same per-frame rule as the script: add what the guests made, then decay by 0.99.
    float total = outsideNoise * 0.99f;
    for (auto& [id, input] : inputs)
    {
        if (GuestInHouse(CNetworkPlayerManager::GetPlayer(id), input)) total += input.pendingNoise * 0.99f;
        input.pendingNoise = 0;
    }
    outsideNoise = std::min(total, 1000.0f);
    *reinterpret_cast<int*>(&CTheScripts::ScriptSpace[noiseVar]) = std::min(static_cast<int>(outsideNoise), 100);
    if (outsideNoise >= 100.0f && !woken)
    {
        woken = true; // the sleeper wakes up: the mission fails for everybody, as inside the house
        CMissionMirror::LogEventf("home-invasion woke-outside epoch=%u", session.epoch);
        CCoopMission::RequestMissionFail();
    }
}
CPedClothesDesc* Clothes()
{
    auto me = FindPlayerPed(0);
    return me && me->m_pPlayerData ? me->m_pPlayerData->m_pPedClothesDesc : nullptr;
}
uint32_t MaskKey() { return CKeyGen::GetUppercaseKey("BALACLAVA"); }
void Dress(bool on)
{
    auto me = FindPlayerPed(0);
    auto clothes = Clothes();
    if (!me || !clothes || !me->m_pRwClump) return;
    if (ownLook && CPools::GetPedRef(me) != dressedPed)
        ownLook = false; // A replacement player has its own saved profile.
    if (on)
    {
        if (ownLook || me->m_nPedFlags.bInVehicle || !me->IsAlive()) return;
        if (clothes->m_anModelKeys[9] == MaskKey()) return;
        ownModel = clothes->m_anModelKeys[9]; ownTexture = clothes->m_anTextureKeys[17];
        dressedPed = CPools::GetPedRef(me); ownLook = true; maskGiven = true;
        clothes->SetTextureAndModel("BALACLAVA", "BALACLAVA", 17);
    }
    else
    {
        if (!ownLook) return;
        // Do not overwrite a later wardrobe/load change that replaced our mask.
        if (clothes->m_anModelKeys[9] == MaskKey()) clothes->m_anModelKeys[9] = ownModel;
        if (clothes->m_anTextureKeys[17] == MaskKey()) clothes->m_anTextureKeys[17] = ownTexture;
        ownLook = false;
    }
    CClothes::RebuildPlayer(me, false);
    CMissionMirror::LogEventf("home-invasion mask=%d", on);
}
void CleanGuest()
{
    Dress(false);
    // Mission over for this guest: no mask and no stealth walk left (2026-11-12). Retried twice a second until the
    // mask is gone, because a respawn, a cutscene rebuild or the script's own clothes call can bring it back.
    if (!maskGiven) return;
    auto me = FindPlayerPed(0);
    auto clothes = Clothes();
    const DWORD now = GetTickCount();
    if (!me || !clothes || !me->m_pRwClump || now - maskCleanAt < 500) return;
    maskCleanAt = now;
    if (clothes->m_anModelKeys[9] == MaskKey() || clothes->m_anTextureKeys[17] == MaskKey())
    {
        if (clothes->m_anModelKeys[9] == MaskKey()) clothes->m_anModelKeys[9] = ownModel;
        if (clothes->m_anTextureKeys[17] == MaskKey()) clothes->m_anTextureKeys[17] = ownTexture;
        CClothes::RebuildPlayer(me, false);
        CMissionMirror::LogEventf("home-invasion mask-cleanup");
        return;
    }
    // the stealth walk is the anim group of the masked CJ: back to the normal one (ProcessAnimGroups sets the
    // weapon variants again on the next frame)
    if (me->m_nAnimGroup != ANIM_GROUP_PLAYER && !me->m_nPedFlags.bInVehicle)
    {
        me->m_nAnimGroup = ANIM_GROUP_PLAYER;
        me->SetMoveAnim();
        CMissionMirror::LogEventf("home-invasion stealth-cleanup");
    }
    maskGiven = false;
}
void EndOwner()
{
    if (active && session.epoch && CNetwork::m_bAuthenticated)
    {
        session.active = false; ++session.sequence;
        GetPacketFactory().Send(session);
    }
    ReleaseAll();
    for (auto& holder : session.holders) holder = playerid_t{};
    ClearBar(); outsideNoise = 0; woken = false;
    active = nullptr; inputs.clear();
}
bool Original(CRunningScript* s)
{
    if (!s || !s->m_bIsMission || _strnicmp(s->m_szName, "guns1", 8) ||
        s->m_pBaseIP != reinterpret_cast<uint8_t*>(0xA7A6A0) || !COpCodeSync::IsScriptSynced(s)) return false;
    uint32_t hash = 2166136261u;
    for (uint32_t i = 0; i < 41159; ++i) hash = (hash ^ s->m_pBaseIP[i]) * 16777619u;
    return hash == 0xDA1FBDD4;
}
bool House(CPed* me)
{
    if (!me || me->m_nAreaCode != 8) return false;
    auto pos = me->GetPosition();
    return pos.x > 2750 && pos.x < 2860 && pos.y > -1220 && pos.y < -1100 && pos.z > 980 && pos.z < 1080;
}
bool HouseStage()
{
    auto locals = CTheScripts::LocalVariablesForCurrentMission;
    return locals[51].iParam == 6 && locals[96].iParam == 0;
}
}


void CHomeInvasion::StripMissionLook(CPedClothesDesc& clothes)
{
    auto me = FindPlayerPed(0);
    if (!ownLook || !me || CPools::GetPedRef(me) != dressedPed) return;
    if (clothes.m_anModelKeys[9] == MaskKey()) clothes.m_anModelKeys[9] = ownModel;
    if (clothes.m_anTextureKeys[17] == MaskKey()) clothes.m_anTextureKeys[17] = ownTexture;
}


void CHomeInvasion::OnCommandStart(CRunningScript* s, uint16_t op)
{
    if (s && s->m_bIsMission && op == 0x03A4 && !COpCodeSync::bProcessingNetworkOpcode)
        EndOwner(); // SCRIPT_NAME also runs when the same slot is reused on retry.
    if (Enabled() && s == active && op == 0x03C4)
        ClearBar(); // the script shows its own bar again; never leave two
    if (Enabled() && s == active && op == 0x0108 &&
        CommandHooks::GetCurrentCommandIP() == s->m_pBaseIP + 0xBD67A - 0xB69A0)
    {
        const int index = CTheScripts::LocalVariablesForCurrentMission[36].iParam;
        if (index >= 0 && index < 7) BeforeObjectDelete(Object(session.crates[index]));
    }
}
void CHomeInvasion::OnCommandExecuted(CRunningScript* s, uint16_t op)
{
    if (Enabled() && s == active && !COpCodeSync::bProcessingNetworkOpcode &&
        CCoopMission::IsScriptAuthority())
    {
        const auto ip = CommandHooks::GetCurrentCommandIP();
        const int index = CTheScripts::LocalVariablesForCurrentMission[36].iParam;
        if ((ip == s->m_pBaseIP + 0xBAA62 - 0xB69A0 || ip == s->m_pBaseIP + 0xBAD13 - 0xB69A0) &&
            index >= 0 && index < 7 && session.holders[index].valid() && session.holders[index] != session.owner)
            Follow(index);
    }
    if (!Enabled() || COpCodeSync::bProcessingNetworkOpcode || !CCoopMission::IsScriptAuthority() ||
        !active || s != active || op != 0x0090 || !HouseStage() ||
        CommandHooks::GetCurrentCommandIP() != s->m_pBaseIP + 0xBAD5A - 0xB69A0) return;
    // Add each guest's unconsumed contribution to the native accumulator, not only its HUD.
    auto& value = CTheScripts::LocalVariablesForCurrentMission[167].fParam;
    for (auto& [id, input] : inputs)
    {
        auto player = CNetworkPlayerManager::GetPlayer(id);
        if (CTheScripts::LocalVariablesForCurrentMission[94].iParam == 1 &&
            GetTickCount() - input.at < 600 && Eligible(player) && House(player->m_pPed))
            value = std::min(value + input.pendingNoise * 0.99f, 1000.0f);
        input.pendingNoise = 0;
    }
    *reinterpret_cast<int*>(&CTheScripts::ScriptSpace[noiseVar]) = static_cast<int>(value);
}
void CHomeInvasion::Process()
{
    auto me = FindPlayerPed(0);
    if (!Enabled() || !me || !me->IsAlive())
    {
        EndOwner(); ReleaseAll(); CleanGuest();
        if (!CNetwork::m_bAuthenticated) session = P{};
        return;
    }
    const DWORD now = GetTickCount();
    if (CCoopMission::IsScriptAuthority())
    {
        CleanGuest();
        CRunningScript* mission = nullptr;
        for (auto s = CTheScripts::pActiveScripts; s; s = s->m_pNext)
            if (s->m_bIsMission && !_strnicmp(s->m_szName, "guns1", 8)) { mission = s; break; }
        if (mission != active)
        {
            EndOwner();
            if (!Original(mission)) return;
            active = mission;
            session = P{}; session.snapshot = true; session.active = true;
            session.owner = CNetworkPlayerManager::m_localPlayerId;
            session.epoch = ++nextEpoch;
            if (!session.epoch) session.epoch = ++nextEpoch;
            announced = now - 200;
        }
        if (!active) return;
        if (!HouseStage())
            for (auto& [id, input] : inputs) input.pendingNoise = 0;
        OwnerCrates();
        OutsideNoise(me);
        if (now - announced >= 200)
        {
            announced = now;
            session.mask = Clothes() && Clothes()->m_anModelKeys[9] == MaskKey();
            session.house = HouseStage(); // also with the owner outside: guests inside keep adding noise
            ++session.sequence;
            GetPacketFactory().Send(session);
        }
        return;
    }
    if (!GuestSession()) { ReleaseAll(); CleanGuest(); return; }
    FollowAll();
    Dress(session.mask);
    const bool inside = session.house && House(me);
    // Sample once per game frame; the owner script applies decay to the shared accumulator.
    if (lastFrame != CTimer::m_snTimeInMilliseconds)
    {
        lastFrame = CTimer::m_snTimeInMilliseconds;
        if (inside)
        {
            NativeScope scope;
            float level = 0;
            auto pos = me->GetPosition();
            // only the guest's own noise: with -1 the host's sample and every guest's would count the same shots again
            plugin::Command<Commands::GET_SOUND_LEVEL_AT_COORDS>(CPools::GetPedRef(me), pos.x, pos.y, pos.z, &level);
            level = std::isfinite(level) ? level * 1.6f : 0.0f;
            if (level > 20) noiseTotal += static_cast<uint32_t>(std::min(level - 20, 1000.0f) * 1000.0f);
        }
    }
    const bool enterDown = CTestMode::IsGameWindowFocused() && CPad::GetPad(0)->NewState.ButtonTriangle;
    const bool press = enterDown && !wasEnter;
    wasEnter = enterDown;
    if (now - sent >= 100 || press)
    {
        sent = now;
        P input; input.owner = session.owner; input.epoch = session.epoch;
        input.sequence = ++inputSequence; input.noiseTotal = noiseTotal;
        const uint32_t mine = OwnCrate();
        input.crate = mine;
        input.holding = mine && Held(me) == Object(mine);
        input.enter = press;
        if (press && !mine && !Held(me) && !me->m_nPedFlags.bInVehicle)
        {
            for (int i = 0; i < 7; ++i)
                if (auto object = Object(session.crates[i]); object && (session.liftable & (1 << i)) &&
                    !session.holders[i].valid() &&
                    object->m_nAreaCode == me->m_nAreaCode && (object->GetPosition() - me->GetPosition()).Magnitude() < 1.8f)
                { input.crate = session.crates[i]; input.request = true; break; }
        }
        GetPacketFactory().Send(input);
    }
}
void CHomeInvasion::Receive(const P& p)
{
    if (!Enabled() || !p.epoch || !p.sequence) return;
    auto sender = CNetworkPlayerManager::GetPlayer(p.senderPlayerid);
    if (!Eligible(sender)) return;
    if (p.snapshot)
    {
        if (CCoopMission::IsScriptAuthority() || !Owner(sender) || p.owner != sender->m_playerId ||
            sender->m_nRunningMission != 24 || COpCodeSync::IsLocalMissionRunning()) return;
        if (session.owner == p.owner && (p.epoch < session.epoch ||
            (p.epoch == session.epoch && p.sequence <= session.sequence))) return;
        if (p.owner != session.owner || p.epoch != session.epoch) { ReleaseAll(); CleanGuest(); noiseTotal = 0; }
        session = p; heard = GetTickCount();
        if (!p.active) { ReleaseAll(); CleanGuest(); }
    }
    else if (CCoopMission::IsScriptAuthority() && active && session.active && p.owner == session.owner && p.epoch == session.epoch)
    {
        auto& input = inputs[sender->m_playerId];
        if (p.sequence <= input.sequence) return;
        if (GetTickCount() - input.at >= 600) input.pendingNoise = 0;
        const uint32_t delta = p.noiseTotal - input.noiseTotal;
        input.noiseTotal = p.noiseTotal;
        input.sequence = p.sequence; input.at = GetTickCount();
        if (HouseStage() && House(sender->m_pPed))
            input.pendingNoise = std::min(input.pendingNoise + std::min(delta / 1000.0f, 1000.0f), 1000.0f);
        else input.pendingNoise = 0;
        const int held = CrateIndex(p.crate);
        if (held >= 0 && session.holders[held] == sender->m_playerId)
        {
            input.holding = p.holding;
            if (p.enter) input.enterAt = GetTickCount();
        }
        if (p.request && HouseStage())
        {
            // First confirmed request wins; every player may carry his own crate meanwhile.
            const int i = CrateIndex(p.crate);
            auto object = Object(p.crate);
            if (i >= 0 && object && !session.holders[i].valid() && (session.liftable & (1 << i)) &&
                object->m_nObjectFlags.bIsLiftable && !HoldsAny(sender->m_playerId) &&
                Taking(FindPlayerPed(0)) != object &&
                object->m_nAreaCode == sender->m_pPed->m_nAreaCode &&
                !sender->m_pPed->m_nPedFlags.bInVehicle &&
                (object->GetPosition() - sender->m_pPed->GetPosition()).Magnitude() < 2.5f)
            {
                session.holders[i] = sender->m_playerId;
                input.holding = true; input.enterAt = 0;
                CMissionMirror::LogEventf("home-invasion grant epoch=%u crate=%u holder=%u", session.epoch, session.crates[i], session.holders[i].value());
                Follow(i); AnnounceCrate();
            }
        }
    }
}

bool CHomeInvasion::IsCarried(uint32_t id)
{
    if (!id) return false;
    for (auto& a : applied) if (a.crate == id) return true;
    const int i = CrateIndex(id);
    return i >= 0 && session.active && session.holders[i].valid() &&
        (CCoopMission::IsScriptAuthority() || GuestSession());
}
void CHomeInvasion::BeforeObjectDelete(CObject* object)
{
    if (!object) return;
    for (int i = 0; i < 7; ++i)
        if (applied[i].crate && object == Object(applied[i].crate))
        {
            if (CCoopMission::IsScriptAuthority()) FreeCrate(i);
            else Release(i);
        }
}
bool CHomeInvasion::SuppressGather(CRunningScript* script)
{
    if (!CConfigLoader::ms_bCoopHomeInvasion || !CConfigLoader::ms_bVanillaScripts) return false;
    if (!script)
        for (auto s = CTheScripts::pActiveScripts; s; s = s->m_pNext)
            if (s->m_bIsMission && !_strnicmp(s->m_szName, "guns1", 8)) { script = s; break; }
    if (!script || !script->m_bIsMission || _strnicmp(script->m_szName, "guns1", 8)) return false;
    // Inside the house, or from the entry to the delivery (51@ 3..6): each player enters/leaves by himself.
    auto me = FindPlayerPed(0);
    const int stage = CTheScripts::LocalVariablesForCurrentMission[51].iParam;
    return (me && me->m_nAreaCode != 0) || (stage >= 3 && stage <= 6);
}
bool CHomeInvasion::FilterCompare(CRunningScript* s, uint16_t op, bool state, bool& handled)
{
    handled = false;
    if (!Enabled() || COpCodeSync::bProcessingNetworkOpcode || s != active ||
        !CCoopMission::IsScriptAuthority()) return state;
    // A guest that confirmed his crate: eligible, still reporting and holding it.
    auto guestHolding = [](int i) -> CNetworkPlayer*
    {
        const auto holder = session.holders[i];
        if (!holder.valid() || holder == session.owner) return nullptr;
        auto player = CNetworkPlayerManager::GetPlayer(holder);
        auto input = inputs.find(holder);
        if (!Eligible(player) || input == inputs.end() || !input->second.holding ||
            GetTickCount() - input->second.at > 1500) return nullptr;
        return player;
    };
    const auto p = reinterpret_cast<const int*>(ScriptParams);
    const int cj = *reinterpret_cast<int*>(&CTheScripts::ScriptSpace[12]);
    if ((op == 0x0737 || op == 0x0475) && p[0] == cj)
    {
        // Several crates may be carried: answer for the one the script asks about.
        auto object = CPools::GetObject(p[1]);
        for (int i = 0; object && i < 7; ++i)
            if (guestHolding(i) && Object(session.crates[i]) == object) { handled = true; return true; }
        return state;
    }
    static const uint8_t truck[] = {0xFF,0x00,0x02,0x0C,0x00,0x03,0x9A,0x00,0x03,0x9B,0x00,0x06,
        0x00,0x00,0xC0,0x41,0x06,0x00,0x00,0xC0,0x3F,0x06,0x00,0x00,0xC0,0x3F,0x06,0x00,0x00,0x00,0x40,0x04,0x00};
    const auto locals = CTheScripts::LocalVariablesForCurrentMission;
    const int index = locals[36].iParam;
    if (index < 0 || index > 6 || locals[60].iParam != 1 || !session.holders[index].valid() ||
        CObjectSync::GetNetworkId(locals[117 + index].iParam) != session.crates[index]) return state;
    const auto ip = CommandHooks::GetCurrentCommandIP();
    const bool ownerCrate = session.holders[index] == session.owner;
    if (op == 0x00FF && p[0] == cj && ip == s->m_pBaseIP + 0xBD1DE - 0xB69A0 && !memcmp(ip, truck, sizeof(truck)))
    {
        // The owner's own delivery must not wait for guests who are inside (T91): native answer, no wait rule.
        if (ownerCrate) { handled = true; return state; }
        auto player = guestHolding(index);
        if (!player) return state;
        float f[6]; memcpy(f, p + 1, sizeof(f));
        auto pos = player->m_pPed->GetPosition();
        atTruck = !player->m_pPed->m_nPedFlags.bInVehicle && player->m_pPed->m_nAreaCode == 0 &&
            std::fabs(pos.x - f[0]) <= f[3] && std::fabs(pos.y - f[1]) <= f[4] && std::fabs(pos.z - f[2]) <= f[5];
        handled = true; return atTruck;
    }
    if (op == 0x00E1 && p[0] == 0 && p[1] == 15 && ip == s->m_pBaseIP + 0xBD264 - 0xB69A0 && !ownerCrate)
    {
        if (!guestHolding(index)) return state;
        auto& input = inputs[session.holders[index]];
        handled = true;
        const bool pressed = atTruck && input.enterAt && GetTickCount() - input.enterAt < 1000;
        atTruck = false;
        if (pressed) input.enterAt = 0;
        return pressed;
    }
    return state;
}
