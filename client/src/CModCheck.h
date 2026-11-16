#pragma once

// mod-check: this game's cars, peds and weapons (and plugins) go to the server, which compares them with the host's
class CModCheck
{
public:
    // after the handshake (every connection, reconnects included)
    static void SendManifest();
};
