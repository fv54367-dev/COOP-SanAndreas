#include "stdafx.h"
#include "CPedDamageSync.h"
#include "CNetworkPed.h"
#include "CMissionMirror.h"
#include "game_sa/CPedDamageResponseInfo.h"
#include <Fx_c.h>

// gta-reversed EventDamage.h: m_weaponType of CEventDamage, after CEventEditableResponse (0x14 bytes)
static constexpr size_t EVENT_DAMAGE_WEAPON_TYPE = 0x1C;
// m_damageResponse (0x38) + m_bDamageCalculated (0xA), gta-reversed PedDamageResponse.h
static constexpr size_t EVENT_DAMAGE_CALCULATED = 0x42;
// gta-reversed PedDamageResponseCalculator.h (size 0x14)
static constexpr size_t CALCULATOR_DAMAGER = 0x0;
static constexpr size_t CALCULATOR_BODY_PART = 0x8;
static constexpr size_t CALCULATOR_WEAPON_TYPE = 0xC;

// eWeaponType: 0..46 are the weapons a ped can hold (47+ are vehicles, explosions, falls, drowning...)
static constexpr int LAST_HELD_WEAPON = 46;

// inside ApplyAuthoritativeDamage: the damage comes from the authority, it is neither discarded nor reported again
static bool s_bApplying = false;

// gta-reversed Event.h: GetSourceEntity is vtable slot 10 (for an occupied vehicle CEventDamage gives its driver)
static CEntity* GetEventSource(void* event)
{
    auto** vtable = *reinterpret_cast<void***>(event);
    return reinterpret_cast<CEntity*(__thiscall*)(void*)>(vtable[10])(event);
}

static int GetEventWeapon(void* event)
{
    return *reinterpret_cast<int*>(reinterpret_cast<char*>(event) + EVENT_DAMAGE_WEAPON_TYPE);
}

// the ped behind a damage: the ped itself or the driver of the vehicle
static CPed* GetDamagerPed(CEntity* source)
{
    if (source == nullptr)
        return nullptr;
    if (source->m_nType == ENTITY_TYPE_PED)
        return static_cast<CPed*>(source);
    if (source->m_nType == ENTITY_TYPE_VEHICLE)
        return static_cast<CVehicle*>(source)->m_pDriver;
    return nullptr;
}

// explosions, fire and projectiles travel on their own packets and the syncer simulates them itself
static bool IsAreaWeapon(int weaponType)
{
    switch (weaponType)
    {
    case 16:  // grenade
    case 17:  // tear gas
    case 18:  // molotov
    case 35:  // rocket launcher
    case 36:  // heat seeking rocket launcher
    case 37:  // flamethrower
    case 39:  // satchel charge
    case 51:  // explosion
        return true;
    }
    return false;
}

// damage a player does himself: his weapons, his fists, the vehicle he drives
static bool IsLocalPlayerDamage(CEntity* source, int weaponType)
{
    CPed* damager = GetDamagerPed(source);
    return damager != nullptr && damager == FindPlayerPed(0) && !IsAreaWeapon(weaponType);
}

static bool IsRemotePlayerDamage(CEntity* source, int weaponType)
{
    CPed* damager = GetDamagerPed(source);
    return damager != nullptr && CNetworkPlayerManager::GetPlayer(damager) != nullptr && !IsAreaWeapon(weaponType);
}

bool CPedDamageSync::IsEnabled()
{
    return CConfigLoader::ms_bPedDamageAuthority && CNetwork::m_bAuthenticated;
}

