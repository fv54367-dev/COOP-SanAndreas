#include <limits>
// tools/testbot/testbot.cpp
//
// B3: testbot — ejecutable de consola que se conecta a un server local como 1 o 2 jugadores falsos y
// verifica automaticamente el traspaso de NPCs del server (handlers de PED_RELEASE en
// server/src/PacketHandlers/peds.cpp + CHandover, y la desconexion del dueno en server/src/CNetwork.cpp).
//
// Uso: testbot.exe [host] [puerto] [escenario]
//   escenario: all (a, b, c, e, f, g, h, i, j, k, l, m, n, o) | a | b | c | d | e | f | g | h | i | j | k | l | m | n | o
//   (a), (b) y (c) necesitan un server con handover = 1; (d) uno con handover = 0.
// Imprime [PASS]/[FAIL] por chequeo y ">>> escenario (x): PASS/FAIL". Sale con 0 si todo pasa.

#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "stdafx.h"
#include "TestbotPackets.h"
#include "network/packets/mission_photos.h"
#include "network/packets/gps.h"
#include "network/packets/world.h"
#include "network/packets/vehicles.h"

#include "enet/enet.h"
#include "config.h"
#include "semver.h"

namespace
{
constexpr size_t MAX_PACKET_BYTES = 10 * 1024;
constexpr uint32_t CONNECT_TIMEOUT_MS = 5000;
constexpr uint32_t HANDSHAKE_TIMEOUT_MS = 5000;

// ambos jugadores en el mapa principal (area 0); el ped se crea a 5 m del jugador B
constexpr uint8_t MAIN_MAP_AREA = 0;
constexpr float PED_X = 5.0f;
constexpr float GROUND_Z = 3.0f;

int g_nChecks = 0;
int g_nFailures = 0;

bool Expect(bool condition, const char* what)
{
    ++g_nChecks;
    if (condition)
    {
        printf("[PASS] %s\n", what);
    }
    else
    {
        ++g_nFailures;
        printf("[FAIL] %s\n", what);
    }
    return condition;
}

// para los fallos que no son un chequeo (conexion, serializacion): cuentan igual en el resumen
bool Fail(const char* format, ...)
{
    char buffer[512];
    va_list args;
    va_start(args, format);
    vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);
    return Expect(false, buffer);
}

const char* TypeName(ePacketType type)
{
    uint16_t raw = static_cast<uint16_t>(type);
    return raw < static_cast<uint16_t>(ePacketType::PACKET_ID_MAX) ? ePacketType_ToString(type) : "?";
}

// lo unico que el bot necesita recordar de lo que le llega
struct RecvEvent
{
    ePacketType type = ePacketType::PED_REMOVE;
    pedid_t pedid{};
    bool syncing = false;
    bool takeover = false;
    // MISSION_LOCK_STATE (escenario e)
    int missionId = -1;
    bool locked = false;
    playerid_t owner{};
};

class Bot
{
public:
    ~Bot() { Disconnect(false); }

    bool Connect(const char* hostName, uint16_t port, const std::string& name);
    void Disconnect(bool graceful);
    bool Handshake();
    void Pump(uint32_t ms);
    void Send(Packet& packet);
    void SendOnFoot(float x, float y, float z);

    void ClearEvents() { m_events.clear(); }
    bool Received(ePacketType type, pedid_t pedid) const { return First(type, pedid) != nullptr; }
    const RecvEvent* First(ePacketType type, pedid_t pedid) const;
    // el ultimo MISSION_LOCK_STATE de esa mision (nullptr si no llego ninguno)
    const RecvEvent* LastLock(int missionId) const;
    std::string EventLog() const;
    std::vector<Packets::Peds::InteriorPopulation> populationEvents;
    std::vector<Packets::Scripts::SocialAction> socialEvents;
    std::vector<Packets::Scripts::EmoteState> emoteEvents;
    const Packets::Scripts::SocialAction* LastSocial(uint8_t action) const {
        for (auto it = socialEvents.rbegin(); it != socialEvents.rend(); ++it) if (it->action == action) return &*it;
        return nullptr;
    }
    std::vector<Packets::Players::VehicleGps> gpsEvents;
    std::vector<Packets::World::GarageDoor> garageEvents;
    std::vector<Packets::Scripts::MissionPhoto> photoEvents;
    std::vector<Packets::Scripts::CutscenePartners> sceneEvents;
    std::vector<Packets::Scripts::ActivityChallenge> challengeEvents;
    const Packets::Scripts::ActivityChallenge* LastChallenge(uint8_t action) const {
        for (auto it = challengeEvents.rbegin(); it != challengeEvents.rend(); ++it) if (it->action == action) return &*it;
        return nullptr;
    }
    std::vector<Packets::Scripts::StoryRace> storyRaceEvents;
    std::vector<Packets::Scripts::CoopPool> poolEvents;
    const Packets::Scripts::CoopPool* LastPool(uint8_t action) const {
        for (auto it = poolEvents.rbegin(); it != poolEvents.rend(); ++it) if (it->action == action) return &*it;
        return nullptr;
    }

    playerid_t Id() const { return m_id; }
    const std::string& Name() const { return m_name; }

private:
    void HandleEvent(ENetEvent& event);
    void HandlePacket(const uint8_t* data, int size);

    std::string m_name;
    ENetHost* m_host = nullptr;
    ENetPeer* m_peer = nullptr;
    playerid_t m_id{};
    bool m_handshaken = false;
    bool m_peerDropped = false;
    uint8_t m_rejectReason = 0;  // 0 = el server no nos rechazo
    std::vector<RecvEvent> m_events;
};

bool Bot::Connect(const char* hostName, uint16_t port, const std::string& name)
{
    m_name = name;

    m_host = enet_host_create(nullptr, 1, (int)ePacketChannel::COUNT, 0, 0);
    if (m_host == nullptr)
    {
        return Fail("%s: enet_host_create fallo", m_name.c_str());
    }

    ENetAddress address{};
    enet_address_set_host(&address, hostName);
    address.port = port;

    uint32_t packedVersion = semver_parse(COOPSA_VERSION, nullptr);
    m_peer = enet_host_connect(m_host, &address, (int)ePacketChannel::COUNT, packedVersion);
    if (m_peer == nullptr)
    {
        return Fail("%s: enet_host_connect fallo", m_name.c_str());
    }

    bool connected = false;
    ENetEvent event;
    uint32_t deadline = enet_time_get() + CONNECT_TIMEOUT_MS;
    while (!connected && enet_time_get() < deadline)
    {
        while (enet_host_service(m_host, &event, 20) > 0)
        {
            if (event.type == ENET_EVENT_TYPE_CONNECT)
            {
                connected = true;
                break;
            }
            HandleEvent(event);
        }
    }

    if (!connected)
    {
        return Fail("%s: el server no respondio en %s:%u (%u ms)", m_name.c_str(), hostName, port, CONNECT_TIMEOUT_MS);
    }

    return true;
}

bool Bot::Handshake()
{
    // lo mismo que el cliente real: PLAYER_CONNECTED con la version y el nick (client/src/CNetwork.cpp)
    Packets::System::PlayerConnected packet{};
    packet.payload.isAlreadyConnected = false;
    packet.payload.playerid = -1;
    packet.payload.version = semver_parse(COOPSA_VERSION, nullptr);
    snprintf(packet.payload.name, Config::MAX_NICKNAME_LENGTH + 1, "%s", m_name.c_str());
    Send(packet);

    uint32_t deadline = enet_time_get() + HANDSHAKE_TIMEOUT_MS;
    while (!m_handshaken && m_rejectReason == 0 && !m_peerDropped && enet_time_get() < deadline)
    {
        Pump(50);
    }

    if (m_rejectReason != 0)
    {
        return Fail("%s: el server rechazo la conexion (reason %u: %s)", m_name.c_str(), m_rejectReason,
            m_rejectReason == 1 ? "version distinta" : "nick ya en uso");
    }
    if (!m_handshaken)
    {
        return Fail("%s: no llego PLAYER_HANDSHAKE (%s)", m_name.c_str(),
            m_peerDropped ? "peer desconectado" : "timeout");
    }

    printf("[INFO] %s conectado, playerid %u, version %s\n", m_name.c_str(), m_id.value(), COOPSA_VERSION);
    return true;
}

void Bot::Disconnect(bool graceful)
{
    if (m_peer != nullptr && m_host != nullptr)
    {
        if (graceful)
        {
            enet_peer_disconnect(m_peer, 0);

            ENetEvent event;
            uint32_t deadline = enet_time_get() + 2000;
            while (enet_time_get() < deadline)
            {
                bool dropped = false;
                while (enet_host_service(m_host, &event, 10) > 0)
                {
                    if (event.type == ENET_EVENT_TYPE_DISCONNECT)
                    {
                        dropped = true;
                        break;
                    }
                    HandleEvent(event);
                }
                if (dropped)
                {
                    break;
                }
            }
        }
        else if (m_peer->state != ENET_PEER_STATE_DISCONNECTED)
        {
            enet_peer_disconnect_now(m_peer, 0);
        }

        m_peer = nullptr;
    }

    if (m_host != nullptr)
    {
        enet_host_destroy(m_host);
        m_host = nullptr;
    }
}

