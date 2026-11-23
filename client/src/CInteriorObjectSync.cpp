#include "stdafx.h"
#include "CInteriorObjectSync.h"
#include "CInteriorOwnership.h"
#include "CNetworkPed.h"
#include "CNetworkPedManager.h"
#include "CNetworkPlayerManager.h"
#include "CConfigLoader.h"
#include "CMissionMode.h"
#include "CMissionMirror.h"
#include "COpCodeSync.h"
#include "SceneCompat/Game.h"
#include <CObject.h>
#include <CAnimManager.h>
#include <CAnimBlendAssociation.h>
#include <CAnimBlendHierarchy.h>
#include <CStreaming.h>
#include <CPools.h>
#include <CWorld.h>
#include <CTaskSimpleHoldEntity.h>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <cstring>

// CTaskSimpleHoldEntity read-only overlay: plugin-sdk ships an empty stub, so the fields are read at the
// offsets verified live (18-11) against gta-reversed's TaskSimpleHoldEntity.h. GetTaskHold() already
// vtable-checks the task (hold / pickup / putdown), we only read what it points to.
struct SHoldLayout
{
    void* vtable;                  // 0x00
    uint32_t taskFlags;            // 0x04
    CEntity* entity;               // 0x08 m_pEntityToHold
    CVector position;              // 0x0C m_vecPosition
    uint8_t bone;                  // 0x18 m_nBoneFrameId
    uint8_t boneFlags;             // 0x19 m_bBoneFlags
    uint8_t pad1A[2];              // 0x1A
    float rotation;                // 0x1C m_fRotation
    int32_t animId;                // 0x20
    int32_t animGroup;             // 0x24
    uint32_t animFlags;            // 0x28
    void* animBlock;               // 0x2C
    void* hierarchy;               // 0x30
    uint8_t dropped;               // 0x34 m_bEntityDropped
    uint8_t requiresProcessing;    // 0x35
    uint8_t disallowDrop;          // 0x36
    uint8_t pad37;                 // 0x37
    CAnimBlendAssociation* assoc;  // 0x38
};

struct SHeldInfo
{
    uint16_t model = 0;
    uint8_t bone = 0;
    uint8_t boneFlags = 16;  // HOLD_ENTITY_UPDATE_TRANSLATION_ONLY (vanilla food holds)
    CVector offset{};
    bool Same(const SHeldInfo& o) const
    {
        return model == o.model && bone == o.bone && boneFlags == o.boneFlags &&
            (offset - o.offset).Magnitude() <= 0.02f;
    }
};

// owner: what the others have, per held ped (one object per ped)
struct SSentHeld
{
    uint32_t objectid = 0;
    SHeldInfo info;
    uint32_t lastSent = 0;
};
static std::unordered_map<pedid_t, SSentHeld> ms_sent;
static uint32_t ms_nNextId = 1;
static bool ms_bResendAll = false;

// guest: visual copies by the owner's objectid; lastKnown survives stream-out and feeds the handover
struct SCopy
{
    CObject* object = nullptr;
    pedid_t pedid{};
    SHeldInfo info;
    uint32_t since = 0;  // pending since (leak guard)
};
static std::unordered_map<uint32_t, SCopy> ms_copies;
static std::unordered_map<pedid_t, SHeldInfo> ms_lastKnown;

// owner: mod-created objects on real peds (handover reconstruction); tracked to the bone like the copies
struct SOwned
{
    CObject* object = nullptr;
    SHeldInfo info;
};
static std::unordered_map<pedid_t, SOwned> ms_owned;

// owner: a guest's serving sequence applied to the real keeper
struct SOverride
{
    playerid_t sender{};
    uint32_t lastSeen = 0;
    char block[16] = {'\0'};
    uint32_t hash = 0;
    uint16_t flags = 0;
    float time = 0.0f, speed = 1.0f;
    bool hasHeld = false;
    SHeldInfo held;
    CAnimBlendAssociation* anim = nullptr;
    RpClump* animClump = nullptr;  // the clump it was blended into (address-reuse guard)
    int blockIndex = -1;
    CObject* object = nullptr;
    bool yielded = false;
    bool greetingAborted = false;
};
static std::unordered_map<pedid_t, SOverride> ms_overrides;
static std::unordered_map<CPed*, uint32_t> ms_nativeUntil;  // native script activity per ped (owner)
static std::unordered_map<CPed*, uint32_t> ms_localServiceUntil;  // guest: our hidden keeper's script is serving
static bool ms_bOwnerInside = false;                        // cheap gate for the opcode observer

// guest: per hidden local service ped, the remote keeper we last forwarded to (CLEAR needs it)
static std::unordered_map<CPed*, pedid_t> ms_serviceTarget;
static uint32_t ms_nLastServiceSend = 0;

constexpr uint32_t SERVICE_TIMEOUT_MS = 1500;
constexpr uint32_t NATIVE_GRACE_MS = 2000;
constexpr uint32_t COPY_PENDING_TIMEOUT_MS = 30000;
constexpr uint32_t HELD_RESEND_MS = 3000;
constexpr float MAX_OFFSET = 5.0f;
constexpr int MAX_BONE = 19;        // CPed::m_apBones[19]
constexpr int MAX_MODEL = 26316;    // CStreaming::ms_aInfoForModel[26316]

static bool IsParachuteModel(int modelId)
{
    return modelId == 3131 || modelId == 3060;
}

