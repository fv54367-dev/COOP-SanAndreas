#include "stdafx.h"
#include "CPedDamageSync.h"
#include "CJackSync.h"
#include <CPedModelInfo.h>
#include "CNetworkPed.h"
#include "CInteriorObjectSync.h"


#include <CTaskSimpleCarSetPedInAsPassenger.h>
#include <CCarEnterExit.h>
#include <CTaskSimpleCarSetPedOut.h>
#include <CTaskComplexEnterCarAsPassenger.h>
#include <eAnimations.h>
#include <eModelID.h>
#include <CAnimBlendHierarchy.h>
#include <Hooks/PedHooks.h>
#include "CMissionMirror.h"
#include "CInteriorOwnership.h"
#include <COpCodeSync.h>
#include <CTaskComplexDie.h>
#include <CTaskSimpleGangDriveBy.h>

// npc-task-sync: games are 50 ms long
static const uint32_t TASK_SYNC_DURATION = 5000;
// npc-task-sync: animations are sent at most once per second
static const uint32_t TASK_SYNC_SEND_INTERVAL = 1000;
// npc-named-anim-sync: while the ped has an animation the full state is resent this often (the receiver may have
// streamed in / finished loading the block after the last change and would never receive it otherwise)
static const uint32_t TASK_SYNC_ANIM_RESEND_INTERVAL = 3000;
// npc-named-anim-sync: only peds this close to some network player get the periodic resend (indoors they are few)
static const float TASK_SYNC_ANIM_RESEND_RADIUS = 60.0f;
// npc-named-anim-sync: a state whose animation block is still loading is retried for this long
static const uint32_t TASK_SYNC_PENDING_TIMEOUT = CNetworkPed::PENDING_TASK_STATE_TIMEOUT;
// npc-named-anim-sync: the association of a named animation has no group (see the CAnimBlendAssociation
// (RpClump*, CAnimBlendHierarchy*) constructor): m_nAnimGroup is -1 and m_nAnimId -1
static const uint16_t NAMED_ANIM_GROUP = 0xFFFF;

// plugin-sdk declares CAnimManager::ms_aAnimAssocGroups as an array, but 0xB4EA34 holds a pointer to it
static CAnimBlendAssocGroup* GetAnimAssocGroups()
{
    return *(CAnimBlendAssocGroup**)0xB4EA34;
}

// the game requests the animation block `index` as the streaming model 25575 + index (see CParachuteSync)
static const int ANIM_BLOCK_MODEL_BASE = 25575;

// index of the block the group plays from (-1 if the group has no block)
static int GetAnimBlockIndex(CAnimBlendAssocGroup& assocGroup, int group)
{
    // D bug 2: ask the block by name instead of trusting m_pAnimBlock, which is nullptr until THIS game loaded the
    // block (CAnimManager::CreateAnimAssocGroups skips unloaded blocks): dancing, bar, int_house... never loaded
    // would otherwise be dropped. With the switch off, the old (broken) behaviour is kept.
    if (CConfigLoader::ms_bNpcNamedAnimSync)
    {
        const char* blockName = CAnimManager::GetAnimBlockName(group);
        if (blockName != nullptr && blockName[0] != '\0')
            return CAnimManager::GetAnimationBlockIndex(blockName);
        return -1;
    }

    if (assocGroup.m_pAnimBlock == nullptr)
        return -1;

    const int index = (int)(assocGroup.m_pAnimBlock - CAnimManager::ms_aAnimBlocks);
    if (index < 0 || index >= CAnimManager::ms_numAnimBlocks)
        return -1;
    return index;
}

// the animation groups worth replicating (the rest follow from the position and the move state):
// falls / knock downs / getting up, cower, hands up, phone, sitting, cashier, idle chat, melee, swimming
static bool IsRelevantDefaultAnimation(int animId)
{
    // falls, knock downs and getting up
    if (animId >= ANIM_DEFAULT_KO_SHOT_FRONT_0 && animId <= ANIM_DEFAULT_KO_SPIN_L)
        return true;
    if (animId == ANIM_DEFAULT_FLOOR_HIT || animId == ANIM_DEFAULT_FLOOR_HIT_F)
        return true;
    if (animId >= ANIM_DEFAULT_GETUP_0 && animId <= ANIM_DEFAULT_GETUP_FRONT)
        return true;

    // cower and hands up
    if (animId >= ANIM_DEFAULT_DUCK_COWER && animId <= ANIM_DEFAULT_HANDSCOWER)
        return true;

    // phone
    if (animId >= ANIM_DEFAULT_PHONE_IN && animId <= ANIM_DEFAULT_PHONE_TALK)
        return true;

    // sitting and the ATM (the cashier itself is in the int_shop group)
    if (animId >= ANIM_DEFAULT_SEAT_DOWN && animId <= ANIM_DEFAULT_SEAT_IDLE)
        return true;
    if (animId == ANIM_DEFAULT_ATM)
        return true;

    // idle chat
    if (animId == ANIM_DEFAULT_IDLE_CHAT)
        return true;

    return false;
}

static bool IsRelevantAnimation(unsigned short group, short animId)
{
    switch (group)
    {
    case ANIM_GROUP_DEFAULT:
        return IsRelevantDefaultAnimation(animId);
    case ANIM_GROUP_MELEE_1:
    case ANIM_GROUP_MELEE_2:
    case ANIM_GROUP_MELEE_3:
    case ANIM_GROUP_MELEE_4:
    case ANIM_GROUP_BBBAT_1:
    case ANIM_GROUP_GCLUB_1:
    case ANIM_GROUP_KNIFE_1:
    case ANIM_GROUP_SWORD_1:
    case ANIM_GROUP_DILDO_1:
    case ANIM_GROUP_FLOWERS_1:
    case ANIM_GROUP_CSAW_1:
    case ANIM_GROUP_KICK_STD:
    case ANIM_GROUP_PISTLWHP:
    case ANIM_GROUP_STEALTH_KN:
    case ANIM_GROUP_BEACH:
    case ANIM_GROUP_SUNBATHE:
    case ANIM_GROUP_PLAYIDLES:
    case ANIM_GROUP_RIOT:
    case ANIM_GROUP_GANGS:
    case ANIM_GROUP_ATTRACTORS:
    case ANIM_GROUP_SWIM:
    case ANIM_GROUP_INT_HOUSE:
    case ANIM_GROUP_INT_OFFICE:
    case ANIM_GROUP_INT_SHOP:
        return true;
    default:
        return false;
    }
}

// Peds the game makes inside an interior (diners, clerks) keep m_nAreaCode 0 (testeo 17-11: dup-ped area=0/5, diners
// hosted with area=0): the interior pose rules must follow the record, which knows the interior, not the engine tag.
static bool IsInteriorActor(CPed* ped)
{
    if (!CConfigLoader::ms_bInteriorPopulationSync)
        return false;
    if (ped->m_nAreaCode != 0)
        return true;
    if (!CConfigLoader::ms_bInteriorAreaFallback)
        return false;
    CNetworkPed* record = CNetworkPedManager::GetPed(ped);
    return record && record->m_bInteriorPopulation;
}

// the association with the biggest blend among the whitelisted groups
static CAnimBlendAssociation* FindRelevantAssociation(CPed* ped)
{
    if (ped == nullptr || ped->m_pRwClump == nullptr)
        return nullptr;

    // an interior actor's seat pose (the pizzeria diners, 2026-11-18): SEAT_IDLE is a one-shot, it finishes and stays
    // frozen at full blend while the ped keeps sitting; the playing filter dropped it and the copies stood on the
    // chairs (the named-anim path below already relaxes it the same way for interiors)
    const bool interiorActor = IsInteriorActor(ped);

    CAnimBlendAssociation* best = nullptr;
    for (CAnimBlendAssociation* assoc = RpAnimBlendClumpGetFirstAssociation(ped->m_pRwClump); assoc;
         assoc = RpAnimBlendGetNextAssociation(assoc))
    {
        if ((!assoc->m_bPlaying && !interiorActor) || assoc->m_fBlendAmount <= 0.0f)
            continue;
        if (!IsRelevantAnimation(assoc->m_nAnimGroup, assoc->m_nAnimId))
            continue;
        if (best == nullptr || assoc->m_fBlendAmount > best->m_fBlendAmount)
            best = assoc;
    }
    return best;
}

// D npc-named-anim-sync: the animation of a named IFP block (dancing, bar, strip, casino, food...) has no assoc
// group: it is the one with the biggest blend whose group is -1 and whose hierarchy is known. Partial associations
// (look / walk overlays) are never replicated.
// the block a hierarchy belongs to, by its slot in CAnimManager::ms_aAnimations (each block owns the range
// startAnimation .. +animationCount); m_nAnimBlockId as a fallback
static int BlockOfHierarchy(const CAnimBlendHierarchy* hierarchy)
{
    const int index = static_cast<int>(hierarchy - CAnimManager::ms_aAnimations);
    for (int i = 0; i < CAnimManager::ms_numAnimBlocks; i++)
    {
        const CAnimBlock& block = CAnimManager::ms_aAnimBlocks[i];
        if (block.bLoaded && index >= block.startAnimation && index < block.startAnimation + block.animationCount)
            return i;
    }
    return hierarchy->m_nAnimBlockId;
}

static bool IsNamedAnimTask(CPed* ped)
{
    if (ped->m_pIntelligence == nullptr)
        return false;
    CTask* task = ped->m_pIntelligence->m_TaskMgr.m_aPrimaryTasks[TASK_PRIMARY_PRIMARY];
    for (; task != nullptr; task = task->GetSubTask())
    {
        if (task->GetTaskType() == TASK_SIMPLE_NAMED_ANIM)
            return true;
    }
    return false;
}

static CAnimBlendAssociation* FindNamedAssociation(CPed* ped, bool forceInteriorActor = false)
{
    if (ped == nullptr || ped->m_pRwClump == nullptr)
        return nullptr;

    // a script's TASK_PLAY_ANIM in an interior (Home Invasion's sleeping owner, testeo 17-11): the script may play it
    // as a partial one (INT_HOUSE BED_Loop_R, flags 0x401B) and it was never sent: the copy stood.
    // the pizzeria diners (Well Stacked, testeo 18-11): PCHAIR plays their seat pose with 0x0812
    // (TASK_PLAY_ANIM_NON_INTERRUPTABLE), which leaves a partial FOOD association but NO named-anim task
    // (interior-anim-owner: task=200, primary slots empty, the top full-body anim is the standing idle).
    // Requiring the task dropped the pose and the copies stood on the chairs: for an interior actor a strong
    // partial named association is the pose itself, task or not (the bartender's 0x0605 does leave a task).
    // npc-anim-attached (Catalyst, 20-11): the same for a ped ATTACHED to an entity. Ryder stands on the Picador
    // (ryder3.sc:1853) and catches the crates with TASK_PICK_UP_OBJECT ... CATCH_BOX BOX (:2156); holding an
    // animation BY HIERARCHY leaves group ANIM_GROUP_NONE (-1, AnimBlendAssociation.cpp), so it was dropped twice:
    // here (partial without an interior) and in SendAnimSet's group loop. The guest only saw his idle.
    const bool interiorActor = forceInteriorActor || IsInteriorActor(ped);
    const bool poseActor = interiorActor || (CConfigLoader::ms_bNpcAnimAttached && ped->m_pAttachedTo != nullptr);

    CAnimBlendAssociation* best = nullptr;
    for (CAnimBlendAssociation* assoc = RpAnimBlendClumpGetFirstAssociation(ped->m_pRwClump); assoc;
         assoc = RpAnimBlendGetNextAssociation(assoc))
    {
        if ((!assoc->m_bPlaying && !poseActor) ||
            assoc->m_fBlendAmount <= 0.0f || (assoc->m_bPartial && !(poseActor && assoc->m_fBlendAmount > 0.5f)) ||
            (poseActor && assoc->m_fBlendDelta < 0.0f))
            continue;
        if (assoc->m_pHierarchy == nullptr)
            continue;
        const int blockId = BlockOfHierarchy(assoc->m_pHierarchy);
        if (assoc->m_nAnimGroup != NAMED_ANIM_GROUP)
            continue;
        if (blockId < 0 || blockId >= CAnimManager::ms_numAnimBlocks)
            continue;
        if (best == nullptr || assoc->m_fBlendAmount > best->m_fBlendAmount)
            best = assoc;
    }
    return best;
}

