#pragma once

#include <cstdint>

// raw UDP queries answered by the server on its game port (outside ENet), used by the launcher:
// every datagram starts with FF FF FF FF 'C' 'A' <type>, integers are little endian
//
// INFO_REQUEST  -> INFO_REPLY:  u16 players, u16 maxPlayers, u32 storySize, u32 storyCrc,
//                               u8 nameLength, name, u8 versionLength, version, u32 serverId
//                               (random per server run: the same server answers once per network interface)
// STORY_REQUEST u32 offset -> STORY_REPLY: u32 offset, u32 storySize, u32 storyCrc, u16 length, data
namespace ServerQuery
{
constexpr uint8_t HEADER[6] = {0xFF, 0xFF, 0xFF, 0xFF, 'C', 'A'};
constexpr int HEADER_SIZE = 7;  // HEADER + type

constexpr uint8_t INFO_REQUEST = 'i';
constexpr uint8_t INFO_REPLY = 'I';
constexpr uint8_t STORY_REQUEST = 's';
constexpr uint8_t STORY_REPLY = 'S';

constexpr uint16_t STORY_CHUNK_SIZE = 1024;  // fits in one datagram over a VPN
constexpr uint32_t MAX_STORY_SIZE = 1024 * 1024;
}  // namespace ServerQuery