static bool ValidObject(CEntity* entity)
{
    return entity && entity->IsVTableValid() && entity->m_nType == ENTITY_TYPE_OBJECT &&
        CPools::ms_pObjectPool && CPools::ms_pObjectPool->IsObjectValid((CObject*)entity);
}

static bool LoadObjectModel(int modelId)
{
    if (modelId <= 0 || modelId >= MAX_MODEL)
        return false;
    if (CStreaming::ms_aInfoForModel[modelId].m_nLoadState == LOADSTATE_LOADED)
        return true;
    CStreaming::RequestModel(modelId, eStreamingFlags::MISSION_REQUIRED);
    CStreaming::LoadAllRequestedModels(false);
    return CStreaming::ms_aInfoForModel[modelId].m_nLoadState == LOADSTATE_LOADED;
}

static void DestroyObject(CObject*& object)
{
    if (object && object->IsVTableValid())
    {
        CObject* toDelete = object;
        toDelete->CleanUpOldReference((CEntity**)&object);
        CWorld::Remove(toDelete);
        CWorld::RemoveReferencesToDeletedObject(toDelete);
        delete toDelete;
    }
    object = nullptr;
}

// a visual prop: no physics, never removed by the game's mission cleanup; tracked to the bone by FrameUpdate
static CObject* CreateCopyObject(int modelId, const CVector& pos)
{
    if (!LoadObjectModel(modelId))
        return nullptr;
    CObject* object = CObject::Create(modelId);
    if (!object)
        return nullptr;
    object->m_nObjectType = OBJECT_MISSION;
    object->SetIsStatic(true);
    object->m_nPhysicalFlags.bDisableMoveForce = true;
    object->m_nPhysicalFlags.bInfiniteMass = true;
    object->m_bUsesCollision = false;
    if (!object->m_matrix)
        object->AllocateMatrix();
    if (object->m_matrix)
        object->m_matrix->pos = pos;
    CWorld::Add(object);
    return object;
}

static bool NativeActive(CPed* ped)
{
    auto it = ms_nativeUntil.find(ped);
    return it != ms_nativeUntil.end() && it->second > GetTickCount();
}

static bool LocalServing(CPed* ped)
{
    auto it = ms_localServiceUntil.find(ped);
    return it != ms_localServiceUntil.end() && it->second > GetTickCount();
}

static void PruneActivity()
{
    const uint32_t now = GetTickCount();
    for (auto it = ms_nativeUntil.begin(); it != ms_nativeUntil.end();)
        if (it->second <= now) it = ms_nativeUntil.erase(it); else ++it;
    for (auto it = ms_localServiceUntil.begin(); it != ms_localServiceUntil.end();)
        if (it->second <= now) it = ms_localServiceUntil.erase(it); else ++it;
}

// a PLAYING named partial of a DIFFERENT hierarchy: the native script is doing something else (a long
// native anim outlives the 2 s opcode grace; the frozen idle greeting is not playing, so it stays overridable)
constexpr uint16_t NAMED_GROUP = 0xFFFF;
static bool NativeOtherPose(CPed* ped, CAnimBlendAssociation* ours)
{
    if (!ped || !ped->m_pRwClump)
        return false;
    CAnimBlendAssociation* best = nullptr;
    for (CAnimBlendAssociation* a = RpAnimBlendClumpGetFirstAssociation(ped->m_pRwClump); a;
         a = RpAnimBlendGetNextAssociation(a))
    {
        if (a->m_bPartial && a->m_nAnimGroup == NAMED_GROUP && a->m_bPlaying && a->m_fBlendAmount > 0.5f &&
            (best == nullptr || a->m_fBlendAmount > best->m_fBlendAmount))
            best = a;
    }
    if (!best || best == ours)
        return false;
    return !ours || !ours->m_pHierarchy || best->m_pHierarchy != ours->m_pHierarchy;
}

static bool IsAssociationAlive(CPed* ped, CAnimBlendAssociation* target)
{
    if (!ped || !ped->m_pRwClump || !target)
        return false;
    for (CAnimBlendAssociation* a = RpAnimBlendClumpGetFirstAssociation(ped->m_pRwClump); a;
         a = RpAnimBlendGetNextAssociation(a))
        if (a == target)
            return true;
    return false;
}

// the game's bone functions (the CSceneFidget pattern): the matrix array is where the frame shows the bones
static bool BoneMatrix(CPed* ped, uint8_t bone, float out[16])
{
    if (bone >= MAX_BONE || !ped->m_pRwClump || !ped->m_apBones[bone])
        return false;
    auto* clump = (uint8_t*)ped->m_pRwClump;
    if (clump[0] != 2)  // rpCLUMP
        return false;
    void* hier = scene_game::Call<0x734A40, void*>(clump);  // GetAnimHierarchyFromSkinClump
    if (!hier)
        return false;
    auto* mats = scene_game::Call<0x7C5120, uint8_t*>(hier);  // RpHAnimHierarchyGetMatrixArray
    if (!mats)
        return false;
    const int tag = (int)ped->m_apBones[bone]->m_nNodeId;
    const int idx = scene_game::Call<0x7C51A0, int>(hier, tag);  // RpHAnimIDGetIndex
    if (idx < 0)
        return false;
    memcpy(out, mats + (size_t)idx * 0x40, 64);
    return true;
}

