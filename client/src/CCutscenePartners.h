#pragma once
class CPed;
namespace Packets::Scripts { class CutscenePartners; }
class CCutscenePartners {
public:
    static void Init();
    static void Process();
    static void Render();
    static void Reset();
    static bool InScriptedGuestScene();
    static bool ControlsPed(CPed* ped);
    static void HandlePacket(const Packets::Scripts::CutscenePartners& packet);
};
