#pragma once

// player-list-coopsa: the local player's money goes to the others for their TAB list (PLAYER_MONEY) when it changes and
// every few seconds, so a player that joins later gets it too. It is only shown: each one keeps his own money.
class CPlayerMoney
{
public:
    static void Process();
};
