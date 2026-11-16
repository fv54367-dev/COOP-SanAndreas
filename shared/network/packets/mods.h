#pragma once

#include "network/packet.h"
#include "network/packet_types.h"
#include "network/serializable_types.h"
#include "serialize.h"

// NOLINTBEGIN(readability-simplify-boolean-expr)
// NOLINTBEGIN(readability-isolate-declaration)

// mod-check: what of a game has to be the same as the host's for the sync to work. The models travel by number
// (PED_SPAWN, VEHICLE_SPAWN...), so a car, ped or weapon added or swapped by a mod is something else, or a crash, in
// the other game. Visual mods (textures, ENB, sounds) are not looked at.
namespace Packets::Mods
{
struct ModelEntry
{
    uint16_t id = 0;
    uint8_t kind = 0;    // 1 vehicle, 2 ped, 3 weapon
    uint32_t key = 0;    // CBaseModelInfo::m_nKey, CKeyGen hash of the model name
    char gameName[8]{};  // vehicles: CVehicleModelInfo::m_szGameName (GXT key of the name shown in game)

    template <typename Stream>
    bool Serialize(Stream& stream)
    {
        serialize_uint16(stream, id);
        serialize_int(stream, kind, 1, 3);
        serialize_uint32(stream, key);
        serialize_string(stream, gameName, sizeof(gameName));
        return true;
    }
};

// client -> server after the handshake, in parts: the models first, the last part with the rest
class ModManifest : public Packet
{
    DEFINE_PACKET_TYPE(ModManifest, ePacketType::MOD_MANIFEST, ePacketChannel::EVENT);

public:
    static constexpr int MAX_MODELS = 200;
    static constexpr int MAX_NAMES = 128;

    uint8_t part = 0;
    bool last = false;
    int count = 0;
    ModelEntry models[MAX_MODELS]{};
    // last part only
    uint32_t handlingHash = 0;
    uint32_t weaponHash = 0;
    int nameCount = 0;
    char names[MAX_NAMES][32]{};  // loaded .asi / .cleo plugins and cleo scripts, lower case

private:
    template <typename Stream>
    bool Serialize(Stream& stream)
    {
        serialize_uint8(stream, part);
        serialize_bool(stream, last);
        serialize_int(stream, count, 0, MAX_MODELS);
        for (int i = 0; i < count; i++)
            serialize_object(stream, models[i]);
        if (last)
        {
            serialize_uint32(stream, handlingHash);
            serialize_uint32(stream, weaponHash);
            serialize_int(stream, nameCount, 0, MAX_NAMES);
            for (int i = 0; i < nameCount; i++)
                serialize_string(stream, names[i], sizeof(names[i]));
        }
        return true;
    }
};

// server -> the player that joined: how his game differs from the host's
class ModCheckResult : public Packet
{
    DEFINE_PACKET_TYPE(ModCheckResult, ePacketType::MOD_CHECK_RESULT, ePacketChannel::SYSTEM);

public:
    static constexpr int MAX_MODELS = 24;
    static constexpr int MAX_NAMES = 12;
    enum : uint8_t
    {
        MODEL_HOST_ONLY = 0,    // the host has it, this game does not
        MODEL_PLAYER_ONLY = 1,  // this game has it, the host does not
        MODEL_DIFFERENT = 2     // same number, another model
    };

    bool blocked = false;  // the server disconnects him right after this
    char hostName[25]{};
    int totalModels = 0;   // differences found (only the first MAX_MODELS travel)
    int count = 0;
    ModelEntry models[MAX_MODELS]{};
    uint8_t status[MAX_MODELS]{};
    bool handlingDiffers = false;
    bool weaponsDiffer = false;
    int hostOnlyCount = 0;
    char hostOnly[MAX_NAMES][32]{};
    int playerOnlyCount = 0;
    char playerOnly[MAX_NAMES][32]{};

private:
    template <typename Stream>
    bool Serialize(Stream& stream)
    {
        serialize_bool(stream, blocked);
        serialize_string(stream, hostName, sizeof(hostName));
        serialize_int(stream, totalModels, 0, 65535);
        serialize_int(stream, count, 0, MAX_MODELS);
        for (int i = 0; i < count; i++)
        {
            serialize_object(stream, models[i]);
            serialize_int(stream, status[i], MODEL_HOST_ONLY, MODEL_DIFFERENT);
        }
        serialize_bool(stream, handlingDiffers);
        serialize_bool(stream, weaponsDiffer);
        serialize_int(stream, hostOnlyCount, 0, MAX_NAMES);
        for (int i = 0; i < hostOnlyCount; i++)
            serialize_string(stream, hostOnly[i], sizeof(hostOnly[i]));
        serialize_int(stream, playerOnlyCount, 0, MAX_NAMES);
        for (int i = 0; i < playerOnlyCount; i++)
            serialize_string(stream, playerOnly[i], sizeof(playerOnly[i]));
        return true;
    }
};
}  // namespace Packets::Mods

// NOLINTEND(readability-isolate-declaration)
// NOLINTEND(readability-simplify-boolean-expr)