bool CPedDamageSync::ShouldDiscard(void* event, CPed* ped)
{
    if (event == nullptr || ped == nullptr || s_bApplying || !IsEnabled())
        return false;

    // AffectsPed runs again when the ped handles the event (its reaction, its death). The damage of an event
    // that got this far was already decided: discarding it here left the NPC with no health and no death
    // (log of 2026-11-10: ped-damage-apply after=0 alive=1, then it kept walking).
    if (*reinterpret_cast<bool*>(reinterpret_cast<char*>(event) + EVENT_DAMAGE_CALCULATED))
        return false;

    CEntity* source = GetEventSource(event);
    const int weaponType = GetEventWeapon(event);

    if (ped->IsPlayer())
    {
        // the copies of the NPCs of other players do not hurt us by themselves: their syncer reports the hits
        if (ped != FindPlayerPed(0) || source == nullptr || source->m_nType != ENTITY_TYPE_PED ||
            weaponType > LAST_HELD_WEAPON)
            return false;

        CNetworkPed* networkPed = CNetworkPedManager::GetPed(source);
        return networkPed != nullptr && !networkPed->m_bSyncing;
    }

    CNetworkPed* networkPed = CNetworkPedManager::GetPed(ped);
    if (networkPed == nullptr)
        return false;

    // the shooter reports his own damage: the replay of his shot hurts no NPC, ours or a copy
    if (ms_bReplayingRemoteShot)
        return true;

    // our NPC: what another player does to it himself (his ped punches and drives here too) arrives as PED_DAMAGE
    if (networkPed->m_bSyncing)
        return IsRemotePlayerDamage(source, weaponType);

    // a copy only takes the damage of the local player; the rest is decided by its syncer
    return !IsLocalPlayerDamage(source, weaponType);
}

bool CPedDamageSync::OnDamageComputed(void* calculator, CPed* ped, CPedDamageResponseInfo* response)
{
    if (calculator == nullptr || ped == nullptr || response == nullptr || s_bApplying || !IsEnabled())
        return false;

    char* fields = reinterpret_cast<char*>(calculator);
    CEntity* source = *reinterpret_cast<CEntity**>(fields + CALCULATOR_DAMAGER);
    const int weaponType = *reinterpret_cast<int*>(fields + CALCULATOR_WEAPON_TYPE);
    const int pedPiece = std::clamp(*reinterpret_cast<int*>(fields + CALCULATOR_BODY_PART), 0, 15);
    const float fLoss = std::max(response->m_fDamageHealth, 0.0f) + std::max(response->m_fDamageArmor, 0.0f);
    if (weaponType < 0 || weaponType > WEAPON_FLARE)
        return false;

    static int nLogged = 0;

    if (ped->IsPlayer())
    {
        // one of our NPCs hurt another player: he applies it to himself (PED_HIT_PLAYER)
        CNetworkPlayer* target = CNetworkPlayerManager::GetPlayer(ped);
        if (target == nullptr || source == nullptr || source->m_nType != ENTITY_TYPE_PED ||
            weaponType > LAST_HELD_WEAPON || fLoss < 0.5f)
            return false;

        CNetworkPed* networkPed = CNetworkPedManager::GetPed(source);
        if (networkPed == nullptr || !networkPed->m_bSyncing)
            return false;

        Packets::Peds::PedHitPlayer packet{};
        packet.pedid = networkPed->m_pedId;
        packet.playerid = target->m_playerId;
        packet.weaponType = static_cast<eWeaponType>(weaponType);
        packet.pedPiece = static_cast<uint8_t>(pedPiece);
        packet.loss = static_cast<uint16_t>(std::clamp(static_cast<int>(std::ceil(fLoss)), 1, 5000));
        GetPacketFactory().Send(packet);

        if (nLogged < 80)
        {
            nLogged++;
            CMissionMirror::LogEventf("ped-hit-player-report weapon=%d loss=%d", weaponType, (int)packet.loss);
        }
        return false;
    }

    CNetworkPed* networkPed = CNetworkPedManager::GetPed(ped);
    if (networkPed == nullptr || networkPed->m_bSyncing || !IsLocalPlayerDamage(source, weaponType))
        return false;

    const bool bKilled = response->m_bHealthZero || response->m_bForceDeath;
    if (fLoss >= 0.5f || bKilled)
    {
        networkPed->m_nLocalDamageUntil = (CConfigLoader::GetSyncTime() + LOCAL_DAMAGE_WINDOW) | 1;

        Packets::Peds::PedDamage packet{};
        packet.pedid = networkPed->m_pedId;
        packet.weaponType = static_cast<eWeaponType>(weaponType);
        packet.pedPiece = static_cast<uint8_t>(pedPiece);
        packet.loss = static_cast<uint16_t>(std::clamp(static_cast<int>(std::ceil(fLoss)), 0, 5000));
        packet.killed = bKilled;
        GetPacketFactory().Send(packet);

        if (nLogged < 80)
        {
            nLogged++;
            CMissionMirror::LogEventf("ped-damage-report model=%d weapon=%d loss=%d killed=%d", ped->m_nModelIndex,
                weaponType, (int)packet.loss, (int)bKilled);
        }
    }
    return true;
}

