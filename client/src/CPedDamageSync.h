#pragma once

class CNetworkPed;
class CPedDamageResponseInfo;

// ped-damage-authority: one authority per NPC. Its syncer decides its health and its death, and decides whether its
// NPCs hurt a player. A copy only takes the damage of the local player, which is reported as the game computes it
// (PED_DAMAGE); the shots replayed from other players and from the copies' own weapons draw, but hurt nobody.
class CPedDamageSync
{
public:
    // a copy the local player hurt is not healed back by its syncer for this long (ms)
    static constexpr uint32_t LOCAL_DAMAGE_WINDOW = 1500;
    // a copy dead here whose syncer keeps it alive is rebuilt after this long (ms)
    static constexpr uint32_t DEAD_MISMATCH_TIME = 1000;
    // a copy asked to die through a damage event gets this long before the die task is given by hand (ms)
    static constexpr uint32_t DEATH_EVENT_TIME = 500;

    // inside the replay of another player's shot (PLAYER_BULLET_SHOT)
    static inline bool ms_bReplayingRemoteShot = false;

    static bool IsEnabled();

    // CEventDamage::AffectsPed (see DamageHooks): true = this game does not decide that damage
    static bool ShouldDiscard(void* event, CPed* ped);
    // CPedDamageResponseCalculator::ComputeWillKillPed (see PlayerHooks), after the game computed the damage of one
    // event on a ped that is not the local player. true = the damage stays applied on the copy (it was reported)
    static bool OnDamageComputed(void* calculator, CPed* ped, CPedDamageResponseInfo* response);

    // the syncer sent health 0 (CNetworkPed::ApplyRemoteDeath). false = give the die task by hand
    static bool KillCopy(CNetworkPed* networkPed);
    static void ProcessCopy(CNetworkPed* networkPed);
    static void AddReplayBlood(CEntity* victim, CColPoint& colPoint, int incrementalHit);

    static void HandleDamage(Packets::Peds::PedDamage& packet);
    static void HandleHitPlayer(Packets::Peds::PedHitPlayer& packet);
};
