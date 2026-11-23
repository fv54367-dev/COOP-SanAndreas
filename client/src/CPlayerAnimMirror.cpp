#include "stdafx.h"
#include "CPlayerAnimMirror.h"

#include <algorithm>
#include <unordered_map>
#include <vector>
#include <eAnimations.h>
#include <CAnimBlendHierarchy.h>
#include "CCutscenePartners.h"
#include "CMissionMirror.h"
#include "CCoopMission.h"

// Same idea as npc-anim-mirror / npc-named-anim-sync (CNetworkPed.cpp), per player. The helpers below are small copies
// of the static ones there, so CNetworkPed.cpp stays untouched. Evidence (gta-reversed Animation/):
//  - CAnimManager::BlendAnimation retires every other non-partial association of the clump (and, for a partial one,
//    the other partials), so a full-body action replaces the walk/idle the controls gave the copy and the game gives
//    it back through the move state once ours faded out (EnsureBaseAnimation does that explicitly).
//  - an association created from a hierarchy (scripts: CTaskSimpleRunNamedAnim) has no group (-1, -1): those are the
//    named ones; the ones of tasks have (group, id).
//  - 0x4 is ANIMATION_IS_BLEND_AUTO_REMOVE (plugin-sdk calls it FREEZE_LAST_FRAME): deleted once faded out.
namespace
{
const uint16_t NAMED_ANIM_GROUP = 0xFFFF;
const int ANIM_BLOCK_MODEL_BASE = 25575;  // the game requests animation block N as streaming model 25575 + N

const uint32_t CHECK_INTERVAL = 100;       // ms between looks at the local CJ / at a copy
const uint32_t REFRESH_INTERVAL = 400;     // ms between refreshes while the CJ plays something (time / speed)
const uint32_t EMPTY_RESEND_1 = 250;       // ms after the set became empty: resend it (the channel is unreliable)
const uint32_t EMPTY_RESEND_2 = 700;
const uint32_t REMOTE_TIMEOUT = 1500;      // ms without a refresh: the copy lets the animations go
const float TIME_DRIFT = 0.25f;            // seconds a playing animation may differ before being moved

CAnimBlendAssocGroup* GetAnimAssocGroups()
{
    return *(CAnimBlendAssocGroup**)0xB4EA34;  // plugin-sdk declares an array, the address holds a pointer to it
}

// CAnimManager::BlendAnimation wants the global id; accept both forms and return the global one (-1: cannot play)
int NormalizeAnimId(int group, int animId)
{
    CAnimBlendAssocGroup* groups = GetAnimAssocGroups();
    if (groups == nullptr || group < 0 || group >= CAnimManager::ms_numAnimAssocDefinitions)
        return -1;

    CAnimBlendAssocGroup& assocGroup = groups[group];
    if (assocGroup.m_pAnimBlock == nullptr || !assocGroup.m_pAnimBlock->bLoaded || assocGroup.m_nNumAnimations == 0)
        return -1;

    const int offset = assocGroup.m_nIdOffset;
    const int count = (int)assocGroup.m_nNumAnimations;
    if (animId >= offset && animId < offset + count)
        return animId;
    if (animId >= 0 && animId < count)
        return animId + offset;
    return -1;
}

// the default-group animations that no control produces: falls, knock downs, getting up, cower, phone, sitting, ATM
bool IsActionDefaultAnimation(int animId)
{
    return (animId >= ANIM_DEFAULT_KO_SHOT_FRONT_0 && animId <= ANIM_DEFAULT_KO_SPIN_L) ||
           animId == ANIM_DEFAULT_FLOOR_HIT || animId == ANIM_DEFAULT_FLOOR_HIT_F ||
           (animId >= ANIM_DEFAULT_GETUP_0 && animId <= ANIM_DEFAULT_GETUP_FRONT) ||
           (animId >= ANIM_DEFAULT_DUCK_COWER && animId <= ANIM_DEFAULT_HANDSCOWER) ||
           (animId >= ANIM_DEFAULT_PHONE_IN && animId <= ANIM_DEFAULT_PHONE_TALK) ||
           (animId >= ANIM_DEFAULT_SEAT_DOWN && animId <= ANIM_DEFAULT_SEAT_IDLE) || animId == ANIM_DEFAULT_ATM ||
           animId == ANIM_DEFAULT_IDLE_CHAT;
}

// Groups of "things the CJ does" that scripts and tasks play. Left out on purpose: the walk style group of the ped,
// the weapon groups (colt45, ak, ...), melee / kick, swimming, driving, jump / climb and parachute (own packet): all
// of those follow from the controls, the position or the vehicle sync of the copy.
bool IsActionAnimation(unsigned short group, int animId)
{
    switch (group)
    {
    case ANIM_GROUP_DEFAULT:
        return IsActionDefaultAnimation(animId);
    case ANIM_GROUP_BEACH:
    case ANIM_GROUP_SUNBATHE:
    case ANIM_GROUP_PLAYIDLES:
    case ANIM_GROUP_RIOT:
    case ANIM_GROUP_GANGS:
    case ANIM_GROUP_ATTRACTORS:
    case ANIM_GROUP_INT_HOUSE:
    case ANIM_GROUP_INT_OFFICE:
    case ANIM_GROUP_INT_SHOP:
        return true;
    default:
        return false;
    }
}

bool IsAssociationAlive(CPed* ped, CAnimBlendAssociation* target)
{
    if (ped == nullptr || ped->m_pRwClump == nullptr || target == nullptr)
        return false;

    for (CAnimBlendAssociation* assoc = RpAnimBlendClumpGetFirstAssociation(ped->m_pRwClump); assoc;
         assoc = RpAnimBlendGetNextAssociation(assoc))
    {
        if (assoc == target)
            return true;
    }
    return false;
}

// a non-partial association that stays (not fading out, not a finished one-shot): the ped is not left without
bool HasLastingFullBodyAnimation(RpClump* clump)
{
    for (CAnimBlendAssociation* assoc = RpAnimBlendClumpGetFirstAssociation(clump); assoc;
         assoc = RpAnimBlendGetNextAssociation(assoc))
    {
        const unsigned short flags = assoc->m_nFlags;
        if (flags & ANIMATION_PARTIAL)
            continue;
        if ((flags & ANIMATION_FREEZE_LAST_FRAME) && assoc->m_fBlendDelta < 0.0f)
            continue;
        if ((flags & ANIMATION_UNLOCK_LAST_FRAME) && !(flags & ANIMATION_LOOPED) && assoc->m_pHierarchy &&
            assoc->m_fCurrentTime >= assoc->m_pHierarchy->m_fTotalTime - 0.1f)
            continue;
        return true;
    }
    return false;
}

// Once an action ended the copy may be left without a full-body animation (the controls only blend one when the move
// state changes); give it its move animation back the way CNetworkPed::EnsureBaseAnimation does. Slot 23 of the CPed
// vtable is SetMoveAnim in 1.0 US (plugin-sdk CPed::SetMoveAnim calls slot 24 = CPed::Save, which crashes).
void EnsureBaseAnimation(CPed* ped)
{
    if (ped == nullptr || ped->m_pRwClump == nullptr || !ped->IsAlive() || ped->m_fHealth < 1.0f ||
        ped->m_nPedFlags.bInVehicle || HasLastingFullBodyAnimation(ped->m_pRwClump))
        return;

    ped->field_538 = PEDMOVE_NONE;
    ((void(__thiscall*)(CPed*))(*reinterpret_cast<void***>(ped))[23])(ped);
    if (!HasLastingFullBodyAnimation(ped->m_pRwClump))
        CAnimManager::BlendAnimation(ped->m_pRwClump, (int)ped->m_nAnimGroup, 3 /* ANIM_ID_IDLE */, 4.0f);
}

bool LocalSceneBusy()
{
    return CCutsceneMgr::ms_running;
}

// ----------------------------------------------------------------------------------------------------------------
// emitter (the local CJ)
// ----------------------------------------------------------------------------------------------------------------
uint32_t ms_nLastCheck = 0;
uint32_t ms_nLastSend = 0;
uint64_t ms_nLastSignature = 0;
int ms_nLastCount = 0;
int ms_nEmptyResends = 0;  // 0..2 empty sets still to resend after the last one became empty

// The scene moves CJ through a go-to task while the remote on-foot controller can have an empty pad.
bool IsScriptedWalk(CPed* ped)
{
    if (!CConfigLoader::ms_bSceneWalkSync || !CCutscenePartners::InScriptedGuestScene() || !ped->m_pIntelligence)
        return false;
    CTask* task = ped->m_pIntelligence->m_TaskMgr.GetSimplestActiveTask();
    if (!task)
        return false;
    const auto type = task->GetTaskType();
    return type == TASK_SIMPLE_GO_TO_POINT || type == TASK_SIMPLE_GO_TO_POINT_FINE;
}

bool IsWalkClip(int group, int id)
{
    return group >= ANIM_GROUP_PLAYER && group <= ANIM_GROUP_PLAYERSNEAK &&
        (id == ANIM_DEFAULT_WALK_CIVI || id == ANIM_DEFAULT_RUN_CIVI);
}

bool Describe(CAnimBlendAssociation* assoc, bool scriptedWalk, Packets::Players::PlayerAnimMirror::SAnim& out, uint64_t& signature)
{
    if (assoc->m_pHierarchy == nullptr || assoc->m_fBlendAmount < 0.1f || assoc->m_fBlendDelta < 0.0f ||
        (assoc->m_nFlags & ANIMATION_INDESTRUCTIBLE))  // facial / always-on ones
        return false;

    const float total = assoc->m_pHierarchy->m_fTotalTime;
    out = {};
    out.blend = (uint8_t)(std::clamp(assoc->m_fBlendAmount, 0.0f, 1.0f) * 255.0f + 0.5f);
    out.time = total > 0.001f ? (uint8_t)(std::clamp(assoc->m_fCurrentTime / total, 0.0f, 1.0f) * 255.0f + 0.5f) : 0;
    out.speed = (uint8_t)(std::clamp(assoc->m_fSpeed, 0.0f, 5.0f) * 50.0f + 0.5f);
    out.playing = assoc->m_bPlaying != 0;
    out.looped = assoc->m_bLooped != 0;
    out.partial = assoc->m_bPartial != 0;

    if (assoc->m_nAnimGroup == NAMED_ANIM_GROUP)
    {
        const int blockId = assoc->m_pHierarchy->m_nAnimBlockId;
        if (blockId < 0 || blockId >= CAnimManager::ms_numAnimBlocks)
            return false;
        out.named = true;
        strncpy_s(out.block, sizeof(out.block), CAnimManager::ms_aAnimBlocks[blockId].szName, _TRUNCATE);
        out.hash = assoc->m_pHierarchy->m_hashKey;
        signature = ((uint64_t)out.hash << 8) ^ (uint64_t)(unsigned char)out.block[0] ^ 0x100000000ull;
        return true;
    }

    const int id = NormalizeAnimId(assoc->m_nAnimGroup, assoc->m_nAnimId);
    const bool gait = scriptedWalk && out.playing && !out.partial && IsWalkClip(assoc->m_nAnimGroup, id);
    if (id < 0 || id > 8191 || assoc->m_nAnimGroup > 4095 || (!gait && !IsActionAnimation(assoc->m_nAnimGroup, id)))
        return false;
    out.group = assoc->m_nAnimGroup;
    out.animId = (uint16_t)id;
    signature = ((uint64_t)out.group << 16) | out.animId;
    return true;
}

void ProcessEmitter(uint32_t now)
{
    if (now - ms_nLastCheck < CHECK_INTERVAL)
        return;
    ms_nLastCheck = now;

    Packets::Players::PlayerAnimMirror packet{};
    uint64_t signature = 0;

    CPed* ped = FindPlayerPed(0);
    if (CConfigLoader::ms_bPlayerAnimMirror && ped != nullptr && ped->m_pRwClump != nullptr && ped->IsAlive() &&
        !ped->m_nPedFlags.bInVehicle && !LocalSceneBusy())
    {
        const bool scriptedWalk = IsScriptedWalk(ped);
        float blends[Packets::Players::PlayerAnimMirror::MAX_ANIMS]{};
        for (CAnimBlendAssociation* assoc = RpAnimBlendClumpGetFirstAssociation(ped->m_pRwClump); assoc;
             assoc = RpAnimBlendGetNextAssociation(assoc))
        {
            Packets::Players::PlayerAnimMirror::SAnim anim;
            uint64_t sig = 0;
            if (!Describe(assoc, scriptedWalk, anim, sig))
                continue;

            // strongest first, at most MAX_ANIMS
            int slot = packet.count;
            while (slot > 0 && blends[slot - 1] < assoc->m_fBlendAmount)
                slot--;
            if (slot >= Packets::Players::PlayerAnimMirror::MAX_ANIMS)
                continue;
            const int last = std::min<int>(packet.count, Packets::Players::PlayerAnimMirror::MAX_ANIMS - 1);
            for (int i = last; i > slot; i--)
            {
                packet.anims[i] = packet.anims[i - 1];
                blends[i] = blends[i - 1];
            }
            packet.anims[slot] = anim;
            blends[slot] = assoc->m_fBlendAmount;
            if (packet.count < Packets::Players::PlayerAnimMirror::MAX_ANIMS)
                packet.count++;
        }

        // the signature is the set of identities (order independent): a change is sent at once
        for (int i = 0; i < packet.count; i++)
        {
            const auto& a = packet.anims[i];
            signature += a.named ? (((uint64_t)a.hash << 8) | 0x1000000000ull | (uint64_t)(unsigned char)a.block[0])
                                 : (((uint64_t)a.group << 16) | a.animId);
        }
    }

    const bool changed = packet.count != ms_nLastCount || signature != ms_nLastSignature;
    bool send = false;
    if (packet.count > 0)
    {
        send = changed || now - ms_nLastSend >= REFRESH_INTERVAL;
        ms_nEmptyResends = 2;
    }
    else if (ms_nLastCount > 0)
    {
        send = true;  // the set just ended: tell them now, and twice more below
        ms_nEmptyResends = 2;
        ms_nLastSend = now;
    }
    else if (ms_nEmptyResends > 0)
    {
        const uint32_t wait = ms_nEmptyResends == 2 ? EMPTY_RESEND_1 : EMPTY_RESEND_2;
        if (now - ms_nLastSend >= wait)
        {
            send = true;
            ms_nEmptyResends--;
        }
    }

    ms_nLastCount = packet.count;
    ms_nLastSignature = signature;
    if (!send)
        return;

    ms_nLastSend = now;
    GetPacketFactory().Send(packet);

    static int nLogged = 0;
    if (changed && nLogged < 40)
    {
        nLogged++;
        CMissionMirror::LogEventf("player-anim-send count=%d first=%s/%u", packet.count,
            packet.count > 0 && packet.anims[0].named ? packet.anims[0].block : "grp",
            packet.count > 0 ? (packet.anims[0].named ? packet.anims[0].hash : packet.anims[0].animId) : 0u);
    }
}

// ----------------------------------------------------------------------------------------------------------------
// receivers (the copies of the other players)
// ----------------------------------------------------------------------------------------------------------------
struct SOwned
{
    CAnimBlendAssociation* assoc = nullptr;
    CAnimBlendHierarchy* hierarchy = nullptr;
    uint16_t group = NAMED_ANIM_GROUP;
    int animId = -1;
};

struct SRemote
{
    Packets::Players::PlayerAnimMirror packet{};
    uint32_t heard = 0;
    uint32_t checked = 0;
    CPed* ped = nullptr;
    std::vector<SOwned> owned;
    std::vector<int> blocks;  // animation blocks we hold a reference on while the associations play
    // player-anim-slide (diagnostic): last sample of the copy's head bone
    uint32_t diagAt = 0;
    uint32_t sceneWalkFrames = 0;
    bool hasBone = false;
    CVector lastBone{};
    bool poseAny = false;
    float poseMin = 0.0f;
    float poseMax = 0.0f;
};

std::unordered_map<CNetworkPlayer*, SRemote> ms_remotes;

// fade an association we created out, only while it is still the one we put there
void FadeOwned(CPed* ped, const SOwned& owned)
{
    if (!IsAssociationAlive(ped, owned.assoc))
        return;
    const bool same = owned.group == NAMED_ANIM_GROUP
                          ? (owned.assoc->m_nAnimGroup == NAMED_ANIM_GROUP && owned.assoc->m_pHierarchy == owned.hierarchy)
                          : (owned.assoc->m_nAnimGroup == owned.group &&
                                NormalizeAnimId(owned.group, owned.assoc->m_nAnimId) == owned.animId);
    if (!same)
        return;
    owned.assoc->m_nFlags |= (unsigned short)ANIMATION_FREEZE_LAST_FRAME;  // auto remove once faded
    if (owned.assoc->m_fBlendDelta > -4.0f)
        owned.assoc->m_fBlendDelta = -4.0f;
}

void ReleaseBlocks(SRemote& remote)
{
    for (int blockIndex : remote.blocks)
        CAnimManager::RemoveAnimBlockRef(blockIndex);
    remote.blocks.clear();
}

void ReleaseAll(SRemote& remote)
{
    const bool had = !remote.owned.empty();
    CPed* ped = remote.ped;
    // the ped of a respawned / destroyed player is another object: only touch it while it is the one we used
    if (ped != nullptr && ped->IsVTableValid() && ped->m_pRwClump != nullptr)
    {
        for (const SOwned& owned : remote.owned)
            FadeOwned(ped, owned);
        if (had)
            EnsureBaseAnimation(ped);
    }
    remote.owned.clear();
    ReleaseBlocks(remote);
}

void AddBlockRef(SRemote& remote, int blockIndex)
{
    if (blockIndex <= 0 || std::find(remote.blocks.begin(), remote.blocks.end(), blockIndex) != remote.blocks.end())
        return;
    CAnimManager::AddAnimBlockRef(blockIndex);
    remote.blocks.push_back(blockIndex);
}

// The owner's time is the one of his last packet: it is moved on by the time since it arrived, because this runs every
// CHECK_INTERVAL against the same packet. Without that the copy was pulled back to the old time every ~300 ms and the
// idle animations played in little jumps (test with a friend, 2026-11-13).
float ExpectedTime(CAnimBlendAssociation* assoc, const Packets::Players::PlayerAnimMirror::SAnim& anim, uint32_t sinceMs)
{
    const float total = assoc->m_pHierarchy->m_fTotalTime;
    float time = anim.time / 255.0f * total;
    if (anim.playing)
        time += sinceMs / 1000.0f * (anim.speed > 0 ? anim.speed / 50.0f : assoc->m_fSpeed);
    if (anim.looped)
        return std::fmod(time, total);
    return std::min(time, total);
}

void SyncPlaying(CAnimBlendAssociation* assoc, const Packets::Players::PlayerAnimMirror::SAnim& anim, uint32_t sinceMs)
{
    assoc->m_fSpeed = anim.speed > 0 ? anim.speed / 50.0f : assoc->m_fSpeed;
    assoc->m_bPlaying = anim.playing ? 1 : 0;
    if (assoc->m_pHierarchy != nullptr && assoc->m_pHierarchy->m_fTotalTime > 0.001f)
    {
        const float total = assoc->m_pHierarchy->m_fTotalTime;
        const float time = ExpectedTime(assoc, anim, sinceMs);
        float drift = std::fabs(assoc->m_fCurrentTime - time);
        if (assoc->m_bLooped)
            drift = std::min(drift, total - drift);
        if (drift > TIME_DRIFT)
            assoc->SetCurrentTime(time);
    }
}

// Is the copy in a state where the game (or another sync) owns its animations?
bool CanApply(CNetworkPlayer* player, CPed* ped)
{
    if (ped == nullptr || !ped->IsVTableValid() || ped->m_pRwClump == nullptr || !ped->IsAlive() ||
        ped->m_fHealth < 1.0f || player->m_bDead || player->m_bBusted || ped->m_nPedFlags.bInVehicle ||
        ped->m_nAreaCode != CGame::currArea || LocalSceneBusy() || CCutscenePartners::ControlsPed(ped))
        return false;
    return true;
}

void ApplyRemote(CNetworkPlayer* player, SRemote& remote, uint32_t now)
{
    CPed* ped = player->m_pPed;

    if (remote.ped != ped)  // respawned / recreated: nothing of the old ped is ours anymore
    {
        remote.owned.clear();
        ReleaseBlocks(remote);
        remote.ped = ped;
    }

    if (!CConfigLoader::ms_bPlayerAnimMirror || !CanApply(player, ped) || now - remote.heard > REMOTE_TIMEOUT ||
        remote.packet.count == 0)
    {
        if (!remote.owned.empty() || !remote.blocks.empty())
            ReleaseAll(remote);
        return;
    }

    CAnimBlendAssocGroup* groups = GetAnimAssocGroups();
    std::vector<SOwned> keep;
    bool fullBody = false;

    for (int i = 0; i < remote.packet.count && i < Packets::Players::PlayerAnimMirror::MAX_ANIMS; i++)
    {
        const Packets::Players::PlayerAnimMirror::SAnim& anim = remote.packet.anims[i];

        if (!anim.named && IsWalkClip(anim.group, anim.animId))
            continue; // the native player controller blends the gait, without taking ownership of its associations

        int blockIndex = -1;
        CAnimBlendHierarchy* hierarchy = nullptr;
        int animId = -1;

        if (anim.named)
        {
            blockIndex = CAnimManager::GetAnimationBlockIndex(anim.block);
            if (blockIndex < 0 || blockIndex >= CAnimManager::ms_numAnimBlocks)
                continue;  // not a block of this game
        }
        else
        {
            if (groups == nullptr || anim.group >= CAnimManager::ms_numAnimAssocDefinitions)
                continue;
            const char* blockName = CAnimManager::GetAnimBlockName(anim.group);
            blockIndex = blockName != nullptr && blockName[0] != '\0' ? CAnimManager::GetAnimationBlockIndex(blockName) : -1;
            if (blockIndex < 0 || blockIndex >= CAnimManager::ms_numAnimBlocks)
                continue;
        }

        // not loaded here yet: ask for it (a plain request; our reference holds it once it plays) and wait
        if (blockIndex != 0 && !CAnimManager::ms_aAnimBlocks[blockIndex].bLoaded)
        {
            // same as the game's own tasks (gta-reversed AnimManager.cpp:566, TaskComplexSitDownThenIdleThenStandUp):
            // KEEP_IN_MEMORY to load it, then our block reference keeps it while it plays
            CStreaming::RequestModel(ANIM_BLOCK_MODEL_BASE + blockIndex, eStreamingFlags::KEEP_IN_MEMORY);
            continue;
        }

        if (anim.named)
        {
            hierarchy = CAnimManager::GetAnimation(anim.hash, &CAnimManager::ms_aAnimBlocks[blockIndex]);
            if (hierarchy == nullptr)
                continue;
        }
        else
        {
            animId = NormalizeAnimId(anim.group, anim.animId);
            if (animId < 0)
                continue;
        }

        // is it playing already? Then it is either ours (keep it in time) or the game's own (do not touch it)
        CAnimBlendAssociation* found = nullptr;
        for (CAnimBlendAssociation* it = RpAnimBlendClumpGetFirstAssociation(ped->m_pRwClump); it;
             it = RpAnimBlendGetNextAssociation(it))
        {
            if (anim.named ? (it->m_nAnimGroup == NAMED_ANIM_GROUP && it->m_pHierarchy == hierarchy)
                           : (it->m_nAnimGroup == anim.group && NormalizeAnimId(anim.group, it->m_nAnimId) == animId))
            {
                found = it;
                break;
            }
        }

        if (found != nullptr)
        {
            bool ours = false;
            for (const SOwned& o : remote.owned)
                ours = ours || o.assoc == found;
            if (ours)
            {
                SyncPlaying(found, anim, now - remote.heard);
                SOwned o;
                o.assoc = found;
                o.hierarchy = hierarchy;
                o.group = anim.named ? NAMED_ANIM_GROUP : anim.group;
                o.animId = animId;
                keep.push_back(o);
            }
            if (!found->m_bPartial)
                fullBody = true;
            continue;
        }

        AddBlockRef(remote, blockIndex);

        CAnimBlendAssociation* assoc = nullptr;
        if (anim.named)
        {
            // the translate bits would move the ped away from the position its owner gives it
            unsigned short flags = ANIMATION_STARTED | (anim.looped ? ANIMATION_LOOPED : 0) |
                                   (anim.partial ? ANIMATION_PARTIAL : 0);
            assoc = CAnimManager::BlendAnimation(ped->m_pRwClump, hierarchy, flags, 4.0f);
        }
        else
            assoc = CAnimManager::BlendAnimation(ped->m_pRwClump, (int)anim.group, animId, 4.0f);

        if (assoc == nullptr)
            continue;

        assoc->m_nFlags &= ~(unsigned short)(ANIMATION_TRANLSATE_X | ANIMATION_TRANLSATE_Y);
        SyncPlaying(assoc, anim, now - remote.heard);
        if (assoc->m_pHierarchy != nullptr && assoc->m_pHierarchy->m_fTotalTime > 0.001f)
            assoc->SetCurrentTime(ExpectedTime(assoc, anim, now - remote.heard));

        SOwned o;
        o.assoc = assoc;
        o.hierarchy = hierarchy;
        o.group = anim.named ? NAMED_ANIM_GROUP : anim.group;
        o.animId = animId;
        keep.push_back(o);
        if (!assoc->m_bPartial)
            fullBody = true;

        static int nLogged = 0;
        if (nLogged < 40)
        {
            nLogged++;
            CMissionMirror::LogEventf("player-anim-play player=%d %s=%s/%u partial=%d", (int)player->m_playerId.value(),
                anim.named ? "named" : "group", anim.named ? anim.block : "-",
                anim.named ? anim.hash : (uint32_t)anim.animId, (int)anim.partial);
        }
    }

    // the ones of ours that the owner no longer plays fade out
    bool dropped = false;
    for (const SOwned& o : remote.owned)
    {
        bool stays = false;
        for (const SOwned& k : keep)
            stays = stays || k.assoc == o.assoc;
        if (!stays)
        {
            FadeOwned(ped, o);
            dropped = true;
        }
    }
    remote.owned = keep;
    if (remote.owned.empty())
        ReleaseBlocks(remote);
    if (dropped && !fullBody)
        EnsureBaseAnimation(ped);
}

// player-anim-slide (diagnostic only, no behaviour change).
// CPed::GetBonePosition(updateSkinBones=false) does NOT read the animated skeleton unless CPed::PreRender already ran
// this frame: it returns GetPedBoneStdPosition(bone) transformed by the ped matrix, a CONSTANT table (gta-reversed
// Ped.cpp:2108). So the previous line was reading a fixed pose and could never prove anything.
// bCalledPreRender is cleared by CPedIntelligence::Process every frame and set by CPed::PreRender (Ped.cpp:2843), so
// `pre=` alone answers the real question: is the copy ever passing through PreRender? Only with pre=1 is `pose` read
// from the animated hierarchy (head-foot distance: it depends on the rotations, never on where the ped is).
void DiagnoseCopy(CNetworkPlayer* player, SRemote& remote)
{
    if (!CConfigLoader::ms_bPlayerAnimDiag)
        return;
    CPed* ped = remote.ped;
    if (ped == nullptr || !ped->IsAlive() || ped->m_pRwClump == nullptr)
        return;

    const bool pre = ped->m_nPedFlags.bCalledPreRender != 0;
    // same instant, our own ped: it always goes through CPed::PreRender. If this is 1 and `pre` is 0, the copy really
    // never gets there; if both are 0 it is only that this runs before the render phase and the reading means nothing.
    CPed* local = FindPlayerPed();
    const int localPre = local != nullptr ? (local->m_nPedFlags.bCalledPreRender != 0 ? 1 : 0) : -1;
    float pose = -1.0f;
    RwV3d head{};
    if (pre)
    {
        RwV3d foot{};
        ped->GetBonePosition(head, 8 /*BONE_HEAD*/, false);
        ped->GetBonePosition(foot, 44 /*BONE_LEFTFOOT*/, false);
        const float dx = head.x - foot.x, dy = head.y - foot.y, dz = head.z - foot.z;
        pose = std::sqrt(dx * dx + dy * dy + dz * dz);
        if (!(pose > 0.05f && pose < 5.0f))
            pose = -1.0f;
    }

    if (pose > 0.0f)
    {
        if (!remote.poseAny)
        {
            remote.poseMin = remote.poseMax = pose;
            remote.poseAny = true;
        }
        else
        {
            remote.poseMin = std::min(remote.poseMin, pose);
            remote.poseMax = std::max(remote.poseMax, pose);
        }
    }

    const uint32_t now = GetTickCount();
    if (now - remote.diagAt < 2000)
        return;
    remote.diagAt = now;

    CAnimBlendAssociation* top = nullptr;
    for (CAnimBlendAssociation* assoc = RpAnimBlendClumpGetFirstAssociation(ped->m_pRwClump); assoc;
         assoc = RpAnimBlendGetNextAssociation(assoc))
    {
        if (assoc->m_bPartial || assoc->m_fBlendAmount <= 0.01f)
            continue;
        if (top == nullptr || assoc->m_fBlendAmount > top->m_fBlendAmount)
            top = assoc;
    }

    float bone = -1.0f;
    if (pre && remote.hasBone)
    {
        const float bx = head.x - remote.lastBone.x, by = head.y - remote.lastBone.y, bz = head.z - remote.lastBone.z;
        bone = std::sqrt(bx * bx + by * by + bz * bz);
        if (bone > 50.0f)  // teleport or a new ped: not a pose reading
            bone = -1.0f;
    }
    remote.hasBone = pre;
    if (pre)
        remote.lastBone = CVector(head.x, head.y, head.z);

    const float poseRange = remote.poseAny ? remote.poseMax - remote.poseMin : -1.0f;
    remote.poseAny = false;

    CMissionMirror::LogEventf(
        "player-anim-slide player=%d pose=%.3f poseRange=%.3f pre=%d localpre=%d off=%d dont=%d bone=%.3f anim=%d/%d "
        "blend=%.2f clipSpeed=%.2f time=%.2f/%.2f speed=%.2f sceneWalkFrames=%u",
        (int)player->m_playerId.value(), pose, poseRange, pre ? 1 : 0, localPre, ped->m_bOffscreen ? 1 : 0,
        ped->m_bDontUpdateHierarchy ? 1 : 0, bone, top ? (int)top->m_nAnimGroup : -2,
        top ? (int)top->m_nAnimId : -2, top ? top->m_fBlendAmount : 0.0f, top ? top->m_fSpeed : 0.0f,
        top ? top->m_fCurrentTime : 0.0f, (top && top->m_pHierarchy) ? top->m_pHierarchy->m_fTotalTime : 0.0f,
        ped->m_vecMoveSpeed.Magnitude() * 50.0f, remote.sceneWalkFrames);
    remote.sceneWalkFrames = 0;
}

void ProcessReceivers(uint32_t now)
{
    for (auto it = ms_remotes.begin(); it != ms_remotes.end();)
    {
        CNetworkPlayer* player = it->first;
        const bool connected = CNetwork::m_bAuthenticated &&
                               std::find(CNetworkPlayerManager::m_pPlayers.begin(),
                                   CNetworkPlayerManager::m_pPlayers.end(), player) != CNetworkPlayerManager::m_pPlayers.end();
        if (!connected)
        {
            // the player (and his ped) may be gone already: only the references are ours to give back
            ReleaseBlocks(it->second);
            it = ms_remotes.erase(it);
            continue;
        }

        SRemote& remote = it->second;
        if (now - remote.checked >= CHECK_INTERVAL)
        {
            remote.checked = now;
            ApplyRemote(player, remote, now);
        }
        DiagnoseCopy(player, remote);
        ++it;
    }
}
// PlayerControlZelda calls this after it computes the blend ratio from the pad. Supplying the gait
// here lets the game keep a single walk cycle and use the model's own animations.
void __fastcall SetSceneMoveAnim(CPlayerPed* ped)
{
    if (CConfigLoader::ms_bSceneWalkSync && CConfigLoader::ms_bPlayerAnimMirror &&
        CCutscenePartners::InScriptedGuestScene() && ped->m_pPlayerData && !ped->m_nPedFlags.bIsDucking)
    {
        auto* player = CNetworkPlayerManager::GetPlayer(ped);
        auto it = ms_remotes.find(player);
        if (it != ms_remotes.end() && it->second.ped == ped && CanApply(player, ped) &&
            CCoopMission::ShouldIncludeNetworkPlayer(player) && GetTickCount() - it->second.heard < 2 * REFRESH_INTERVAL)
        {
            // The packet is sorted by blend weight. A stronger full-body action takes priority over a step.
            const auto& packet = it->second.packet;
            for (int i = 0; i < packet.count; ++i)
            {
                const auto& anim = packet.anims[i];
                if (anim.partial)
                    continue;
                if (!anim.named && anim.playing && IsWalkClip(anim.group, anim.animId))
                {
                    if (CConfigLoader::ms_bPlayerAnimDiag)
                        ++it->second.sceneWalkFrames;
                    const bool run = anim.animId == ANIM_DEFAULT_RUN_CIVI;
                    ped->m_nMoveState = run ? PEDMOVE_RUN : PEDMOVE_WALK;
                    ped->m_pPlayerData->m_fMoveBlendRatio = run ? 1.8f : 1.0f;
                }
                break;
            }
        }
    }
    ped->SetRealMoveAnim();
}
}  // namespace

