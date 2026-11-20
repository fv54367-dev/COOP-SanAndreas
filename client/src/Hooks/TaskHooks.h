#pragma once
class TaskHooks
{
public:
	static void InjectHooks();

	// the CTaskComplexLeaveCar hook sent a VehicleExit just now (within a few seconds): the in->out poll in Main
	// must not repeat it for the same exit
	static bool LeaveTaskExitRecent();
};
