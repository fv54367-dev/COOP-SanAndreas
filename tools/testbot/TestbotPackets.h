#pragma once

// B3: el testbot reimplementa lo minimo de un cliente (ENet + shared/) y para eso necesita las clases de
// paquete de shared/network/packets. Cada clase se registra sola contra GetPacketFactory() (ver
// PacketRegistrar en shared/network/packet.h), asi que el bot trae su propia factory: la del server
// (server/src/CPacketFactory.h) hace la red de verdad y no se puede linkear aca. Aca el registro solo
// guarda una instancia por tipo para poder deserializar lo que llega.
//
// IMPORTANTE: incluir este header *antes* que cualquier cabecera de shared/network/packets, por el mismo
// motivo por el que el server incluye CPacketFactory.h primero: GetPacketFactory tiene que estar declarada
// cuando se instancia PacketRegistrar.

#include <unordered_map>

#include "network/packet_types.h"

class Packet;

class CTestbotPacketFactory
{
public:
    void RegisterPacket(Packet* pPacket);
    Packet* Get(ePacketType packetType) const;

    std::unordered_map<ePacketType, Packet*> m_map;
};

inline CTestbotPacketFactory& GetPacketFactory()
{
    static CTestbotPacketFactory factory;
    return factory;
}

#include "network/packet.h"
#include "network/ids.h"
#include <network/packets/system.h>
#include <network/packets/players.h>  // peds.h usa Packets::Players::SHealthSnapshot / SWeaponSnapshot
#include <network/packets/peds.h>
#include <network/packets/scripts.h>  // MISSION_LOCK_* (escenario e)

inline void CTestbotPacketFactory::RegisterPacket(Packet* pPacket)
{
    if (pPacket != nullptr)
    {
        m_map[pPacket->GetType()] = pPacket;
    }
}

inline Packet* CTestbotPacketFactory::Get(ePacketType packetType) const
{
    auto it = m_map.find(packetType);
    return it == m_map.end() ? nullptr : it->second;
}