bool CNetworkPed::GetNamedPose(CPed* ped, char block[16], uint32_t& hash, uint16_t& flags, float& time, float& speed)
{
    CAnimBlendAssociation* assoc = FindNamedAssociation(ped, true);
    if (!assoc || !assoc->m_pHierarchy)
        return false;
    const int blockId = BlockOfHierarchy(assoc->m_pHierarchy);
    if (blockId < 0 || blockId >= CAnimManager::ms_numAnimBlocks)
        return false;
    memset(block, 0, 16);
    strncpy_s(block, 16, CAnimManager::ms_aAnimBlocks[blockId].szName, _TRUNCATE);
    hash = assoc->m_pHierarchy->m_hashKey;
    flags = assoc->m_nFlags;
    time = assoc->m_fCurrentTime;
    speed = assoc->m_fSpeed;
    return true;
}



// D bug 3: the periodic resend of the state is only worth it near another player (indoors that is a handful of peds)
static bool IsNearAnyNetworkPlayer(CPed* ped, float radius)
{
    for (CNetworkPlayer* networkPlayer : CNetworkPlayerManager::m_pPlayers)
    {
        if (networkPlayer->m_pPed != nullptr &&
            DistanceBetweenPoints(networkPlayer->m_pPed->GetPosition(), ped->GetPosition()) < radius)
            return true;
    }
    return false;
}

// CAnimManager::BlendAnimation wants the global id (the one in eAnimations.h); in a loaded group the id can be
// read either way, so accept both and return the global one (-1 when the animation can't be played)
static int NormalizeAnimId(int group, int animId)
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

static bool IsCarTask(int taskType)
{
    return taskType == TASK_COMPLEX_ENTER_CAR_AS_DRIVER || taskType == TASK_COMPLEX_ENTER_CAR_AS_PASSENGER ||
           taskType == TASK_COMPLEX_LEAVE_CAR;
}

static int8_t SanitizeDoor(int door)
{
    return (door == 8 || door == 9 || door == 11) ? (int8_t)door : (int8_t)0;
}

CNetworkPed::CNetworkPed(pedid_t pedid, int modelId, ePedType pedType, CVector pos, unsigned char createdBy, char specialModelName[], bool createNow)
{
    m_pedId = pedid;
    m_nPedType = pedType;
    m_bSyncing = false;
    m_nCreatedBy = createdBy;
    m_nModelId = modelId;
    m_vecLastPos = pos;
    if (specialModelName)
    {
        memcpy(m_szSpecialModelName, specialModelName, sizeof(m_szSpecialModelName));
        m_szSpecialModelName[sizeof(m_szSpecialModelName) - 1] = '\0';
    }

    if (!createNow)
    {
        m_bStreamedOut = true;
        return;
    }

    m_bStreamedOut = !CreateGamePed(pos);
}

void CNetworkPed::LogCopyReason(const char* reason, int slot)
{
    if (m_bSyncing || std::find(m_aCopyReasons.begin(), m_aCopyReasons.end(), reason) != m_aCopyReasons.end())
        return;
    m_aCopyReasons.emplace_back(reason);
    if (slot < 0) slot = m_nLocalModelId;
    auto info = slot >= 0 && slot < 300 ? CModelInfo::ms_modelInfoPtrs[slot] : nullptr;
    CMissionMirror::LogEventf("ped-copy-fail reason=%s model=%d special=%s slot=%d refs=%d ped=%s area=%u local-area=%d visible=%d pos=%.1f,%.1f,%.1f",
        reason, m_nModelId, m_szSpecialModelName, slot, info ? info->m_nRefCount : -1,
        to_string(m_pedId).c_str(), m_pPed ? m_pPed->m_nAreaCode : m_nAreaCode,
        FindPlayerPed(0) ? FindPlayerPed(0)->m_nAreaCode : -1, m_pPed ? m_pPed->m_bIsVisible : 0,
        m_vecLastPos.x, m_vecLastPos.y, m_vecLastPos.z);
}

void CNetworkPed::ValidateCopyModel()
{
    if (m_bSyncing) return;
    if (m_pPed && m_nCreatedBy == MISSION_CHAR)
    {
        auto local = FindPlayerPed(0);
        if (local && local->m_nAreaCode != m_pPed->m_nAreaCode) LogCopyReason("area-mismatch");
        if (!m_pPed->m_bIsVisible) LogCopyReason("hidden");
    }
    if (m_pPed && m_nModelId >= 290 && m_nModelId <= 299 && m_szSpecialModelName[0] &&
        (m_pPed->m_nModelIndex != m_nLocalModelId || m_nLocalModelId < 290 || m_nLocalModelId > 299 ||
         !CModelInfo::ms_modelInfoPtrs[m_nLocalModelId] ||
         CModelInfo::ms_modelInfoPtrs[m_nLocalModelId]->m_nKey != CKeyGen::GetUppercaseKey(m_szSpecialModelName)))
    {
        m_vecLastPos = m_pPed->GetPosition();
        DestroyGamePed("special-reassigned");
        m_bStreamedOut = true;
    }
    if (!m_pPed && !m_bStreamedOut)
    {
        LogCopyReason("removed-by-game");
        m_bStreamedOut = true;
    }
}

bool CNetworkPed::CreateGamePed(CVector pos)
{
    if (m_nCopyRetryAt && (int32_t)(CTimer::m_snTimeInMilliseconds - m_nCopyRetryAt) < 0)
        return false;
    m_nCopyRetryAt = CTimer::m_snTimeInMilliseconds + 1000;
    auto fail = [this](const char* reason, int slot) { LogCopyReason(reason, slot); return false; };
    int modelId = m_nModelId;
    ePedType pedType = m_nPedType;
    char* specialModelName = m_szSpecialModelName;

    // reject ids that are not ped models (a corrupted/foreign packet would crash the streaming code)
    if (modelId < 0 || modelId > 299 || !CModelInfo::ms_modelInfoPtrs[modelId]
        || CModelInfo::ms_modelInfoPtrs[modelId]->GetModelType() != MODEL_INFO_PED)
        return fail("invalid-model", modelId);

    if (modelId >= 290 && modelId <= 299)
    {
        // RequestSpecialModel (0x409D10) does not check that the name exists in the extra objects dir: a missing or
        // unknown name leaves the slot with size 0 and LoadAllRequestedModels spins forever in
        // CStreaming::RetryLoadFile (0x4076C0) -> the game hangs (gta-reversed Streaming.cpp)
        unsigned int offset = 0, size = 0;
        if (!specialModelName[0] || !CStreaming::ms_pExtraObjectsDir
            || !CStreaming::ms_pExtraObjectsDir->FindItem(specialModelName, offset, size) || !size)
            return fail("special-missing", modelId);

        // Streaming.cpp 0x409D10 deletes users of a reassigned slot. Resolve by name locally.
        const auto key = CKeyGen::GetUppercaseKey(specialModelName);
        int freeSlot = -1, matchingSlot = -1;
        for (int slot = 290; slot <= 299; ++slot)
        {
            auto info = CModelInfo::ms_modelInfoPtrs[slot];
            if (!info || info->GetModelType() != MODEL_INFO_PED) continue;
            auto& stream = CStreaming::ms_aInfoForModel[slot];
            if (info->m_nKey == key && stream.m_nCdSize != 0)
            {
                matchingSlot = slot;
                break;
            }
            // A zero-ref slot can still belong to LOAD_SPECIAL_CHARACTER or a cutscene.
            if (freeSlot < 0 && !info->m_nRefCount && !(stream.m_nFlags & 0x3E) &&
                (stream.m_nLoadState == LOADSTATE_NOT_LOADED || stream.m_nLoadState == LOADSTATE_LOADED))
                freeSlot = slot;
        }
        modelId = matchingSlot >= 0 ? matchingSlot : freeSlot;
        if (modelId < 0)
            return fail("special-slots-busy", m_nModelId);

        CStreaming::RequestSpecialModel(modelId, specialModelName, 0);
        strcpy_s(PedHooks::ms_aszLoadedSpecialModels[modelId - 290], m_szSpecialModelName);
    }
    else
        CStreaming::RequestModel(modelId, 0);

    CStreaming::LoadAllRequestedModels(false);

    if (CStreaming::ms_aInfoForModel[modelId].m_nLoadState != LOADSTATE_LOADED)
        return fail("load-failed", modelId);

    // CPed::CPed indexes the stats table with the ped type (a garbage type crashes inside the constructor)
    if ((unsigned)pedType >= 32)
        return fail("invalid-ped-type", modelId);

    // CPed::CPed reads the stats table with the model's stat id: a model slot that is not fully set up (special
    // models whose name did not arrive, a stale slot) has a garbage id and the game crashes inside the constructor
    if (((CPedModelInfo*)CModelInfo::ms_modelInfoPtrs[modelId])->m_nStatType >= 64)
        return fail("invalid-stat-type", modelId);

    m_nLocalModelId = modelId;

    if (pedType == PED_TYPE_COP)
    {
        switch (modelId) 
        {
        case MODEL_LAPDM1:
            modelId = COP_TYPE_LAPDM1;
            break;
        case MODEL_CSHER:
            modelId = COP_TYPE_CSHER;
            break;
        case MODEL_SWAT:
            modelId = COP_TYPE_SWAT1;
            break;
        case MODEL_FBI:
            modelId = COP_TYPE_FBI;
            break;
        case MODEL_ARMY:
            modelId = COP_TYPE_ARMY;
            break;
        }
    }

    if (pedType == PED_TYPE_COP)
    {
        m_pPed = new CCopPed((eCopType)modelId);
    }
    else if (pedType == PED_TYPE_MEDIC || pedType == PED_TYPE_FIREMAN)
    {
        m_pPed = new CEmergencyPed(pedType, modelId);
    }
    else
    {
        m_pPed = new CCivilianPed(pedType, modelId);
    }

    // Keep the record so streaming can retry after the pool has room.
    if (!m_pPed)
        return fail("ped-pool-full", m_nLocalModelId);

    m_pPed->RegisterReference((CEntity**)&m_pPed);

    m_pPed->m_nCreatedBy = 2;
    m_pPed->m_pIntelligence->SetPedDecisionMakerType(-1);
    m_pPed->m_pIntelligence->SetSeeingRange(30.0);
    m_pPed->m_pIntelligence->SetHearingRange(30.0);
    m_pPed->m_pIntelligence->m_fDmRadius = 0.0f;
    m_pPed->m_pIntelligence->m_nDmNumPedsToScan = 0;
    
    m_pPed->SetPosn(pos);
    m_pPed->SetOrientation(0.f, 0.f, 0.f);
    CWorld::Add(m_pPed);

    // keep the interior the record knows (a remote copy created while we were outside has no area yet)
    if (m_nAreaCode != 0)
        m_pPed->m_nAreaCode = m_nAreaCode;

    // The copy has no AI to give it a first animation and the game reads the first animation of every ped without checking
    // it (CPed::PlayFootSteps, the RpAnimBlend callbacks): a copy whose first ProcessControl ran before its first synced
    // animation crashed the game (2026-10-20: dump of Jugador1, a civilian copy with an empty animation list). It starts idle.
    if (m_pPed->m_pRwClump && RpAnimBlendClumpGetFirstAssociation(m_pPed->m_pRwClump) == nullptr)
        CAnimManager::BlendAnimation(m_pPed->m_pRwClump, (int)m_pPed->m_nAnimGroup, 3 /* ANIM_ID_IDLE */, 4.0f);

    // THIS IS AN EXPERIMENTAL SOLUTION FOR THE 0x4D68BA CRASH
    m_pPed->m_bStreamingDontDelete = true;

    m_pPed->m_fHealth = m_fHealth;
    m_bStreamedOut = false;
    CMissionMirror::LogEventf("ped-copy-created ped=%s model=%d special=%s slot=%d area=%u",
        to_string(m_pedId).c_str(), m_nModelId, m_szSpecialModelName, m_nLocalModelId, m_nAreaCode);

    // a copy that died while streamed out falls again instead of coming back standing
    ApplyRemoteDeath();
    return true;
}