// keep the object in the ped's hand: vanilla CTaskSimpleHoldEntity::SetPedPosition, without the task
static void TrackToBone(CObject* object, CPed* ped, const SHeldInfo& info)
{
    if (!object || !object->IsVTableValid() || !ped || !ped->m_matrix)
        return;
    float M[16];
    if (!BoneMatrix(ped, info.bone, M) || !ped->IsAlive())
    {
        object->m_bIsVisible = false;
        return;
    }
    if (!object->m_matrix)
        object->AllocateMatrix();
    CMatrix* matrix = object->m_matrix;
    if (!matrix)
        return;
    CVector pos;
    if (info.boneFlags & 0x10)  // HOLD_ENTITY_UPDATE_TRANSLATION_ONLY: rotation straight from the bone
    {
        pos.x = M[0] * info.offset.x + M[4] * info.offset.y + M[8] * info.offset.z + M[12];
        pos.y = M[1] * info.offset.x + M[5] * info.offset.y + M[9] * info.offset.z + M[13];
        pos.z = M[2] * info.offset.x + M[6] * info.offset.y + M[10] * info.offset.z + M[14];
        matrix->right = CVector(M[0], M[1], M[2]);
        matrix->up = CVector(M[4], M[5], M[6]);
        matrix->at = CVector(M[8], M[9], M[10]);
    }
    else  // full orientate: vanilla adds the bone position to the ped-transformed offset, ped rotation
    {
        pos = (*ped->m_matrix) * info.offset + CVector(M[12], M[13], M[14]);
        matrix->right = ped->m_matrix->right;
        matrix->up = ped->m_matrix->up;
        matrix->at = ped->m_matrix->at;
    }
    matrix->pos = pos;
    object->m_nAreaCode = ped->m_nAreaCode;
    object->m_bIsVisible = ped->m_bIsVisible;
    object->UpdateRwMatrix();
    object->UpdateRwFrame();
}

static bool NearAnyNetworkPlayer(CPed* ped, float radius)
{
    for (CNetworkPlayer* player : CNetworkPlayerManager::m_pPlayers)
        if (player->m_pPed && DistanceBetweenPoints(player->m_pPed->GetPosition(), ped->GetPosition()) < radius)
            return true;
    return false;
}

static SHeldInfo InfoFromHold(const SHoldLayout* hold)
{
    SHeldInfo info;
    info.model = (uint16_t)((CObject*)hold->entity)->m_nModelIndex;
    info.bone = hold->bone < MAX_BONE ? hold->bone : 0;
    info.boneFlags = hold->boneFlags;
    info.offset = hold->position;
    if (info.offset.Magnitude() > MAX_OFFSET)
        info.offset = CVector{};
    return info;
}

// the native hold of a real ped (hold secondary + active pickup/putdown), as a validated object
static bool NativeHeldInfo(CPed* ped, SHeldInfo& info)
{
    if (!ped->m_pIntelligence)
        return false;
    auto* hold = reinterpret_cast<const SHoldLayout*>(ped->m_pIntelligence->GetTaskHold(false));
    if (!hold || hold->dropped || !ValidObject(hold->entity))
        return false;
    if (IsParachuteModel(((CObject*)hold->entity)->m_nModelIndex))
        return false;
    info = InfoFromHold(hold);
    return true;
}

static void SendHeld(const pedid_t& pedid, uint32_t objectid, const SHeldInfo& info, uint16_t model)
{
    Packets::Peds::InteriorHeldObject packet{};
    packet.objectid = objectid;
    packet.pedid = pedid;
    packet.modelId = model;
    packet.bone = info.bone;
    packet.boneFlags = info.boneFlags;
    packet.offX = info.offset.x; packet.offY = info.offset.y; packet.offZ = info.offset.z;
    packet.area = CInteriorOwnership::GetLocalArea();
    GetPacketFactory().Send(packet);
}

static bool OverrideAnimAlive(CPed* ped, const SOverride& ov)
{
    return ov.anim && ped && ov.animClump == ped->m_pRwClump && IsAssociationAlive(ped, ov.anim);
}

static void ClearOverride(const pedid_t& pedid, SOverride& ov)
{
    CPed* ped = nullptr;
    if (CNetworkPed* record = CNetworkPedManager::GetPed(pedid))
        ped = record->m_pPed;
    // fade only what we blended, and only while the native script is not driving the same association
    const bool nativeBusy = ped && (NativeActive(ped) || NativeOtherPose(ped, ov.anim));
    if (!nativeBusy && OverrideAnimAlive(ped, ov))
    {
        ov.anim->m_nFlags |= (unsigned short)ANIMATION_FREEZE_LAST_FRAME;
        ov.anim->m_fBlendDelta = -4.0f;
    }
    if (ov.blockIndex >= 0)
        CAnimManager::RemoveAnimBlockRef(ov.blockIndex);
    DestroyObject(ov.object);
    CMissionMirror::LogEventf("interior-service-clear ped=%s sender=%u", to_string(pedid).c_str(), ov.sender.value());
}

static void ClearAllOverrides()
{
    for (auto& [pedid, ov] : ms_overrides)
        ClearOverride(pedid, ov);
    ms_overrides.clear();
}

static void ClearGuestCopies()
{
    for (auto& [id, copy] : ms_copies)
        DestroyObject(copy.object);
    ms_copies.clear();
}

static void ClearOwned()
{
    for (auto& [pedid, owned] : ms_owned)
        DestroyObject(owned.object);
    ms_owned.clear();
}

