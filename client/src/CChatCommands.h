#pragma once

#include "network/packets/players.h"

// Local chat commands ("/..."), typed in the chat but executed on the game thread
class CChatCommands
{
public:
    // called from the chat input (window thread); returns true if the text was a command
    static bool Queue(const std::wstring& text);
    // called every frame from the game loop
    static void Process();

    static void HandleGiveAmmo(Packets::Players::GiveAmmo* packet);
};