void CNetworkPed::DestroyGamePed(const char* reason)
{
    if (m_pPed) LogCopyReason(reason);
    CInteriorObjectSync::OnPedDestroyed(this);


    // the association belongs to the clump that is going away, and the block it played from must be free again
    ReleaseSyncedAnimBlock();
    m_pSyncedAnim = nullptr;
    m_bSyncedAnimNamed = false;
    StopHostedAnim();
    m_nSkipPosSyncUntil = 0;
    m_nDeathRequestedAt = 0;
    ReleaseMirrorAnimBlocks();

    if (m_pPed && m_bAdoptedServicePed)
    {
        // the barber / cashier still belongs to BARB, JFUD, CLOTH...: the script keeps using its handle
        m_pPed->CleanUpOldReference((CEntity**)&m_pPed);
    }
    else if (m_pPed)
    {
        m_pPed->CleanUpOldReference((CEntity**)&m_pPed);

        if (m_pPed->m_matrix && m_pPed->m_matrix->m_pOwner)
        {
            if (m_nBlipHandle != -1)
            {
                CRadar::ClearBlipForEntity(eBlipType::BLIP_CHAR, CPools::GetPedRef(m_pPed));
                m_nBlipHandle = -1;
            }

            if (m_pPed->m_nPedFlags.bInVehicle)
            {
                plugin::Command<Commands::WARP_CHAR_FROM_CAR_TO_COORD>(CPools::GetPedRef(m_pPed), 0.f, 0.f, 0.f);
            }

            CWorld::Remove(m_pPed);
            //CWorld::RemoveReferencesToDeletedObject(m_pPed);
            m_pPed->Remove();
            delete m_pPed;
        }
    }
    m_pPed = nullptr;
    m_bAdoptedServicePed = false;
}

void CNetworkPed::StreamOut()
{
    if (m_bSyncing || !m_pPed)
        return;

    m_vecLastPos = m_pPed->GetPosition();
    m_fHealth = m_pPed->m_fHealth;
    DestroyGamePed("distance-stream-out");
    m_bHasPosError = false;
    m_bStreamedOut = true;
}

bool CNetworkPed::StreamIn()
{
    if (!m_bStreamedOut)
        return m_pPed != nullptr;

    // suppressed interior duplicate: keep only the record until its condition goes away
    if (m_bSuppressedDuplicate)
    {
        LogCopyReason("interior-suppressed");
        return false;
    }

    return CreateGamePed(m_vecLastPos);
}

// B2 handover: the game called CWorld::Remove on a ped we host. Keep the record as a remote streamed out copy of
// another player (the game finishes deleting the game ped right after the hook) and offer it to the server.
void CNetworkPed::Release()
{
    if (!m_pPed || !m_pPed->IsVTableValid())
        return;

    m_vecLastPos = m_pPed->GetPosition();
    m_fHealth = m_pPed->m_fHealth;
    if (m_pPed->m_nAreaCode != 0)
        m_nAreaCode = m_pPed->m_nAreaCode;

    Packets::Peds::PedRelease packet{};
    packet.pedid = m_pedId;
    packet.pos = m_vecLastPos;
    packet.heading = m_pPed->GetHeading();
    packet.health = m_fHealth;
    packet.areaCode = m_pPed->m_nAreaCode;
    packet.reason = 0;
    GetPacketFactory().Send(packet);

    // same clean up as DestroyGamePed but without removing / deleting the game ped: the game is about to do it
    ReleaseSyncedAnimBlock();
    m_pSyncedAnim = nullptr;
    m_bSyncedAnimNamed = false;
    StopHostedAnim();
    m_nSkipPosSyncUntil = 0;

    if (m_nBlipHandle != -1)
    {
        CRadar::ClearBlipForEntity(eBlipType::BLIP_CHAR, CPools::GetPedRef(m_pPed));
        m_nBlipHandle = -1;
    }

    m_pPed->CleanUpOldReference((CEntity**)&m_pPed);

    m_pPed = nullptr;
    m_bAdoptedServicePed = false;
    m_bSyncing = false;
    m_bStreamedOut = true;
    m_bHasPosError = false;
}

// destroy the game ped but keep the record: another player's copy of an interior ped we already have locally
void CNetworkPed::SuppressDuplicate(unsigned char areaCode)
{
    if (m_bSyncing)
        return;

    if (m_pPed)
    {
        m_vecLastPos = m_pPed->GetPosition();
        m_fHealth = m_pPed->m_fHealth;
        if (m_pPed->m_nAreaCode != 0)
            m_nAreaCode = m_pPed->m_nAreaCode;
        DestroyGamePed("interior-suppressed");
    }
    LogCopyReason("interior-suppressed");

    if (areaCode != 0)
        m_nAreaCode = areaCode;

    m_bHasPosError = false;
    m_bStreamedOut = true;
    m_bSuppressedDuplicate = true;
}

CNetworkPed::~CNetworkPed()
{
    // the record goes away (as a copy or because it was taken over by us): the block reference must go too
    ReleaseSyncedAnimBlock();
    StopHostedAnim();
    ReleaseMirrorAnimBlocks();

    if (m_bSyncing)
    {
        if (m_pPed)
        {
            m_pPed->CleanUpOldReference((CEntity**)&m_pPed);
        }

        Packets::Peds::PedRemove packet{};
        packet.pedid = m_pedId;
        GetPacketFactory().Send(packet);
    }
    else
    {
        DestroyGamePed();
    }
    m_pPed = nullptr;
    m_bAdoptedServicePed = false;
}

CNetworkPed* CNetworkPed::CreateHosted(CPed* ped)
{
    ped->field_54C += 5000; // m_nTimeTillWeNeedThisPed

    CNetworkPed* networkPed = new CNetworkPed();

    networkPed->m_pPed = ped;
    ped->RegisterReference((CEntity**)&networkPed->m_pPed);

    networkPed->m_pedId = CNetworkPedManager::AllocatePedId();
    networkPed->m_nCreatedBy = ped->m_nCreatedBy;
    networkPed->m_nModelId = ped->m_nModelIndex;
    networkPed->m_nPedType = static_cast<ePedType>(ped->m_nPedType);  // used when this record is re-created in another client
    unsigned char hostedArea = ped->m_nAreaCode;
    // interior-area-fallback: a ped the game makes inside an interior has no area tag (diners, clerks; testeo 17-11):
    // it takes the interior we stand in (interiors are far from the main map, so distance is enough)
    if (hostedArea == 0 && CConfigLoader::ms_bInteriorPopulationSync && CConfigLoader::ms_bInteriorAreaFallback)
    {
        CPlayerPed* local = FindPlayerPed(0);
        if (local && local->m_nAreaCode != 0 && ped != local && ped->m_matrix &&
            (CInteriorOwnership::IsServicePed(ped) ||
                (ped->m_nCreatedBy != MISSION_CHAR && COpCodeSync::GetScriptRunningCommand() == nullptr)) &&
            DistanceBetweenPoints(ped->GetPosition(), local->GetPosition()) < CInteriorOwnership::SAME_INTERIOR_RADIUS)
            hostedArea = local->m_nAreaCode;
    }
    networkPed->m_nAreaCode = hostedArea;
    networkPed->m_bSyncing = true;
    networkPed->m_bInteriorPopulation = CConfigLoader::ms_bInteriorPopulationSync &&
        (CInteriorOwnership::IsServicePed(ped) || (ped->m_nCreatedBy != MISSION_CHAR && hostedArea != 0 &&
            COpCodeSync::GetScriptRunningCommand() == nullptr));
    if (CConfigLoader::ms_bInteriorPopulationSync)
        if (auto script = COpCodeSync::GetScriptRunningCommand(); script && script->m_bIsMission)
            networkPed->m_nCreatedBy = MISSION_CHAR;
    if (networkPed->m_bInteriorPopulation)
        CMissionMirror::LogEventf("interior-publish ped=%s model=%d area=%u ped-area=%u",
            to_string(networkPed->m_pedId).c_str(), ped->m_nModelIndex, hostedArea, ped->m_nAreaCode);

    CNetworkPedManager::Add(networkPed);

    Packets::Peds::PedSpawn packet{};
    packet.pedid = networkPed->m_pedId;
    packet.modelId = static_cast<eModelID>(ped->m_nModelIndex);
    packet.pos = ped->m_matrix->pos;
    packet.pedType = static_cast<ePedType>(ped->m_nPedType);
    packet.createdBy = static_cast<eCharCreatedBy>(networkPed->m_nCreatedBy);
    packet.interiorPopulation = networkPed->m_bInteriorPopulation;
    packet.areaCode = networkPed->m_nAreaCode;

    if (packet.modelId >= MODEL_SPECIAL01 && packet.modelId <= MODEL_SPECIAL10)
    {
        strcpy_s(packet.specialModelName, PedHooks::ms_aszLoadedSpecialModels[packet.modelId - MODEL_SPECIAL01]);
        packet.specialModelName[7] = '\0';
        strcpy_s(networkPed->m_szSpecialModelName, packet.specialModelName);
    }
    GetPacketFactory().Send(packet);

    return networkPed;
}

void CNetworkPed::WarpIntoVehicleDriver(CVehicle* vehicle)
{
    COOP_VERIFY(m_pPed != nullptr);

    if (!vehicle->IsVTableValid() || !m_pPed->IsVTableValid())
    {
        return;
    }

    if (m_pPed->m_nPedFlags.bInVehicle && m_pPed->m_pVehicle)
    {
        RemoveFromVehicle(m_pPed->m_pVehicle);
    }

    m_pPed->m_pIntelligence->FlushImmediately(false);

    if (!m_bSyncing)
    {
        m_pPed->m_nPedFlags.CantBeKnockedOffBike = 1; // 1 - never
    }

    auto task = CTaskSimpleCarSetPedInAsDriver(vehicle, nullptr);
    task.m_bWarpingInToCar = true;
    task.ProcessPed(m_pPed);
    CUtil::ReleaseStackCarTask(task);
}

void CNetworkPed::WarpIntoVehiclePassenger(CVehicle* vehicle, int seatid)
{
    COOP_VERIFY(m_pPed != nullptr);

    if (!vehicle->IsVTableValid() || !m_pPed->IsVTableValid())
    {
        return;
    }

    if (m_pPed->m_nPedFlags.bInVehicle && m_pPed->m_pVehicle)
    {
        RemoveFromVehicle(m_pPed->m_pVehicle);
    }

    m_pPed->m_pIntelligence->FlushImmediately(false);

    if (!m_bSyncing)
    {
        m_pPed->m_nPedFlags.CantBeKnockedOffBike = 1; // 1 - never
    }

    int doorId = CCarEnterExit::ComputeTargetDoorToEnterAsPassenger(vehicle, seatid);
    auto task = CTaskSimpleCarSetPedInAsPassenger(vehicle, doorId, nullptr);
    task.m_bWarpingInToCar = true;
    task.ProcessPed(m_pPed);
    CUtil::ReleaseStackCarTask(task);
}

void CNetworkPed::RemoveFromVehicle(CVehicle* vehicle, bool bWarpOut)
{
    COOP_VERIFY(m_pPed != nullptr);

    if (!vehicle->IsVTableValid() || !m_pPed->IsVTableValid())
    {
        return;
    }

    m_pPed->m_pIntelligence->m_TaskMgr.SetTask(nullptr, TASK_PRIMARY_PRIMARY, false);

    if (!m_bSyncing)
    {
        m_pPed->m_nPedFlags.CantBeKnockedOffBike = 2; // 2 - normal
    }

    auto task = CTaskSimpleCarSetPedOut(vehicle, 1, false);
    task.m_bWarpingOutOfCar = bWarpOut;
    task.ProcessPed(m_pPed);
    CUtil::ReleaseStackCarTask(task);

    // C2: when it is pulled out by a player the exit animation must run: no default task, no FlushImmediately
    if (bWarpOut)
    {
        m_pPed->m_pIntelligence->FlushImmediately(true); // create a default primary task (fix bug)
    }
}

