#pragma once
#include "../CCustomCommand.h"

// the same as the *ForNetworkPlayer commands but for every network player at once: one line per mission event
// instead of a loop over $NETWORK_PLAYER (missions have a size limit of 69000 bytes)
class CCommandUpdateCheckpointForAllNetworkPlayers : public CCustomCommand { void Process(CRunningScript* script) override; };
class CCommandRemoveCheckpointForAllNetworkPlayers : public CCustomCommand { void Process(CRunningScript* script) override; };
class CCommandUpdateCharBlipForAllNetworkPlayers : public CCustomCommand { void Process(CRunningScript* script) override; };
class CCommandRemoveCharBlipForAllNetworkPlayers : public CCustomCommand { void Process(CRunningScript* script) override; };
class CCommandUpdateCarBlipForAllNetworkPlayers : public CCustomCommand { void Process(CRunningScript* script) override; };
class CCommandRemoveCarBlipForAllNetworkPlayers : public CCustomCommand { void Process(CRunningScript* script) override; };
class CCommandClearAllEntityBlipsForAllNetworkPlayers : public CCustomCommand { void Process(CRunningScript* script) override; };

// the blips the script asked for on an entity that had no network id yet (a char or car created in the same frame) are sent
// once it has one; called every frame
void ProcessDeferredEntityBlips();
// forWho: one player (the *ForNetworkPlayer commands); invalid: every participant
void SendOrDeferEntityBlip(playerid_t forWho, CEntity* pEntity, int friendly, int display, int color, int scale);
void DropDeferredEntityBlip(playerid_t forWho, CEntity* pEntity);
