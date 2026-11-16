#include "stdafx.h"
#include "CCollectibleSync.h"
#include "CMissionMode.h"
#include <CPickups.h>
#include <CStats.h>
#include <CAudioEngine.h>
#include <game_sa/CTagManager.h>

namespace
{
constexpr int NUM_PICKUP_SLOTS = 620;
constexpr int NUM_TAG_SLOTS = 150;
// a tag counts as sprayed above this alpha (gta-reversed CTagManager::ALPHA_TAGGED)
constexpr uint8_t ALPHA_TAGGED = 228;
// more than this at once is a loaded game, not somebody collecting
constexpr int MAX_COLLECTED_PER_POLL = 3;
// a longer pause between polls (loading screen, disconnected) only resets what is known
constexpr uint32_t MAX_POLL_GAP_MS = 2000;
constexpr float MATCH_DISTANCE = 2.0f;

enum eCollectible : uint8_t
{
    COLLECTIBLE_OYSTER,
    COLLECTIBLE_HORSESHOE,
    COLLECTIBLE_SNAPSHOT,
    COLLECTIBLE_COUNT
};

// stats raised by CPickups::PickedUpOyster (0x4552D0), PickedUpHorseShoe (0x455390) and PictureTaken (0x456A70)
constexpr uint16_t COLLECTED_STAT[COLLECTIBLE_COUNT] = {243, 241, 231};
constexpr uint16_t STAT_SNAPSHOTS_TAKEN = 231;
constexpr uint16_t STAT_TOTAL_SNAPSHOTS = 232;

uint8_t s_aTagAlpha[NUM_TAG_SLOTS];
int s_nTagsKnown = -1;

int8_t s_aPickupKind[NUM_PICKUP_SLOTS];  // -1: no collectible alive in the slot
short s_aPickupRef[NUM_PICKUP_SLOTS];
int s_aStat[COLLECTIBLE_COUNT];
bool s_bPickupsKnown = false;

uint32_t s_nLastPollTick = 0;

int GetCollectibleKind(const CPickup& pickup)
{
    if (pickup.m_nPickupType == PICKUP_NONE)
        return -1;
    if (pickup.m_nPickupType == PICKUP_SNAPSHOT)
        return COLLECTIBLE_SNAPSHOT;
    // MI_OYSTER and MI_HORSESHOE (gta-reversed ModelIndices.cpp)
    if (pickup.m_nModelIndex == *reinterpret_cast<int16_t*>(0x8CD750))
        return COLLECTIBLE_OYSTER;
    if (pickup.m_nModelIndex == *reinterpret_cast<int16_t*>(0x8CD754))
        return COLLECTIBLE_HORSESHOE;
    return -1;
}

int GetStat(uint16_t stat)
{
    return static_cast<int>(CStats::GetStatValue(stat));
}

int GetNumTags()
{
    return std::clamp(CTagManager::ms_numTags, 0, NUM_TAG_SLOTS);
}

void ResetTags()
{
    s_nTagsKnown = GetNumTags();
    for (int i = 0; i < s_nTagsKnown; i++)
        s_aTagAlpha[i] = CTagManager::ms_tagDesc[i].m_nAlpha;
}

void ResetPickups()
{
    for (int i = 0; i < NUM_PICKUP_SLOTS; i++)
    {
        s_aPickupKind[i] = static_cast<int8_t>(GetCollectibleKind(CPickups::aPickUps[i]));
        s_aPickupRef[i] = CPickups::aPickUps[i].m_nReferenceIndex;
    }
    for (int k = 0; k < COLLECTIBLE_COUNT; k++)
        s_aStat[k] = GetStat(COLLECTED_STAT[k]);
    s_bPickupsKnown = true;
}

void SendTag(const CEntity* entity, uint8_t alpha)
{
    Packets::World::TagUpdate packet{};
    // a sprayed tag goes as fully sprayed: the packet sends partial alphas in steps of 8 and 229..231 would arrive
    // as 224, not sprayed for the others
    packet.payload.bFullySprayed = alpha > ALPHA_TAGGED;
    packet.payload.alpha = alpha;
    packet.payload.pos_x = static_cast<int16_t>(floor(entity->GetPosition().x));
    packet.payload.pos_y = static_cast<int16_t>(floor(entity->GetPosition().y));
    packet.payload.pos_z = static_cast<int16_t>(floor(entity->GetPosition().z));
    GetPacketFactory().Send(packet);
}

// the local tags that got more paint since the last poll go to the others (also the partial spraying, so they see it)
void ProcessTags(bool bShare)
{
    const int numTags = GetNumTags();
    if (numTags != s_nTagsKnown)
    {
        ResetTags();
        return;
    }

    int newlySprayed = 0;
    for (int i = 0; i < numTags; i++)
    {
        const uint8_t alpha = CTagManager::ms_tagDesc[i].m_nAlpha;
        if (alpha > ALPHA_TAGGED && s_aTagAlpha[i] <= ALPHA_TAGGED)
            newlySprayed++;
    }

    if (newlySprayed > MAX_COLLECTED_PER_POLL)
    {
        ResetTags();
        return;
    }

    for (int i = 0; i < numTags; i++)
    {
        const auto& tagDesc = CTagManager::ms_tagDesc[i];
        if (bShare && tagDesc.m_pEntity && tagDesc.m_nAlpha > s_aTagAlpha[i])
            SendTag(tagDesc.m_pEntity, tagDesc.m_nAlpha);
        s_aTagAlpha[i] = tagDesc.m_nAlpha;
    }
}

// a collectible pickup that disappeared while its stat went up by the same amount was collected here
void ProcessPickups(bool bShare)
{
    if (!s_bPickupsKnown)
    {
        ResetPickups();
        return;
    }

    int collected[COLLECTIBLE_COUNT]{};
    int totalCollected = 0;
    for (int i = 0; i < NUM_PICKUP_SLOTS; i++)
    {
        const CPickup& pickup = CPickups::aPickUps[i];
        if (s_aPickupKind[i] >= 0 && pickup.m_nPickupType == PICKUP_NONE && pickup.m_nReferenceIndex == s_aPickupRef[i])
        {
            collected[s_aPickupKind[i]]++;
            totalCollected++;
        }
    }

    bool bValid = totalCollected <= MAX_COLLECTED_PER_POLL;
    for (int k = 0; k < COLLECTIBLE_COUNT && bValid; k++)
    {
        if (GetStat(COLLECTED_STAT[k]) - s_aStat[k] != collected[k])
            bValid = false;
    }

    if (bValid && bShare && totalCollected > 0)
    {
        for (int i = 0; i < NUM_PICKUP_SLOTS; i++)
        {
            CPickup& pickup = CPickups::aPickUps[i];
            if (s_aPickupKind[i] < 0 || pickup.m_nPickupType != PICKUP_NONE || pickup.m_nReferenceIndex != s_aPickupRef[i])
                continue;

            Packets::World::CollectiblePicked packet{};
            packet.kind = static_cast<uint8_t>(s_aPickupKind[i]);
            packet.pos = pickup.GetPosn();
            GetPacketFactory().Send(packet);
        }
    }

    ResetPickups();
}

// CPickups::PictureTaken (0x456A70) after it found the pickup: $100, sound, stat and message ($100000 for the last one)
void GiveSnapshotReward()
{
    CWorld::Players[0].m_nMoney += 100;
    AudioEngine.ReportFrontendAudioEvent(13 /* AE_FRONTEND_PICKUP_COLLECTABLE1 */, 0.0f, 1.0f);
    CStats::IncrementStat(STAT_SNAPSHOTS_TAKEN, 1.0f);

    const int taken = GetStat(STAT_SNAPSHOTS_TAKEN);
    const int total = GetStat(STAT_TOTAL_SNAPSHOTS);
    if (taken == total)
    {
        plugin::Call<0x447B80, const char*, int16_t, uint16_t, int16_t>("SN_ALL", -1, 5000, -1);
        CWorld::Players[0].m_nMoney += 100000;
    }
    else
    {
        plugin::Call<0x447B80, const char*, int16_t, uint16_t, int16_t>(
            "SN_ONE", static_cast<int16_t>(taken), 5000, static_cast<int16_t>(total));
    }
}

bool ShouldReceiveFrom(CNetworkPlayer* pNetworkPlayer)
{
    return !CMissionMode::IsIndividual() && pNetworkPlayer && !pNetworkPlayer->m_bIndividualMode;
}
}