// SET_CHAR_ONLY_DAMAGED_BY_PLAYER only sets bInvulnerable, and CEventDamage::AffectsPed (gta-reversed EventDamage.cpp:106)
// lets through only FindPlayerPed(): the damage of another coop player was refused by the syncer (DRUGS1 59@, model
// 111: ped-damage-apply before=150 after=150) and the copy the guest killed stood up again. For an NPC every player
// counts as "the player"; the flag is lifted only around the authoritative damage.
struct InvulnerableLift
{
    CPed* ped = nullptr;
    InvulnerableLift(CPed* victim, bool bByPlayer)
    {
        if (CConfigLoader::ms_bPedDamageAnyPlayer && bByPlayer && victim != nullptr && !victim->IsPlayer() &&
            victim->m_nPhysicalFlags.bInvulnerable)
        {
            ped = victim;
            ped->m_nPhysicalFlags.bInvulnerable = false;
        }
    }
    ~InvulnerableLift()
    {
        if (ped != nullptr && IsPedPointerValid(ped))
            ped->m_nPhysicalFlags.bInvulnerable = true;
    }
};

static float GetTotalHealth(CPed* ped)
{
    return std::max(ped->m_fHealth, 0.0f) + std::max(ped->m_fArmour, 0.0f);
}

// The damage goes through the game's own path (CWeapon::GenerateDamageEvent 0x73A530, gta-reversed Weapon.cpp:158:
// AffectsPed, damage response, reaction, death) and is then lowered to the exact value the other side computed.
// Never heals. An invulnerable ped, a protected ally or a cutscene refuse it like any other damage.
static void ApplyAuthoritativeDamage(CPed* victim, CEntity* attacker, eWeaponType weaponType, uint8_t pedPiece,
    float fLoss, bool bKill)
{
    const float fBefore = GetTotalHealth(victim);
    const float fExpected = fBefore - fLoss;
    const int nDamage = (bKill || fExpected < 1.0f) ? 5000 : std::max(1, static_cast<int>(std::ceil(fLoss)));

    // same direction the game computes for a bullet (Weapon.cpp:717)
    int nDirection = 0;
    if (attacker != nullptr)
    {
        const CVector offset = attacker->GetPosition() - victim->GetPosition();
        nDirection = victim->GetLocalDirection(CVector2D(offset.x, offset.y));
    }

    {
        const bool bByPlayer = attacker != nullptr && attacker->m_nType == ENTITY_TYPE_PED &&
                               static_cast<CPed*>(attacker)->IsPlayer();
        InvulnerableLift lift(victim, bByPlayer);
        s_bApplying = true;
        CWeapon::GenerateDamageEvent(victim, attacker, weaponType, nDamage, static_cast<ePedPieceTypes>(pedPiece), nDirection);
        s_bApplying = false;
    }

    if (!victim->IsAlive() || fExpected < 1.0f)
        return;

    const float fAfter = GetTotalHealth(victim);
    if (fAfter >= fBefore || fAfter <= fExpected)
        return;

    const float fExtra = fAfter - fExpected;
    const float fFromArmour = std::min(std::max(victim->m_fArmour, 0.0f), fExtra);
    victim->m_fArmour -= fFromArmour;
    victim->m_fHealth = std::max(1.0f, victim->m_fHealth - (fExtra - fFromArmour));
}

