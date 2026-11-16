#include "stdafx.h"
#include "CStorySave.h"
#include "CPersistence.h"
#include "crc32.h"
#include "network/query.h"
#include <random>

static const std::filesystem::path STORY_PATH = std::filesystem::path("saves") / "story.b";

void CStorySave::Load()
{
    std::ifstream file(STORY_PATH, std::ios::binary);
    if (!file)
        return;

    std::vector<uint8_t> data((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    if (data.empty() || data.size() > ServerQuery::MAX_STORY_SIZE)
        return;

    ms_data = std::move(data);
    ms_nCrc = Crc32(ms_data.data(), ms_data.size());
    CServerLog::Printf("[Persistence] : story savegame loaded (%u bytes)\n", (unsigned)ms_data.size());
}

void CStorySave::HandleChunk(CNetworkPlayer* pNetworkPlayer, Packets::Players::StorySaveChunk* pChunk)
{
    if (!pNetworkPlayer->m_bIsHost || !CPersistence::IsEnabled())
        return;

    if (pChunk->totalSize == 0 || pChunk->totalSize > ServerQuery::MAX_STORY_SIZE)
        return;

    if (pChunk->offset == 0)
    {
        ms_incoming.assign(pChunk->totalSize, 0);
        ms_nIncomingReceived = 0;
    }

    // chunks come in order (reliable channel), anything else is a broken upload
    if (ms_incoming.size() != pChunk->totalSize || pChunk->offset != ms_nIncomingReceived ||
        pChunk->offset + pChunk->length > pChunk->totalSize)
    {
        ms_incoming.clear();
        ms_nIncomingReceived = 0;
        return;
    }

    memcpy(ms_incoming.data() + pChunk->offset, pChunk->data, pChunk->length);
    ms_nIncomingReceived += pChunk->length;

    if (ms_nIncomingReceived != pChunk->totalSize)
        return;

    std::error_code ec;
    std::filesystem::create_directories(STORY_PATH.parent_path(), ec);

    std::filesystem::path tmpPath = STORY_PATH;
    tmpPath += ".tmp";
    {
        std::ofstream file(tmpPath, std::ios::binary | std::ios::trunc);
        file.write((const char*)ms_incoming.data(), ms_incoming.size());
        if (!file)
        {
            CServerLog::Printf("[Persistence] : couldn't write the story savegame\n");
            return;
        }
    }
    std::filesystem::rename(tmpPath, STORY_PATH, ec);

    ms_data = std::move(ms_incoming);
    ms_nCrc = Crc32(ms_data.data(), ms_data.size());
    ms_incoming.clear();
    ms_nIncomingReceived = 0;

    logger::info("story savegame received from the host %s (%u bytes)", pNetworkPlayer->m_Name, (unsigned)ms_data.size());
}

template <typename T>
static void Append(std::vector<uint8_t>& buffer, T value)
{
    const uint8_t* p = (const uint8_t*)&value;
    buffer.insert(buffer.end(), p, p + sizeof(T));
}

static void AppendHeader(std::vector<uint8_t>& buffer, uint8_t type)
{
    buffer.insert(buffer.end(), ServerQuery::HEADER, ServerQuery::HEADER + sizeof(ServerQuery::HEADER));
    buffer.push_back(type);
}

static void AppendString(std::vector<uint8_t>& buffer, const std::string& str)
{
    uint8_t length = (uint8_t)std::min<size_t>(str.size(), 255);
    buffer.push_back(length);
    buffer.insert(buffer.end(), str.begin(), str.begin() + length);
}

int ENET_CALLBACK CServerQuery::Intercept(ENetHost* host, ENetEvent* event)
{
    const uint8_t* data = host->receivedData;
    size_t size = host->receivedDataLength;

    if (size < ServerQuery::HEADER_SIZE || memcmp(data, ServerQuery::HEADER, sizeof(ServerQuery::HEADER)) != 0)
        return 0;  // a regular ENet datagram

    std::vector<uint8_t> reply;
    uint8_t type = data[sizeof(ServerQuery::HEADER)];

    if (type == ServerQuery::INFO_REQUEST)
    {
        AppendHeader(reply, ServerQuery::INFO_REPLY);
        Append<uint16_t>(reply, (uint16_t)CNetworkPlayerManager::m_pPlayers.size());
        Append<uint16_t>(reply, Config::MAX_SERVER_PLAYERS);
        Append<uint32_t>(reply, (uint32_t)CStorySave::ms_data.size());
        Append<uint32_t>(reply, CStorySave::ms_nCrc);
        AppendString(reply, CConfigManager::GetConfigName());
        AppendString(reply, COOPSA_VERSION);
        static const uint32_t serverId = std::random_device{}();
        Append<uint32_t>(reply, serverId);
    }
    else if (type == ServerQuery::STORY_REQUEST && size >= ServerQuery::HEADER_SIZE + sizeof(uint32_t))
    {
        uint32_t offset;
        memcpy(&offset, data + ServerQuery::HEADER_SIZE, sizeof(offset));

        const auto& story = CStorySave::ms_data;
        uint16_t length = 0;
        if (offset < story.size())
            length = (uint16_t)std::min<size_t>(ServerQuery::STORY_CHUNK_SIZE, story.size() - offset);

        AppendHeader(reply, ServerQuery::STORY_REPLY);
        Append<uint32_t>(reply, offset);
        Append<uint32_t>(reply, (uint32_t)story.size());
        Append<uint32_t>(reply, CStorySave::ms_nCrc);
        Append<uint16_t>(reply, length);
        if (length)
            reply.insert(reply.end(), story.begin() + offset, story.begin() + offset + length);
    }
    else
    {
        return 1;  // ours but unknown, drop it
    }

    ENetBuffer buffer;
    buffer.data = reply.data();
    buffer.dataLength = reply.size();
    enet_socket_send(host->socket, &host->receivedAddress, &buffer, 1);
    return 1;
}
