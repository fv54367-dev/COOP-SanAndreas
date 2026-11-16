#pragma once

#include "network/packets/scripts.h"

// scenes of the host's coop mission (cutscenes and scripted scenes) are skipped for everyone at once,
// when every player pressed the skip button; while a cutscene plays the network players are hidden
class CCutsceneSkipVote
{
public:
    static void InjectHooks();
    static void Process();
    static void HandlePacket(Packets::Scripts::SkipCutscene* pPacket);

    // not host: the cutscene being played was loaded by the host's mission
    static inline bool ms_bNetworkCutscene = false;
};
