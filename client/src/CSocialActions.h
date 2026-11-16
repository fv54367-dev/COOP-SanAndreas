#pragma once
#include <string>
namespace Packets::Scripts { class SocialAction; class EmoteState; }
class CSocialActions {
public:
    static void Reset();
    static void Process();
    static bool Command(const std::wstring& name, const std::wstring& args);
    static void OnPacket(const Packets::Scripts::SocialAction& p);
    static void OnAnimation(const Packets::Scripts::EmoteState& p);
    static bool Visible();
    static void Draw();
};