void CCollectibleSync::Process()
{
    if (!FindPlayerPed(0))
        return;

    const uint32_t tick = GetTickCount();
    const bool bGap = s_nLastPollTick == 0 || tick - s_nLastPollTick > MAX_POLL_GAP_MS;
    s_nLastPollTick = tick;

    if (bGap)
    {
        ResetTags();
        ResetPickups();
        return;
    }

    const bool bShare = !CMissionMode::IsIndividual();
    ProcessTags(bShare);
    ProcessPickups(bShare);
}

void CCollectibleSync::HandleTag(Packets::World::TagUpdate* packet)
{
    // TAG_UPDATE has no sender: an individual player does not send them
    if (CMissionMode::IsIndividual() || s_nTagsKnown != GetNumTags())
        return;

    // what was sprayed here before this packet still goes out
    ProcessTags(true);

    for (int i = 0; i < s_nTagsKnown; i++)
    {
        auto& tagDesc = CTagManager::ms_tagDesc[i];
        if (!tagDesc.m_pEntity)
            continue;

        const CVector& pos = tagDesc.m_pEntity->GetPosition();
        if (static_cast<int16_t>(floor(pos.x)) != packet->payload.pos_x ||
            static_cast<int16_t>(floor(pos.y)) != packet->payload.pos_y ||
            static_cast<int16_t>(floor(pos.z)) != packet->payload.pos_z)
            continue;

        // only more paint: another player's partial spraying never erases a tag sprayed here
        if (packet->payload.alpha > tagDesc.m_nAlpha)
            CTagManager::SetAlpha(tagDesc.m_pEntity, packet->payload.alpha);  // "TAG_ONE" message when it gets sprayed

        s_aTagAlpha[i] = tagDesc.m_nAlpha;
    }
}