// ---------------------------------------------------------------- owner: publish held objects
static void PublishHeld()
{
    const uint32_t now = GetTickCount();
    std::unordered_set<pedid_t> seen;
    for (CNetworkPed* record : CNetworkPedManager::m_pPeds)
    {
        if (!record->m_bSyncing || !record->m_bInteriorPopulation || !record->m_pPed ||
            !record->m_pPed->IsVTableValid() || !record->m_pPed->IsAlive() ||
            record->m_nAreaCode != CInteriorOwnership::GetLocalArea())
            continue;
        CPed* ped = record->m_pPed;
        const pedid_t pedid = record->m_pedId;
        SHeldInfo info;
        bool has = NativeHeldInfo(ped, info);
        auto ovIt = ms_overrides.find(pedid);
        const bool ovActive = ovIt != ms_overrides.end() && !ovIt->second.yielded &&
            now - ovIt->second.lastSeen <= SERVICE_TIMEOUT_MS;
        if (!has && ovActive && ovIt->second.hasHeld)
        {
            info = ovIt->second.held;
            has = true;
        }
        if (!has)
        {
            auto ownedIt = ms_owned.find(pedid);
            if (ownedIt != ms_owned.end() && ownedIt->second.object)
            {
                info = ownedIt->second.info;
                has = true;
            }
            else
            {
                // handover reconstruction: we were just a guest and know what this ped held
                auto knownIt = ms_lastKnown.find(pedid);
                if (knownIt != ms_lastKnown.end() && !ovActive)
                {
                    CObject* object = CreateCopyObject(knownIt->second.model, ped->GetPosition());
                    if (object)
                    {
                        ms_owned[pedid] = SOwned{object, knownIt->second};
                        ms_owned[pedid].object->RegisterReference((CEntity**)&ms_owned[pedid].object);
                        info = knownIt->second;
                        has = true;
                        CMissionMirror::LogEventf("interior-held-rebuild ped=%s model=%u",
                            to_string(pedid).c_str(), (unsigned)info.model);
                    }
                }
            }
        }
        else
        {
            // native (or override) state supersedes a reconstructed object
            auto ownedIt = ms_owned.find(pedid);
            if (ownedIt != ms_owned.end())
            {
                DestroyObject(ownedIt->second.object);
                ms_owned.erase(ownedIt);
            }
        }
        if (!has)
            continue;
        seen.insert(pedid);
        auto it = ms_sent.find(pedid);
        if (it == ms_sent.end())
        {
            it = ms_sent.emplace(pedid, SSentHeld{}).first;
            it->second.objectid = ms_nNextId++;  // per-owner space: guests filter by sender + clear on owner change
        }
        const bool changed = !it->second.info.Same(info);
        const bool resend = ms_bResendAll ||
            (now - it->second.lastSent >= HELD_RESEND_MS && NearAnyNetworkPlayer(ped, 30.0f));
        if (changed || resend || it->second.lastSent == 0)
        {
            SendHeld(pedid, it->second.objectid, info, info.model);
            it->second.info = info;
            it->second.lastSent = now;
        }
    }
    ms_bResendAll = false;
    for (auto it = ms_sent.begin(); it != ms_sent.end();)
    {
        if (seen.count(it->first))
        {
            ++it;
            continue;
        }
        // a transient skip (re-host / stream / area flicker) keeps the state for the next tick; only a gone or
        // dead ped, or a scannable ped that no longer holds anything, removes the object for everyone
        CNetworkPed* record = CNetworkPedManager::GetPed(it->first);
        CPed* ped = record ? record->m_pPed : nullptr;
        const bool gone = !record || !ped || !ped->IsVTableValid() || !ped->IsAlive();
        const bool scannable = record && record->m_bSyncing && ped && ped->IsVTableValid() && ped->IsAlive() &&
            record->m_nAreaCode == CInteriorOwnership::GetLocalArea();
        if (!gone && !scannable)
        {
            ++it;
            continue;
        }
        SendHeld(it->first, it->second.objectid, SHeldInfo{}, 0);
        ms_lastKnown.erase(it->first);
        it = ms_sent.erase(it);
    }
}