void Bot::Pump(uint32_t ms)
{
    ENetEvent event;
    uint32_t deadline = enet_time_get() + ms;
    while (enet_time_get() < deadline)
    {
        while (enet_host_service(m_host, &event, 5) > 0)
        {
            HandleEvent(event);
        }
    }

    // lo que ya quedo encolado
    while (enet_host_service(m_host, &event, 0) > 0)
    {
        HandleEvent(event);
    }
}

void Bot::Send(Packet& packet)
{
    static uint8_t buffer[MAX_PACKET_BYTES];
    memset(buffer, 0, sizeof(buffer));

    // mismo formato que server/src/CPacketFactory.cpp (BuildPacketStream): tipo de 16 bits y, si el canal
    // no es SYSTEM, el reloj del server (el cliente manda 0, el server lo pisa al reenviar)
    serialize::WriteStream stream(buffer, sizeof(buffer));
    uint16_t packetType = static_cast<uint16_t>(packet.GetType());
    stream.SerializeBits(packetType, 16);
    if (packet.GetChannel() != ePacketChannel::SYSTEM)
    {
        stream.SerializeBits(packet.serverTime, 32);
    }

    if (!packet.SerializeWrite(stream))
    {
        Fail("%s: no se pudo serializar el paquete %s", m_name.c_str(), TypeName(packet.GetType()));
        return;
    }
    stream.Flush();

    ePacketReliability reliability = GetChannelReliability(packet.GetChannel());
    ENetPacket* enetPacket = enet_packet_create(stream.GetData(), stream.GetBytesProcessed(),
        reliability == ePacketReliability::RELIABLE ? ENET_PACKET_FLAG_RELIABLE : 0);
    enet_peer_send(m_peer, static_cast<enet_uint8>(packet.GetChannel()), enetPacket);
    enet_host_flush(m_host);
}

void Bot::SendOnFoot(float x, float y, float z)
{
    Packets::Players::OnFootUpdate packet{};
    packet.vecPos = CVector(x, y, z);
    packet.areaCode = MAIN_MAP_AREA;

    // PLAYER_ONFOOT_UPDATE es el unico paquete con el que el server aprende donde esta un jugador
    // (server/src/PacketHandlers/players.cpp) y elige candidatos con eso; va por el canal SYNC
    // (unreliable), asi que lo mandamos dos veces
    Send(packet);
    Send(packet);
}

const RecvEvent* Bot::First(ePacketType type, pedid_t pedid) const
{
    for (const RecvEvent& event : m_events)
    {
        if (event.type == type && event.pedid == pedid)
        {
            return &event;
        }
    }
    return nullptr;
}

const RecvEvent* Bot::LastLock(int missionId) const
{
    const RecvEvent* last = nullptr;
    for (const RecvEvent& event : m_events)
    {
        if (event.type == ePacketType::MISSION_LOCK_STATE && event.missionId == missionId)
        {
            last = &event;
        }
    }
    return last;
}

std::string Bot::EventLog() const
{
    if (m_events.empty())
    {
        return "(ninguno)";
    }

    std::string log;
    for (const RecvEvent& event : m_events)
    {
        if (!log.empty())
        {
            log += ", ";
        }
        log += TypeName(event.type);
        if (event.pedid.valid())
        {
            log += " " + to_string(event.pedid);
        }
        if (event.type == ePacketType::ASSIGN_PED)
        {
            log += event.syncing ? " syncing" : " !syncing";
            log += event.takeover ? " takeover" : " !takeover";
        }
    }
    return log;
}

void Bot::HandleEvent(ENetEvent& event)
{
    switch (event.type)
    {
        case ENET_EVENT_TYPE_RECEIVE:
            HandlePacket(event.packet->data, static_cast<int>(event.packet->dataLength));
            enet_packet_destroy(event.packet);
            break;
        case ENET_EVENT_TYPE_DISCONNECT:
            m_peerDropped = true;
            break;
        default:
            break;
    }
}

void Bot::HandlePacket(const uint8_t* data, int size)
{
    serialize::ReadStream stream(data, size);

    uint32_t rawType = 0;
    if (!stream.SerializeBits(rawType, 16))
    {
        return;
    }

    ePacketType type = static_cast<ePacketType>(rawType);
    Packet* packet = GetPacketFactory().Get(type);
    if (packet == nullptr)
    {
        // un tipo que el bot no usa (vehiculos, scripts, blips...): se ignora
        return;
    }

    if (packet->GetChannel() != ePacketChannel::SYSTEM)
    {
        uint32_t serverTime = 0;
        if (!stream.SerializeBits(serverTime, 32))
        {
            return;
        }
        packet->serverTime = serverTime;
    }

    if (!packet->SerializeRead(stream))
    {
        return;
    }

    if(type==ePacketType::SOCIAL_ACTION) { socialEvents.push_back(*static_cast<Packets::Scripts::SocialAction*>(packet)); return; }
    if(type==ePacketType::EMOTE_STATE) { emoteEvents.push_back(*static_cast<Packets::Scripts::EmoteState*>(packet)); return; }
    if (type == ePacketType::GARAGE_DOOR) { garageEvents.push_back(*static_cast<Packets::World::GarageDoor*>(packet)); return; }
    if (type == ePacketType::VEHICLE_GPS) { gpsEvents.push_back(*static_cast<Packets::Players::VehicleGps*>(packet)); return; }
    if (type == ePacketType::MISSION_PHOTO) { photoEvents.push_back(*static_cast<Packets::Scripts::MissionPhoto*>(packet)); return; }
    if(type==ePacketType::CUTSCENE_PARTNERS) { sceneEvents.push_back(*static_cast<Packets::Scripts::CutscenePartners*>(packet)); return; }
    if (type == ePacketType::ACTIVITY_CHALLENGE) {
        challengeEvents.push_back(*static_cast<Packets::Scripts::ActivityChallenge*>(packet)); return;
    }
    if (type == ePacketType::STORY_RACE) {
        storyRaceEvents.push_back(*static_cast<Packets::Scripts::StoryRace*>(packet)); return;
    }
    if (type == ePacketType::COOP_POOL) {
        poolEvents.push_back(*static_cast<Packets::Scripts::CoopPool*>(packet));
        return;
    }
    if (type == ePacketType::INTERIOR_POPULATION) { populationEvents.push_back(*static_cast<Packets::Peds::InteriorPopulation*>(packet)); return; }
    switch (type)
    {
        case ePacketType::PLAYER_HANDSHAKE:
        {
            auto* pHandshake = static_cast<Packets::System::PlayerHandshake*>(packet);
            m_id = pHandshake->yourid;
            m_handshaken = true;
            break;
        }
        case ePacketType::PLAYER_DISCONNECTED:
        {
            auto* pDisconnected = static_cast<Packets::System::PlayerDisconnected*>(packet);
            if (pDisconnected->payload.playerid == -1)
            {
                m_rejectReason = pDisconnected->payload.reason;
            }
            break;
        }
        case ePacketType::PED_SPAWN:
        {
            auto* pSpawn = static_cast<Packets::Peds::PedSpawn*>(packet);
            RecvEvent event;
            event.type = type;
            event.pedid = pSpawn->pedid;
            m_events.push_back(event);
            break;
        }
        case ePacketType::PED_REMOVE:
        {
            auto* pRemove = static_cast<Packets::Peds::PedRemove*>(packet);
            RecvEvent event;
            event.type = type;
            event.pedid = pRemove->pedid;
            m_events.push_back(event);
            break;
        }
        case ePacketType::ASSIGN_PED:
        {
            auto* pAssign = static_cast<Packets::Peds::AssignPedSyncer*>(packet);
            RecvEvent event;
            event.type = type;
            event.pedid = pAssign->pedid;
            event.syncing = pAssign->syncing;
            event.takeover = pAssign->takeover;
            m_events.push_back(event);
            break;
        }
        case ePacketType::MISSION_LOCK_STATE:
        {
            auto* pState = static_cast<Packets::Scripts::MissionLockState*>(packet);
            RecvEvent event;
            event.type = type;
            event.missionId = pState->missionId;
            event.locked = pState->locked;
            event.owner = pState->ownerId;
            m_events.push_back(event);
            break;
        }
        default:
            break;
    }
}

void SendPedSpawn(Bot& bot, pedid_t pedid, eCharCreatedBy createdBy)
{
    Packets::Peds::PedSpawn packet{};
    packet.pedid = pedid;
    packet.modelId = MODEL_MALE01;  // ped civil valido (7)
    packet.pedType = PED_TYPE_CIVMALE;
    packet.pos = CVector(PED_X, 0.0f, GROUND_Z);
    packet.createdBy = createdBy;
    bot.Send(packet);
}

void SendPedRelease(Bot& bot, pedid_t pedid, uint8_t reason)
{
    Packets::Peds::PedRelease packet{};
    packet.pedid = pedid;
    packet.pos = CVector(PED_X, 0.0f, GROUND_Z);
    packet.heading = 0.0f;
    packet.health = 100.0f;
    packet.areaCode = MAIN_MAP_AREA;
    packet.reason = reason;
    bot.Send(packet);
}

bool OpenPair(Bot& a, Bot& b, const char* host, uint16_t port, const char* nameA, const char* nameB)
{
    if (!a.Connect(host, port, nameA) || !b.Connect(host, port, nameB))
    {
        return false;
    }

    if (!a.Handshake() || !b.Handshake())
    {
        return false;
    }

    a.Pump(100);
    b.Pump(100);
    return true;
}