void CCollectibleSync::HandleCollectible(Packets::World::CollectiblePicked* packet)
{
    if (packet->kind >= COLLECTIBLE_COUNT || !FindPlayerPed(0))
        return;

    if (!ShouldReceiveFrom(CNetworkPlayerManager::GetPlayer(packet->playerid)))
        return;

    // what was collected here before this packet still goes out
    ProcessPickups(true);

    int best = -1;
    float bestDist = MATCH_DISTANCE;
    for (int i = 0; i < NUM_PICKUP_SLOTS; i++)
    {
        if (GetCollectibleKind(CPickups::aPickUps[i]) != packet->kind)
            continue;

        const float dist = DistanceBetweenPoints(CPickups::aPickUps[i].GetPosn(), packet->pos);
        if (dist < bestDist)
        {
            best = i;
            bestDist = dist;
        }
    }

    if (best == -1)
        return;  // already collected here

    CPickup& pickup = CPickups::aPickUps[best];
    plugin::CallMethod<0x4556C0, CPickup*>(&pickup);  // CPickup::Remove

    switch (packet->kind)
    {
    case COLLECTIBLE_OYSTER:
        plugin::Call<0x4552D0>();  // CPickups::PickedUpOyster: $100, stat, message
        break;
    case COLLECTIBLE_HORSESHOE:
        plugin::Call<0x455390>();  // CPickups::PickedUpHorseShoe: $100, stat, luck, message
        break;
    case COLLECTIBLE_SNAPSHOT:
        GiveSnapshotReward();
        break;
    }

    // not collected here: it must not go back out
    s_aPickupKind[best] = -1;
    s_aStat[packet->kind] = GetStat(COLLECTED_STAT[packet->kind]);
}