// ---------------------------------------------------------------- owner: apply the guest's serving sequence
static void RefreshOverrides()
{
    const uint32_t now = GetTickCount();
    for (auto it = ms_overrides.begin(); it != ms_overrides.end();)
    {
        SOverride& ov = it->second;
        CNetworkPed* record = CNetworkPedManager::GetPed(it->first);
        CPed* ped = record ? record->m_pPed : nullptr;
        if (now - ov.lastSeen > SERVICE_TIMEOUT_MS || !record || !record->m_bSyncing || !ped ||
            !ped->IsVTableValid() || !ped->IsAlive())
        {
            ClearOverride(it->first, ov);
            it = ms_overrides.erase(it);
            continue;
        }
        SHeldInfo nativeInfo;
        const bool nativeHeld = NativeHeldInfo(ped, nativeInfo);
        const bool nativeBusy = NativeActive(ped) || nativeHeld || NativeOtherPose(ped, ov.anim);
        if (nativeBusy)
        {
            if (!ov.yielded)
            {
                ov.yielded = true;
                ov.greetingAborted = false;  // re-arm: a native named-anim task may remain after the yield
                DestroyObject(ov.object);  // the native purchase brings its own tray
                CMissionMirror::LogEventf("interior-service-yield ped=%s", to_string(it->first).c_str());
            }
            ++it;
            continue;
        }
        ov.yielded = false;
        // the greeting pose's task would fight our time updates: abort it (junkfud plays it at init)
        if (!ov.greetingAborted && ped->m_pIntelligence)
        {
            if (CTask* greeting = ped->m_pIntelligence->m_TaskMgr.FindActiveTaskByType(TASK_SIMPLE_NAMED_ANIM))
            {
                greeting->MakeAbortable(ped, ABORT_PRIORITY_URGENT, nullptr);
                ov.greetingAborted = true;
            }
        }
        if (ov.hash != 0 && ov.block[0] != '\0')
        {
            const int blockIndex = CAnimManager::GetAnimationBlockIndex(ov.block);
            if (blockIndex >= 0 && blockIndex < CAnimManager::ms_numAnimBlocks)
            {
                CAnimBlock& block = CAnimManager::ms_aAnimBlocks[blockIndex];
                if (!block.bLoaded)
                {
                    CStreaming::RequestModel(25575 + blockIndex, eStreamingFlags::GAME_REQUIRED);
                }
                else
                {
                    CAnimBlendHierarchy* hierarchy = CAnimManager::GetAnimation(ov.hash, &block);
                    if (hierarchy)
                    {
                        if (OverrideAnimAlive(ped, ov) && ov.anim->m_pHierarchy == hierarchy)
                        {
                            ov.anim->SetCurrentTime(ov.time);
                            ov.anim->m_fSpeed = ov.speed;
                            ov.anim->m_bPlaying = (ov.flags & 1) != 0;
                        }
                        else
                        {
                            if (ov.blockIndex >= 0 && ov.blockIndex != blockIndex)
                                CAnimManager::RemoveAnimBlockRef(ov.blockIndex);
                            if (blockIndex != 0 && ov.blockIndex != blockIndex)
                            {
                                CAnimManager::AddAnimBlockRef(blockIndex);
                                ov.blockIndex = blockIndex;
                            }
                            const unsigned short flags = ov.flags & ~(unsigned short)(ANIMATION_TRANLSATE_X | ANIMATION_TRANLSATE_Y);
                            ov.anim = CAnimManager::BlendAnimation(ped->m_pRwClump, hierarchy, flags, 4.0f);
                            ov.animClump = ped->m_pRwClump;
                            CMissionMirror::LogEventf("interior-service-apply ped=%s block=%.15s hash=%08X anim=%d",
                                to_string(it->first).c_str(), ov.block, ov.hash, ov.anim ? 1 : 0);
                            if (ov.anim)
                            {
                                ov.anim->SetCurrentTime(ov.time);
                                ov.anim->m_fSpeed = ov.speed;
                            }
                        }
                    }
                }
            }
        }
        else if (OverrideAnimAlive(ped, ov))
        {
            ov.anim->m_nFlags |= (unsigned short)ANIMATION_FREEZE_LAST_FRAME;
            ov.anim->m_fBlendDelta = -4.0f;
            ov.anim = nullptr;
        }
        // the tray: a mod-created object tracked to the real keeper's hand (published by PublishHeld)
        if (ov.hasHeld && CConfigLoader::ms_bInteriorObjectSync)
        {
            if (ov.object && ov.object->m_nModelIndex != ov.held.model)
                DestroyObject(ov.object);
            if (!ov.object)
            {
                ov.object = CreateCopyObject(ov.held.model, ped->GetPosition());
                if (ov.object)
                    ov.object->RegisterReference((CEntity**)&ov.object);
            }
        }
        else
            DestroyObject(ov.object);
        ++it;
    }
}

