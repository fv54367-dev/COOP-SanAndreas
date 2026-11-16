#include "stdafx.h"
#include "CParachuteSync.h"

#include <algorithm>
#include <CAnimBlendStaticAssociation.h>
#include <CObject.h>
#include <unordered_map>

namespace
{
// model of the open parachute the PLCHUTE script creates with Object.Create(#PARACHUTE)
constexpr int PARACHUTE_MODEL = 3131;

// the game requests the animation block `index` as the streaming model 25575 + index (see COpCodeSync)
constexpr int ANIM_BLOCK_MODEL_BASE = 25575;

struct SParachuteAnim
{
    const char* name;
    bool bOpen;  // from this animation on the parachute is already deployed
    bool bLoop;
};

// names taken from scm/scripts/PLCHUTE.txt: the script plays these on the player ped from the PARACHUTE block
// (the *_O entries there are animations of the parachute object, not of the ped). Index 0 means "no parachute".
constexpr SParachuteAnim PARACHUTE_ANIMS[] = {
    {"FALL_SKYDIVE", false, true},
    {"FALL_SKYDIVE_ACCEL", false, true},
    {"FALL_SKYDIVE_L", false, true},
    {"FALL_SKYDIVE_R", false, true},
    {"FALL_SKYDIVE_DIE", false, false},
    {"PARA_OPEN", true, false},
    {"PARA_STEERR", true, true},
    {"PARA_STEERL", true, true},
    {"PARA_DECEL", true, true},
    {"PARA_FLOAT", true, true},
    {"PARA_LAND_WATER", true, false},
    {"PARA_LAND", true, false},
};

constexpr uint8_t NUM_PARACHUTE_ANIMS = (uint8_t)(sizeof(PARACHUTE_ANIMS) / sizeof(PARACHUTE_ANIMS[0]));
static_assert(NUM_PARACHUTE_ANIMS <= Packets::Players::PlayerParachuteState::MAX_ANIM,
    "the parachute anim table does not fit in the packet");

struct SPlayerParachuteState
{
    uint8_t requestedAnim = 0;  // last index received from the network
    uint8_t appliedAnim = 0;    // index actually playing on the ped (0 if it could not be applied yet)
    CPed* appliedPed = nullptr;
    CObject* pParachute = nullptr;
};

std::unordered_map<CNetworkPlayer*, SPlayerParachuteState> ms_states;

int ms_nAnimBlockIndex = -1;
bool ms_bAnimBlockRefAdded = false;

// emitter
uint32_t ms_nLastCheckTick = 0;
uint32_t ms_nLastSendTick = 0;
uint8_t ms_nLastSentAnim = 0;

bool IsOpenAnim(uint8_t anim)
{
    return anim != 0 && anim <= NUM_PARACHUTE_ANIMS && PARACHUTE_ANIMS[anim - 1].bOpen;
}

bool EnsureAnimBlock()
{
    if (ms_nAnimBlockIndex < 0)
    {
        ms_nAnimBlockIndex = CAnimManager::GetAnimationBlockIndex("PARACHUTE");

        // the block should already be part of the game's anim block list; register it just in case
        if (ms_nAnimBlockIndex < 0)
        {
            ms_nAnimBlockIndex = CAnimManager::RegisterAnimBlock("PARACHUTE");
            if (ms_nAnimBlockIndex < 0)
                return false;
        }
    }

    if (!CAnimManager::ms_aAnimBlocks[ms_nAnimBlockIndex].bLoaded)
    {
        CStreaming::RequestModel(ANIM_BLOCK_MODEL_BASE + ms_nAnimBlockIndex,
            eStreamingFlags::MISSION_REQUIRED | eStreamingFlags::GAME_REQUIRED);
        CStreaming::LoadAllRequestedModels(false);

        if (!CAnimManager::ms_aAnimBlocks[ms_nAnimBlockIndex].bLoaded)
            return false;
    }

    // keep the block in memory while it is being used
    if (!ms_bAnimBlockRefAdded)
    {
        CAnimManager::AddAnimBlockRef(ms_nAnimBlockIndex);
        ms_bAnimBlockRefAdded = true;
    }

    return true;
}

void StopParachuteAnimations(CPed* pPed)
{
    if (pPed == nullptr || pPed->m_pRwClump == nullptr)
        return;

    for (const SParachuteAnim& anim : PARACHUTE_ANIMS)
    {
        if (CAnimBlendAssociation* pAssoc = RpAnimBlendClumpGetAssociation(pPed->m_pRwClump, anim.name))
            pAssoc->SetBlendTo(0.0f, -1000.0f);
    }
}

bool PlayParachuteAnim(CPed* pPed, uint8_t anim)
{
    if (pPed == nullptr || pPed->m_pRwClump == nullptr || anim == 0 || anim > NUM_PARACHUTE_ANIMS)
        return false;

    if (!EnsureAnimBlock())
        return false;

    const char* name = PARACHUTE_ANIMS[anim - 1].name;
    int flags = ANIMATION_STARTED | (PARACHUTE_ANIMS[anim - 1].bLoop ? ANIMATION_LOOPED : 0);

    CAnimBlendAssociation* pAssoc = nullptr;
    CAnimBlendHierarchy* pHierarchy =
        CAnimManager::GetAnimation(name, &CAnimManager::ms_aAnimBlocks[ms_nAnimBlockIndex]);
    if (pHierarchy != nullptr)
        pAssoc = CAnimManager::BlendAnimation(pPed->m_pRwClump, pHierarchy, flags, 4.0f);
    else
    {
        // no hierarchy in the block: blend through the assoc group of the animation definition
        int assocGroupId = CAnimManager::GetFirstAssocGroup("PARACHUTE");
        if (assocGroupId >= 0)
        {
            if (CAnimBlendStaticAssociation* pStaticAssoc = CAnimManager::GetAnimAssociation(assocGroupId, name))
                pAssoc = CAnimManager::BlendAnimation(pPed->m_pRwClump, assocGroupId, pStaticAssoc->m_nAnimId, 4.0f);
        }
    }

    if (pAssoc == nullptr)
        return false;

    // Normalize reused/group associations too; 0x8 auto-removes at the end, it does not hold the last frame.
    pAssoc->m_nFlags &= ~(ANIMATION_LOOPED | ANIMATION_UNLOCK_LAST_FRAME);
    pAssoc->m_nFlags |= flags;
    return true;
}

bool LoadParachuteModel()
{
    if (CStreaming::ms_aInfoForModel[PARACHUTE_MODEL].m_nLoadState == LOADSTATE_LOADED)
        return true;

    CStreaming::RequestModel(PARACHUTE_MODEL, eStreamingFlags::MISSION_REQUIRED | eStreamingFlags::GAME_REQUIRED);
    CStreaming::LoadAllRequestedModels(false);

    return CStreaming::ms_aInfoForModel[PARACHUTE_MODEL].m_nLoadState == LOADSTATE_LOADED;
}

// the PLCHUTE script attaches the parachute with offset (0,0,0): the model pivot already lines up with the ped
void AttachParachute(SPlayerParachuteState& state, CPed* pPed)
{
    if (!LoadParachuteModel())
        return;

    CObject* pObject = CObject::Create(PARACHUTE_MODEL);
    if (pObject == nullptr)
        return;

    pObject->m_nObjectType = OBJECT_MISSION;  // the game must not clean it up as an ambient object
    pObject->m_bUsesCollision = false;

    if (!pObject->m_matrix)
        pObject->AllocateMatrix();
    pObject->SetPosn(pPed->GetPosition());
    pObject->UpdateRwMatrix();
    pObject->UpdateRwFrame();

    // `state.pParachute` is the reference holder: the game nulls it for us if the object is deleted
    state.pParachute = pObject;
    pObject->RegisterReference((CEntity**)&state.pParachute);

    CWorld::Add(pObject);

    pObject->AttachEntityToEntity(pPed, CVector(0.0f, 0.0f, 0.0f), CVector(0.0f, 0.0f, 0.0f));
}

void DestroyParachute(SPlayerParachuteState& state)
{
    CObject* pObject = state.pParachute;
    if (pObject == nullptr)
        return;

    if (pObject->IsVTableValid())
    {
        // The ped holds a reference to this field; remove it before freeing the object.
        if (pObject->m_pAttachedTo != nullptr)
        {
            pObject->m_pAttachedTo->CleanUpOldReference((CEntity**)&pObject->m_pAttachedTo);
            pObject->m_pAttachedTo = nullptr;
        }

        pObject->CleanUpOldReference((CEntity**)&state.pParachute);
        CWorld::Remove(pObject);
        CWorld::RemoveReferencesToDeletedObject(pObject);
        delete pObject;
    }

    state.pParachute = nullptr;
}

void ApplyState(CNetworkPlayer* pPlayer, SPlayerParachuteState& state)
{
    CPed* pPed = pPlayer->m_pPed;

    if (pPed == nullptr)
    {
        DestroyParachute(state);
        state.appliedAnim = 0;
        state.appliedPed = nullptr;
        return;
    }

    bool bAnimationMissing = state.appliedAnim != 0 &&
        (pPed->m_pRwClump == nullptr ||
            RpAnimBlendClumpGetAssociation(pPed->m_pRwClump, PARACHUTE_ANIMS[state.appliedAnim - 1].name) == nullptr);
    if (state.appliedAnim != state.requestedAnim || state.appliedPed != pPed || bAnimationMissing)
    {
        // a respawned ped cannot keep the old parachute object attached to it
        if (state.appliedPed != pPed)
            DestroyParachute(state);

        StopParachuteAnimations(pPed);
        state.appliedAnim = 0;
        state.appliedPed = pPed;

        if (state.requestedAnim != 0 && PlayParachuteAnim(pPed, state.requestedAnim))
            state.appliedAnim = state.requestedAnim;
    }

    // while the block is still being loaded this is retried every frame (appliedAnim != requestedAnim)
    if (IsOpenAnim(state.appliedAnim))
    {
        if (state.pParachute == nullptr)
            AttachParachute(state, pPed);
    }
    else
    {
        DestroyParachute(state);
    }
}

// index (1..NUM_PARACHUTE_ANIMS) of the parachute animation the local ped is playing, 0 if none
uint8_t GetLocalParachuteAnim(CPed* pPed)
{
    if (pPed == nullptr || pPed->m_pRwClump == nullptr)
        return 0;

    uint8_t bestAnim = 0;
    float bestBlend = 0.1f;

    for (uint8_t i = 0; i < NUM_PARACHUTE_ANIMS; i++)
    {
        CAnimBlendAssociation* pAssoc = RpAnimBlendClumpGetAssociation(pPed->m_pRwClump, PARACHUTE_ANIMS[i].name);
        if (pAssoc == nullptr)
            continue;

        if (pAssoc->m_fBlendAmount > bestBlend)
        {
            bestBlend = pAssoc->m_fBlendAmount;
            bestAnim = i + 1;
        }
    }

    return bestAnim;
}
}  // namespace