// (a) A y B cerca: PED_RELEASE con handover = 1 debe traspasar el ped a B (ASSIGN_PED takeover)
bool ScenarioA(const char* host, uint16_t port)
{
    printf("\n=== (a) B a 5 m, handover = 1: A libera el ped y B debe recibir ASSIGN_PED takeover ===\n");

    Bot a;
    Bot b;
    if (!OpenPair(a, b, host, port, "TBA1", "TBB1"))
    {
        return false;
    }

    a.SendOnFoot(0.0f, 0.0f, GROUND_Z);
    b.SendOnFoot(0.0f, 0.0f, GROUND_Z);
    a.Pump(300);
    b.Pump(300);
    a.ClearEvents();
    b.ClearEvents();

    pedid_t pedid(a.Id(), 1);
    SendPedSpawn(a, pedid, RANDOM_CHAR);
    b.Pump(1000);

    bool ok = Expect(b.Received(ePacketType::PED_SPAWN, pedid), "(a) B recibio PED_SPAWN del ped ambiente de A");

    SendPedRelease(a, pedid, 0);
    b.Pump(1500);
    a.Pump(200);

    const RecvEvent* assign = b.First(ePacketType::ASSIGN_PED, pedid);
    ok &= Expect(assign != nullptr, "(a) B recibio ASSIGN_PED");
    if (assign != nullptr)
    {
        ok &= Expect(assign->syncing && assign->takeover, "(a) ASSIGN_PED con syncing = true y takeover = true");
    }
    ok &= Expect(!b.Received(ePacketType::PED_REMOVE, pedid), "(a) B NO recibio PED_REMOVE");

    if (!ok)
    {
        printf("[INFO] (a) paquetes de B: %s\n", b.EventLog().c_str());
    }

    a.Disconnect(false);
    b.Disconnect(false);
    return ok;
}

// (b) B a 1000 m: el ped no tiene candidato y se borra para todos
bool ScenarioB(const char* host, uint16_t port)
{
    printf("\n=== (b) B a 1000 m, handover = 1: A libera el ped y todos deben recibir PED_REMOVE ===\n");

    Bot a;
    Bot b;
    if (!OpenPair(a, b, host, port, "TBA2", "TBB2"))
    {
        return false;
    }

    a.SendOnFoot(0.0f, 0.0f, GROUND_Z);
    b.SendOnFoot(1000.0f, 0.0f, GROUND_Z);
    a.Pump(300);
    b.Pump(300);
    a.ClearEvents();
    b.ClearEvents();

    pedid_t pedid(a.Id(), 1);
    SendPedSpawn(a, pedid, RANDOM_CHAR);
    b.Pump(1000);

    bool ok = Expect(b.Received(ePacketType::PED_SPAWN, pedid), "(b) B recibio PED_SPAWN del ped ambiente de A");

    SendPedRelease(a, pedid, 0);
    b.Pump(1500);
    a.Pump(200);

    ok &= Expect(b.Received(ePacketType::PED_REMOVE, pedid), "(b) B recibio PED_REMOVE");
    ok &= Expect(!b.Received(ePacketType::ASSIGN_PED, pedid), "(b) B NO recibio ASSIGN_PED");
    ok &= Expect(a.Received(ePacketType::PED_REMOVE, pedid), "(b) A tambien recibio PED_REMOVE (handover = 1)");

    if (!ok)
    {
        printf("[INFO] (b) paquetes de B: %s\n", b.EventLog().c_str());
    }

    a.Disconnect(false);
    b.Disconnect(false);
    return ok;
}

// (c) A se desconecta con el ped en la mano y B cerca: el ped pasa a B, no se borra
bool ScenarioC(const char* host, uint16_t port)
{
    printf("\n=== (c) A se desconecta con B a 5 m: B debe recibir ASSIGN_PED (y no PED_REMOVE) ===\n");

    Bot a;
    Bot b;
    if (!OpenPair(a, b, host, port, "TBA3", "TBB3"))
    {
        return false;
    }

    a.SendOnFoot(0.0f, 0.0f, GROUND_Z);
    b.SendOnFoot(0.0f, 0.0f, GROUND_Z);
    a.Pump(300);
    b.Pump(300);
    a.ClearEvents();
    b.ClearEvents();

    pedid_t pedid(a.Id(), 1);
    SendPedSpawn(a, pedid, RANDOM_CHAR);
    b.Pump(1000);

    bool ok = Expect(b.Received(ePacketType::PED_SPAWN, pedid), "(c) B recibio PED_SPAWN del ped ambiente de A");
    b.ClearEvents();

    a.Disconnect(true);  // el server lo ve como ENET_EVENT_TYPE_DISCONNECT -> HandlePlayerDisconnected
    b.Pump(2000);

    ok &= Expect(b.Received(ePacketType::ASSIGN_PED, pedid), "(c) B recibio ASSIGN_PED al desconectarse A");
    ok &= Expect(!b.Received(ePacketType::PED_REMOVE, pedid), "(c) B NO recibio PED_REMOVE");

    if (!ok)
    {
        printf("[INFO] (c) paquetes de B: %s\n", b.EventLog().c_str());
    }

    b.Disconnect(false);
    return ok;
}

// (d) con handover = 0 el PED_RELEASE se comporta como PED_REMOVE
bool ScenarioD(const char* host, uint16_t port)
{
    printf("\n=== (d) handover = 0: A libera el ped y todos deben recibir PED_REMOVE ===\n");

    Bot a;
    Bot b;
    if (!OpenPair(a, b, host, port, "TBA4", "TBB4"))
    {
        return false;
    }

    a.SendOnFoot(0.0f, 0.0f, GROUND_Z);
    b.SendOnFoot(0.0f, 0.0f, GROUND_Z);
    a.Pump(300);
    b.Pump(300);
    a.ClearEvents();
    b.ClearEvents();

    pedid_t pedid(a.Id(), 1);
    SendPedSpawn(a, pedid, RANDOM_CHAR);
    b.Pump(1000);

    bool ok = Expect(b.Received(ePacketType::PED_SPAWN, pedid), "(d) B recibio PED_SPAWN del ped ambiente de A");

    SendPedRelease(a, pedid, 0);
    b.Pump(1500);
    a.Pump(200);

    ok &= Expect(b.Received(ePacketType::PED_REMOVE, pedid), "(d) B recibio PED_REMOVE");
    ok &= Expect(!b.Received(ePacketType::ASSIGN_PED, pedid), "(d) B NO recibio ASSIGN_PED");

    // sin handover el server borra el ped ignorando al que lo solto (RemovePed(ped, pSender)): A ya lo tenia
    // descartado en el cliente, asi que no se le vuelve a avisar
    printf("[INFO] (d) A %s PED_REMOVE (el emisor queda excluido del borrado, es lo esperado)\n",
        a.Received(ePacketType::PED_REMOVE, pedid) ? "recibio" : "no recibio");

    if (!ok)
    {
        printf("[INFO] (d) paquetes de B: %s\n", b.EventLog().c_str());
    }

    a.Disconnect(false);
    b.Disconnect(false);
    return ok;
}

// (e) bloqueo de misiones (mission-lock = 1): una mision, un jugador
bool ScenarioE(const char* host, uint16_t port)
{
    printf("\n=== (e) mission-lock: A reserva la mision, B no puede, se libera, B la toma, el snapshot del recien llegado ===\n");

    constexpr int MISSION = 15;

    Bot a;
    Bot b;
    if (!OpenPair(a, b, host, port, "TBA5", "TBB5"))
    {
        return false;
    }
    a.ClearEvents();
    b.ClearEvents();

    // A reserva: el server lo avisa a todos
    {
        Packets::Scripts::MissionLockRequest request{};
        request.missionId = MISSION;
        a.Send(request);
    }
    a.Pump(300);
    b.Pump(500);
    bool ok = Expect(b.LastLock(MISSION) && b.LastLock(MISSION)->locked && b.LastLock(MISSION)->owner == a.Id(),
        "(e) B supo que A tiene la mision");

    // B intenta la misma: se le niega (solo a B le llega la respuesta con el dueno real)
    a.ClearEvents();
    b.ClearEvents();
    {
        Packets::Scripts::MissionLockRequest request{};
        request.missionId = MISSION;
        b.Send(request);
    }
    b.Pump(500);
    a.Pump(300);
    ok &= Expect(b.LastLock(MISSION) && b.LastLock(MISSION)->locked && b.LastLock(MISSION)->owner == a.Id(),
        "(e) a B se le nego y el server le dijo que el dueno es A");
    ok &= Expect(a.LastLock(MISSION) == nullptr, "(e) A no recibio nada por el intento de B");

    // otra mision distinta sigue libre para B
    a.ClearEvents();
    {
        Packets::Scripts::MissionLockRequest request{};
        request.missionId = MISSION + 1;
        b.Send(request);
    }
    b.Pump(300);
    a.Pump(500);
    ok &= Expect(a.LastLock(MISSION + 1) && a.LastLock(MISSION + 1)->locked && a.LastLock(MISSION + 1)->owner == b.Id(),
        "(e) B pudo reservar otra mision mientras A tiene la suya");

    // A la libera: B la puede tomar
    b.ClearEvents();
    a.ClearEvents();
    {
        Packets::Scripts::MissionLockRelease release{};
        release.missionId = MISSION;
        a.Send(release);
    }
    a.Pump(300);
    b.Pump(500);
    ok &= Expect(b.LastLock(MISSION) && !b.LastLock(MISSION)->locked, "(e) B recibio que la mision quedo libre");

    {
        Packets::Scripts::MissionLockRequest request{};
        request.missionId = MISSION;
        b.Send(request);
    }
    b.Pump(300);
    a.Pump(500);
    ok &= Expect(a.LastLock(MISSION) && a.LastLock(MISSION)->locked && a.LastLock(MISSION)->owner == b.Id(),
        "(e) ahora B tiene la mision y A lo sabe");

    // un jugador que entra despues recibe el estado actual
    Bot c;
    if (!c.Connect(host, port, "TBC5") || !c.Handshake())
    {
        a.Disconnect(false);
        b.Disconnect(false);
        return false;
    }
    c.Pump(500);
    ok &= Expect(c.LastLock(MISSION) && c.LastLock(MISSION)->locked && c.LastLock(MISSION)->owner == b.Id(),
        "(e) C (recien llegado) recibio el snapshot: B tiene la mision");

    // B se desconecta: sus misiones quedan libres para todos
    a.ClearEvents();
    c.ClearEvents();
    b.Disconnect(true);
    a.Pump(1500);
    c.Pump(300);
    ok &= Expect(a.LastLock(MISSION) && !a.LastLock(MISSION)->locked && c.LastLock(MISSION) && !c.LastLock(MISSION)->locked,
        "(e) al desconectarse B, su mision quedo libre para A y C");

    if (!ok)
    {
        printf("[INFO] (e) paquetes de A: %s\n", a.EventLog().c_str());
    }

    a.Disconnect(false);
    c.Disconnect(false);
    return ok;
}