// ---------------------------------------------------------------- guest: forward the hidden keeper's serving
static void SendServicePoses()
{
    const uint32_t now = GetTickCount();
    if (now - ms_nLastServiceSend < 150)
        return;
    ms_nLastServiceSend = now;
    const playerid_t owner = CInteriorOwnership::GetOwnerId();
    if (!owner.valid())
        return;
    static uint32_t lastGuestDiag = 0;
    const bool diag = now - lastGuestDiag >= 2000;
    if (diag)
        lastGuestDiag = now;
    for (CPed* local : CInteriorOwnership::GetHiddenServicePeds())
    {
        auto targetIt = ms_serviceTarget.find(local);
        char block[16] = {'\0'};
        uint32_t hash = 0;
        uint16_t flags = 0;
        float time = 0.0f, speed = 1.0f;
        const bool pose = CNetworkPed::GetNamedPose(local, block, hash, flags, time, speed);
        SHeldInfo held;
        const bool hasHeld = NativeHeldInfo(local, held);
        // serving = recent script opcode, a PLAYING named pose (long loops outlive the 2 s opcode grace) or an
        // object in hand; the frozen idle greeting (playing=0) alone must not hold an override on the owner
        const bool active = LocalServing(local) || (pose && (flags & 1)) || hasHeld;
        if (!active)
        {
            if (diag)
                CMissionMirror::LogEventf("interior-service-guest model=%d serving=0 pose=%d held=%d target=%d",
                    local->m_nModelIndex, pose ? 1 : 0, hasHeld ? 1 : 0, targetIt != ms_serviceTarget.end() ? 1 : 0);
            if (targetIt != ms_serviceTarget.end())
            {
                Packets::Peds::InteriorServicePose packet{};
                packet.owner = owner;
                packet.pedid = targetIt->second;
                packet.area = CInteriorOwnership::GetLocalArea();
                packet.clear = true;
                GetPacketFactory().Send(packet);
                ms_serviceTarget.erase(targetIt);
            }
            continue;
        }
        if (diag)
            CMissionMirror::LogEventf("interior-service-guest model=%d serving=1 pose=%d held=%d",
                local->m_nModelIndex, pose ? 1 : 0, hasHeld ? 1 : 0);
        if (!pose && !hasHeld)
            continue;
        // the nearest remote keeper of the same model (two model-155 keepers stand 3.3 m apart)
        CNetworkPed* best = nullptr;
        float bestDist = 4.0f;
        for (CNetworkPed* record : CNetworkPedManager::m_pPeds)
        {
            if (record->m_bSyncing || !record->m_bInteriorPopulation || !record->m_pPed ||
                !record->m_pPed->IsVTableValid() || record->m_nModelId != local->m_nModelIndex)
                continue;
            const float dist = DistanceBetweenPoints(record->m_pPed->GetPosition(), local->GetPosition());
            if (dist < bestDist)
            {
                bestDist = dist;
                best = record;
            }
        }
        if (!best)
        {
            if (diag)
                CMissionMirror::LogEventf("interior-service-guest model=%d serving=1 pose=%d held=%d match=0",
                    local->m_nModelIndex, pose ? 1 : 0, hasHeld ? 1 : 0);
            continue;
        }
        if (diag)
            CMissionMirror::LogEventf("interior-service-tx ped=%s block=%.15s hash=%08X held=%d",
                to_string(best->m_pedId).c_str(), block, hash, hasHeld ? 1 : 0);
        if (targetIt != ms_serviceTarget.end() && targetIt->second != best->m_pedId)
        {
            Packets::Peds::InteriorServicePose bye{};
            bye.owner = owner;
            bye.pedid = targetIt->second;
            bye.area = CInteriorOwnership::GetLocalArea();
            bye.clear = true;
            GetPacketFactory().Send(bye);
        }
        ms_serviceTarget[local] = best->m_pedId;
        Packets::Peds::InteriorServicePose packet{};
        packet.owner = owner;
        packet.pedid = best->m_pedId;
        packet.area = CInteriorOwnership::GetLocalArea();
        memcpy(packet.animBlock, block, 16);
        packet.animHash = hash;
        packet.animFlags = flags;
        packet.animTime = time;
        packet.animSpeed = speed;
        packet.hasHeld = hasHeld;
        if (hasHeld)
        {
            packet.heldModel = held.model;
            packet.bone = held.bone;
            packet.boneFlags = held.boneFlags;
            packet.offX = held.offset.x; packet.offY = held.offset.y; packet.offZ = held.offset.z;
        }
        GetPacketFactory().Send(packet);
    }
}

// ---------------------------------------------------------------- guest: maintain the visual copies
static void MaintainCopies()
{
    const uint32_t now = GetTickCount();
    for (auto it = ms_copies.begin(); it != ms_copies.end();)
    {
        SCopy& copy = it->second;
        CNetworkPed* record = CNetworkPedManager::GetPed(copy.pedid);
        if (!record)
        {
            DestroyObject(copy.object);
            ms_lastKnown.erase(copy.pedid);
            it = ms_copies.erase(it);
            continue;
        }
        CPed* ped = record->m_pPed;
        if (!ped || !ped->IsVTableValid() || record->m_bStreamedOut)
        {
            // keep the state; recreate when the ped streams back in (leak guard after 30 s)
            DestroyObject(copy.object);
            if (now - copy.since > COPY_PENDING_TIMEOUT_MS)
            {
                ms_lastKnown.erase(copy.pedid);
                it = ms_copies.erase(it);
            }
            else
                ++it;
            continue;
        }
        if (!copy.object)
        {
            copy.object = CreateCopyObject(copy.info.model, ped->GetPosition());
            if (copy.object)
                copy.object->RegisterReference((CEntity**)&copy.object);
            if (!copy.object)
            {
                if (now - copy.since > COPY_PENDING_TIMEOUT_MS)
                {
                    ms_lastKnown.erase(copy.pedid);
                    it = ms_copies.erase(it);
                }
                else
                    ++it;
                continue;
            }
            CMissionMirror::LogEventf("interior-held-copy model=%u ped=%s", (unsigned)copy.info.model,
                to_string(copy.pedid).c_str());
        }
        copy.since = now;
        ++it;
    }
}

// ---------------------------------------------------------------- public
void CInteriorObjectSync::Process()
{
    if (!CNetwork::m_bAuthenticated)
    {
        Reset();
        return;
    }
    const bool objOn = CConfigLoader::ms_bInteriorObjectSync;
    const bool svcOn = CConfigLoader::ms_bInteriorServiceGuest;
    if (!objOn && !svcOn)
    {
        ClearGuestCopies();
        ClearAllOverrides();
        ClearOwned();
        ms_sent.clear();
        ms_lastKnown.clear();
        ms_bOwnerInside = false;
        return;
    }
    ms_bOwnerInside = CConfigLoader::ms_bInteriorPopulationSync && CInteriorOwnership::IsOwner() &&
        CInteriorOwnership::IsInside();
    PruneActivity();
    if (CInteriorOwnership::IsOwner() && CInteriorOwnership::IsInside())
    {
        if (objOn)
            PublishHeld();
        else
            ClearOwned();
        if (svcOn)
            RefreshOverrides();
        else
            ClearAllOverrides();
    }
    else if (CInteriorOwnership::IsGuest())
    {
        if (objOn)
            MaintainCopies();
        else
            ClearGuestCopies();
        if (svcOn && !CMissionMode::IsIndividual())
            SendServicePoses();
    }
}