void CPlayerAnimMirror::Init()
{
    // GTA SA 1.0 US, CTaskSimplePlayerOnFoot::PlayerControlZelda -> CPlayerPed::SetRealMoveAnim.
    const unsigned char call[] = {0xE8, 0x52, 0x23, 0xF8, 0xFF};
    if (memcmp(reinterpret_cast<const void*>(0x688669), call, sizeof(call)) == 0)
        patch::RedirectCall(0x688669, SetSceneMoveAnim);
    else
        CMissionMirror::LogEvent("scene-walk-sync: native animation call differs; hook disabled");
}

PACKET_HANDLER(ePacketType::PLAYER_ANIM_MIRROR, Packets::Players::PlayerAnimMirror* pAnimMirror)
{
    if (!CConfigLoader::ms_bPlayerAnimMirror)
        return;

    CNetworkPlayer* pNetworkPlayer = CNetworkPlayerManager::GetPlayer(pAnimMirror->senderPlayerid);
    if (pNetworkPlayer == nullptr)
        return;

    SRemote& remote = ms_remotes[pNetworkPlayer];
    remote.packet = *pAnimMirror;
    remote.heard = GetTickCount();
}

void CPlayerAnimMirror::Process()
{
    const uint32_t now = GetTickCount();

    ProcessReceivers(now);

    if (!CNetwork::m_bAuthenticated)
    {
        ms_nLastCount = 0;
        ms_nLastSignature = 0;
        ms_nEmptyResends = 0;
        return;
    }

    ProcessEmitter(now);
}
