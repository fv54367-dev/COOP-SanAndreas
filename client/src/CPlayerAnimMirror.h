#pragma once

// player-anim-mirror: the "action" animations the local CJ is playing (named IFP animations of scripts, shops, food,
// gym, sitting, phone, falls, getting up...) go to the other players, whose copy of him plays the same. Walking,
// running, aiming and shooting are not sent: they come out of the replicated controls.
class CPlayerAnimMirror
{
public:
    static void Process();
};
