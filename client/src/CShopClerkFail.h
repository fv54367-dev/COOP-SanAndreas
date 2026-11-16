#pragma once

// A mission that sends the players to a clothes shop / barber (the shop script CLOTH / BARB runs, the clerk is a script ped)
// fails for everybody when the clerk dies: otherwise the one in the changing room / the chair stays stuck (the shop script
// never finishes its animation). Only the script authority decides; interruptor mission-shop-clerk-fail.
class CShopClerkFail
{
public:
    static void Process();  // every frame, after the scripts ran
};
