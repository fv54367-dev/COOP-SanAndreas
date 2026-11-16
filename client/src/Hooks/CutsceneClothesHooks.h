#pragma once

// Rebuild the shared player TXD from the local descriptor without an incremental
// clothes cache before constructing CSPLAY. Each player sees his own CJ (T48/T84).
class CutsceneClothesHooks
{
public:
    static void InjectHooks();
};