void CInteriorObjectSync::FrameUpdate()
{
    if (!CNetwork::m_bAuthenticated || !CInteriorOwnership::IsInside())
        return;
    if (CInteriorOwnership::IsGuest() && CConfigLoader::ms_bInteriorObjectSync)
    {
        for (auto& [id, copy] : ms_copies)
        {
            if (!copy.object)
                continue;
            CNetworkPed* record = CNetworkPedManager::GetPed(copy.pedid);
            if (record && record->m_pPed && record->m_pPed->IsVTableValid())
                TrackToBone(copy.object, record->m_pPed, copy.info);
        }
    }
    else if (CInteriorOwnership::IsOwner())
    {
        for (auto& [pedid, owned] : ms_owned)
        {
            CNetworkPed* record = CNetworkPedManager::GetPed(pedid);
            if (record && record->m_pPed && record->m_pPed->IsVTableValid())
                TrackToBone(owned.object, record->m_pPed, owned.info);
        }
        if (CConfigLoader::ms_bInteriorServiceGuest)
            for (auto& [pedid, ov] : ms_overrides)
            {
                if (!ov.object || ov.yielded)
                    continue;
                CNetworkPed* record = CNetworkPedManager::GetPed(pedid);
                if (record && record->m_pPed && record->m_pPed->IsVTableValid())
                    TrackToBone(ov.object, record->m_pPed, ov.held);
            }
    }
}

void CInteriorObjectSync::HandleHeldObject(Packets::Peds::InteriorHeldObject* packet)
{
    if (!CConfigLoader::ms_bInteriorObjectSync || !CInteriorOwnership::IsGuest())
        return;
    // only the current owner's objects: in-flight packets of a previous owner die here (handover)
    if (packet->senderPlayerid.value != CInteriorOwnership::GetOwnerId())
        return;
    if (packet->area != CInteriorOwnership::GetLocalArea())
        return;
    if (packet->modelId == 0)
    {
        auto it = ms_copies.find(packet->objectid);
        if (it != ms_copies.end())
        {
            DestroyObject(it->second.object);
            ms_copies.erase(it);
        }
        ms_lastKnown.erase(packet->pedid);
        return;
    }
    if (packet->bone >= MAX_BONE || packet->modelId >= MAX_MODEL || IsParachuteModel(packet->modelId))
        return;
    SHeldInfo info;
    info.model = packet->modelId;
    info.bone = packet->bone;
    info.boneFlags = packet->boneFlags;
    info.offset = CVector(packet->offX, packet->offY, packet->offZ);
    if (!std::isfinite(info.offset.x) || !std::isfinite(info.offset.y) || !std::isfinite(info.offset.z) ||
        info.offset.Magnitude() > MAX_OFFSET)
        info.offset = CVector{};
    auto it = ms_copies.find(packet->objectid);
    if (it == ms_copies.end())
    {
        SCopy copy;
        copy.pedid = packet->pedid;
        copy.info = info;
        copy.since = GetTickCount();
        ms_copies.emplace(packet->objectid, copy);
    }
    else
    {
        if (it->second.info.model != info.model)
            DestroyObject(it->second.object);
        it->second.info = info;
        it->second.pedid = packet->pedid;
    }
    ms_lastKnown[packet->pedid] = info;
}

void CInteriorObjectSync::HandleServicePose(Packets::Peds::InteriorServicePose* packet)
{
    if (!CConfigLoader::ms_bInteriorServiceGuest || !CInteriorOwnership::IsOwner())
        return;
    if (packet->senderPlayerid.value == CNetworkPlayerManager::m_localPlayerId)
        return;
    if (packet->area != CInteriorOwnership::GetLocalArea())
        return;
    CNetworkPed* record = CNetworkPedManager::GetPed(packet->pedid);
    if (!record || !record->m_bSyncing || !record->m_bInteriorPopulation || !record->m_pPed ||
        !record->m_pPed->IsVTableValid() || record->m_nAreaCode != packet->area)
        return;
    if (packet->clear)
    {
        auto it = ms_overrides.find(packet->pedid);
        if (it != ms_overrides.end() && it->second.sender == packet->senderPlayerid.value)
        {
            ClearOverride(it->first, it->second);
            ms_overrides.erase(it);
        }
        return;
    }
    // input clamps: a buggy or malicious guest must not crash the owner (held fields only when used)
    if (packet->hasHeld && (packet->bone >= MAX_BONE || packet->heldModel >= MAX_MODEL))
        return;
    packet->animBlock[15] = '\0';
    CVector offset(packet->offX, packet->offY, packet->offZ);
    if (!std::isfinite(offset.x) || !std::isfinite(offset.y) || !std::isfinite(offset.z) ||
        offset.Magnitude() > MAX_OFFSET)
        offset = CVector{};
    if (!std::isfinite(packet->animTime) || packet->animTime < 0.0f || packet->animTime > 100000.0f)
        packet->animTime = 0.0f;
    if (!std::isfinite(packet->animSpeed) || packet->animSpeed <= 0.0f || packet->animSpeed > 1000.0f)
        packet->animSpeed = 1.0f;
    auto it = ms_overrides.find(packet->pedid);
    if (it != ms_overrides.end() && it->second.sender != packet->senderPlayerid.value &&
        GetTickCount() - it->second.lastSeen <= SERVICE_TIMEOUT_MS)
    {
        CMissionMirror::LogEventf("interior-service-busy ped=%s sender=%u", to_string(packet->pedid).c_str(),
            packet->senderPlayerid.value.value());
        return;  // first sender wins while its override is fresh
    }
    SOverride& ov = (it == ms_overrides.end())
        ? ms_overrides.emplace(packet->pedid, SOverride{}).first->second
        : it->second;
    static uint32_t lastRxDiag = 0;
    if (GetTickCount() - lastRxDiag >= 2000)
    {
        lastRxDiag = GetTickCount();
        CMissionMirror::LogEventf("interior-service-rx ped=%s sender=%u block=%.15s hash=%08X held=%d",
            to_string(packet->pedid).c_str(), packet->senderPlayerid.value.value(), packet->animBlock,
            packet->animHash, packet->hasHeld ? 1 : 0);
    }
    ov.sender = packet->senderPlayerid.value;
    ov.lastSeen = GetTickCount();
    memcpy(ov.block, packet->animBlock, 16);
    ov.hash = packet->animHash;
    ov.flags = packet->animFlags;
    ov.time = packet->animTime;
    ov.speed = packet->animSpeed;
    ov.hasHeld = packet->hasHeld;
    if (packet->hasHeld)
    {
        ov.held.model = packet->heldModel;
        ov.held.bone = packet->bone;
        ov.held.boneFlags = packet->boneFlags;
        ov.held.offset = offset;
    }
}

