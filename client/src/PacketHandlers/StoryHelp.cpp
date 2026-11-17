#include "stdafx.h"
#include "CStoryHelp.h"
#include "network/packets/story_help.h"

PACKET_HANDLER(ePacketType::STORY_HELP, Packets::Scripts::StoryHelp* packet)
{
    CStoryHelp::OnPacket(*packet);
}

PACKET_HANDLER(ePacketType::STORY_CONTACTS, Packets::Scripts::StoryContacts* packet)
{
    CStoryHelp::OnContacts(*packet);
}