void CPedDamageSync::HandleDamage(Packets::Peds::PedDamage& packet)
{
    if (!IsEnabled())
        return;

    // only the syncer applies it (the server sends it to the current one; a change in flight is dropped here)
    CNetworkPed* networkPed = CNetworkPedManager::GetPed(packet.pedid);
    if (networkPed == nullptr || !networkPed->m_bSyncing)
        return;

    CPed* ped = networkPed->m_pPed;
    if (!IsPedPointerValid(ped) || !ped->IsAlive())
        return;

    // without its author the ally protection (DamageHooks::ShouldDiscardAllyEvent) could not judge it
    CNetworkPlayer* attacker = CNetworkPlayerManager::GetPlayer(packet.attackerId);
    if (attacker == nullptr || attacker->m_pPed == nullptr || !attacker->m_pPed->IsVTableValid())
        return;

    // the health travels in one byte: the copy of a ped with more than 255 can die long before the real one
    const bool bKill = packet.killed && ped->m_fHealth <= 255.0f;
    const float fBefore = GetTotalHealth(ped);
    const int nModel = ped->m_nModelIndex;
    ApplyAuthoritativeDamage(ped, attacker->m_pPed, packet.weaponType, packet.pedPiece, packet.loss, bKill);

    static int nLogged = 0;
    if (nLogged < 80)
    {
        nLogged++;
        const bool bValid = IsPedPointerValid(ped);
        CMissionMirror::LogEventf("ped-damage-apply model=%d weapon=%d loss=%d killed=%d before=%.0f after=%.0f alive=%d",
            nModel, (int)packet.weaponType, (int)packet.loss, (int)packet.killed, fBefore,
            bValid ? GetTotalHealth(ped) : 0.0f, bValid ? (int)ped->IsAlive() : 0);
    }
}

void CPedDamageSync::HandleHitPlayer(Packets::Peds::PedHitPlayer& packet)
{
    if (!IsEnabled() || !(packet.playerid == CNetworkPlayerManager::m_localPlayerId))
        return;

    CPed* localPlayer = FindPlayerPed(0);
    if (localPlayer == nullptr || !localPlayer->IsAlive())
        return;

    // the copy of the NPC is the author when we have it; streamed out, the damage has no author
    CNetworkPed* networkPed = CNetworkPedManager::GetPed(packet.pedid);
    CEntity* shooter = networkPed != nullptr && !networkPed->m_bSyncing && !networkPed->m_bStreamedOut &&
                               networkPed->m_pPed != nullptr && networkPed->m_pPed->IsVTableValid()
                           ? networkPed->m_pPed
                           : nullptr;

    const float fBefore = GetTotalHealth(localPlayer);
    ApplyAuthoritativeDamage(localPlayer, shooter, packet.weaponType, packet.pedPiece, packet.loss, false);

    static int nLogged = 0;
    if (nLogged < 80)
    {
        nLogged++;
        CMissionMirror::LogEventf("ped-hit-player-apply weapon=%d loss=%d before=%.0f after=%.0f", (int)packet.weaponType,
            (int)packet.loss, fBefore, GetTotalHealth(localPlayer));
    }
}