bool ScenarioF(const char* host, uint16_t port)
{
    using P = Packets::Scripts::CoopPool;
    printf("\n=== (f) POOL2: reserva, consentimiento, aislamiento y limpieza ===\n");
    Bot a, b, c, d;
    if (!OpenPair(a, b, host, port, "POOL_A", "POOL_B") || !c.Connect(host, port, "POOL_C") || !c.Handshake() ||
        !d.Connect(host, port, "POOL_D") || !d.Handshake()) return false;
    auto mode = [](Bot& bot, bool individual) {
        Packets::Scripts::PlayerMissionMode m{}; m.individual = individual; m.vanillaScripts = true; bot.Send(m);
    };
    mode(a, false); mode(b, false); mode(c, true); mode(d, false);
    a.SendOnFoot(0, 0, GROUND_Z); b.SendOnFoot(1, 0, GROUND_Z);
    c.SendOnFoot(2, 0, GROUND_Z); d.SendOnFoot(100, 0, GROUND_Z);
    auto pumpFor = [&](uint32_t ms) {
        const uint32_t start = enet_time_get();
        do { a.Pump(1); b.Pump(1); c.Pump(1); d.Pump(1); }
        while (uint32_t(enet_time_get() - start) < ms);
    };
    auto pump = [&] { pumpFor(500); };
    // SYNC is unreliable: stream the current state like CCoopPool does in-game.
    // Wait for identity only; validate the payload afterwards so corruption still fails.
    auto state = [&](Bot& from, Bot& to, P& packet) {
        for (int attempt = 0; attempt < 20; ++attempt) {
            from.Send(packet);
            pumpFor(50);
            const auto* received = to.LastPool(packet.action);
            if (received && received->session == packet.session && received->sequence == packet.sequence) return;
        }
        printf("[INFO] (f) no state received: action=%u session=%u sequence=%u\n",
            unsigned(packet.action), unsigned(packet.session), unsigned(packet.sequence));
    };
    pump();
    P p{}; p.owner = a.Id(); p.session = 551; p.action = P::OFFER;
    bool ok = Expect(p.GetChannel() == ePacketChannel::SCRIPT, "(f) control de billar por canal fiable");
    p.action = P::INPUT;
    ok &= Expect(p.GetChannel() == ePacketChannel::SYNC, "(f) input de billar por canal de estado");
    p.action = P::VIEW;
    ok &= Expect(p.GetChannel() == ePacketChannel::SYNC, "(f) vista de billar por canal de estado");
    p.action = P::OFFER;
    a.Send(p); pump();
    ok &= Expect(b.LastPool(P::OFFER) && b.LastPool(P::OFFER)->sender == a.Id(), "(f) oferta cercana con remitente real");
    ok &= Expect(!c.LastPool(P::OFFER) && !d.LastPool(P::OFFER), "(f) individual y lejano no reciben oferta");
    p.action = P::START; p.partner = b.Id(); a.Send(p); pump();
    ok &= Expect(!b.LastPool(P::START), "(f) START sin consentimiento rechazado");
    p.action = P::JOIN; p.sender = a.Id(); b.Send(p); pump();
    ok &= Expect(a.LastPool(P::JOIN) && a.LastPool(P::JOIN)->sender == b.Id(), "(f) JOIN fija segundo pad y sella remitente");
    mode(c, false); d.SendOnFoot(3, 0, GROUND_Z); pump();
    c.Send(p); pump();
    ok &= Expect(a.LastPool(P::JOIN) && a.LastPool(P::JOIN)->partner == b.Id(), "(f) tercer jugador no reemplaza al segundo");
    p.action = P::START; p.partner = c.Id(); a.Send(p); pump();
    ok &= Expect(!b.LastPool(P::START), "(f) START con otro segundo pad rechazado");
    p.partner = b.Id(); a.Send(p); pump();
    ok &= Expect(b.LastPool(P::START) && c.LastPool(P::START) && d.LastPool(P::START), "(f) jugador y dos espectadores reciben START");
    p.action = P::INPUT; p.sequence = 1; p.mouseX = 4; p.keys[16] = 255;
    c.Send(p); pump();
    ok &= Expect(!a.LastPool(P::INPUT), "(f) INPUT de espectador rechazado");
    p.session = 550; b.Send(p); pump();
    ok &= Expect(!a.LastPool(P::INPUT), "(f) INPUT de sesion vieja rechazado");
    p.session = 551; state(b, a, p);
    ok &= Expect(a.LastPool(P::INPUT) && a.LastPool(P::INPUT)->session == 551 &&
        a.LastPool(P::INPUT)->sequence == 1 && a.LastPool(P::INPUT)->sender == b.Id() &&
        a.LastPool(P::INPUT)->mouseX == 4 && a.LastPool(P::INPUT)->keys[16] == 255,
        "(f) INPUT del segundo conserva mouse y teclas");
    p.sequence = 2; p.mouseX = 0; p.keys[16] = 0; p.keys[15] = 255; b.Send(p);
    p.sequence = 3; p.keys[15] = 0; p.keys[14] = 255; state(b, a, p);
    ok &= Expect(a.LastPool(P::INPUT) && a.LastPool(P::INPUT)->sequence == 3 && a.LastPool(P::INPUT)->keys[14] == 255,
        "(f) inputs consecutivos conservan el ultimo boton");
    p.action = P::VIEW; p.count = 75;
    for (int i = 0; i < 75; ++i) { p.objects[i].slot = i; p.objects[i].model = 3003; }
    state(a, b, p);
    ok &= Expect(b.LastPool(P::VIEW) && b.LastPool(P::VIEW)->count == 75 && b.LastPool(P::VIEW)->objects[74].model == 3003,
        "(f) snapshot maximo cabe en paquete y se deserializa completo");
    p.action = P::RESULT; p.turn = 1; c.Send(p); pump();
    ok &= Expect(!b.LastPool(P::RESULT), "(f) resultado de espectador rechazado");
    a.Send(p); pump();
    ok &= Expect(b.LastPool(P::RESULT) && b.LastPool(P::RESULT)->turn == 1, "(f) resultado del dueno llega al segundo");
    p.action = P::END; c.Send(p); pump();
    ok &= Expect(!a.LastPool(P::END), "(f) espectador no termina la partida");
    mode(b, true); pump(); b.Send(p); pump();
    ok &= Expect(a.LastPool(P::END) && a.LastPool(P::END)->sender == b.Id(), "(f) END llega aunque segundo cambie a individual");
    p.action = P::OFFER; p.session = 552; a.Send(p); pump();
    ok &= Expect(c.LastPool(P::OFFER) && c.LastPool(P::OFFER)->session == 552, "(f) terminar libera la reserva para otra partida");
    p.action = P::END; a.Send(p); pump();
    a.Disconnect(false); b.Disconnect(false); c.Disconnect(false); d.Disconnect(false);
    return ok;
}