void CNetworkPed::ClaimOnRelease()
{
    if (m_bClaimOnRelease || m_bSyncing)
        return;

    Packets::Peds::PedClaimOnRelease packet{};
    packet.pedid = m_pedId;
    GetPacketFactory().Send(packet);

    m_bClaimOnRelease = true;
}

void CNetworkPed::CancelClaim()
{
    if (!m_bClaimOnRelease || m_bSyncing)
        return;

    Packets::Peds::PedCancelClaim packet{};
    packet.pedid = m_pedId;
    GetPacketFactory().Send(packet);

    m_bClaimOnRelease = false;
}

void CNetworkPed::ApplyWeaponSnapshot(Packets::Players::SWeaponSnapshot& weaponSnapshot)
{
    if (m_pPed == nullptr)
    {
        return;
    }

    // TODO refactor CUtil
    CUtil::GiveWeaponByPacket(this, weaponSnapshot.iWeaponType, weaponSnapshot.nAmmo);
    m_pPed->m_aWeapons[m_pPed->m_nActiveWeaponSlot].m_nState = static_cast<eWeaponState>(weaponSnapshot.iWeaponState);
}

// a full-body animation that stays: not one fading out to be removed (0x4, ANIMATION_IS_BLEND_AUTO_REMOVE in gta-reversed)
// nor a one-shot about to remove itself at its end (0x8, ANIMATION_IS_FINISH_AUTO_REMOVE)
static bool HasLastingFullBodyAnimation(RpClump* clump)
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

// The game never leaves a ped without a full-body animation: CPed::SetModelIndex adds the idle, and when another one is
// blended CAnimManager::BlendAnimation marks the rest to remove themselves once faded (gta-reversed Ped.cpp,
// AnimManager.cpp). On the game's own peds the next one comes from a task through CPed::SetMoveAnim, which only blends
// when the move state changed (the last one is kept in m_nSwimmingMoveState, +0x538, plugin-sdk field_538). A copy has
// no task to do it: once its synced animation ended or was faded out its list was empty, and CPed::PlayFootSteps and
// the RpAnimBlend callbacks read the first animation unchecked (the crashes guarded in CrashfixHooks). The copy gets its
// move animation back the same way the game's tasks give it.
static bool IsAssociationAlive(CPed* ped, CAnimBlendAssociation* target);

void CNetworkPed::EnsureBaseAnimation()
{
    CPed* ped = m_pPed;
    if (ped == nullptr || ped->m_pRwClump == nullptr || !ped->IsAlive() || ped->m_fHealth < 1.0f ||
        ped->m_nPedFlags.bInVehicle || HasLastingFullBodyAnimation(ped->m_pRwClump))
        return;

    // npc-synced-pose-keep: the pose its syncer sent is the full-body animation of the copy even when it stopped on
    // its last frame (sitting, eating: the interiors keep those stopped). The idle blended over it faded the pose out,
    // the next resend blended it back, and the ped stood on its chair in between
    if (CConfigLoader::ms_bNpcSyncedPoseKeep && m_pSyncedAnim != nullptr && IsAssociationAlive(ped, m_pSyncedAnim) &&
        !m_pSyncedAnim->m_bPartial && m_pSyncedAnim->m_fBlendAmount > 0.0f && m_pSyncedAnim->m_fBlendDelta >= 0.0f)
        return;

    ped->field_538 = PEDMOVE_NONE;
    // plugin-sdk CPed::SetMoveAnim calls vtable slot 24, which in 1.0 US is CPed::Save (0x5D5730): it wrote a save
    // block into the null work buffer and crashed. SetMoveAnim (0x5E4A00) is slot 23 (gta-reversed Ped.cpp).
    ((void(__thiscall*)(CPed*))(*reinterpret_cast<void***>(ped))[23])(ped);
    if (!HasLastingFullBodyAnimation(ped->m_pRwClump))
        CAnimManager::BlendAnimation(ped->m_pRwClump, (int)ped->m_nAnimGroup, 3 /* ANIM_ID_IDLE */, 4.0f);

    static int nLogged = 0;
    if (nLogged < 20)
    {
        nLogged++;
        CMissionMirror::LogEventf("ped-base-anim model=%d move=%d", ped->m_nModelIndex, ped->m_nMoveState);
    }
}

// Health alone does not kill a ped: CPed::IsAlive reads the ped state, and only the death task (CTaskComplexDie ->
// CTaskSimpleDie::StartAnim 0x637520: StopNonPartialAnims, PEDSTATE_DIE, then CTaskSimpleDead) makes it fall and stay
// down (gta-reversed Ped.cpp, TaskSimpleDie.cpp). A copy only received health 0 and kept standing, and
// EnsureBaseAnimation gave it its walk back. Same task the game gives on SET_CHAR_HEALTH 0 and that CNetworkPlayer uses.
void CNetworkPed::ApplyRemoteDeath()
{
    CPed* ped = m_pPed;
    if (m_bSyncing || m_bStreamedOut || ped == nullptr || !ped->IsVTableValid() || ped->m_pIntelligence == nullptr ||
        m_fHealth >= 1.0f || !ped->IsAlive() || ped->m_nPedFlags.bInVehicle ||
        ped->m_pIntelligence->m_TaskMgr.FindActiveTaskByType(TASK_COMPLEX_DIE))
        return;

    // ped-damage-authority: the game's own death first (see CPedDamageSync::KillCopy)
    if (CPedDamageSync::IsEnabled() && CPedDamageSync::KillCopy(this))
    {
        StopSyncedAnimation();
        return;
    }

    StopSyncedAnimation();
    ped->m_fHealth = 0.0f;
    auto task = new CTaskComplexDie(WEAPON_UNARMED, ANIM_GROUP_DEFAULT, ANIM_DEFAULT_KO_SHOT_FRONT_0, 4.0f, 0.0f, false,
        false, 0, false);
    ped->m_pIntelligence->m_TaskMgr.SetTask(task, TASK_PRIMARY_PRIMARY, true);

    static int nLogged = 0;
    if (nLogged < 20)
    {
        nLogged++;
        CMissionMirror::LogEventf("ped-remote-death model=%d", ped->m_nModelIndex);
    }
}

// npc-anim-mirror: only peds this close to another player send their animation set
static const float ANIM_MIRROR_RADIUS = 70.0f;
// npc-aim-mirror-exclusive: how long a received set keeps the copy's own gun task disabled (ms)
static const uint32_t ANIM_SET_FRESH_MS = 400;
// npc-anim-mirror: a playing animation is only moved in time when it drifted more than this (seconds)
static const float ANIM_MIRROR_TIME_DRIFT = 0.15f;

// npc-anim-mirror. The position sync and the move state only tell a copy "walking" or "standing": everything the
// tasks of the real ped blend on top (fight stance and its steps forward and back, strafes, hits, turns, gestures)
// was either one whitelisted animation sent at most once a second (npc-task-sync) or nothing, so the copy slid in
// its walk cycle. The syncer now sends the strongest associations of the clump as they are (group, id, blend, time,
// speed) ten times a second, and the copy plays that same set.
static bool IsMountedMotorcycleCop(const CPed* ped)
{
    return ped->m_nPedType == PED_TYPE_COP && ped->m_nPedFlags.bInVehicle && ped->m_pVehicle &&
           ped->m_pVehicle->m_nModelIndex == MODEL_COPBIKE;
}

static bool IsBikeDriveByAnimation(int group, int id)
{
    return ((group >= ANIM_GROUP_BIKES && group <= ANIM_GROUP_BIKED) &&
            id >= ANIM_BIKES_BIKES_DRIVEBYLHS && id <= ANIM_BIKES_BIKES_DRIVEBYFT) ||
           (group == ANIM_GROUP_BIKE_DBZ &&
            id >= ANIM_BIKE_DBZ_PASS_DRIVEBY_LHS && id <= ANIM_BIKE_DBZ_PASS_DRIVEBY_BWD);
}

void CNetworkPed::SendAnimSet()
{
    CPed* ped = m_pPed;
    if (!m_bSyncing || ped == nullptr || ped->m_pRwClump == nullptr || !ped->IsAlive() ||
        !IsNearAnyNetworkPlayer(ped, ANIM_MIRROR_RADIUS))
        return;
    const bool motorcycleCop = IsMountedMotorcycleCop(ped);
    if (ped->m_nPedFlags.bInVehicle && !motorcycleCop)
        return;

    Packets::Peds::PedAnimSet packet{};
    packet.pedid = m_pedId;
    float blends[Packets::Peds::PedAnimSet::MAX_ANIMS]{};
    CAnimBlendAssociation* named = CConfigLoader::ms_bNpcNamedAnimSync ? FindNamedAssociation(ped) : nullptr;

    for (CAnimBlendAssociation* assoc = RpAnimBlendClumpGetFirstAssociation(ped->m_pRwClump); assoc;
         assoc = RpAnimBlendGetNextAssociation(assoc))
    {
        // the named ones (no assoc group, or a script's one) travel with npc-named-anim-sync
        if (assoc->m_nAnimGroup == NAMED_ANIM_GROUP || assoc == named || assoc->m_pHierarchy == nullptr ||
            assoc->m_fBlendAmount < 0.02f)
            continue;

        const int id = NormalizeAnimId(assoc->m_nAnimGroup, assoc->m_nAnimId);
        if (id < 0 || id > 8191 || assoc->m_nAnimGroup > 4095 ||
            motorcycleCop != IsBikeDriveByAnimation(assoc->m_nAnimGroup, id))
            continue;

        // keep the strongest ones, sorted
        int slot = packet.count;
        while (slot > 0 && blends[slot - 1] < assoc->m_fBlendAmount)
            slot--;
        if (slot >= Packets::Peds::PedAnimSet::MAX_ANIMS)
            continue;

        const int last = std::min<int>(packet.count, Packets::Peds::PedAnimSet::MAX_ANIMS - 1);
        for (int i = last; i > slot; i--)
        {
            packet.anims[i] = packet.anims[i - 1];
            blends[i] = blends[i - 1];
        }

        const float total = assoc->m_pHierarchy->m_fTotalTime;
        Packets::Peds::PedAnimSet::SAnim& anim = packet.anims[slot];
        anim.group = assoc->m_nAnimGroup;
        anim.animId = static_cast<uint16_t>(id);
        anim.blend = static_cast<uint8_t>(std::clamp(assoc->m_fBlendAmount, 0.0f, 1.0f) * 255.0f + 0.5f);
        anim.time = total > 0.001f
                        ? static_cast<uint8_t>(std::clamp(assoc->m_fCurrentTime / total, 0.0f, 1.0f) * 255.0f + 0.5f)
                        : 0;
        anim.speed = static_cast<uint8_t>(std::clamp(assoc->m_fSpeed, 0.0f, 5.0f) * 50.0f + 0.5f);
        anim.playing = assoc->m_bPlaying != 0;
        blends[slot] = assoc->m_fBlendAmount;

        if (packet.count < Packets::Peds::PedAnimSet::MAX_ANIMS)
            packet.count++;
    }

    if (packet.count > 0 || motorcycleCop)
        GetPacketFactory().Send(packet);
}

void CNetworkPed::ReleaseMirrorAnimBlocks()
{
    for (int blockIndex : m_aMirrorAnimBlocks)
        CAnimManager::RemoveAnimBlockRef(blockIndex);
    m_aMirrorAnimBlocks.clear();
}