// The syncer sent health 0. The copy dies like a ped the game kills: a damage event with its death response,
// handled by the ped itself (CEventDamage has priority over whatever response task the copy is running; a die
// task put by hand in the primary slot waited behind it and the copy stayed standing).
bool CPedDamageSync::KillCopy(CNetworkPed* networkPed)
{
    CPed* ped = networkPed->m_pPed;
    const uint32_t now = CConfigLoader::GetSyncTime();
    if (networkPed->m_nDeathRequestedAt != 0)
    {
        // asked already: the ped handles the event on its next frames; after that the caller falls back
        return (int32_t)(now - networkPed->m_nDeathRequestedAt) < (int32_t)DEATH_EVENT_TIME;
    }

    networkPed->m_nDeathRequestedAt = now | 1;
    ped->m_fHealth = 1.0f;
    {
        // the syncer already decided the death: an only-damaged-by-player copy must not refuse it
        InvulnerableLift lift(ped, true);
        s_bApplying = true;
        CWeapon::GenerateDamageEvent(ped, nullptr, WEAPON_PISTOL, 5000, static_cast<ePedPieceTypes>(3 /* torso */), 0);
        s_bApplying = false;
    }

    const bool bAccepted = ped->m_fHealth < 1.0f;
    ped->m_fHealth = 0.0f;

    static int nLogged = 0;
    if (nLogged < 40)
    {
        nLogged++;
        CMissionMirror::LogEventf("ped-copy-kill model=%d accepted=%d", ped->m_nModelIndex, (int)bAccepted);
    }
    return bAccepted;
}

// A copy the local player killed whose syncer did not kill it (protected ally, invulnerable, more health than the
// byte carries) would stay on the floor here and alive there. Once the answer window is over it is built again from
// the record, standing and with the last task state of its syncer.
void CPedDamageSync::ProcessCopy(CNetworkPed* networkPed)
{
    if (networkPed == nullptr || networkPed->m_bSyncing || networkPed->m_bStreamedOut)
        return;

    CPed* ped = networkPed->m_pPed;
    if (ped == nullptr || !IsEnabled() || ped->IsAlive() || networkPed->m_fHealth < 1.0f ||
        ped->m_nPedFlags.bInVehicle)
    {
        networkPed->m_nDeadMismatchSince = 0;
        return;
    }

    const uint32_t now = CConfigLoader::GetSyncTime();
    if (networkPed->m_nLocalDamageUntil != 0 && (int32_t)(networkPed->m_nLocalDamageUntil - now) > 0)
        return;

    if (networkPed->m_nDeadMismatchSince == 0)
    {
        networkPed->m_nDeadMismatchSince = now | 1;
        return;
    }

    if ((int32_t)(now - networkPed->m_nDeadMismatchSince) < (int32_t)DEAD_MISMATCH_TIME)
        return;

    networkPed->m_nDeadMismatchSince = 0;
    networkPed->m_nLocalDamageUntil = 0;

    static int nLogged = 0;
    if (nLogged < 80)
    {
        nLogged++;
        CMissionMirror::LogEventf("ped-copy-revive model=%d health=%.0f", ped->m_nModelIndex, networkPed->m_fHealth);
    }

    // StreamOut keeps the health of the game ped (the dead copy): the one of the syncer is the one that counts
    const float fHealth = networkPed->m_fHealth;
    networkPed->StreamOut();
    networkPed->m_fHealth = fHealth;
    if (networkPed->StreamIn())
        networkPed->ApplySavedTaskState();
}

// DoBulletImpact only adds the blood when its own damage event was accepted (gta-reversed Weapon.cpp:723): the
// replay of another player's shot on an NPC is discarded, so the blood is added here.
void CPedDamageSync::AddReplayBlood(CEntity* victim, CColPoint& colPoint, int incrementalHit)
{
    if (!IsEnabled() || victim == nullptr || incrementalHit > 0 || victim->m_nType != ENTITY_TYPE_PED)
        return;

    CPed* ped = static_cast<CPed*>(victim);
    if (!IsPedPointerValid(ped) || ped->IsPlayer() || CNetworkPedManager::GetPed(ped) == nullptr)
        return;

    g_fx.AddBlood(colPoint.m_vecPoint, colPoint.m_vecNormal, 8, ped->m_fContactSurfaceBrightness);
}
