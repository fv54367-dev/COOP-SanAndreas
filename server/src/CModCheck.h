#pragma once

class CNetworkPlayer;

// mod-check: every game tells the server its cars, peds and weapons (MOD_MANIFEST) and they are compared with the
// host's. Different models break the sync (they travel by number) and keep the player out; different handling,
// weapon data or plugins are only told to him and to the host.
namespace CModCheck
{
// the player is gone: forget his manifest
void ForgetPlayer(CNetworkPlayer* pPlayer);
}  // namespace CModCheck