void CNetworkPed::ApplyAnimSet(Packets::Peds::PedAnimSet& packet)
{
    if (!CConfigLoader::ms_bNpcAnimMirror || m_bSyncing || m_bStreamedOut)
        return;

    // a dead copy keeps its death, a seated one belongs to the car, and the enter / leave car task plays its own
    CPed* ped = m_pPed;
    if (ped == nullptr || !ped->IsVTableValid() || ped->m_pRwClump == nullptr || !ped->IsAlive() ||
        ped->m_fHealth < 1.0f || IsPosSyncSkipped() || CJackSync::IsPedJackInProgress(m_pedId))
        return;
    const bool motorcycleCop = IsMountedMotorcycleCop(ped);
    if (ped->m_nPedFlags.bInVehicle && !motorcycleCop)
        return;

    CAnimBlendAssocGroup* groups = GetAnimAssocGroups();
    if (groups == nullptr)
        return;

    CAnimBlendAssociation* kept[Packets::Peds::PedAnimSet::MAX_ANIMS]{};
    int nKept = 0;
    bool bFullBody = false;

    for (int i = 0; i < packet.count && i < Packets::Peds::PedAnimSet::MAX_ANIMS; i++)
    {
        const Packets::Peds::PedAnimSet::SAnim& anim = packet.anims[i];
        const int group = anim.group;
        if (group >= CAnimManager::ms_numAnimAssocDefinitions ||
            motorcycleCop != IsBikeDriveByAnimation(group, anim.animId))
            continue;

        const int blockIndex = GetAnimBlockIndex(groups[group], group);
        if (blockIndex < 0 || blockIndex >= CAnimManager::ms_numAnimBlocks)
            continue;

        // not loaded here yet (fight styles, weapons...): ask for it, a later set plays it. A plain request: with
        // GAME_REQUIRED the streaming would keep the block forever, here the references below hold it
        if (blockIndex != 0 && !CAnimManager::ms_aAnimBlocks[blockIndex].bLoaded)
        {
            CStreaming::RequestModel(ANIM_BLOCK_MODEL_BASE + blockIndex, 0);
            continue;
        }

        const int animId = NormalizeAnimId(group, anim.animId);
        if (animId < 0)
            continue;

        // the association points into the block: keep it in memory while this copy exists (block 0 always is)
        if (blockIndex != 0 &&
            std::find(m_aMirrorAnimBlocks.begin(), m_aMirrorAnimBlocks.end(), blockIndex) == m_aMirrorAnimBlocks.end())
        {
            CAnimManager::AddAnimBlockRef(blockIndex);
            m_aMirrorAnimBlocks.push_back(blockIndex);
        }

        CAnimBlendAssociation* assoc = nullptr;
        for (CAnimBlendAssociation* it = RpAnimBlendClumpGetFirstAssociation(ped->m_pRwClump); it;
             it = RpAnimBlendGetNextAssociation(it))
        {
            if (it->m_nAnimGroup == group && NormalizeAnimId(group, it->m_nAnimId) == animId)
            {
                assoc = it;
                break;
            }
        }

        if (assoc == nullptr)
        {
            assoc = CAnimManager::AddAnimation(ped->m_pRwClump, group, animId);
            if (assoc == nullptr)
                continue;
        }

        // plugin-sdk names 0x4 "freeze last frame"; in the game it removes the association once faded out
        assoc->m_nFlags &= ~(unsigned short)ANIMATION_FREEZE_LAST_FRAME;
        assoc->m_fBlendAmount = anim.blend / 255.0f;
        assoc->m_fBlendDelta = 0.0f;
        assoc->m_bPlaying = anim.playing ? 1 : 0;
        assoc->m_fSpeed = anim.speed / 50.0f;

        if (assoc->m_pHierarchy != nullptr && assoc->m_pHierarchy->m_fTotalTime > 0.001f)
        {
            const float total = assoc->m_pHierarchy->m_fTotalTime;
            const float time = anim.time / 255.0f * total;
            float drift = std::fabs(assoc->m_fCurrentTime - time);
            if (assoc->m_bLooped)
                drift = std::min(drift, total - drift);
            if (drift > (anim.playing ? ANIM_MIRROR_TIME_DRIFT : 0.03f))
                assoc->SetCurrentTime(time);
        }

        if (!assoc->m_bPartial)
            bFullBody = true;
        kept[nKept++] = assoc;
    }

    // nothing of the set could be played here: leave the copy as it is
    if (!bFullBody && !motorcycleCop)
        return;

    // the rest fades out the way CAnimManager::BlendAnimation retires the previous animations
    for (CAnimBlendAssociation* assoc = RpAnimBlendClumpGetFirstAssociation(ped->m_pRwClump); assoc;
         assoc = RpAnimBlendGetNextAssociation(assoc))
    {
        if (assoc->m_nAnimGroup == NAMED_ANIM_GROUP || std::find(kept, kept + nKept, assoc) != kept + nKept ||
            (motorcycleCop && !IsBikeDriveByAnimation(assoc->m_nAnimGroup,
                                                      NormalizeAnimId(assoc->m_nAnimGroup, assoc->m_nAnimId))))
            continue;

        assoc->m_nFlags |= (unsigned short)ANIMATION_FREEZE_LAST_FRAME;
        if (assoc->m_fBlendDelta > -4.0f)
            assoc->m_fBlendDelta = -4.0f;
    }

    // npc-aim-mirror-exclusive: this set is the pose of the copy from now on
    m_nLastAnimSetAt = CConfigLoader::GetSyncTime();
}

// npc-aim-sync. CTaskSimpleUseGun only lives while something commands it every frame: ControlGun (0x61E040) sets
// m_IsInControl and the command, ProcessPed (0x62A380) consumes them, and without control the task counts down and
// ends (gta-reversed TaskSimpleUseGun.cpp). On the syncer the AI of the NPC does it. The copy has no AI: its task was
// created by PED_ONFOOT and only got a new target every 50 ms, so it ended and was created again and the copy never
// held its aim; the shots (PED_SHOT_SYNC) called CWeapon::Fire directly and played no animation. Here the copy
// commands its own task: aim while its syncer aims, fire once per shot received. The bullet of the task itself is
// cut by CWeapon__Fire_Hook (PedHooks): what hits is decided by the syncer (ped-damage-authority).
void CNetworkPed::ProcessAim()
{
    CPed* ped = m_pPed;
    if (m_bSyncing || ped == nullptr || ped->m_pIntelligence == nullptr)
        return;

    // npc-aim-mirror-exclusive: the mirror already brings the gun animation of its syncer as a partial,
    // and the blend this task does (SetMoveAnim) retires the crouch the mirror applies: the copy flickered
    // standing/crouched. While the set keeps arriving, the task is not created and nothing is commanded.
    if (CConfigLoader::ms_bNpcAimMirrorExclusive && CConfigLoader::ms_bNpcAnimMirror && m_nLastAnimSetAt != 0 &&
        CConfigLoader::GetSyncTime() - m_nLastAnimSetAt < ANIM_SET_FRESH_MS)
    {
        m_bFireRequested = false;
        return;
    }

    bool bAim = m_bAiming;
    if (m_nShotAimUntil != 0)
    {
        if ((int32_t)(m_nShotAimUntil - CConfigLoader::GetSyncTime()) > 0)
            bAim = true;
        else
            m_nShotAimUntil = 0;
    }

    if (!bAim || !ped->IsAlive() || ped->m_fHealth < 1.0f || ped->m_nPedFlags.bInVehicle)
    {
        m_bFireRequested = false;
        return;
    }

    CTaskSimpleUseGun* useGun = ped->m_pIntelligence->GetTaskUseGun();
    if (useGun == nullptr)
    {
        ped->m_pIntelligence->m_TaskMgr.SetTaskSecondary(
            new CTaskSimpleUseGun(nullptr, m_vecAimTarget, 1 /* AIM */, 1, false), TASK_SECONDARY_ATTACK);
        useGun = ped->m_pIntelligence->GetTaskUseGun();
        if (useGun == nullptr)
            return;
    }

    // same as ControlGun(ped, nullptr, command) with a position as target
    const unsigned char nCommand = m_bFireRequested ? 2 /* FIRE */ : 1 /* AIM */;
    m_bFireRequested = false;
    if (useGun->m_pTarget != nullptr)
    {
        useGun->m_pTarget = nullptr;
        useGun->m_ArmIKInUse = false;
        useGun->m_LookIKInUse = false;
    }
    useGun->m_vecTarget = m_vecAimTarget;
    useGun->m_bIsInControl = true;
    if (useGun->m_nNextCommand < nCommand)
        useGun->m_nNextCommand = nCommand;
}

// interior-population-sync diagnosis, the copy's side of interior-anim-owner: the pose it got and what it shows
void CNetworkPed::DiagnoseInteriorCopy()
{
    CPed* ped = m_pPed;
    const uint32_t now = CConfigLoader::GetSyncTime();
    if (!CConfigLoader::ms_bInteriorPopulationSync || !m_bInteriorPopulation || m_bSyncing || ped == nullptr ||
        ped->m_pRwClump == nullptr || now - m_nInteriorDiagAt < 10000)
        return;
    m_nInteriorDiagAt = now;
    CAnimBlendAssociation* top = nullptr;
    for (CAnimBlendAssociation* assoc = RpAnimBlendClumpGetFirstAssociation(ped->m_pRwClump); assoc;
         assoc = RpAnimBlendGetNextAssociation(assoc))
    {
        if (!assoc->m_bPartial && (top == nullptr || assoc->m_fBlendAmount > top->m_fBlendAmount))
            top = assoc;
    }
    const bool synced = m_pSyncedAnim != nullptr && IsAssociationAlive(ped, m_pSyncedAnim);
    CMissionMirror::LogEventf("interior-anim-copy model=%d state=%d named=%d block=%.15s pending=%d synced=%d "
        "synced-blend=%.2f top-is-synced=%d top-group=%d top-id=%d top-blend=%.2f",
        ped->m_nModelIndex, m_bHasLastTaskState ? 1 : 0, m_bHasLastTaskState && m_lastTaskState.namedAnim ? 1 : 0,
        m_bHasLastTaskState ? m_lastTaskState.animBlock : "", m_bPendingTaskState ? 1 : 0, synced ? 1 : 0,
        synced ? m_pSyncedAnim->m_fBlendAmount : 0.0f, synced && top == m_pSyncedAnim ? 1 : 0,
        top ? (int)top->m_nAnimGroup : -2, top ? (int)top->m_nAnimId : -2, top ? top->m_fBlendAmount : 0.0f);
}

// npc-driveby-sync. A copy in a vehicle got its shots (PED_SHOT_SYNC: bullets and sound out of the window) but no
// animation: ProcessAim only drives the on-foot gun task. While its syncer fires from the vehicle the copy runs its
// own CTaskSimpleGangDriveBy aimed at the last shot's target, in the temporary event response slot so the in-car task
// under it stays and comes back when it is removed. Its own bullets are cut by CWeapon__Fire_Hook (the copy does not
// sync): what hits is still decided by the syncer.
void CNetworkPed::ProcessDriveBy()
{
    CPed* ped = m_pPed;
    CTaskManager& tasks = ped->m_pIntelligence->m_TaskMgr;
    CTask* temp = tasks.m_aPrimaryTasks[TASK_PRIMARY_EVENT_RESPONSE_TEMP];
    const bool active = temp != nullptr && temp->GetTaskType() == TASK_SIMPLE_GANG_DRIVEBY;
    if (!active)
        m_bCopyDriveBy = false;

    bool want = CConfigLoader::ms_bNpcDriveBySync && m_nDriveByUntil != 0 && ped->IsAlive() && ped->m_fHealth >= 1.0f &&
        ped->m_pVehicle != nullptr;
    if (want && (int32_t)(m_nDriveByUntil - CConfigLoader::GetSyncTime()) <= 0)
    {
        m_nDriveByUntil = 0;
        want = false;
    }

    if (want)
    {
        if (active)
        {
            static_cast<CTaskSimpleGangDriveBy*>(temp)->m_vecCoords = m_vecAimTarget;
        }
        else if (temp == nullptr)
        {
            // the side of the car the ped sits on (passengers 1 and 3 are on the right)
            const bool seatRHS = ped->m_pVehicle->m_pDriver != ped &&
                (ped->m_pVehicle->m_apPassengers[0] == ped || ped->m_pVehicle->m_apPassengers[2] == ped);
            tasks.SetTask(new CTaskSimpleGangDriveBy(nullptr, &m_vecAimTarget, 0.0f, 100, DRIVEBY_AI_ALL_DIRN, seatRHS),
                TASK_PRIMARY_EVENT_RESPONSE_TEMP, false);
            m_bCopyDriveBy = true;
        }
    }
    else if (active && m_bCopyDriveBy)
    {
        temp->MakeAbortable(ped, ABORT_PRIORITY_URGENT, nullptr);
        m_bCopyDriveBy = false;
    }
}

