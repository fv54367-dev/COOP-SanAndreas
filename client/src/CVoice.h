#pragma once
#include "network/packets/voice.h"
#include <vector>
#include <string>
class CVoice
{
public:
    inline static bool ms_bMuted = false;
    static void Process();
    static void Shutdown();
    static void Configure(const Packets::Voice::Settings& settings);
    static void Receive(const Packets::Voice::Frame& frame);
    static bool ShouldDraw();
    static void Draw();
    // voice menu helpers (CModeMenu): input devices by name, level of the last captured frame (0..1), F4 state
    static std::vector<std::string> GetMicDevices();
    static float GetMicLevel();
    static bool IsTalking();
};
