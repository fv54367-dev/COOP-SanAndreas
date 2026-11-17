#pragma once

class CRunningScript;
class CVehicle;

namespace mission_guest_cars {

bool IsStoryMissionRunning();
bool IsMissionCleanupVehicle(CVehicle* vehicle);
void OnOgLocBikeHeadingSet(CRunningScript* pScript);
void OnCarBlipAdded(CRunningScript* pScript);
void ProcessOgLocGuestCar();
void OnOgLocPassengerWarp(CRunningScript* script);
void OnDriveThruCarHeadingSet(CRunningScript* script);
void OnMusic1GarageOpen(CRunningScript* script);
void ProcessCountrysideGuestVehicles();
void ProcessDriveByGuestCars(bool& bTeleportPending);
void ProcessLosSepulcrosGuestCars(bool& bTeleportPending);
void ProcessRobbingUncleSamGuestCars();
void ProcessHomeInvasionGuestCars();
void ProcessDriveThruGuestCars();
void ProcessCatalystGuestCars();
void ProcessRunningDogGuestCars(bool& bTeleportPending);
void ProcessWrongSideTracksGuestCars(bool& bTeleportPending);
void ProcessJustBusinessGuestCars(bool& bTeleportPending);
void ProcessTripSkip();
void ProcessMissionBoarding(bool& bTeleportPending);
void ResetGuestCarTriggers();
void ResetObjectivesIfIdle();

}  // namespace mission_guest_cars