// ped-damage-authority: the health of the syncer always goes to the record, but it must not heal a copy the local
// player just hurt (its PED_DAMAGE is still on its way) nor give health to a copy that already fell: that one is
// rebuilt by CPedDamageSync::ProcessCopy if its syncer keeps it alive.
void CNetworkPed::ApplyAuthorityHealth(float health)
{
    m_fHealth = health;

    CPed* ped = m_pPed;
    if (ped == nullptr)
        return;

    if (CPedDamageSync::IsEnabled())
    {
        if (!ped->IsAlive() && health >= 1.0f)
            return;

        if (m_nLocalDamageUntil != 0)
        {
            if ((int32_t)(m_nLocalDamageUntil - CConfigLoader::GetSyncTime()) > 0)
            {
                ped->m_fHealth = std::min(ped->m_fHealth, health);
                return;
            }
            m_nLocalDamageUntil = 0;
        }
    }

    ped->m_fHealth = health;
}

// the association can be destroyed by the game (damage, task changes) at any time
static bool IsAssociationAlive(CPed* ped, CAnimBlendAssociation* target)
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

void CNetworkPed::ReleaseSyncedAnimBlock()
{
    if (m_nSyncedAnimBlock < 0)
        return;

    // never twice: the index is the only owner of the reference (a block without our ref is freed by the streaming)
    CAnimManager::RemoveAnimBlockRef(m_nSyncedAnimBlock);
    m_nSyncedAnimBlock = -1;
}

void CNetworkPed::StopSyncedAnimation()
{
    // only stop the association we blended ourselves, and only while it is still the one we put there
    bool ours = false;
    if (m_pSyncedAnim && IsAssociationAlive(m_pPed, m_pSyncedAnim) && m_pSyncedAnim->m_nAnimGroup == m_nSyncedAnimGroup)
    {
        ours = m_bSyncedAnimNamed
                   ? (m_pSyncedAnim->m_pHierarchy != nullptr && m_pSyncedAnim->m_pHierarchy->m_hashKey == m_nSyncedAnimHash)
                   : (NormalizeAnimId(m_pSyncedAnim->m_nAnimGroup, m_pSyncedAnim->m_nAnimId) == (int)m_nSyncedAnimId);
    }

    if (ours)
    {
        // fade it out (delta -4) and let the clump delete the association once the blend reaches zero; plugin-sdk
        // names 0x4 "freeze last frame", but in the game it is the auto remove flag (UpdateBlend)
        m_pSyncedAnim->m_nFlags |= (unsigned short)ANIMATION_FREEZE_LAST_FRAME;
        m_pSyncedAnim->m_fBlendDelta = -4.0f;
    }

    m_pSyncedAnim = nullptr;
    m_bSyncedAnimNamed = false;
    ReleaseSyncedAnimBlock();
}

bool CNetworkPed::IsPosSyncSkipped()
{
    if (m_nSkipPosSyncUntil == 0)
        return false;

    uint32_t now = CConfigLoader::GetSyncTime();
    if ((int32_t)(m_nSkipPosSyncUntil - now) > 0)
        return true;

    // the task took too long (the ped got stuck): let the normal position sync put it in / take it out
    m_nSkipPosSyncUntil = 0;
    return false;
}

void CNetworkPed::ReleaseHostedAnimBlock()
{
    if (m_nHostedAnimBlock < 0)
        return;

    CAnimManager::RemoveAnimBlockRef(m_nHostedAnimBlock);
    m_nHostedAnimBlock = -1;
}

// D interior handover: the ped was handed over to us; stop the named animation we were keeping for it
void CNetworkPed::StopHostedAnim()
{
    if (m_pHostedAnim && IsAssociationAlive(m_pPed, m_pHostedAnim))
    {
        m_pHostedAnim->m_nFlags |= (unsigned short)ANIMATION_FREEZE_LAST_FRAME;
        m_pHostedAnim->m_fBlendDelta = -4.0f;
    }

    m_pHostedAnim = nullptr;
    ReleaseHostedAnimBlock();
}

// D interior handover: the previous owner left the interior and its (still dancing / sitting) peds were handed to
// us. There is no AI indoors (the brains ran only on the owner and wander would walk through walls): keep the last
// named animation playing in loop, or let the caller stand the ped still.
bool CNetworkPed::PlayHostedNamedAnim(Packets::Peds::PedTaskState& state)
{
    if (!state.namedAnim || state.animBlock[0] == '\0' || m_pPed == nullptr || !m_pPed->IsVTableValid() ||
        m_pPed->m_pRwClump == nullptr)
        return false;

    const int blockIndex = CAnimManager::GetAnimationBlockIndex(state.animBlock);
    if (blockIndex < 0)
        return false;

    if (blockIndex != 0 && !CAnimManager::ms_aAnimBlocks[blockIndex].bLoaded)
    {
        // the block is not loaded here yet: ask for it and let the caller retry (CNetworkPedManager::Process)
        CStreaming::RequestModel(ANIM_BLOCK_MODEL_BASE + blockIndex, eStreamingFlags::GAME_REQUIRED);
        return false;
    }

    CAnimBlendHierarchy* hierarchy =
        CAnimManager::GetAnimation(state.animHash, &CAnimManager::ms_aAnimBlocks[blockIndex]);
    if (hierarchy == nullptr)
        return false;

    if (m_pHostedAnim && IsAssociationAlive(m_pPed, m_pHostedAnim) && m_pHostedAnim->m_pHierarchy == hierarchy)
        return true;  // the very same animation is already ours

    StopHostedAnim();

    if (blockIndex != 0 && m_nHostedAnimBlock != blockIndex)
    {
        CAnimManager::AddAnimBlockRef(blockIndex);
        m_nHostedAnimBlock = blockIndex;
    }

    // the translate bits would move the ped away from the position its previous owner left it
    unsigned short flags =
        state.animFlags & ~(unsigned short)(ANIMATION_TRANLSATE_X | ANIMATION_TRANLSATE_Y | ANIMATION_FREEZE_TRANSLATION);
    flags |= (unsigned short)ANIMATION_LOOPED;

    m_pHostedAnim = CAnimManager::BlendAnimation(m_pPed->m_pRwClump, hierarchy, flags, 4.0f);
    m_nHostedAnimHash = state.animHash;

    if (m_pHostedAnim != nullptr)
    {
        m_pHostedAnim->SetCurrentTime(state.animTime);
        if (state.animSpeed > 0.0f && state.animSpeed < 5.0f)
            m_pHostedAnim->m_fSpeed = state.animSpeed;
    }
    return true;
}

// D npc-named-anim-sync: the animation of a named IFP block, as the syncer described it. Returns false while the
// block is still loading (the state has to be retried).
bool CNetworkPed::ApplyNamedAnimState(Packets::Peds::PedTaskState& packet)
{
    CPed* ped = m_pPed;
    if (ped->m_pRwClump == nullptr || packet.animBlock[0] == '\0')
    {
        StopSyncedAnimation();
        return true;
    }

    const int blockIndex = CAnimManager::GetAnimationBlockIndex(packet.animBlock);
    if (blockIndex < 0)
    {
        // the block is not part of this game: nothing to wait for
        StopSyncedAnimation();
        return true;
    }

    // the association points into the block, so the streaming must not free it while it plays: while it is still
    // loading, ask for it and keep the state pending (bug 2)
    if (blockIndex != 0 && !CAnimManager::ms_aAnimBlocks[blockIndex].bLoaded)
    {
        CStreaming::RequestModel(ANIM_BLOCK_MODEL_BASE + blockIndex, eStreamingFlags::GAME_REQUIRED);
        return false;
    }

    CAnimBlendHierarchy* hierarchy =
        CAnimManager::GetAnimation(packet.animHash, &CAnimManager::ms_aAnimBlocks[blockIndex]);
    if (hierarchy == nullptr)
    {
        StopSyncedAnimation();
        return true;
    }

    // the very same animation is already ours: keep it playing (do not restart it)
    if (m_pSyncedAnim && m_bSyncedAnimNamed && IsAssociationAlive(ped, m_pSyncedAnim) &&
        m_pSyncedAnim->m_pHierarchy == hierarchy)
    {
        if (CConfigLoader::ms_bInteriorPopulationSync && ped->m_nAreaCode != 0)
        {
            m_pSyncedAnim->SetCurrentTime(packet.animTime);
            m_pSyncedAnim->m_fSpeed = packet.animSpeed;
            m_pSyncedAnim->m_bPlaying = (packet.animFlags & 1) != 0;
        }
        return true;
    }

    if (m_pSyncedAnim && !IsAssociationAlive(ped, m_pSyncedAnim))
    {
        ReleaseSyncedAnimBlock();
        m_pSyncedAnim = nullptr;
        m_bSyncedAnimNamed = false;
    }

    if (m_pSyncedAnim)
        StopSyncedAnimation();

    // keep the block in memory until we let this animation go (block 0, `ped`, is always loaded)
    if (blockIndex != 0 && m_nSyncedAnimBlock != blockIndex)
    {
        ReleaseSyncedAnimBlock();
        CAnimManager::AddAnimBlockRef(blockIndex);
        m_nSyncedAnimBlock = blockIndex;
    }

    // the translate bits would move the ped away from the position its syncer gives it
    unsigned short flags = packet.animFlags & ~(unsigned short)(ANIMATION_TRANLSATE_X | ANIMATION_TRANLSATE_Y);

    m_pSyncedAnim = CAnimManager::BlendAnimation(ped->m_pRwClump, hierarchy, flags, 4.0f);
    if (CConfigLoader::ms_bInteriorPopulationSync && m_bInteriorPopulation && m_pSyncedAnim)
        CMissionMirror::LogEventf("interior-anim-play model=%d block=%.15s hash=%u time=%.2f flags=%u",
            ped->m_nModelIndex, packet.animBlock, packet.animHash, packet.animTime, packet.animFlags);
    m_bSyncedAnimNamed = m_pSyncedAnim != nullptr;
    m_nSyncedAnimGroup = NAMED_ANIM_GROUP;
    m_nSyncedAnimId = 0;
    m_nSyncedAnimHash = packet.animHash;

    if (m_pSyncedAnim != nullptr)
    {
        m_pSyncedAnim->SetCurrentTime(packet.animTime);
        if (packet.animSpeed > 0.0f &&
            packet.animSpeed <= (CConfigLoader::ms_bInteriorServiceGuest ? 1000.0f : 5.0f))
            m_pSyncedAnim->m_fSpeed = packet.animSpeed;
    }
    return true;
}