bool ScenarioG(const char* host, uint16_t port) {
    using P = Packets::Scripts::ActivityChallenge;
    Bot a, b, c, d;
    if (!OpenPair(a,b,host,port,"CH_A","CH_B") || !c.Connect(host,port,"CH_C") || !c.Handshake() ||
        !d.Connect(host,port,"CH_D") || !d.Handshake()) return false;
    auto mode = [](Bot& bot, bool solo) { Packets::Scripts::PlayerMissionMode p{}; p.individual=solo; p.vanillaScripts=true; bot.Send(p); };
    mode(a,false); mode(b,false); mode(c,true); mode(d,false);
    a.SendOnFoot(0,0,GROUND_Z); b.SendOnFoot(1,0,GROUND_Z); c.SendOnFoot(2,0,GROUND_Z); d.SendOnFoot(100,0,GROUND_Z);
    auto pump = [&] { a.Pump(100); b.Pump(100); c.Pump(100); d.Pump(100); a.Pump(100); }; pump();
    P p{}; p.owner=a.Id(); p.partner=c.Id(); p.session=561;
    a.Send(p); pump();
    bool ok=Expect(!c.LastChallenge(P::OFFER),"(g) individual no recibe desafio");
    p.partner=d.Id(); a.Send(p); pump();
    ok &= Expect(!d.LastChallenge(P::OFFER),"(g) lejano no recibe desafio");
    p.partner=b.Id(); a.Send(p); pump();
    ok &= Expect(b.LastChallenge(P::OFFER) && !c.LastChallenge(P::OFFER) && !d.LastChallenge(P::OFFER),"(g) oferta dirigida a uno de cuatro");
    p.action=P::STATE; p.state=1; p.score=10; a.Send(p); pump();
    ok &= Expect(!b.LastChallenge(P::STATE),"(g) puntajes sin consentimiento rechazados");
    p.action=P::ACCEPT; c.Send(p); pump();
    ok &= Expect(!a.LastChallenge(P::ACCEPT),"(g) tercero no acepta por otro");
    p.sender=a.Id(); b.Send(p); pump();
    ok &= Expect(a.LastChallenge(P::ACCEPT) && a.LastChallenge(P::ACCEPT)->sender==b.Id(),"(g) aceptar sella remitente real");
    p.action=P::STATE; p.session=560; a.Send(p); pump();
    ok &= Expect(!b.LastChallenge(P::STATE),"(g) sesion vieja rechazada");
    p.session=561; a.Send(p); pump();
    ok &= Expect(b.LastChallenge(P::STATE) && b.LastChallenge(P::STATE)->score==10,"(g) puntaje del participante llega");
    p.score=99; c.Send(p); pump();
    ok &= Expect(b.LastChallenge(P::STATE)->score==10,"(g) tercero no altera puntaje");
    p.state=2; p.score=20; a.Send(p); pump();
    p.score=100; a.Send(p); pump();
    ok &= Expect(b.LastChallenge(P::STATE)->score==20,"(g) resultado final inmutable con latido");
    p.action=P::LEAVE; mode(b,true); pump(); b.Send(p); pump();
    ok &= Expect(a.LastChallenge(P::LEAVE),"(g) abandono llega tras cambiar a individual");
    mode(b,false); pump(); p.action=P::OFFER; p.session=562; a.Send(p); pump();
    ok &= Expect(b.LastChallenge(P::OFFER)->session==562,"(g) abandono libera siguiente intento");
    p.action=P::DECLINE; b.Send(p); pump();
    ok &= Expect(a.LastChallenge(P::DECLINE),"(g) rechazo libera oferta");
    p.action=P::OFFER; p.session=563; p.kind=7; p.variant=2; a.Send(p); pump();
    ok &= Expect(b.LastChallenge(P::OFFER) && b.LastChallenge(P::OFFER)->variant==2,"(g) triatlon conserva variante nativa");
    p.action=P::ACCEPT; b.Send(p); pump();
    ok &= Expect(b.LastChallenge(P::ACCEPT) && b.LastChallenge(P::ACCEPT)->session==563,"(g) invitado recibe confirmacion del servidor");
    p.action=P::STATE; p.state=2; p.score=-105000; a.Send(p); pump();
    ok &= Expect(b.LastChallenge(P::STATE)->score==-105000,"(g) tiempo conserva signo y precision en ms");
    p.score=-110000; b.Send(p); pump();
    p.action=P::OFFER; p.session=564; a.Send(p); pump();
    ok &= Expect(b.LastChallenge(P::OFFER)->session==564,"(g) ambos terminan y liberan sesion");
    p.action=P::DECLINE; b.Send(p); pump();
    a.Disconnect(false); b.Disconnect(false); c.Disconnect(false); d.Disconnect(false); return ok;
}

// Photo routing and real packet serialization with four clients; no GTA required.
bool ScenarioJ(const char* host, uint16_t port)
{
    using P = Packets::Scripts::MissionPhoto;
    Bot a, b, c, d;
    if (!OpenPair(a, b, host, port, "PH_A", "PH_B") || !c.Connect(host, port, "PH_C") || !c.Handshake() ||
        !d.Connect(host, port, "PH_D") || !d.Handshake()) return false;
    auto mode = [](Bot& bot, bool individual) {
        Packets::Scripts::PlayerMissionMode p{}; p.individual = individual; p.vanillaScripts = true; bot.Send(p);
    };
    auto pump = [&] { a.Pump(80); b.Pump(80); c.Pump(80); d.Pump(80); a.Pump(80); };
    mode(a, false); mode(b, false); mode(c, false); mode(d, true); pump();
    P p{}; p.owner = a.Id(); p.epoch = 65; p.mission = 25; p.request = true;
    a.Send(p); pump();
    bool ok = Expect(b.photoEvents.empty(), "(j) no photo session outside mission");
    Packets::Scripts::OnMissionFlagSync mission{}; mission.bOnMission = true; a.Send(mission); pump();
    a.Send(p); pump();
    ok &= Expect(b.photoEvents.size() == 1 && c.photoEvents.size() == 1 && d.photoEvents.empty(),
        "(j) session reaches coop partners, excludes individual");
    b.Send(p); pump();
    ok &= Expect(c.photoEvents.size() == 1, "(j) guest cannot announce authority session");
    p.request = false; p.shotAt = 0xF2345678; p.ped = pedid_t(a.Id(), 0x12345678); p.senderPlayerid = a.Id();
    b.Send(p); pump();
    ok &= Expect(a.photoEvents.size() == 1 && a.photoEvents.back().senderPlayerid.value == b.Id() &&
        a.photoEvents.back().ped == p.ped && a.photoEvents.back().epoch == 65 && a.photoEvents.back().shotAt == p.shotAt,
        "(j) full ped identity and epoch survive; sender is sealed");
    ok &= Expect(c.photoEvents.size() == 1, "(j) guest photo is directed only to owner");
    p.object = true; p.entity = 0xF1234567; p.ped = pedid_t{}; c.Send(p); pump();
    ok &= Expect(a.photoEvents.size() == 2 && a.photoEvents.back().object && a.photoEvents.back().entity == p.entity,
        "(j) object identity keeps all 32 bits");
    d.Send(p); pump();
    ok &= Expect(a.photoEvents.size() == 2, "(j) individual photo is rejected");
    p.epoch = 0; b.Send(p); pump();
    ok &= Expect(a.photoEvents.size() == 2, "(j) empty session rejected");
    p.epoch = 65; mission.bOnMission = false; a.Send(mission); pump(); b.Send(p); pump();
    ok &= Expect(a.photoEvents.size() == 2, "(j) mission end stops late photos");
    a.Disconnect(false); b.Disconnect(false); c.Disconnect(false); d.Disconnect(false);
    return ok;
}

bool ScenarioH(const char* host,uint16_t port) {
    using P=Packets::Scripts::CutscenePartners;
    Bot a,b,c,d;
    if(!OpenPair(a,b,host,port,"CS_A","CS_B") || !c.Connect(host,port,"CS_C") || !c.Handshake() ||
        !d.Connect(host,port,"CS_D") || !d.Handshake()) return false;
    auto mode=[](Bot& bot,bool individual) { Packets::Scripts::PlayerMissionMode p{}; p.individual=individual; p.vanillaScripts=true; bot.Send(p); };
    mode(a,false); mode(b,false); mode(c,false); mode(d,true);
    auto pump=[&] { a.Pump(80); b.Pump(80); c.Pump(80); d.Pump(80); a.Pump(80); };
    pump();
    P p{}; p.owner=a.Id(); p.sender=a.Id(); p.session=580; p.sequence=1; p.shot=1;
    strcpy_s(p.scene,"INTRO1A"); p.count=2; p.partners[0]=b.Id(); p.partners[1]=c.Id();
    a.Send(p); pump();
    bool ok=Expect(b.sceneEvents.empty(),"(h) sin mision no hay extras");
    Packets::Scripts::OnMissionFlagSync mission{}; mission.bOnMission=true; a.Send(mission); pump();
    a.Send(p); pump();
    ok &= Expect(b.sceneEvents.size()==1 && c.sceneEvents.size()==1 && d.sceneEvents.empty(),"(h) decision a coop, individual excluido");
    p.sequence=2; b.Send(p); pump();
    ok &= Expect(c.sceneEvents.size()==1,"(h) invitado no suplanta decision del dueno");
    p.kind=1; p.sender=a.Id(); b.Send(p); pump();
    ok &= Expect(a.sceneEvents.size()==1 && a.sceneEvents.back().sender==b.Id(),"(h) READY dirigido al dueno con remitente sellado");
    d.Send(p); pump();
    ok &= Expect(a.sceneEvents.size()==1,"(h) individual no confirma escena");
    p.kind=0; p.sender=a.Id(); p.partners[1]=b.Id(); a.Send(p); pump();
    ok &= Expect(b.sceneEvents.size()==1,"(h) ids duplicados rechazados");
    p.partners[1]=d.Id(); a.Send(p); pump();
    ok &= Expect(b.sceneEvents.size()==1,"(h) no colocar jugador individual");
    p.partners[1]=c.Id(); p.at=std::numeric_limits<float>::infinity(); a.Send(p); pump();
    ok &= Expect(b.sceneEvents.size()==1,"(h) reloj infinito rechazado");
    p.at=10; p.pose=7; p.ground={1,2,3}; p.viaSet=true; p.via={4,5,6}; p.seats=2; p.seat[1]={7,8,9};
    a.Send(p); pump();
    ok &= Expect(b.sceneEvents.size()==2 && b.sceneEvents.back().pose==7 && b.sceneEvents.back().seat[1].z==9 &&
        b.sceneEvents.back().viaSet,"(h) WAY y asientos/via conservan datos");
    p.active=false; a.Send(p); pump();
    ok &= Expect(b.sceneEvents.size()==3 && !b.sceneEvents.back().active,"(h) fin de escena llega a los participantes");
    mission.bOnMission=false; a.Send(mission); pump();
    p.active=true; a.Send(p); pump();
    ok &= Expect(b.sceneEvents.size()==3,"(h) fin de mision bloquea decisiones tardias");
    Packets::Scripts::CoopMissionOffer offer{}; strcpy_s(offer.scriptName,"COPCAR"); strcpy_s(offer.title,"T58");
    b.Send(offer); pump();
    Packets::Scripts::CoopMissionReply reply{}; reply.accept=true; a.Send(reply); c.Send(reply); reply.accept=false; d.Send(reply); pump();
    mission.bOnMission=true; b.Send(mission); pump();
    p.owner=b.Id(); p.sender=b.Id(); p.session=581; p.count=2; p.partners[0]=a.Id(); p.partners[1]=c.Id();
    a.sceneEvents.clear(); c.sceneEvents.clear(); a.Send(p); pump();
    ok &= Expect(c.sceneEvents.empty(),"(h) host del server no decide por dueno no host");
    b.Send(p); pump();
    ok &= Expect(a.sceneEvents.size()==1 && c.sceneEvents.size()==1 && a.sceneEvents.back().owner==b.Id(),"(h) dueno no host decide escena");
    auto oldOwner=b.Id(); b.Disconnect(true); a.Pump(150); c.Pump(150); d.Pump(150);
    p.owner=oldOwner; a.Send(p); a.Pump(150); c.Pump(150); d.Pump(150);
    ok &= Expect(c.sceneEvents.size()==1,"(h) dueno desconectado no deja decisiones validas");
    a.Disconnect(false); c.Disconnect(false); d.Disconnect(false); return ok;
}

