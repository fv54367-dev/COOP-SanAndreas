#include "stdafx.h"
#include "CutsceneClothesHooks.h"
#include "../CMissionMirror.h"
#include <CPedClothesDesc.h>

static void __cdecl CClothes__RebuildCutscenePlayer_OwnClothes(CPlayerPed* player, int modelId)
{
    auto* local = FindPlayerPed(0);
    if (local && local->m_pPlayerData && local->m_pPlayerData->m_pPedClothesDesc && local->m_pRwClump)
    {
        auto& clothes = *local->m_pPlayerData->m_pPedClothesDesc;
        clothes.m_fFatStat = CStats::GetStatValue(STAT_FAT);
        clothes.m_fMuscleStat = CStats::GetStatValue(STAT_MUSCLE);

        // RebuildPlayer compares against the global clothes cache and can skip textures.
        // CSPLAY shares the player TXD but never builds textures itself (0x5A69D0).
        // A null old descriptor forces a full native build, without undressing the ped.
        CClothes::ConstructPedModel(local->m_nModelIndex, clothes, nullptr, false);
        plugin::Call<0x5A8270, CPlayerPed*, int>(local, modelId);
        CMissionMirror::LogEventf("cutscene-cj-rebuild ped=%p requested=%p model=%d sourceModel=%d full=1 head=%08X torso=%08X fat=%.1f muscle=%.1f",
            static_cast<void*>(local), static_cast<void*>(player), modelId, local->m_nModelIndex,
            clothes.m_anTextureKeys[1], clothes.m_anTextureKeys[0], clothes.m_fFatStat, clothes.m_fMuscleStat);
        return;
    }

    CMissionMirror::LogEventf("cutscene-cj-rebuild ped=%p requested=%p model=%d full=0 reason=local-unavailable",
        static_cast<void*>(local), static_cast<void*>(player), modelId);
    plugin::Call<0x5A8270, CPlayerPed*, int>(player, modelId);
}

// CClothes::RebuildPlayer (0x5A82C0) passes PlayerClothes (0xBC1C78) as the old descriptor, and with nothing changed
// CClothesBuilder::CreateSkinnedClump (0x5A69D0, checked in the exe at 0x5A6B84) returns no clump: model 0 keeps the
// clump it has and CPed::Dress clones it. All the CJs share model 0, and the cutscene build above (and every other
// player's rebuild) changes it without the cache knowing, so a remote CJ rebuilt with his unchanged clothes was dressed
// with ours (host after MUSIC1: net-rebuild-create, guest shown in the host's clothes). Without the cache it is always
// a full native build, as above.
static void __cdecl CClothes__ConstructPedModel_Full(uint32_t modelId, CPedClothesDesc& newClothes,
    const CPedClothesDesc* oldClothes, bool bCutscenePlayer)
{
    if (CConfigLoader::ms_bPlayerFullRebuild)
        oldClothes = nullptr;
    CClothes::ConstructPedModel(modelId, newClothes, oldClothes, bCutscenePlayer);
}

void CutsceneClothesHooks::InjectHooks()
{
    patch::RedirectCall(0x5A834D, CClothes__ConstructPedModel_Full);
    patch::RedirectCall(0x4D5E43, CClothes__RebuildCutscenePlayer_OwnClothes);
    patch::RedirectCall(0x4D5EA3, CClothes__RebuildCutscenePlayer_OwnClothes);
}
