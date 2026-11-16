#include "stdafx.h"
#include "CMissionMachines.h"
#include "CMissionObjectMoves.h"
#include "network/packets/mission_machines.h"
PACKET_HANDLER(ePacketType::MACHINE_PHASE, Packets::Scripts::MachinePhase* packet)
{
    CMissionMachines::OnPhase(*packet);
}
PACKET_HANDLER(ePacketType::GUEST_MACHINE_DAMAGE, Packets::Scripts::GuestMachineDamage* packet)
{
    CMissionMachines::OnDamage(*packet);
}

PACKET_HANDLER(ePacketType::GUEST_OBJECT_MOVE, Packets::Scripts::GuestObjectMove* packet)
{
    CMissionObjectMoves::Receive(*packet);
}
