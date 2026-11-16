#pragma once

// Paramedic coop: AMBULAN runs on the mission owner and sends each patient to the ambulance of the nearest player of the
// mission (AMBULAN_FINDMEDIC). The patient is a ped of the owner, so when that ambulance was a guest's the boarding task
// ran on the owner's copy of a vehicle the guest drives far away and never finished. Now the command goes where it can
// work: TASK_ENTER_CAR_AS_PASSENGER (05CA) toward a vehicle another player drives hands the patient to that player
// (PED_PATIENT_TASK ENTER, the server reassigns it) and he runs the same command on his real ped and ambulance. The
// script's later commands on that patient (go to the hospital door 05D3, time out 0321, delivered 01C2) run on whoever
// simulates it. The script keeps working on the owner's copy: the passenger/on foot sync seats it, takes it out, and
// its death or removal reaches the copy. Switch: patient-handover.
class CPatientHandover
{
public:
    // after every command of every script (COpCodeSync::BuildAndSendOpcode)
    static void OnCommandExecuted(CRunningScript* pScript, uint16_t opcode);
    static void HandlePacket(const Packets::Peds::PedPatientTask& packet);
    static void Process();
    // CRunningScript::UpdateCompareFlag: in AMBULAN the "is the player's ambulance next to the patient / at the hospital"
    // checks look at the owner only; here a guest's ambulance can satisfy them (ambulance-guest-patients)
    static bool FilterCompare(CRunningScript* pScript, uint16_t opcode, bool state);
};