bool ScenarioI(const char* host, uint16_t port) {
    using P = Packets::Scripts::SocialAction;
    Bot a,b,c,d;
    if (!OpenPair(a,b,host,port,"SOC_A","SOC_B") || !c.Connect(host,port,"SOC_C") || !c.Handshake() ||
        !d.Connect(host,port,"SOC_D") || !d.Handshake()) return false;
    auto mode=[](Bot& b,bool solo) { Packets::Scripts::PlayerMissionMode p{}; p.individual=solo; p.vanillaScripts=true; b.Send(p); };
    mode(a,false); mode(b,false); mode(c,false); mode(d,true);
    auto ready=[&] { a.SendOnFoot(0,0,GROUND_Z); b.SendOnFoot(1,0,GROUND_Z); c.SendOnFoot(2,0,GROUND_Z); d.SendOnFoot(3,0,GROUND_Z);
        Packets::Players::KeyPressed p{}; a.Send(p); b.Send(p); c.Send(p); d.Send(p); };
    auto pump=[&] { ready(); a.Pump(70); b.Pump(70); c.Pump(70); d.Pump(70); a.Pump(70); };
    pump();
    P p{}; p.owner=a.Id(); p.partner=b.Id(); p.session=600; p.kind=P::GO;
    a.Send(p); pump();
    bool ok=Expect(b.LastSocial(P::OFFER) && c.socialEvents.empty() && d.socialEvents.empty(),"(i) TP dirigido solo al destino");
    p.action=P::ACCEPT; c.Send(p); pump();
    ok &= Expect(!a.LastSocial(P::START),"(i) tercero no acepta por el destino");
    p.session=599; b.Send(p); pump();
    ok &= Expect(!a.LastSocial(P::START),"(i) respuesta vieja no ejecuta TP");
    p.session=600; p.pos={999,999,999}; b.Send(p); pump();
    ok &= Expect(a.LastSocial(P::START) && b.LastSocial(P::START) && a.LastSocial(P::START)->pos.x==1,
        "(i) consentimiento ejecuta con coordenadas del servidor");
    a.socialEvents.clear(); b.socialEvents.clear(); b.Send(p); pump();
    ok &= Expect(!a.LastSocial(P::START),"(i) no repetir TP consumido");
    p.action=P::OFFER; p.session=601; p.partner=d.Id(); a.Send(p); pump();
    ok &= Expect(d.socialEvents.empty() && a.LastSocial(P::CANCEL),"(i) individual no recibe TP");
    p.partner=b.Id(); p.session=602; a.Send(p); pump();
    // a non-host reports his mission through PLAYER_MISSION_MODE (the server stores ON_MISSION only for the host)
    Packets::Scripts::PlayerMissionMode inmission{}; inmission.individual=false; inmission.vanillaScripts=true; inmission.runningMission=7; b.Send(inmission); pump();
    p.action=P::ACCEPT; b.Send(p); pump();
    ok &= Expect(!a.LastSocial(P::START),"(i) entrar en mision invalida pedido pendiente");
    inmission.runningMission=-1; b.Send(inmission); pump();
    a.socialEvents.clear(); b.socialEvents.clear();
    p.action=P::OFFER; p.kind=P::PAIR; p.session=603; p.emote=15; a.Send(p); pump();
    ok &= Expect(b.LastSocial(P::OFFER) && b.LastSocial(P::OFFER)->emote==15,"(i) pareja transmite el clip 16");
    P other=p; other.owner=c.Id(); other.partner=b.Id(); other.session=604; c.Send(other); pump();
    ok &= Expect(c.LastSocial(P::CANCEL),"(i) dos ofertas no se apropian del mismo jugador");
    p.action=P::ACCEPT; b.Send(p); pump();
    ok &= Expect(a.LastSocial(P::START) && b.LastSocial(P::START),"(i) pareja inicia solo tras aceptar");
    Packets::Scripts::EmoteState anim{}; anim.player=d.Id(); anim.session=603; anim.sequence=1; anim.active=true;
    anim.hash=123; strcpy_s(anim.block,"GANGS"); a.Send(anim); pump();
    ok &= Expect(b.emoteEvents.size()==1 && c.emoteEvents.size()==1 && d.emoteEvents.empty() && b.emoteEvents.back().player==a.Id(),
        "(i) pose por nombre llega a espectadores coop y sella remitente");
    anim.time=std::numeric_limits<float>::infinity(); a.Send(anim); pump();
    ok &= Expect(b.emoteEvents.size()==1,"(i) tiempo no finito no llega al motor");
    p.action=P::CANCEL; b.Send(p); pump();
    ok &= Expect(a.LastSocial(P::CANCEL),"(i) cualquiera de la pareja puede terminar");
    p.action=P::OFFER; p.kind=P::BRING; p.session=605; a.Send(p); pump();
    p.action=P::ACCEPT; b.Send(p); pump();
    ok &= Expect(b.LastSocial(P::START) && b.LastSocial(P::START)->kind==P::BRING && b.LastSocial(P::START)->pos.x==0,
        "(i) traer usa al solicitante como ancla");
    p.action=P::OFFER; p.session=606; a.Send(p); pump();
    auto gone=b.Id(); b.Disconnect(true); a.Pump(100); c.Pump(100); d.Pump(100);
    p.action=P::ACCEPT; p.partner=gone; c.Send(p); a.Pump(100); c.Pump(100); d.Pump(100);
    ok &= Expect(!a.LastSocial(P::START) || a.LastSocial(P::START)->session!=606,"(i) desconectado no autoriza teletransporte");
    a.Disconnect(false); c.Disconnect(false); d.Disconnect(false); return ok;
}

