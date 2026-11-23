#pragma once

// player-anim-mirror: the "action" animations the local CJ is playing (named IFP animations of scripts, shops, food,
// gym, sitting, phone, falls, getting up...) go to the other players, whose copy of him plays the same. Walking,
// running, aiming and shooting normally come from the controls. Scripted walks also send their gait.
class CPlayerAnimMirror
{
public:
    static void Init();
    static void Process();
};
