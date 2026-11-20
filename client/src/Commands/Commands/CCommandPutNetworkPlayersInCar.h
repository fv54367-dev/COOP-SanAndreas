#pragma once
#include "../CCustomCommand.h"

class CCommandPutNetworkPlayersInCar : public CCustomCommand
{
public:
    // every player of the mission into a free passenger seat of the car, or an equal car of his own behind it.
    // Returns how many boardings were sent. `skip`: a player to leave out (defensive: the remote player list never holds
    // the host of this client).
    static int PutPlayersInCar(CVehicle* pVehicle, bool bMissionBoarding = false, playerid_t skip = {});
    // this player has a boarding of ours on its way in (packet waiting for the car, or the native walk already started)
    static bool IsBoardingThisPlayer(playerid_t player);
    static bool HasMissionPassenger(CVehicle* vehicle, bool includeEntering = false);
    static void ProcessPendingBoarding();
    static void ClearPendingBoarding();
    // the authority: every player of the mission gets an equal vehicle parked next to this one (CMissionSyncRules, the
    // objective bike of the original scripts); nobody is put in it. Returns how many were sent.
    static int ParkCopiesNextTo(CVehicle* pVehicle, int guestsPerCopy = 1);
    // a receiver: the parked copy it made (PUT_IN_VEHICLE_SCRIPTED with seat PARKED_COPY_SEAT) keeps a blip until the
    // local player gets on it, and is released then or when the authority's mission ends
    static void AddParkedCopy(int vehicleHandle, CRunningScript* mirrorScript = nullptr);
    // phase (T98): GUEST_PARK_ALL as before (park and put the guests in); GUEST_PARK_ONLY parks the vehicles the guests
    // need besides the host's free seats, with their blip, and puts nobody in them; GUEST_HOST_SEATS_ONLY only sits
    // guests in the host's free seats
    static constexpr int GUEST_PARK_ALL = 0;
    static constexpr int GUEST_PARK_ONLY = 1;
    static constexpr int GUEST_HOST_SEATS_ONLY = 2;
    // GUEST_PARK_BOARD: park the car and put the guests inside it (driver + passengers) even with mission-no-auto-board; the
    // guests of the mission are counted wherever they are (a trip skip, the second Drive-Thru car after the cutscene)
    static constexpr int GUEST_PARK_BOARD = 3;
    static int ParkSharedGuestCar(CVehicle* vehicle, CRunningScript* script, const char* rule = "drive-thru", int perVehicle = 3,
        bool waitForHost = false, int phase = GUEST_PARK_ALL);
    static void ProcessParkedCopies();
    // the parked guest vehicles made so far; the oldest `count` lose their blip and are released (a newer car replaces them)
    static size_t ParkedCopyCount();
    static void ReleaseParkedCopies(size_t count);
    // trip skip: guest vehicles left farther than `radius` from `at` are released and the pending boardings of `players` dropped
    static void ReleaseParkedCopiesFar(const CVector& at, float radius, const std::vector<playerid_t>& players);
    // trip skip: the host (in `hostVehicle`) was moved; each guest takes a free seat of it or, when none is left, one shared
    // 4-door car parked beside it (the fewest vehicles). Returns the guests sent to a seat.
    static int BoardGuestsAfterTripSkip(CVehicle* hostVehicle, CRunningScript* script, const std::vector<playerid_t>& guests);
    // mission-no-auto-board: a guest car was already parked in this mission (reset when no story mission runs)
    static bool MissionGuestCarMade();
    static void ResetMissionGuestCar();
    // one of the vehicles parked for the guests (AddParkedCopy), not the mission's own
    static bool IsParkedCopy(CVehicle* vehicle);
    // live parked guest vehicles within `radius` of `at`
    static int CountParkedCopiesNear(const CVector& at, float radius);

    static void ProcessGuestVehicles();
    // the authority's vehicle (already its own, with a driver of the mission): this guest is put in passenger seat `seat`
    // (0 = first) once the vehicle has its network id; dropped when the mission ends, `*enabled` goes off, or after 8 s
    static void QueueGuestPassenger(playerid_t player, CVehicle* vehicle, int seat, CRunningScript* script, const bool* enabled);
    static constexpr int8_t GUEST_DRIVER_SEAT = -3;
    static constexpr int8_t PARKED_COPY_SEAT = -2;

private:
    // Inherited via CCustomCommand
    void Process(CRunningScript* script) override;
};