bool ScenarioK(const char* host, uint16_t port) {
    Bot a,b,c,d;
    if (!OpenPair(a,b,host,port,"GPS_A","GPS_B") || !c.Connect(host,port,"GPS_C") || !c.Handshake() ||
        !d.Connect(host,port,"GPS_D") || !d.Handshake()) return false;
    auto mode=[](Bot& bot,bool individual) { Packets::Scripts::PlayerMissionMode p{}; p.individual=individual; p.vanillaScripts=true; bot.Send(p); };
    auto pump=[&] { a.Pump(40); b.Pump(40); c.Pump(40); d.Pump(40); a.Pump(40); };
    mode(a,false); mode(b,false); mode(c,false); mode(d,false);
    a.SendOnFoot(0,0,3); b.SendOnFoot(0,0,3); c.SendOnFoot(0,0,3); d.SendOnFoot(0,0,3); pump();
    Packets::Vehicles::VehicleSpawn spawn{}; spawn.vehicleid=vehicleid_t(a.Id(),79); spawn.modelid=400; a.Send(spawn); pump();
    auto seat=[&](Bot& bot,int n) { Packets::Vehicles::VehicleEnter p{}; p.vehicleid=spawn.vehicleid; p.seatid=n ? n-1 : 0; p.bPassenger=n!=0; p.bForce=true; bot.Send(p); };
    seat(a,0); seat(b,1); seat(c,2); seat(d,3); pump();
    auto refresh=[&] {
        Packets::Vehicles::VehicleDriverUpdate driver{}; driver.vehicleid=spawn.vehicleid; driver.health=1000; a.Send(driver);
        int n=0; for (auto* bot : {&b,&c,&d}) { Packets::Vehicles::VehiclePassengerUpdate passenger{}; passenger.vehicleid=spawn.vehicleid; passenger.seatid=n++; bot->Send(passenger); }
        pump();
    };
    refresh();
    Packets::Players::VehicleGps p{}; p.driver=d.Id(); p.vehicle=spawn.vehicleid; p.active=true; p.x=123; p.y=456;
    a.Send(p); pump();
    bool ok=Expect(b.gpsEvents.size()==1 && c.gpsEvents.size()==1 && d.gpsEvents.size()==1 && a.gpsEvents.empty(),"(k) GPS reaches three passengers only");
    ok &= Expect(!b.gpsEvents.empty() && b.gpsEvents.back().driver==a.Id() && b.gpsEvents.back().x==123,"(k) server stamps actual driver and preserves destination");
    b.Send(p); pump(); ok &= Expect(c.gpsEvents.size()==1,"(k) passenger cannot publish GPS");
    mode(d,true); pump(); refresh(); a.Send(p); pump();
    ok &= Expect(b.gpsEvents.size()==2 && d.gpsEvents.size()==1,"(k) individual passenger excluded");
    mode(a,true); pump(); a.Send(p); pump(); ok &= Expect(b.gpsEvents.size()==2,"(k) individual driver excluded");
    mode(a,false); pump(); refresh(); p.active=false; a.Send(p); pump();
    ok &= Expect(b.gpsEvents.size()==3 && !b.gpsEvents.back().active,"(k) removing waypoint reaches passengers");
    p.vehicle=vehicleid_t(a.Id(),80); a.Send(p); pump(); ok &= Expect(b.gpsEvents.size()==3,"(k) wrong vehicle rejected");
    p.vehicle=spawn.vehicleid; Packets::Vehicles::VehicleExit exit{}; b.Send(exit); pump(); refresh();
    // Refresh above reseats B: explicitly exit again before testing the recipient filter.
    b.Send(exit); pump(); a.Send(p); pump();
    ok &= Expect(b.gpsEvents.size()==3,"(k) former passenger excluded");
    a.Disconnect(false); b.Disconnect(false); c.Disconnect(false); d.Disconnect(false);
    return ok;
}

bool ScenarioN(const char* host, uint16_t port)
{
    printf("\n=== (n) T90: garajes sincronizados ===\n");
    Bot a, b;
    if (!OpenPair(a,b,host,port,"GAR_A","GAR_B")) return false;
    auto mode = [](Bot& bot, bool individual) {
        Packets::Scripts::PlayerMissionMode m{}; m.individual = individual; m.vanillaScripts = true; bot.Send(m);
    };
    mode(a,false); mode(b,false);
    auto pump = [&] { a.Pump(100); b.Pump(100); a.Pump(100); };
    pump();
    Packets::World::GarageDoor g{}; g.index = 3; g.nameHash = 0xABCD1234u; g.open = true;
    a.Send(g); pump();
    bool ok = Expect(b.garageEvents.size() == 1 && b.garageEvents.back().open && b.garageEvents.back().index == 3,
        "(n) la puerta abierta llega al otro jugador");
    ok &= Expect(a.garageEvents.size() == 1, "(n) el emisor recibe el orden del servidor");
    mode(b,true); pump(); g.open = false; a.Send(g); pump();
    ok &= Expect(b.garageEvents.size() == 2 && !b.garageEvents.back().open,
        "(n) F1 individual recibe el movimiento de la puerta de mision");
    Packets::World::GarageDoor service{};
    service.index = 4; service.nameHash = 123u; service.open = true; service.visualOnly = true;
    b.Send(service); pump();
    ok &= Expect(a.garageEvents.size() == 3 && a.garageEvents.back().visualOnly && a.garageEvents.back().index == 4,
        "(n) F1 individual puede compartir movimiento de una puerta de servicio");
    mode(b,false); pump();
    Bot c;
    if (!c.Connect(host,port,"GAR_C") || !c.Handshake()) return false;
    c.Pump(300); a.Pump(100);
    ok &= Expect(c.garageEvents.size() == 2 && !c.garageEvents.front().open && c.garageEvents.front().nameHash == 0xABCD1234u &&
        c.garageEvents.back().open && c.garageEvents.back().visualOnly,
        "(n) quien entra tarde recibe el estado actual");
    a.Disconnect(false); b.Disconnect(false); c.Disconnect(false); return ok;
}

bool ScenarioL(const char* host, uint16_t port)
{
    using P = Packets::Scripts::StoryRace;
    printf("\n=== (l) High Stakes: autoridad, destino y validacion de checkpoints ===\n");
    Bot a, b, c;
    if (!OpenPair(a,b,host,port,"RACE_A","RACE_B") || !c.Connect(host,port,"RACE_C") || !c.Handshake()) return false;
    auto mode = [](Bot& bot, bool individual) {
        Packets::Scripts::PlayerMissionMode m{}; m.individual = individual; m.vanillaScripts = true; bot.Send(m);
    };
    mode(a,false); mode(b,false); mode(c,false);
    auto pump = [&] { a.Pump(100); b.Pump(100); c.Pump(100); a.Pump(100); };
    pump();
    P p{}; p.owner = c.Id(); p.target = b.Id(); p.session = 771; p.sequence = 1; p.phase = P::RACING;
    p.next[0] = 100; p.after[0] = 200; p.grid[0] = 50; p.place = 2; p.total = 8;
    a.Send(p); pump();
    bool ok = Expect(b.storyRaceEvents.empty(), "(l) fuera de mision no se manda una carrera");
    p.phase = P::PREPARE; a.Send(p); pump();
    ok &= Expect(b.storyRaceEvents.size() == 1 && b.storyRaceEvents.back().phase == P::PREPARE,
        "(l) preparacion temprana evita el auto-VS antes de cargar grilla y NPC");
    b.storyRaceEvents.clear(); p.phase = P::RACING;
    Packets::Scripts::OnMissionFlagSync active{}; active.bOnMission = true; a.Send(active); pump();
    a.Send(p); pump();
    ok &= Expect(b.storyRaceEvents.size() == 1 && b.storyRaceEvents.back().owner == a.Id() &&
        b.storyRaceEvents.back().next[0] == 100, "(l) checkpoint dirigido y autoridad sellada por servidor");
    ok &= Expect(c.storyRaceEvents.empty(), "(l) otro corredor no recibe el checkpoint ajeno");
    b.storyRaceEvents.clear(); p.target = c.Id(); b.Send(p); pump();
    ok &= Expect(c.storyRaceEvents.empty(), "(l) invitado no puede falsificar estado de carrera");
    mode(b,true); pump(); p.target = b.Id(); a.Send(p); pump();
    ok &= Expect(b.storyRaceEvents.empty(), "(l) F1 individual excluido");
    mode(b,false); pump(); a.Send(p); pump();
    ok &= Expect(b.storyRaceEvents.empty(), "(l) volver de F1 durante mision no permite reingreso tardio");
    active.bOnMission = false; a.Send(active); pump();
    active.bOnMission = true; a.Send(active); pump(); p.session = 772;
    p.size = 16; a.Send(p); pump();
    ok &= Expect(b.storyRaceEvents.empty(), "(l) radio fuera de rango rechazado");
    p.size = 6; uint32_t nanBits = 0x7FC00000u; memcpy(&p.next[0], &nanBits, 4); a.Send(p); pump();
    ok &= Expect(b.storyRaceEvents.empty(), "(l) coordenada no finita rechazada");
    p.next[0] = 100; p.phase = P::FINISHED; p.sequence = 2; a.Send(p); pump();
    ok &= Expect(b.storyRaceEvents.size() == 1 && b.storyRaceEvents.back().phase == P::FINISHED,
        "(l) llegada conserva sesion y destinatario");
    p.phase = P::END; p.sequence = 3; a.Send(p); pump();
    ok &= Expect(b.storyRaceEvents.size() == 2 && b.storyRaceEvents.back().phase == P::END,
        "(l) fin explicito llega al corredor");
    a.Disconnect(false); b.Disconnect(false); c.Disconnect(false); return ok;
}

