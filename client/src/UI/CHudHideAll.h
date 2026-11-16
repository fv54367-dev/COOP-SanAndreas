#pragma once

// F2: hides absolutely all the HUD (the game's and the mod's) for clean screenshots/videos; F2 again brings it back.
// The game's own HUD is hidden by turning the 2D draw functions called from Render2dStuff into a `ret` (and restoring
// their first byte); the mod's draws consult IsActive().
class CHudHideAll
{
public:
    static void Init();
    static bool IsActive() { return ms_bActive; }
    // the "HUD hidden" notice in the chat stays visible a few seconds before the chat hides too
    static bool IsChatHidden();

private:
    static inline bool ms_bActive = false;
};
