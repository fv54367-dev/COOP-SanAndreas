#pragma once
#include <list>

// The first player that enters an interior owns its population: every player inside sees the peds the owner creates
// (shop clerks, barmen, ambient peds...). The rest are guests: their own interior peds stay local but hidden, and are
// not synced.
class CInteriorOwnership
{
public:
    // interior coordinates are far away from the main map; two entities this close share the interior when the area
    // code of one of them is still unknown
    static constexpr float SAME_INTERIOR_RADIUS = 150.0f;

    // D interior-handover: when the owner leaves, his peds are released (B2) and their take-over must reach us
    // before we show (and host) our own hidden population
    static constexpr uint32_t INTERIOR_HANDOVER_GRACE = 3000;

    // called every 500 ms by CStreamingSync::Process
    static void Process();
    static void HandlePopulation(const Packets::Peds::InteriorPopulation& packet);
    static void ObserveLocalPed(CPed* ped);
    static bool IsServicePed(CPed* ped);
    static void MaintainHiddenPeds();
    static bool AdoptServicePed(CNetworkPed* networkPed);
    // a remote record (the owner's) of the same model stands where this local ped is
    static bool IsRepresentedRemotely(CPed* ped);

    // disconnection / session end: show the peds hidden as a guest again
    static void Reset();

    static bool IsInside() { return s_role != Role::None; }
    static bool IsOwner() { return s_role == Role::Owner; }
    static bool IsGuest() { return s_role == Role::Guest; }
    static unsigned char GetLocalArea() { return s_nAreaCode; }

    // a ped our game is adding to the world: as a guest, hide it instead of syncing it
    static bool ShouldHideLocalPed(CPed* ped);
    static void HideLocalPed(CPed* ped);

    // owner side (tie backup): a remote ped spawned inside our interior by a guest must not be created
    static bool ShouldSuppressRemotePed(const CNetworkPed* networkPed);

    // D interior-handover: a ped of our interior was handed over to us (the owner left); while the grace period
    // runs, this keeps our own hidden population hidden instead of showing a second, different one
    static void OnPedTakeover(unsigned char areaCode);

private:
    enum class Role
    {
        None,
        Owner,
        Guest
    };

    static void RestoreHiddenPeds(bool alsoSync);
    static void HideLocalInteriorPeds();
    static void BecomeOwner(bool unconfirmed);
    static void BecomeGuest(playerid_t ownerId);
    static void LeaveInterior();
    static void RefreshServicePeds();

    static Role s_role;
    static playerid_t s_ownerId;
    static unsigned char s_nAreaCode;
    // the owner was chosen in this interval because nobody was inside: a lower player id wins the tie
    static bool s_bOwnerUnconfirmed;
    // consecutive ticks the owner has not been seen: avoids giving the interior away on a single missing packet
    static unsigned int s_nOwnerMissingTicks;
    // D interior-handover: while these ticks are still ahead, the hidden population waits for the take-overs of
    // the owner that left; s_bGotTakeover says at least one arrived in that period
    static uint32_t s_nHandoverGraceUntil;
    static bool s_bGotTakeover;
    // local peds hidden while we are a guest (RegisterReference nulls the pointer when the game deletes them)
    static std::list<CPed*> s_hiddenPeds;
};