bool ScenarioM(const char* host, uint16_t port)
{
    Bot a,b,c,d;
    if (!OpenPair(a,b,host,port,"INT_A","INT_B") || !c.Connect(host,port,"INT_C") || !c.Handshake() ||
        !d.Connect(host,port,"INT_D") || !d.Handshake()) return false;
    bool aConnected=true;
    auto pump=[&] { if (aConnected) a.Pump(80); b.Pump(80); c.Pump(80); d.Pump(80); };
    auto enter=[&](Bot& bot, uint8_t area, float x) {
        Packets::Peds::InteriorPopulation p{}; p.area=area; p.pos=CVector(x,0,1000);
        p.owner=a.Id(); bot.Send(p); pump(); // forged owner must be ignored
    };
    auto owns=[&](Bot& bot, playerid_t id) { return !bot.populationEvents.empty() && bot.populationEvents.back().owner==id; };
    enter(b,5,0); enter(a,5,0); enter(c,5,0); enter(d,5,1000);
    bool ok=Expect(owns(a,b.Id()) && owns(b,b.Id()) && owns(c,b.Id()),"(m) first entrant B owns room despite A's lower id and forged claim");
    ok &= Expect(owns(d,d.Id()),"(m) same area at another location has independent owner");
    Packets::Peds::PedSpawn spawn{}; spawn.pedid=pedid_t(a.Id(),83); spawn.pos=CVector(0,0,1000);
    spawn.createdBy=MISSION_CHAR; spawn.interiorPopulation=true; a.Send(spawn); pump();
    ok &= Expect(a.Received(ePacketType::PED_REMOVE,spawn.pedid) && !b.Received(ePacketType::PED_SPAWN,spawn.pedid),"(m) guest service publication rejected");
    spawn.pedid=pedid_t(b.Id(),83); b.Send(spawn); pump();
    ok &= Expect(a.Received(ePacketType::PED_SPAWN,spawn.pedid),"(m) owner service reaches other members");
    Packets::Peds::PedSpawn mission{}; mission.pedid=pedid_t(a.Id(),84); mission.pos=CVector(0,0,1000); mission.createdBy=MISSION_CHAR;
    a.Send(mission); pump(); ok &= Expect(b.Received(ePacketType::PED_SPAWN,mission.pedid),"(m) genuine mission ped from guest is preserved");
    enter(b,0,0); ok &= Expect(owns(a,a.Id()) && owns(c,a.Id()),"(m) leaving owner transfers room to oldest remaining entrant");
    const auto assignment=a.First(ePacketType::ASSIGN_PED,spawn.pedid);
    ok &= Expect(assignment && assignment->syncing && assignment->takeover,"(m) service population handed over with existing identity");
    a.Disconnect(true); aConnected=false; c.Pump(100); d.Pump(100); enter(c,5,0);
    ok &= Expect(owns(c,c.Id()),"(m) disconnected owner removed from election");
    enter(b,5,0); ok &= Expect(owns(b,c.Id()),"(m) returning former owner cannot steal room");
    enter(c,0,0); enter(b,0,0); enter(d,0,1000);
    enter(b,5,0); enter(c,5,0);
    ok &= Expect(owns(b,b.Id()) && owns(c,b.Id()),"(m) host first and guest second keep host ownership");
    enter(b,0,0); enter(c,0,0);
    Packets::Peds::InteriorPopulation simultaneous{}; simultaneous.area=5; simultaneous.pos=CVector(0,0,1000);
    b.Send(simultaneous); c.Send(simultaneous); pump();
    ok &= Expect(!b.populationEvents.empty() && !c.populationEvents.empty() &&
        b.populationEvents.back().owner==c.populationEvents.back().owner,"(m) simultaneous requests converge on one owner");
    b.Pump(3100); enter(c,5,0);
    ok &= Expect(owns(c,c.Id()),"(m) stale owner lease expires without keeping the interior locked");
    b.Disconnect(false); c.Disconnect(false); d.Disconnect(false);
    return ok;
}

bool ScenarioO(const char* host, uint16_t port)
{
    printf("\n=== (o) Reclutas Grove y autoridad ===\n");
    Bot a, b;
    if (!OpenPair(a, b, host, port, "GROVE_A", "GROVE_B")) return false;
    auto pump = [&] { a.Pump(100); b.Pump(100); a.Pump(100); };

    a.SendOnFoot(0.0f, 0.0f, GROUND_Z);
    b.SendOnFoot(100.0f, 0.0f, GROUND_Z);
    pump();

    Packets::Peds::PedSpawn spawn{};
    spawn.pedid = pedid_t(a.Id(), 87);
    spawn.pos = CVector(0.0f, 0.0f, GROUND_Z);
    spawn.modelId = MODEL_FAM1;
    spawn.pedType = PED_TYPE_GANG2;
    spawn.createdBy = RANDOM_CHAR;
    a.Send(spawn);
    pump();

    Packets::Peds::PedRecruit recruit{};
    recruit.pedid = spawn.pedid;
    recruit.recruited = true;
    b.Send(recruit);
    pump();
    bool ok = Expect(!b.Received(ePacketType::ASSIGN_PED, spawn.pedid), "(o) reclamo lejano rechazado");

    b.SendOnFoot(2.0f, 0.0f, GROUND_Z);
    pump();
    b.Send(recruit);
    pump();
    const auto* assignment = b.First(ePacketType::ASSIGN_PED, spawn.pedid);
    ok &= Expect(assignment && assignment->syncing && assignment->takeover, "(o) invitado cercano recibe autoridad");

    a.ClearEvents();
    a.Send(recruit);
    pump();
    ok &= Expect(!a.Received(ePacketType::ASSIGN_PED, spawn.pedid), "(o) no se le roba el recluta a otro jugador");

    recruit.recruited = false;
    b.Send(recruit);
    pump();
    recruit.recruited = true;
    a.Send(recruit);
    pump();
    assignment = a.First(ePacketType::ASSIGN_PED, spawn.pedid);
    ok &= Expect(assignment && assignment->syncing && assignment->takeover, "(o) despedir permite un nuevo reclutador");
    a.Disconnect(false);
    b.Disconnect(false);
    return ok;
}

void PrintUsage()
{
    printf("Uso: testbot.exe [host] [puerto] [escenario]\n");
    printf("  escenario: all (a, b, c, e, f, g, h, i, j, k, l, m, n, o) | a | b | c | d | e | f | g | h | i | j | k | l | m | n | o   (a/b/c/e piden handover = 1 y mission-lock = 1, d pide handover = 0)\n");
}
}  // namespace

int main(int argc, char* argv[])
{
    const char* host = argc > 1 ? argv[1] : "127.0.0.1";
    uint16_t port = argc > 2 ? static_cast<uint16_t>(atoi(argv[2])) : Config::DEFAULT_PORT;
    std::string scenario = argc > 3 ? argv[3] : "all";

    if (enet_initialize() != 0)
    {
        printf("[FAIL] enet_initialize fallo\n");
        return 2;
    }

    printf("testbot B3 - server %s:%u - escenario(s): %s\n", host, port, scenario.c_str());

    int nRun = 0;
    bool ok = true;

    if (scenario == "all" || scenario == "a")
    {
        bool pass = ScenarioA(host, port);
        printf(">>> escenario (a): %s\n", pass ? "PASS" : "FAIL");
        ok &= pass;
        ++nRun;
    }
    if (scenario == "all" || scenario == "b")
    {
        bool pass = ScenarioB(host, port);
        printf(">>> escenario (b): %s\n", pass ? "PASS" : "FAIL");
        ok &= pass;
        ++nRun;
    }
    if (scenario == "all" || scenario == "c")
    {
        bool pass = ScenarioC(host, port);
        printf(">>> escenario (c): %s\n", pass ? "PASS" : "FAIL");
        ok &= pass;
        ++nRun;
    }
    if (scenario == "all" || scenario == "e")
    {
        bool pass = ScenarioE(host, port);
        printf(">>> escenario (e): %s\n", pass ? "PASS" : "FAIL");
        ok &= pass;
        ++nRun;
    }
    if (scenario == "all" || scenario == "f")
    {
        bool pass = ScenarioF(host, port);
        printf(">>> escenario (f): %s\n", pass ? "PASS" : "FAIL");
        ok &= pass; ++nRun;
    }
    if (scenario == "all" || scenario == "g") {
        bool pass = ScenarioG(host, port); printf(">>> escenario (g): %s\n", pass ? "PASS" : "FAIL");
        ok &= pass; ++nRun;
    }
    if(scenario=="all" || scenario=="h") { bool pass=ScenarioH(host,port); printf(">>> escenario (h): %s\n",pass?"PASS":"FAIL"); ok &= pass; ++nRun; }
    if (scenario == "all" || scenario == "i") { bool pass=ScenarioI(host,port); printf(">>> escenario (i): %s\n",pass?"PASS":"FAIL"); ok &= pass; ++nRun; }
    if (scenario == "all" || scenario == "k") { bool pass=ScenarioK(host,port); printf(">>> escenario (k): %s\n",pass?"PASS":"FAIL"); ok &= pass; ++nRun; }
    if (scenario == "all" || scenario == "j") { bool pass = ScenarioJ(host, port); printf(">>> escenario (j): %s\n", pass ? "PASS" : "FAIL"); ok &= pass; ++nRun; }
    if (scenario == "all" || scenario == "l") {
        bool pass = ScenarioL(host, port); printf(">>> escenario (l): %s\n", pass ? "PASS" : "FAIL");
        ok &= pass; ++nRun;
    }
    if (scenario == "all" || scenario == "n") { bool pass=ScenarioN(host,port); printf(">>> escenario (n): %s\n",pass?"PASS":"FAIL"); ok &= pass; ++nRun; }
    if (scenario == "all" || scenario == "m") { bool pass=ScenarioM(host,port); printf(">>> escenario (m): %s\n",pass?"PASS":"FAIL"); ok &= pass; ++nRun; }
    if (scenario == "all" || scenario == "o")
    {
        const bool pass = ScenarioO(host, port);
        printf(">>> escenario (o): %s\n", pass ? "PASS" : "FAIL");
        ok &= pass;
        ++nRun;
    }
    if (scenario == "d")
    {
        bool pass = ScenarioD(host, port);
        printf(">>> escenario (d): %s\n", pass ? "PASS" : "FAIL");
        ok &= pass;
        ++nRun;
    }

    enet_deinitialize();

    if (nRun == 0)
    {
        printf("[FAIL] escenario desconocido: %s\n", scenario.c_str());
        PrintUsage();
        return 2;
    }

    printf("\nRESULTADO: %d escenarios, %d chequeos, %d fallas: %s\n", nRun, g_nChecks, g_nFailures,
        ok ? "TODO PASS" : "HAY FALLAS");
    return ok ? 0 : 1;
}
