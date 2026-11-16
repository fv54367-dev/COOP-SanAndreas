#pragma once

// chat-tpm-fijar: /fijar sticks the local player to the vehicle he stands on (keeps his place on it and turns with it,
// he can still turn and aim); /fijar again, jumping, getting into a vehicle, dying or the vehicle going lets him go.
class CVehicleSurf
{
public:
    static void Toggle();
    static void Process();
    static bool IsActive();
};