// in CParachuteSync.cpp because the player packet handlers file is taken by the (bigger) on-foot packet handlers
PACKET_HANDLER(ePacketType::PLAYER_PARACHUTE_STATE, Packets::Players::PlayerParachuteState* pParachuteState)
{
    CNetworkPlayer* pNetworkPlayer = CNetworkPlayerManager::GetPlayer(pParachuteState->senderPlayerid);
    if (pNetworkPlayer == nullptr)
        return;

    ms_states[pNetworkPlayer].requestedAnim = pParachuteState->anim;
}

void CParachuteSync::Process()
{
    // receivers: keep the peds in sync with the last state each player sent
    for (auto it = ms_states.begin(); it != ms_states.end();)
    {
        CNetworkPlayer* pPlayer = it->first;
        bool bStillConnected =
            std::find(CNetworkPlayerManager::m_pPlayers.begin(), CNetworkPlayerManager::m_pPlayers.end(), pPlayer) !=
            CNetworkPlayerManager::m_pPlayers.end();

        if (!CNetwork::m_bAuthenticated || !bStillConnected)
        {
            DestroyParachute(it->second);
            it = ms_states.erase(it);
            continue;
        }

        ApplyState(pPlayer, it->second);
        ++it;
    }

    // emitter
    if (!CNetwork::m_bAuthenticated)
    {
        ms_nLastSentAnim = 0;
        ms_nLastCheckTick = 0;
        ms_nLastSendTick = 0;
        return;
    }

    uint32_t now = GetTickCount();
    if (now - ms_nLastCheckTick < 100)
        return;
    ms_nLastCheckTick = now;

    uint8_t anim = GetLocalParachuteAnim(FindPlayerPed(0));

    // on change, or every second while there is a parachute animation to keep alive on the other clients
    if (anim == ms_nLastSentAnim && (anim == 0 || now - ms_nLastSendTick < 1000))
        return;

    Packets::Players::PlayerParachuteState packet{};
    packet.anim = anim;
    GetPacketFactory().Send(packet);

    ms_nLastSentAnim = anim;
    ms_nLastSendTick = now;
}