void CInteriorObjectSync::ObserveNativeOpcode(CRunningScript* script, uint16_t opcode)
{
    const bool guestInside = CConfigLoader::ms_bInteriorServiceGuest && CInteriorOwnership::IsGuest() &&
        CInteriorOwnership::IsInside();
    if ((!ms_bOwnerInside && !guestInside) || COpCodeSync::bProcessingNetworkOpcode)
        return;
    // play anim / non-interruptable / pick up / the anim polls skbrain runs every frame while serving
    if (opcode != 0x0605 && opcode != 0x0812 && opcode != 0x070A && opcode != 0x0611 && opcode != 0x0613)
        return;
    if (!script || script->m_bIsMission)
        return;
    // This observer runs before the handler: ScriptParams still belongs to the previous opcode.
    const int pedHandle = script->CollectNextParameterWithoutIncreasingPC().iParam;

    auto* pool = CPools::ms_pPedPool;
    if (!pool || (static_cast<uint32_t>(pedHandle) >> 8) >= static_cast<uint32_t>(pool->m_nSize))
        return;
    CPed* ped = CPools::GetPed(pedHandle);
    if (!ped || !ped->IsVTableValid())
        return;
    if (guestInside && !ms_bOwnerInside)
    {
        // our own hidden keeper: the local junkfud/PSHOP is serving (the greeting at init also marks it,
        // for 2 s only; the idle greeting pose alone must not hold an override on the owner)
        if (CInteriorOwnership::IsServicePed(ped))
            ms_localServiceUntil[ped] = GetTickCount() + NATIVE_GRACE_MS;
        return;
    }
    CNetworkPed* record = CNetworkPedManager::GetPed(ped);
    if (CInteriorOwnership::IsServicePed(ped) || (record && record->m_bSyncing && record->m_bInteriorPopulation))
        ms_nativeUntil[ped] = GetTickCount() + NATIVE_GRACE_MS;
}

void CInteriorObjectSync::OnRoleChanged()
{
    if (CInteriorOwnership::IsOwner())
    {
        // we were just a guest: our copies become known state for the reconstruction in PublishHeld
        ClearGuestCopies();
        ClearAllOverrides();
        ms_sent.clear();
        ms_nNextId = 1;
        ms_bResendAll = true;
    }
    else
    {
        // guest or left: mod-created objects on real peds go away (native peds keep their own holds)
        ClearOwned();
        ClearAllOverrides();
        ms_sent.clear();
        ClearGuestCopies();  // the new owner publishes its own id space
        if (!CInteriorOwnership::IsInside())
            ms_lastKnown.clear();
    }
    ms_serviceTarget.clear();
    ms_nativeUntil.clear();
    ms_localServiceUntil.clear();
}

void CInteriorObjectSync::OnPedDestroyed(CNetworkPed* record)
{
    if (!record)
        return;
    const pedid_t pedid = record->m_pedId;
    for (auto& [id, copy] : ms_copies)
        if (copy.pedid == pedid)
            DestroyObject(copy.object);  // state stays: MaintainCopies recreates on stream-in
    auto ownedIt = ms_owned.find(pedid);
    if (ownedIt != ms_owned.end())
    {
        DestroyObject(ownedIt->second.object);
        ms_owned.erase(ownedIt);
    }
    auto ovIt = ms_overrides.find(pedid);
    if (ovIt != ms_overrides.end())
    {
        // the clump is going away: the association dies with it, only the block ref and object are ours
        if (ovIt->second.blockIndex >= 0)
            CAnimManager::RemoveAnimBlockRef(ovIt->second.blockIndex);
        DestroyObject(ovIt->second.object);
        ms_overrides.erase(ovIt);
    }
}

void CInteriorObjectSync::Reset()
{
    ClearGuestCopies();
    ClearOwned();
    ClearAllOverrides();
    ms_sent.clear();
    ms_lastKnown.clear();
    ms_serviceTarget.clear();
    ms_nativeUntil.clear();
    ms_localServiceUntil.clear();
    ms_bOwnerInside = false;
    ms_nNextId = 1;
    ms_bResendAll = false;
}
