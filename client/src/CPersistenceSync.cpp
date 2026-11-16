#include "stdafx.h"
#include "CHomeInvasion.h"
#include "CPersistenceSync.h"
#include "CCutsceneMgr.h"
#include "CStreaming.h"
#include "CClothes.h"
#include "CWeaponInfo.h"

static constexpr unsigned int PERSIST_SEND_INTERVAL = 10000;

static bool ms_bReceived = false;   // the server told us whether it keeps states
static bool ms_bEnabled = false;
static bool ms_bPendingApply = false;
static Packets::Players::SPersistData ms_pendingData{};
static unsigned int ms_nLastSendTime = 0;
static bool ms_bSendNow = false;  // the server asked for the state (forced save)

// last position outside interiors (positions inside one aren't restored)
static bool ms_bHasOutdoorPos = false;
static CVector ms_vecLastOutdoorPos{};
static float ms_fLastOutdoorHeading = 0.0f;

void CPersistenceSync::Reset()
{
    ms_bReceived = false;
    ms_bEnabled = false;
    ms_bPendingApply = false;
    ms_nLastSendTime = 0;
    ms_bSendNow = false;
}

void CPersistenceSync::RequestImmediateSend()
{
    ms_bSendNow = true;
}

void CPersistenceSync::HandleState(Packets::Players::PlayerPersistState* pPacket)
{
    ms_bReceived = true;
    ms_bEnabled = pPacket->enabled;
    ms_bPendingApply = pPacket->enabled && pPacket->hasData;
    if (ms_bPendingApply)
    {
        ms_pendingData = pPacket->data;
    }
}

bool CPersistenceSync::CanApplyOrCollect()
{
    CPlayerPed* pPlayer = FindPlayerPed(0);
    if (!pPlayer || !pPlayer->m_pPlayerData || !pPlayer->m_pPlayerData->m_pPedClothesDesc)
        return false;

    if (CCutsceneMgr::ms_running || CStatsSync::ms_nRemoteProcessingDepth > 0)
        return false;

    // dead or busted: the respawn changes the state, wait for it
    return CWorld::Players[0].m_nPlayerState == PLAYERSTATE_PLAYING && pPlayer->m_fHealth > 0.0f;
}

static void LoadWeaponModel(eWeaponType weaponType)
{
    int model = CWeaponInfo::GetWeaponInfo(weaponType, 1)->m_nModelId1;
    if (model > 0 && CStreaming::ms_aInfoForModel[model].m_nLoadState != LOADSTATE_LOADED)
    {
        CStreaming::RequestModel(model, eStreamingFlags::GAME_REQUIRED);
        CStreaming::LoadAllRequestedModels(false);
    }
}

void CPersistenceSync::Apply(const Packets::Players::SPersistData& data)
{
    // money, weapons, health, position and the rest belong to the game's own save slots,
    // the server only keeps the clothes and the physique
    CPlayerPed* pPlayer = FindPlayerPed(0);

    CStats::StatTypesFloat[STAT_FAT] = data.floatStats[STAT_FAT];
    CStats::StatTypesFloat[STAT_MUSCLE] = data.floatStats[STAT_MUSCLE];

    // the clothes are resent to the others by CStatsSync::ProcessClothes
    *pPlayer->m_pPlayerData->m_pPedClothesDesc = data.clothes;
    if (pPlayer->m_pRwClump)
    {
        CClothes::RebuildPlayer(pPlayer, false);
    }

    CStatsSync::NotifyChanged();
    CChat::AddMessage("{c7a6ff}[Coop]{d6dce8} Se restauro tu ropa guardada en el servidor.");
}

bool CPersistenceSync::Collect(Packets::Players::SPersistData& data)
{
    CPlayerPed* pPlayer = FindPlayerPed(0);
    CPlayerInfo& info = CWorld::Players[0];

    for (int i = 0; i < 224; i++)
        data.intStats[i] = CStats::StatTypesInt[i];

    for (int i = 0; i < 83; i++)
        data.floatStats[i] = CStats::StatTypesFloat[i];

    data.money = info.m_nMoney;
    data.health = pPlayer->m_fHealth;
    data.armour = pPlayer->m_fArmour;
    data.fightingStyle = (uint8_t)pPlayer->m_nFightingStyle;
    data.activeWeaponSlot = std::min<uint8_t>(pPlayer->m_nActiveWeaponSlot, 12);

    for (int slot = 0; slot < 13; slot++)
    {
        const CWeapon& weapon = pPlayer->m_aWeapons[slot];
        bool valid = weapon.m_eWeaponType > WEAPON_UNARMED && weapon.m_eWeaponType <= 46;
        data.weaponTypes[slot] = valid ? (uint8_t)weapon.m_eWeaponType : 0;
        data.weaponAmmo[slot] = valid ? weapon.m_nTotalAmmo : 0;
    }

    data.clothes = *pPlayer->m_pPlayerData->m_pPedClothesDesc;
    CHomeInvasion::StripMissionLook(data.clothes);

    if (pPlayer->m_nAreaCode == 0 && CGame::currArea == 0)
    {
        ms_bHasOutdoorPos = true;
        ms_vecLastOutdoorPos = pPlayer->GetPosition();
        ms_fLastOutdoorHeading = pPlayer->m_fCurrentRotation;
    }

    data.hasPosition = ms_bHasOutdoorPos;
    data.position[0] = ms_vecLastOutdoorPos.x;
    data.position[1] = ms_vecLastOutdoorPos.y;
    data.position[2] = ms_vecLastOutdoorPos.z;
    data.heading = ms_fLastOutdoorHeading;
    return true;
}

void CPersistenceSync::Process(unsigned int tickCount)
{
    if (!CNetwork::m_bAuthenticated || !ms_bReceived || !ms_bEnabled)
        return;

    if (!CanApplyOrCollect())
    {
        // E bug 12: the server asked for the state (forced save) and we cannot collect it yet (loading, cutscene,
        // dead); answer now with an empty state instead of leaving the save-all to time out over us
        if (ms_bSendNow)
        {
            ms_bSendNow = false;

            Packets::Players::PlayerPersistState packet{};
            packet.enabled = true;
            packet.hasData = false;
            GetPacketFactory().Send(packet);
        }

        return;
    }

    if (ms_bPendingApply)
    {
        ms_bPendingApply = false;
        Apply(ms_pendingData);
        ms_nLastSendTime = tickCount;
        return;
    }

    if (ms_nLastSendTime != 0 && !ms_bSendNow && tickCount < ms_nLastSendTime + PERSIST_SEND_INTERVAL)
        return;

    ms_bSendNow = false;

    Packets::Players::PlayerPersistState packet{};
    packet.enabled = true;
    packet.hasData = Collect(packet.data);
    GetPacketFactory().Send(packet);
    ms_nLastSendTime = tickCount;
}
