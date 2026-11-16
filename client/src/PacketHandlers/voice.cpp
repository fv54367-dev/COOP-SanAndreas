#include "stdafx.h"
#include "network/packet_handler.h"
#include "CVoice.h"
PACKET_HANDLER(ePacketType::VOICE_SETTINGS, Packets::Voice::Settings* packet)
{
    CVoice::Configure(*packet);
}
PACKET_HANDLER(ePacketType::VOICE_FRAME, Packets::Voice::Frame* packet)
{
    CVoice::Receive(*packet);
}
