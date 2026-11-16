#pragma once
// coop-car-gunners: while the coop mission's owner rides attached to a car as its gunner (Reuniting the Families:
// ATTACH_CHAR_TO_CAR $scplayer on Sweet's car), each guest nearby is attached to the same car next to him, so they
// ride and shoot too instead of being left behind on foot.
class CMissionCarGunners
{
public:
    static void Process();
};