// apply a state to the game ped; returns false when the animation block is still loading (the caller keeps the
// state pending / retries it). `applyTask` is false for the retries that only wait for the animation block.
bool CNetworkPed::ApplyTaskStateInternal(Packets::Peds::PedTaskState& packet, bool applyTask)
{
    CPed* ped = m_pPed;
    uint32_t now = CConfigLoader::GetSyncTime();

    // a dead copy keeps its death task and animation: no synced task or animation stands it up again
    if (ped != nullptr && (ped->m_fHealth < 1.0f || !ped->IsAlive()) && !ped->m_nPedFlags.bInVehicle)
        return true;

    if (applyTask && packet.hasVehicle)
    {
        CNetworkVehicle* pNetworkVehicle = CNetworkVehicleManager::GetVehicle(packet.vehicleid);
        CVehicle* pVehicle = pNetworkVehicle != nullptr && !pNetworkVehicle->m_bStreamedOut
                                 ? pNetworkVehicle->m_pVehicle
                                 : nullptr;

        if (pVehicle != nullptr && pVehicle->IsVTableValid())
        {
            bool bInVehicle = ped->m_nPedFlags.bInVehicle != 0;

            if (packet.taskType == TASK_COMPLEX_ENTER_CAR_AS_DRIVER)
            {
                if (!bInVehicle && DistanceBetweenPoints(ped->GetPosition(), pVehicle->GetPosition()) < 4.0f)
                {
                    ped->m_pIntelligence->m_TaskMgr.SetTask(
                        new CTaskComplexEnterCarAsDriver(pVehicle), TASK_PRIMARY_PRIMARY, false);
                    m_nSkipPosSyncUntil = now + TASK_SYNC_DURATION;
                    m_bTaskSyncExpectInside = true;
                }
            }
            else if (packet.taskType == TASK_COMPLEX_ENTER_CAR_AS_PASSENGER)
            {
                if (!bInVehicle && DistanceBetweenPoints(ped->GetPosition(), pVehicle->GetPosition()) < 4.0f)
                {
                    int seat = packet.seat < 0 ? 0 : std::clamp((int)packet.seat, 0, 7);
                    ped->m_pIntelligence->m_TaskMgr.SetTask(
                        new CTaskComplexEnterCarAsPassenger(pVehicle, seat, false), TASK_PRIMARY_PRIMARY, false);
                    m_nSkipPosSyncUntil = now + TASK_SYNC_DURATION;
                    m_bTaskSyncExpectInside = true;
                }
            }
            else if (packet.taskType == TASK_COMPLEX_LEAVE_CAR)
            {
                if (bInVehicle && ped->m_pVehicle == pVehicle)
                {
                    ped->m_pIntelligence->m_TaskMgr.SetTask(
                        new CTaskComplexLeaveCar(pVehicle, 0, 0, true, false), TASK_PRIMARY_PRIMARY, false);
                    m_nSkipPosSyncUntil = now + TASK_SYNC_DURATION;
                    m_bTaskSyncExpectInside = false;
                }
            }
        }
    }

    if (!packet.hasAnim)
    {
        StopSyncedAnimation();
        return true;
    }

    // D: a named animation of an interior (dancing, bar, casino...), only with the switch on
    if (packet.namedAnim && CConfigLoader::ms_bNpcNamedAnimSync)
        return ApplyNamedAnimState(packet);

    int group = packet.animGroup;
    CAnimBlendAssocGroup* groups = GetAnimAssocGroups();
    if (groups == nullptr || group < 0 || group >= CAnimManager::ms_numAnimAssocDefinitions || ped->m_pRwClump == nullptr)
    {
        // the animation can't be played here (unknown group): don't keep the previous one playing
        StopSyncedAnimation();
        return true;
    }

    const int blockIndex = GetAnimBlockIndex(groups[group], group);
    if (blockIndex < 0)
    {
        StopSyncedAnimation();
        return true;
    }

    // the association points into the block, so the streaming must not free it while it plays
    if (blockIndex != 0 && !CAnimManager::ms_aAnimBlocks[blockIndex].bLoaded)
    {
        CStreaming::RequestModel(ANIM_BLOCK_MODEL_BASE + blockIndex, eStreamingFlags::GAME_REQUIRED);

        // D bug 2: keep the state pending until the block loads instead of dropping it (the syncer used to resend
        // it only when it changed); with the switch off the old behaviour is kept
        if (CConfigLoader::ms_bNpcNamedAnimSync)
            return false;

        StopSyncedAnimation();
        return true;
    }

    int animId = NormalizeAnimId(group, packet.animId);
    if (animId < 0)
    {
        // the animation can't be played here (block not streamed): don't keep the previous one playing
        StopSyncedAnimation();
        return true;
    }

    // the game may have deleted it already (then it is not ours to stop anymore, and its ref is not needed either)
    if (m_pSyncedAnim && !IsAssociationAlive(ped, m_pSyncedAnim))
    {
        ReleaseSyncedAnimBlock();
        m_pSyncedAnim = nullptr;
        m_bSyncedAnimNamed = false;
    }

    if (m_pSyncedAnim && (m_bSyncedAnimNamed || m_nSyncedAnimGroup != group || m_nSyncedAnimId != animId))
        StopSyncedAnimation();

    if (m_pSyncedAnim == nullptr)
    {
        // stopping the previous animation may have dropped the last reference on this very block (two groups can
        // share one): never blend into a block that is gone, ask for it and let the next state play it
        if (blockIndex != 0 && !CAnimManager::ms_aAnimBlocks[blockIndex].bLoaded)
        {
            CStreaming::RequestModel(ANIM_BLOCK_MODEL_BASE + blockIndex, eStreamingFlags::GAME_REQUIRED);
            return CConfigLoader::ms_bNpcNamedAnimSync ? false : true;
        }

        // keep the block in memory until we let this animation go (block 0, `ped`, is always loaded)
        if (blockIndex != 0 && m_nSyncedAnimBlock != blockIndex)
        {
            ReleaseSyncedAnimBlock();
            CAnimManager::AddAnimBlockRef(blockIndex);
            m_nSyncedAnimBlock = blockIndex;
        }

        m_pSyncedAnim = CAnimManager::BlendAnimation(ped->m_pRwClump, group, animId, 4.0f);
        m_bSyncedAnimNamed = false;
        m_nSyncedAnimGroup = (uint16_t)group;
        m_nSyncedAnimId = (uint16_t)animId;
    }
    return true;
}

void CNetworkPed::ApplyTaskState(Packets::Peds::PedTaskState& packet)
{
    if (!CConfigLoader::ms_bNpcTaskSync || m_bSyncing)
        return;

    const bool namedSync = CConfigLoader::ms_bNpcNamedAnimSync;

    CPed* ped = m_pPed;
    bool havePed = !m_bStreamedOut && ped != nullptr && ped->IsVTableValid() && ped->m_pIntelligence != nullptr;

    // with the switch off a streamed out copy is left alone, exactly like before
    if (!havePed && !namedSync)
        return;

    // ignore a repeated or older state (uint8 arithmetic: works across the wrap); a jump far back is a new
    // syncer that started counting from its own value, so it has to be accepted
    int8_t serialDelta = (int8_t)(uint8_t)(packet.serial - m_nTaskSerial);
    if (m_bHasTaskSerial && serialDelta <= 0 && serialDelta > -32)
        return;

    m_nTaskSerial = packet.serial;
    m_bHasTaskSerial = true;

    // D bug 3: keep the last state of a streamed out copy, it is applied again when the copy streams back in
    if (!havePed)
    {
        m_lastTaskState = packet;
        m_bHasLastTaskState = true;
        return;
    }

    if (namedSync)
    {
        m_lastTaskState = packet;
        m_bHasLastTaskState = true;
        m_bPendingTaskState = false;
    }

    const bool applied = ApplyTaskStateInternal(packet);
    if (!applied)
    {
        m_bPendingTaskState = true;
        m_nPendingTaskStateUntil = CConfigLoader::GetSyncTime() + TASK_SYNC_PENDING_TIMEOUT;
    }

    // interior copy diagnosis (testeo 17-11: Home Invasion's sleeping owner stood up on the guest): what arrived and
    // what the copy plays now
    const uint32_t now = CConfigLoader::GetSyncTime();
    if (CConfigLoader::ms_bInteriorPopulationSync && ped->m_nAreaCode != 0 && ped->m_pRwClump &&
        now - m_nInteriorDiagAt >= 10000)
    {
        m_nInteriorDiagAt = now;
        CAnimBlendAssociation* top = nullptr;
        for (CAnimBlendAssociation* assoc = RpAnimBlendClumpGetFirstAssociation(ped->m_pRwClump); assoc;
             assoc = RpAnimBlendGetNextAssociation(assoc))
        {
            if (!assoc->m_bPartial && (top == nullptr || assoc->m_fBlendAmount > top->m_fBlendAmount))
                top = assoc;
        }
        const char* topBlock = "";
        if (top && top->m_pHierarchy && BlockOfHierarchy(top->m_pHierarchy) >= 0 &&
            BlockOfHierarchy(top->m_pHierarchy) < CAnimManager::ms_numAnimBlocks)
            topBlock = CAnimManager::ms_aAnimBlocks[BlockOfHierarchy(top->m_pHierarchy)].szName;
        CMissionMirror::LogEventf("interior-anim-copy model=%d task=%d has=%d named=%d block=%.15s hash=%08X applied=%d "
            "synced=%d top-group=%d top-block=%.15s top-blend=%.2f top-playing=%d local-area=%d",
            ped->m_nModelIndex, (int)packet.taskType, packet.hasAnim ? 1 : 0, packet.namedAnim ? 1 : 0, packet.animBlock,
            packet.animHash, applied ? 1 : 0, m_pSyncedAnim ? 1 : 0, top ? (int)top->m_nAnimGroup : -2, topBlock,
            top ? top->m_fBlendAmount : 0.0f, top ? (int)top->m_bPlaying : 0,
            FindPlayerPed(0) ? (int)FindPlayerPed(0)->m_nAreaCode : -1);
    }
}

// D bug 3: a copy that just streamed in applies the last state its syncer sent (it is only resent every 3 s)
void CNetworkPed::ApplySavedTaskState()
{
    if (m_bSyncing || !m_bHasLastTaskState || m_bStreamedOut || m_pPed == nullptr || !m_pPed->IsVTableValid() ||
        m_pPed->m_pIntelligence == nullptr)
        return;

    m_bPendingTaskState = false;
    if (!ApplyTaskStateInternal(m_lastTaskState))
    {
        m_bPendingTaskState = true;
        m_nPendingTaskStateUntil = CConfigLoader::GetSyncTime() + TASK_SYNC_PENDING_TIMEOUT;
    }
}

// D npc-named-anim-sync: retry a state whose animation block is still loading (called from CNetworkPedManager::Process)
void CNetworkPed::RetryPendingTaskState()
{
    if (!m_bPendingTaskState || !m_bHasLastTaskState)
        return;

    uint32_t now = CConfigLoader::GetSyncTime();

    if (m_bSyncing)
    {
        // interior take-over: the last named animation is replayed on the ped we are hosting
        if (PlayHostedNamedAnim(m_lastTaskState))
            m_bPendingTaskState = false;
        else if ((int32_t)(now - m_nPendingTaskStateUntil) >= 0)
            m_bPendingTaskState = false;
        return;
    }

    if (m_bStreamedOut || m_pPed == nullptr || !m_pPed->IsVTableValid() || m_pPed->m_pIntelligence == nullptr)
        return;  // applied again when it streams in

    if ((int32_t)(now - m_nPendingTaskStateUntil) >= 0)
    {
        m_bPendingTaskState = false;
        return;
    }

    if (ApplyTaskStateInternal(m_lastTaskState, false))
        m_bPendingTaskState = false;
}

