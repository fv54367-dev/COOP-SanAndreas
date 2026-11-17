#pragma once
#include "network/packet.h"
#include "network/ids.h"
#include "network/packets/blips.h"

namespace Packets::Scripts
{
// story-help-optional: the script authority starts a story mission for which this player is not at the same point of the
// chain (he did it already, or has not reached it). OFFER (authority -> that player): "help <authority> in <mission>?";
// ACCEPT / DECLINE (that player -> the authority); END (authority -> the players it offered): its mission is over, so
// the ones that declined play with the others again. The server relays it, filling senderPlayerid.
class StoryHelp : public Packet
{
    DEFINE_PACKET_TYPE(StoryHelp, ePacketType::STORY_HELP, ePacketChannel::SCRIPT);
public:
    enum Kind : uint8_t { OFFER, ACCEPT, DECLINE, END, KIND_COUNT };
    SenderPlayerId senderPlayerid{};
    playerid_t target{};  // who gets it
    uint8_t kind = OFFER;
    char mission[40]{};  // OFFER: name of the mission

    template <typename Stream> bool Serialize(Stream& stream)
    {
        serialize_object(stream, senderPlayerid);
        serialize_playerid(stream, target);
        serialize_bits(stream, kind, 2);
        serialize_string(stream, mission, sizeof(mission));
        mission[sizeof(mission) - 1] = '\0';
        return true;
    }
};

// contacts-follow-host off: the contact points (icons on the map) the authority's story mission removed and made while it
// ran, sent with its progress. Players whose own progress matches the authority's apply it to their own contacts.
class StoryContacts : public Packet
{
    DEFINE_PACKET_TYPE(StoryContacts, ePacketType::STORY_CONTACTS, ePacketChannel::SCRIPT);
public:
    static constexpr int MAX_CONTACTS = 8;
    uint8_t countRemoved = 0;
    uint8_t countAdded = 0;
    Packets::Blips::_StaticBlipPayload removed[MAX_CONTACTS]{};
    Packets::Blips::_StaticBlipPayload added[MAX_CONTACTS]{};

    template <typename Stream> bool Serialize(Stream& stream)
    {
        serialize_int(stream, countRemoved, 0, MAX_CONTACTS);
        serialize_int(stream, countAdded, 0, MAX_CONTACTS);
        for (int i = 0; i < countRemoved; i++)
            serialize_object(stream, removed[i]);
        for (int i = 0; i < countAdded; i++)
            serialize_object(stream, added[i]);
        return true;
    }
};
}