void CNetworkPed::UpdateTaskSync()
{
    if (!CConfigLoader::ms_bNpcTaskSync || !m_bSyncing)
        return;

    // hosted ped: if the record was a copy before, the block of the animation the copy played is not ours anymore
    ReleaseSyncedAnimBlock();

    CPed* ped = m_pPed;
    if (m_bStreamedOut || ped == nullptr || !ped->IsVTableValid() || ped->m_pIntelligence == nullptr)
        return;

    CTaskManager& taskManager = ped->m_pIntelligence->m_TaskMgr;

    uint16_t taskType = (uint16_t)TASK_NONE;
    CTask* rootTask = taskManager.m_aPrimaryTasks[TASK_PRIMARY_PRIMARY];
    if (rootTask != nullptr)
    {
        int type = rootTask->GetTaskType();
        if (type >= 0 && type <= 0xFFFF)
            taskType = (uint16_t)type;
    }

    bool hasVehicle = false;
    vehicleid_t vehicleId{};
    int8_t seat = -1;
    int8_t door = 0;

    if (taskType == TASK_COMPLEX_ENTER_CAR_AS_DRIVER || taskType == TASK_COMPLEX_ENTER_CAR_AS_PASSENGER)
    {
        CTaskComplexEnterCar* enterTask = (CTaskComplexEnterCar*)rootTask;
        CVehicle* target = enterTask->m_pTargetVehicle;

        if (target != nullptr && target->IsVTableValid())
        {
            CNetworkVehicle* pNetworkVehicle = CNetworkVehicleManager::GetVehicle(target);
            if (pNetworkVehicle != nullptr)
            {
                hasVehicle = true;
                vehicleId = pNetworkVehicle->m_vehicleId;
                door = SanitizeDoor(enterTask->m_nTargetDoor);
                seat = enterTask->m_bAsDriver ? -1 : (int8_t)std::clamp(enterTask->m_nTargetSeat, 0, 7);
            }
        }
    }
    else if (taskType == TASK_COMPLEX_LEAVE_CAR)
    {
        CTaskComplexLeaveCar* leaveTask = (CTaskComplexLeaveCar*)rootTask;
        CVehicle* target = leaveTask->m_pTargetVehicle;

        if (target != nullptr && target->IsVTableValid())
        {
            CNetworkVehicle* pNetworkVehicle = CNetworkVehicleManager::GetVehicle(target);
            if (pNetworkVehicle != nullptr)
            {
                hasVehicle = true;
                vehicleId = pNetworkVehicle->m_vehicleId;
                door = SanitizeDoor(leaveTask->m_nTargetDoor);
            }
        }
    }

    bool hasAnim = false;
    uint16_t animGroup = 0;
    uint16_t animId = 0;
    bool namedAnim = false;
    char animBlock[16] = {'\0'};
    uint32_t animHash = 0;
    uint16_t animFlags = 0;
    float animTime = 0.0f;
    float animSpeed = 1.0f;

    // seated animations belong to the car, the position sync covers the ped from the driver / passenger packet
    if (!ped->m_nPedFlags.bInVehicle)
    {
        CAnimBlendAssociation* relevant = FindRelevantAssociation(ped);
        CAnimBlendAssociation* namedPose = CConfigLoader::ms_bNpcNamedAnimSync ? FindNamedAssociation(ped) : nullptr;
        // npc-anim-attached: a ped attached to an entity (Ryder on the Picador) keeps its idle as the strongest
        // full-body association while its catch is a partial one, so the named pose has to win when it blends as much
        if ((IsInteriorActor(ped) || (CConfigLoader::ms_bNpcAnimAttached && ped->m_pAttachedTo != nullptr)) && namedPose &&
            (!relevant || namedPose->m_fBlendAmount >= relevant->m_fBlendAmount))
            relevant = nullptr;
        if (CAnimBlendAssociation* assoc = relevant)
        {
            int id = NormalizeAnimId(assoc->m_nAnimGroup, assoc->m_nAnimId);
            if (id >= 0)
            {
                hasAnim = true;
                animGroup = assoc->m_nAnimGroup;
                animId = (uint16_t)id;
            }
        }
        // D: no association of a whitelisted group: maybe it is a named animation (dancing, bar, casino...)
        else if (CConfigLoader::ms_bNpcNamedAnimSync)
        {
            if (CAnimBlendAssociation* named = FindNamedAssociation(ped))
            {
                CAnimBlock& block = CAnimManager::ms_aAnimBlocks[BlockOfHierarchy(named->m_pHierarchy)];
                memset(animBlock, 0, sizeof(animBlock));
                strncpy_s(animBlock, sizeof(animBlock), block.szName, _TRUNCATE);

                hasAnim = true;
                namedAnim = true;
                animHash = named->m_pHierarchy->m_hashKey;
                animFlags = named->m_nFlags;
                animTime = named->m_fCurrentTime;
                animSpeed = named->m_fSpeed;
            }
        }
    }

    // npc-anim-attached diagnosis (Catalyst, 20-11): what an attached ped (Ryder on the Picador) actually sends, so the
    // retest tells whether the catch reaches the copies and which association it was
    if (CConfigLoader::ms_bNpcAnimAttached && ped->m_pAttachedTo != nullptr)
    {
        static uint32_t lastAttachedLog = 0;
        const uint32_t nowAttached = GetTickCount();
        if (nowAttached - lastAttachedLog > 2000)
        {
            lastAttachedLog = nowAttached;
            CMissionMirror::LogEventf("ped-anim-attached model=%d invec=%d task=%d anim=%d named=%d block=%.15s grp=%d",
                ped->m_nModelIndex, ped->m_nPedFlags.bInVehicle ? 1 : 0, (int)taskType, hasAnim ? 1 : 0,
                namedAnim ? 1 : 0, animBlock, (int)animGroup);
        }
    }

    uint32_t now = CConfigLoader::GetSyncTime();

    // interior-population-sync diagnosis (testeo 14-11: the diners stood on their chairs for the players that came in
    // after the owner): the strongest full-body animation of an interior ped next to another player, and what goes out
    // mission peds of an interior too (Home Invasion's sleeping owner stood up on the guest, testeo 17-11)
    if (CConfigLoader::ms_bInteriorPopulationSync && (m_bInteriorPopulation || ped->m_nAreaCode != 0) &&
        now - m_nInteriorDiagAt >= 10000 &&
        IsNearAnyNetworkPlayer(ped, 30.0f))
    {
        m_nInteriorDiagAt = now;
        CAnimBlendAssociation* top = nullptr;
        for (CAnimBlendAssociation* assoc = RpAnimBlendClumpGetFirstAssociation(ped->m_pRwClump); assoc;
             assoc = RpAnimBlendGetNextAssociation(assoc))
        {
            if (!assoc->m_bPartial && (top == nullptr || assoc->m_fBlendAmount > top->m_fBlendAmount))
                top = assoc;
        }
        const char* topBlock = "";
        if (top && top->m_pHierarchy && BlockOfHierarchy(top->m_pHierarchy) >= 0 &&
            BlockOfHierarchy(top->m_pHierarchy) < CAnimManager::ms_numAnimBlocks)
            topBlock = CAnimManager::ms_aAnimBlocks[BlockOfHierarchy(top->m_pHierarchy)].szName;
        // the five primary slots (physical response, temp / non-temp event response, primary, default): the diners'
        // sit task may live in a slot other than the primary one that is sent
        int slotTypes[5];
        for (int i = 0; i < 5; i++)
        {
            CTask* slotTask = taskManager.m_aPrimaryTasks[i];
            slotTypes[i] = slotTask ? slotTask->GetTaskType() : -1;
        }
        CMissionMirror::LogEventf("interior-anim-owner model=%d task=%d has=%d named=%d group=%d id=%d block=%.15s "
            "top-group=%d top-id=%d top-block=%.15s top-blend=%.2f top-playing=%d top-flags=%u slots=%d,%d,%d,%d,%d "
            "ped-area=%d rec-area=%u",
            ped->m_nModelIndex, (int)taskType, hasAnim ? 1 : 0, namedAnim ? 1 : 0, (int)animGroup, (int)animId,
            animBlock, top ? (int)top->m_nAnimGroup : -2, top ? (int)top->m_nAnimId : -2, topBlock,
            top ? top->m_fBlendAmount : 0.0f, top ? (int)top->m_bPlaying : 0, top ? (unsigned)top->m_nFlags : 0u,
            slotTypes[0], slotTypes[1], slotTypes[2], slotTypes[3], slotTypes[4], (int)ped->m_nAreaCode,
            (unsigned)m_nAreaCode);
        // the partial named pose (PCHAIR's 0x0812 leaves no task): what the relaxed filter publishes
        CAnimBlendAssociation* part = nullptr;
        for (CAnimBlendAssociation* assoc = RpAnimBlendClumpGetFirstAssociation(ped->m_pRwClump); assoc;
             assoc = RpAnimBlendGetNextAssociation(assoc))
        {
            if (assoc->m_bPartial && assoc->m_nAnimGroup == NAMED_ANIM_GROUP && assoc->m_fBlendAmount > 0.0f &&
                (part == nullptr || assoc->m_fBlendAmount > part->m_fBlendAmount))
                part = assoc;
        }
        const char* partBlock = "";
        if (part && part->m_pHierarchy)
        {
            const int partBlockId = BlockOfHierarchy(part->m_pHierarchy);
            if (partBlockId >= 0 && partBlockId < CAnimManager::ms_numAnimBlocks)
                partBlock = CAnimManager::ms_aAnimBlocks[partBlockId].szName;
        }
        CMissionMirror::LogEventf("interior-anim-owner-part model=%d part=%d block=%.15s hash=%08X blend=%.2f "
            "flags=%u playing=%d named-task=%d",
            ped->m_nModelIndex, part ? 1 : 0, partBlock, part && part->m_pHierarchy ? part->m_pHierarchy->m_hashKey : 0u,
            part ? part->m_fBlendAmount : 0.0f, part ? (unsigned)part->m_nFlags : 0u,
            part ? (int)part->m_bPlaying : 0, IsNamedAnimTask(ped) ? 1 : 0);
    }

    // D bug 3: while the ped has an animation, resend the whole state every 3 s; a copy that streamed in (or that
    // finished loading the block) after the last change would never receive it otherwise
    const uint32_t resendInterval = CConfigLoader::ms_bInteriorPopulationSync && m_bInteriorPopulation ?
        500 : TASK_SYNC_ANIM_RESEND_INTERVAL;
    bool resendAnim = CConfigLoader::ms_bNpcNamedAnimSync && hasAnim && m_bHasSentTaskState &&
                      (uint32_t)(now - m_nLastTaskStateSentAt) >= resendInterval &&
                      IsNearAnyNetworkPlayer(ped, TASK_SYNC_ANIM_RESEND_RADIUS);

    bool changed = !m_bHasSentTaskState || taskType != m_nLastSentTaskType || hasVehicle != m_bLastSentHasVehicle ||
                   vehicleId != m_nLastSentVehicleId || seat != m_nLastSentSeat || door != m_nLastSentDoor ||
                   hasAnim != m_bLastSentHasAnim || animGroup != m_nLastSentAnimGroup || animId != m_nLastSentAnimId ||
                   namedAnim != m_bLastSentNamedAnim || animHash != m_nLastSentAnimHash;
    if (!changed && !resendAnim)
        return;

    // entering / leaving starts (and ends) at once: the rest of the state is limited to one packet per second
    bool importantTask = IsCarTask(taskType) || IsCarTask(m_nLastSentTaskType);
    if (!importantTask && !resendAnim && m_bHasSentTaskState &&
        (uint32_t)(now - m_nLastTaskStateSentAt) < TASK_SYNC_SEND_INTERVAL)
        return;

    Packets::Peds::PedTaskState packet{};
    packet.pedid = m_pedId;
    packet.taskType = taskType;
    packet.hasVehicle = hasVehicle;
    packet.vehicleid = vehicleId;
    packet.seat = seat;
    packet.door = door;
    packet.hasAnim = hasAnim;
    packet.animGroup = animGroup;
    packet.animId = animId;
    packet.namedAnim = namedAnim;
    memcpy(packet.animBlock, animBlock, sizeof(packet.animBlock));
    packet.animHash = animHash;
    packet.animFlags = animFlags;
    packet.animTime = animTime;
    packet.animSpeed = animSpeed;
    packet.serial = ++m_nTaskSerial;

    GetPacketFactory().Send(packet);

    m_bHasSentTaskState = true;
    m_nLastSentTaskType = taskType;
    m_bLastSentHasVehicle = hasVehicle;
    m_nLastSentVehicleId = vehicleId;
    m_nLastSentSeat = seat;
    m_nLastSentDoor = door;
    m_bLastSentHasAnim = hasAnim;
    m_nLastSentAnimGroup = animGroup;
    m_nLastSentAnimId = animId;
    m_bLastSentNamedAnim = namedAnim;
    m_nLastSentAnimHash = animHash;
    m_nLastTaskStateSentAt = now;
}
