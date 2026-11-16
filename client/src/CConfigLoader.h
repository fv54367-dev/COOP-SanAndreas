#pragma once

class CConfigLoader
{
private:
	static void BuildPath();
	static void BuildDefaultConfig();
	static void SetDirMyDocuments_Hook();
	static void EnsureCreated();
	static void Load();

	static inline std::string ms_sDataPath = "";

	static inline std::unordered_map<std::string, std::string> ms_umDefaultConfig =
	{
		{"voice-chat", "true"},
		{"nickname", ""},
		{"ip", ""},
		{"port", std::to_string(Config::DEFAULT_PORT)},
		{"report-crashlogs", "false"},
		{"smooth-vehicles", "true"},
		{"remote-horn-sync", "true"},
		{"remote-heli-rotor-sync", "true"},
		{"smooth-onfoot", "true"},
		{"settings-version", "1"},
		{"cheat-menu-key", "F8"},
		{"high-sync-rate", "true"},
		{"adaptive-jitter-buffer", "true"},
		{"npc-distance-rate", "true"},
		{"precise-timers", "true"},
		{"camera-mirror", "true"},
		{"npc-task-sync", "true"},
		{"npc-named-anim-sync", "true"},
		{"npc-synced-pose-keep", "true"},
		{"interior-handover", "true"},
        {"interior-population-sync", "true"},
		{"mission-interior-safety", "true"},
		{"handover", "true"},
		{"zone-population", "true"},
		{"zone-ped-budget", "25"},
		{"mission-gather-skip-near", "20"},
		{"police-per-player", "true"},
		{"npc-driveby-sync", "true"},
		{"random-cops-cap", "true"},
		{"coop-side-missions", "true"},
		{"own-static-blips", "true"},
		{"contacts-follow-host", "true"},
		{"teleport-load-interior", "true"},
        {"coop-lowrider", "true"},
        {"lowrider-keep-car", "true"},
        {"coop-dance", "true"},
        {"dance-guest-story", "true"},
        {"dance-side-by-side", "true"},
        {"player-full-rebuild", "true"},
        {"music1-beach-arrival", "true"},
        {"mission-audio-zones", "true"},
        {"mission-guest-car-sections", "true"},
        {"fade-guard", "true"},
        {"pool-two-players", "true"},
        {"activity-challenge", "true"},
        {"emotes", "true"},
        {"gps-route", "true"},
        {"garage-sync", "true"},
        {"coop-mission-machines", "true"},
        {"cutscene-partners", "true"},
        {"scene-guests-visible", "true"},
        {"cutscene-partner-smooth", "true"},
        {"just-business-bike-escort", "true"},
        {"just-business-no-marker-wait", "true"},
        {"high-stakes-coop", "true"},
        {"high-stakes-guest-notice", "true"},
        {"guest-clear-area-keep-car", "true"},
        {"high-stakes-no-grid-wait", "true"},
        {"high-stakes-guest-blips", "true"},
        {"high-stakes-board", "true"},
        {"high-stakes-hide-native-hud", "true"},
        {"high-stakes-finish-notice", "true"},
        {"high-stakes-end-cleanup", "true"},
        {"cutscene-hide-extras", "true"},
		{"auto-reconnect", "true"},
		{"clear-messages", "true"},
		{"native-markers", "true"},
		{"mission-hunters", "false"},
		{"mirror-blips", "true"},
		{"mission-sync-rules", "true"},
        {"intro-independent", "true"},
        {"native-hints", "true"},
        {"mission-wait-notices", "true"},
        {"sync-local-text-vars", "true"},
        {"mission-guest-counts", "true"},
        {"mission-guest-actions", "true"},
        {"coop-photos", "true"},
        {"coop-home-invasion", "true"},
		{"mission-ally-follow-area", "true"},
		{"mission-wait-same-place", "true"},
		{"mission-wait-on-entry", "true"},
		{"mission-wait-scenes", "true"},
		{"mission-scene-prelude", "true"},
		{"mission-hold-on-entry", "true"},
		{"mission-entry-no-teleport", "true"},
		{"mission-no-auto-board", "true"},
		{"mission-scene-skip-no-control", "true"},
		{"cutscene-table-fallback", "true"},
		{"intro-follow-host", "true"},
		{"mirror-blips-clear-on-end", "true"},
		{"mission-car-overlap-dedupe", "true"},
		{"mission-wide-car-on-screen", "true"},
		{"scene-guest-end-near-host", "true"},
		{"mission-guest-train", "true"},
		{"mission-player-equipment", "true"},
		{"mission-nines-ammo-repair", "true"},
		{"guest-story-launchers", "true"},
		{"mission-nines-far-boarding", "true"},
		{"mission-nines-board-near", "true"},
		{"mission-equipment-script-ammo", "true"},
		{"mission-audio-guard", "true"},
		{"mission-hood-door-open", "true"},
		{"remote-bullet-no-force", "true"},
		{"ped-damage-authority", "true"},
		{"ped-damage-any-player", "true"},
		{"npc-aim-sync", "true"},
		{"npc-velocity-sync", "true"},
		{"npc-anim-mirror", "true"},
		{"player-anim-mirror", "true"},
		{"emergency-dedupe", "true"},
		{"spawn-watch", "true"},
		{"player-jack-native", "true"},
		{"mission-shared-on-screen", "true"},
		{"mission-scripted-boarding", "true"},
		{"mission-og-loc-bikes", "true"},
		{"mission-og-loc-guest-car", "true"},
		{"mission-drive-thru-guest-car", "true"},
		{"coop-drive-by", "true"},
		{"coop-los-sepulcros", "true"},
		{"coop-running-dog", "true"},
		{"coop-wrong-side-tracks", "true"},
		{"coop-robbing-uncle-sam", "true"},
		{"coop-robbing-guest-deliver", "true"},
		{"object-moving-nocollide", "true"},
		{"mission-guest-car-board", "true"},
		{"mission-drive-thru-first-car", "true"},
		{"mission-trip-skip-guests", "true"},
		{"mission-catalyst-guest-car", "true"},
		{"mission-guest-car-copies", "false"},
		{"mission-scripted-exit", "true"},
		{"mirror-pickups", "true"},
		{"patient-handover", "true"}
	};
	
public:
    inline static bool ms_bVoiceChat = true;
    // voice menu (F1): mic/voices volume in percent (0-200), F4 as hold or toggle, input device by name ("" = Windows default)
    inline static int ms_nVoiceMicVolume = 100;
    inline static int ms_nVoiceOutVolume = 100;
    inline static bool ms_bVoiceToggle = false;
    inline static char ms_szVoiceMic[96] = "";
	static inline const std::string CONFIG_SECTION = "config";
	static inline const std::string CONFIG_FILE_NAME = "coopandreas.ini";

	// experimental: smooth correction of remote vehicle positions instead of hard teleports
	static inline bool ms_bSmoothVehicles = true;
	// Use the remote driver horn state when servicing vehicle audio.
	static inline bool ms_bRemoteHornSync = true;
	// a remote player's helicopter keeps its rotor spinning and its engine (lights) on
	static inline bool ms_bRemoteHeliRotorSync = true;
	// a remote boat is not anchored, shows its rudder and spins its propeller like the driver's one
	static inline bool ms_bRemoteBoatSync = true;
	// experimental: same for players and peds on foot
	static inline bool ms_bSmoothOnFoot = true;
	// remote players that look dead/fallen while the network says alive are rebuilt after a few seconds
	static inline bool ms_bRemoteReviveAuto = true;
	// own vehicle sent at ~35 Hz while moving instead of 20 Hz
	static inline bool ms_bHighSyncRate = true;
	// the delay applied to received packets follows the measured network jitter instead of a fixed 50 ms
	static inline bool ms_bAdaptiveJitterBuffer = true;
	// peds hosted by us far from every other player are sent less often
	static inline bool ms_bNpcDistanceRate = true;
	// sync timers with QueryPerformanceCounter instead of GetTickCount (~15.6 ms steps)
	static inline bool ms_bPreciseTimers = true;
	// clients mirror the host's script camera during the scenes of a synced mission
	static inline bool ms_bCameraMirror = true;
	static inline bool ms_bCameraMirrorInteriorGuard = true;  // camera-mirror-interior-guard: no camera mirroring around interiors
    static inline bool ms_bCoopLowrider = true;
    static inline bool ms_bLowriderKeepCar = true;
    // VS race: the owner sends a common start instant (server clock) and every client starts the race then
    static inline bool ms_bVsRaceSync = true;
    static inline bool ms_bCoopDance = true;
    // story missions have no CCoopMission participants: the dance guest is checked like the lowrider one (T97)
    static inline bool ms_bDanceGuestStory = true;
    // the dance guest dances 1.25 m beside the host, across the dance camera's view, and both are in the shot
    static inline bool ms_bDanceSideBySide = true;
    // CClothes::RebuildPlayer always builds the clump in full (no PlayerClothes cache): a remote CJ took our clothes
    static inline bool ms_bPlayerFullRebuild = true;
    // MUSIC1: the beach arrival scene waits for the mission players and does not gather the near ones (T97)
    static inline bool ms_bMusic1BeachArrival = true;
    // SWITCH_AUDIO_ZONE (0917) of a synced mission reaches the guests: party music, lowrider meet (T97)
    static inline bool ms_bMissionAudioZones = true;
    // requiresHostCar scripts with a validated vanilla block exclude only their listed sections (T97)
    static inline bool ms_bMissionGuestCarSections = true;
    // T98: guest vehicles of the 5 LS rules are parked when the mission makes the host's one, nobody is put in them
    static inline bool ms_bMissionGuestVehiclesSelfBoard = true;
    // T98: a guest driving a mission vehicle only the host can drive there gets the "Solo <host>..." help box
    static inline bool ms_bMissionHostDriverHint = true;
    // T98: a guest-driven car reaching a small marker waits for the host within 50 m (not 12) in the 5 LS rules
    static inline bool ms_bMissionGuestCarHostReach = true;
    // T98 MUSIC1: the garage scene with a guest driving the van makes the host its driver first
    static inline bool ms_bMusic1GarageHostDriver = true;
    // a guest left black by a mirrored fade-out gets the screen back after a few seconds
    static inline bool ms_bFadeGuard = true;
    static inline bool ms_bSkipDeathArrestHelp = false;
    static inline bool ms_bDanceTeamScore = true;
    // F8 menu lists every cheat of the game (92 slots); false = the short list of before
    static inline bool ms_bCheatMenuAll = true;
    // every cheat used (typed or F8) is announced to everybody in the chat; false = as before (nothing)
    static inline bool ms_bCheatAnnounce = true;
    // the clerk of a clothes shop / barber dies during a mission: the mission fails for everybody; false = as before
    static inline bool ms_bShopClerkFail = true;
    // one perf line a minute in the mirror log (memory, pools, FPS); false = nothing logged
    static inline bool ms_bPerfTelemetry = true;
    // weapon models asked for by GiveWeaponByPacket are released after use; false = pinned KEEP_IN_MEMORY as before
    static inline bool ms_bWeaponModelRelease = true;
    static inline bool ms_bRecruitDismissSync = true;
    // recruit-all-players: every player may recruit Grove members (the vanilla "group stuff disabled" flag a script left
    // on a guest is cleared while no mission is running); false = as before
    static inline bool ms_bRecruitAllPlayers = true;
    static inline bool ms_bPoolTwoPlayers = true;
    static inline bool ms_bEmotes = true;
    static inline bool ms_bGpsRoute = true;
    static inline bool ms_bMenusEstiloSa = true;
    static inline bool ms_bHudHideAllF2 = true;  // hud-hide-all-f2: F2 hides all the HUD (game + mod)
    static inline bool ms_bGarageSync = true;
    static inline bool ms_bCoopMissionMachines = true;
    static inline bool ms_bActivityChallenge = true;
    static inline bool ms_bCutscenePartners = true;
    static inline bool ms_bSceneGuestsVisible = true;
    static inline bool ms_bCutscenePartnerSmooth = true;
    static inline bool ms_bJustBusinessBikeEscort = true;
    static inline bool ms_bJustBusinessNoMarkerWait = true;
    static inline bool ms_bCutsceneHideExtras = true;
    static inline bool ms_bMissionGuestCounts = true;
    static inline bool ms_bMissionGuestActions = true;
    static inline bool ms_bCoopPhotos = true;
    static inline bool ms_bCoopHomeInvasion = true;
    static inline bool ms_bHighStakesCoop = true;
    static inline bool ms_bHighStakesGuestNotice = true;
    static inline bool ms_bGuestClearAreaKeepCar = true;
    static inline bool ms_bHighStakesNoGridWait = true;
    static inline bool ms_bHighStakesGuestBlips = true;
    static inline bool ms_bHighStakesBoard = true;
    static inline bool ms_bHighStakesHideNativeHud = true;
    static inline bool ms_bHighStakesFinishNotice = true;
    static inline bool ms_bHighStakesEndCleanup = true;
    static inline bool ms_bHighStakesPassenger = true;
    static inline bool ms_bHighStakesSeatPresence = true;
    static inline bool ms_bHighStakesRestoreSeat = true;
    static inline bool ms_bCutsceneKeepSeat = true;
    static inline bool ms_bHighStakesTeamResult = true;
	// NPC root tasks and the animations that the position sync can't reproduce (falls, cower, phone, sitting...)
	static inline bool ms_bNpcTaskSync = true;
	// D: animations of interiors with a name (dancing, bar, strip, casino, food...) plus the animation-block-by-name
	// and the periodic resend fixes; it depends on npc-task-sync
	static inline bool ms_bNpcNamedAnimSync = true;
	// a copy whose synced pose finished on its last frame (sat down, eating) keeps it: the idle was put on top and
	// the diners of an interior stood on their chairs for the players that came in after its owner (testeo 14-11)
	static inline bool ms_bNpcSyncedPoseKeep = true;
	// D: an interior handed over to us when its owner leaves waits 3 s for the take-overs before showing (and
	// hosting) our own hidden population
	static inline bool ms_bInteriorHandover = true;
    static inline bool ms_bInteriorPopulationSync = true;
	// Keep authority mission peds and defer occupied interior access closures.
	static inline bool ms_bMissionInteriorSafety = true;
	// ambient peds/vehicles the game removes are handed over to another client (B2) instead of being deleted for
	// everyone; with it off the game removal keeps sending PED_REMOVE/VEHICLE_REMOVE as before
	static inline bool ms_bHandover = true;
	// C1: the ambient peds and traffic of the main map are split between the players by distance instead of
	// letting only the player with the lowest id generate (see CStreamingSync::IsPointMine)
	static inline bool ms_bZonePopulation = true;
	// C1: non-player peds allowed within 80 m of the local player before CPopulation::Update stops generating
	static inline int ms_nZonePedBudget = 25;
	// metres: when the mission script moves the authority, the players already this near (same area) are not teleported
	// to him; 0 = everyone is (the old gather)
	static inline int ms_nMissionGatherSkipNear = 20;
	// C2: a player with a wanted level always generates his own pursuit cars, even where another player generates
	// the ambient traffic (and his NumLawEnforcerCars cap counts only the police cars he syncs)
	static inline bool ms_bPolicePerPlayer = true;
	// without stars, a random patrol car (popcycle cops) is not made when another police car is already around the local
	// player (copies of the other players' included): the coop traffic kept 2-4 of them around (testeo 14-11)
	static inline bool ms_bRandomCopsCap = true;
	// F: side missions can be played cooperatively. Off = today's behaviour: the blips a mission script creates
	// are treated as static world blips (the host sends them and a snapshot clears them)
	static inline bool ms_bCoopSideMissions = true;
	// with the original scripts every player keeps his own map icons (contacts, coordinate blips of his own game): the
	// host's world snapshot only brings the blips of the coop mission they play. Off = the snapshot replaces them all
	static inline bool ms_bOwnStaticBlips = true;
	// with own-static-blips, a guest playing coop sees the host's contact points (icon and yellow marker): his own ones the
	// host lacks are hidden, the host's he lacks are made. Off = the guest only sees his own contacts
	static inline bool ms_bContactsFollowHost = true;
	// a mission teleport into the authority's interior shows that interior and loads it (as an entrance does) before the
	// move; off = only the ped's area changes (he fell out to the street with the interior's empty view)
	static inline bool ms_bTeleportLoadInterior = true;
	// G: after losing the connection (timeout / server drop, not a kick nor an intentional disconnect) the client
	// retries every 5 s for 2 minutes instead of ending the session (off = today's behaviour)
	static inline bool ms_bAutoReconnect = true;
	// G: the server already announces the connects in the chat of everyone: when on, the client does not print its
	// own (duplicated) connect line
	static inline bool ms_bClearMessages = true;
	// the guests draw the mission's red cylinders exactly as the script authority's game draws them (CMissionAreaMarkers)
	static inline bool ms_bNativeMarkers = true;
	// mission enemies sent after a player go after the nearest player of the mission (CMissionHunters)
	static inline bool ms_bMissionHunters = false;
	// the blips the script authority's mission creates reach the other players as they are (CMirrorBlips, mirror layer M2);
	// the Coop.*Blip* commands then send nothing
	// track-mirror train sync (CTrainSync): the host simulates every train, the others hold copies
	static inline bool ms_bTrainSync = true;
	// rail-distance sync refinements (CTrainSync): 10 Hz state, elapsed-time jump test, copy never pushed backwards
	static inline bool ms_bTrainSyncRail = true;
	static inline bool ms_bMirrorBlips = true;
	// phase 4: the synced scripts and the teleports to the host decided in C++ (CMissionSyncRules)
	static inline bool ms_bMissionSyncRules = true;
    static inline bool ms_bIntroIndependent = true;
    // hints in the game's own style (help box, mission text) for the guests at launchers and for the mod's mission notices
    static inline bool ms_bNativeHints = true;
    // the authority's "waiting for ..." notices in the help box of every player of the mission, not in the chat
    static inline bool ms_bMissionWaitNotices = true;
    // texts the scripts print from a local string variable (PRINT_NOW 237@s) are synced too
    static inline bool ms_bSyncLocalTextVars = true;
	// a small fixed place of the mission with no marker (the pizza counter in Ryder) waits until every player of the
	// mission is in the same place as the authority (same interior, or outside); off = only the way out of an interior
	static inline bool ms_bMissionWaitSamePlace = true;
	static inline bool ms_bMissionWaitOnEntry = true;
	// the authority holds a cutscene / scripted scene (START_CUTSCENE, widescreen on) until every player of the mission is
	// in the same place (same interior, within range); off = scenes start as before
	static inline bool ms_bMissionWaitScenes = true;
	// the hold starts at the first command that prepares a scene (fade to black, interior select, load cutscene, controls
	// off), not at START_CUTSCENE; off = only START_CUTSCENE / widescreen
	static inline bool ms_bMissionScenePrelude = true;
	// the authority's script stops while he is in an interior that the other players of the mission have not entered yet
	static inline bool ms_bMissionHoldOnEntry = true;
	// no guest is pulled into an interior that has a door (only Home Invasion's script-made entrance); testeo 13-11
	static inline bool ms_bMissionEntryNoTeleport = true;
	// story missions never put a guest in a vehicle (no walk-in, no warp) and park at most one guest car per mission
	static inline bool ms_bMissionNoAutoBoard = true;
	// a scene the script starts after taking the player's control away is not held (nobody can walk to it; SWEET1)
	static inline bool ms_bMissionSceneSkipNoControl = true;
	// a guest loads the cutscene's text table itself when its subtitle keys do not resolve
	static inline bool ms_bCutsceneTableFallback = true;
	// the intro chain (INT: Big Smoke, Ryder) follows the host's contact blips; it has no guest launcher
	static inline bool ms_bIntroFollowHost = true;
	// a guest drops every blip mirrored from the host when the host's mission ends (none stays on the radar or over CJ)
	static inline bool ms_bMirrorBlipsClearOnEnd = true;
	// a guest removes his own empty ambient car that sits inside a mission car copy of the host (Tagging Up Turf twin car)
	static inline bool ms_bMissionCarOverlapDedupe = true;
	// at the end of a scripted scene the local guest stays next to the authority (safe spot), not at his old position
	static inline bool ms_bSceneGuestEndNearHost = true;
	// Catalyst: guests are attached to the caboose beside the owner so that they also throw crates from the moving train
	static inline bool ms_bMissionGuestTrain = true;
	// original BARB: another player in the chair makes the local player wait
	static inline bool ms_bMissionShopOneAtATime = true;
	static inline bool ms_bMissionAnyPlayerInCar = true;
	static inline bool ms_bMissionAnyPlayerTrigger = true;
	static inline bool ms_bMissionAllyNoGuestDamage = true;
	static inline bool ms_bMissionAllyFollowArea = true;
	// story-script weapons for each participant
	static inline bool ms_bMissionPlayerEquipment = true;
	static inline bool ms_bMissionNinesAmmoRepair = true;
	static inline bool ms_bGuestStoryLaunchers = true;
	static inline bool ms_bMissionNinesFarBoarding = true;
	static inline bool ms_bMissionNinesBoardNear = true;
	static inline bool ms_bMissionEquipmentScriptAmmo = true;
	static inline bool ms_bMissionGuestWeaponParity = true;
	static inline bool ms_bRecruitBoardPassenger = true;
	static inline bool ms_bMissionAudioGuard = true;
	static inline bool ms_bMissionHoodDoorOpen = true;
	static inline bool ms_bRemoteBulletNoForce = true;
	// the syncer of an NPC decides its health, its death and its hits on players (see CPedDamageSync)
	static inline bool ms_bPedDamageAuthority = true;
	// SET_CHAR_ONLY_DAMAGED_BY_PLAYER (bInvulnerable) lets any coop player hurt the NPC, not only player 0
	static inline bool ms_bPedDamageAnyPlayer = true;
	// the copy of an NPC keeps its gun task under control every frame, so it aims and plays its shots
	static inline bool ms_bNpcAimSync = true;
	// the copy of an NPC shooting from a vehicle plays the drive-by (its shots only drew bullets out of the window)
	static inline bool ms_bNpcDriveBySync = true;
	// PED_ONFOOT carries the move speed and the copy of an NPC on foot aims at the position extrapolated by it
	static inline bool ms_bNpcVelocitySync = true;
	// the copy of an NPC on foot plays the same set of animations as in the game of its syncer (PED_ANIM_SET)
	static inline bool ms_bNpcAnimMirror = true;
	// the copy of a player plays the action animations (scripts, shops, sitting...) of his CJ (PLAYER_ANIM_MIRROR)
	static inline bool ms_bPlayerAnimMirror = true;
	// a vehicle blip goes away for the player inside that vehicle (mirrored blips and the guests' parked copies)
	static inline bool ms_bGuestCarBlipHide = true;
	// chat commands /tpm (to the map waypoint) and /fijar (stick to the vehicle you stand on)
	static inline bool ms_bChatTpmFijar = true;
	// the CoopSA player list on TAB (logo, money, ping in ms); off = the old centered list. Money goes as PLAYER_MONEY
	static inline bool ms_bPlayerListCoopSa = true;
	// an ambulance / fire truck for an accident or a fire is generated by the player that owns that point only
	static inline bool ms_bEmergencyDedupe = true;
	// log of duplicated peds / emergency vehicles and of the cops around (diagnostic, see CSpawnWatch)
	static inline bool ms_bSpawnWatch = true;
	// a player pulled out of a car by another player goes through the game's own dragged out task
	static inline bool ms_bPlayerJackNative = true;
	// nearby mission participants conservatively count as seeing an entity
	static inline bool ms_bMissionSharedOnScreen = true;
	// IS_CAR_ON_SCREEN of a mission car counts guests up to 250 m (INTRO1 Ballas car rubber band, testeo 13-11)
	static inline bool ms_bMissionWideCarOnScreen = true;
	static inline bool ms_bMissionScriptedBoarding = true;
	static inline bool ms_bMissionOgLocBikes = true;
	static inline bool ms_bMissionOgLocGuestCar = true;
	static inline bool ms_bMissionDriveThruGuestCar = true;
	static inline bool ms_bCoopDriveBy = true;
	static inline bool ms_bCoopLosSepulcros = true;
	static inline bool ms_bCoopRunningDog = true;
	static inline bool ms_bCoopWrongSideTracks = true;
	static inline bool ms_bCoopRobbingUncleSam = true;
	static inline bool ms_bCoopRobbingGuestDeliver = true;
	static inline bool ms_bObjectMovingNoCollide = true;
	// OG Loc / Drive-Thru (2nd car): the guests are put inside the parked guest car (driver + passengers), even with mission-no-auto-board
	static inline bool ms_bMissionGuestCarBoard = true;
	// Drive-Thru: the guests' car is parked after the first cutscene (next to the mission Greenwood), not when the host drives off
	static inline bool ms_bMissionDriveThruFirstCar = true;
	// trip skip (GameLogic SetUpSkip): when the host accepts it the guests are moved to where he ends up, with seats or one shared car
	static inline bool ms_bMissionTripSkipGuests = true;
	// Catalyst (RYDER3): one extra car of the mission's model for the guests to drive; off = the old train ride (mission-guest-train)
	static inline bool ms_bMissionCatalystGuestCar = true;
	static inline bool ms_bCoopJustBusiness = true;
	// MUSIC2: a bike for each guest for the escape from Madd Dogg's mansion
	static inline bool ms_bCoopMaddDogg = true;
	// DRUGS3: the boss chase (escape at 200 m, his pace at 40/60 m) counts the nearest mission player, not only the host
	static inline bool ms_bCoopGrayImports = true;
	// LA1FIN2: the Bravura's free seat and one shared car for the guests on the drive to Sweet
	static inline bool ms_bCoopGreenSabre = true;
	// TWAR7: Freddy does not escape while a guest still chases him (250 m)
	static inline bool ms_bCoopOgLocEscape = true;
	// BCRASH1: the witness's line of sight, escape (100/25 m), flee (10 m) and his body (100 m) count every mission player
	static inline bool ms_bCoopBadlands = true;
	// the countryside: guest vehicles (CAT1 quads, CAT2 bikes, CAT3/TRUTH1/TRUTH2 shared car), TRUTH1's combine driven by
	// any player, TRUTH2's plants burnt by a guest's fire
	static inline bool ms_bCoopCountryside = true;
	// a mission vehicle only damaged by the player (SET_CAR_ONLY_DAMAGED_BY_PLAYER) takes a coop player's reported damage
	static inline bool ms_bVehicleDamageAnyPlayer = true;
	// a guest rides as a gunner on the car the coop mission's owner is attached to (CMissionCarGunners)
	static inline bool ms_bCoopCarGunners = true;
	// mission boarding: the guests the host's car has no seat for share one 4-door car next to it
	static inline bool ms_bMissionBoardingSharedCar = true;
	// the mission's on-screen countdown stands still while the players wait for each other (CMissionHints)
	static inline bool ms_bMissionWaitPauseTimer = true;
	// TWAR7: Freddy's pace follows the mission's nearest player, not only the host
	static inline bool ms_bCoopOgLocSpeed = true;
	// VS race: live standings box of the players (CVsRace::DrawBoard)
	static inline bool ms_bVsRaceBoard = true;
	// VS race lobby offer/wait text in the game's help box style (top left) instead of the ImGui window
	static inline bool ms_bVsLobbyHelpBox = true;
	// MUSIC3: guests take the Elegant's free front seat, the rest one shared car, when the band boards it
	static inline bool ms_bCoopManagementIssues = true;
	static inline bool ms_bMissionGuestCarCopies = false;
	static inline bool ms_bMissionScriptedExit = true;
	// phase 6: the game's original main.scm is used (no Coop.* lines) and the C++ rules do what our scripts did. Read by
	// CPatch from <game>\CoopAndreas\coopandreas.ini (vanilla-scripts), before any script loads
	static inline bool ms_bVanillaScripts = false;
	// the pickups the script authority's mission creates reach the other players; collecting one counts for the
	// authority's script (CMirrorPickups)
	static inline bool ms_bMirrorPickups = true;
	// a mission weapon pickup collected by one player also arms the others (Burning Desire's extinguisher)
	static inline bool ms_bMissionPickupWeaponShare = true;
	// paramedic coop: a patient boards the ambulance of whichever player of the mission gets it (CPatientHandover)
	static inline bool ms_bPatientHandover = true;
	// paramedic coop: a guest's ambulance next to a patient counts for AMBULAN although the owner is far away (the
	// original script only looks at the owner); needs patient-handover
	static inline bool ms_bAmbulanceGuestPatients = true;

	// sync clock, see ms_bPreciseTimers
	static uint32_t GetSyncTime();

	static void Init();
	static void Save();
	// server-client-config: the switches the server imposes for this session (its [clientes] section). They win over
	// the file when read; Save keeps the player's own values in the file. An empty list goes back to the file.
	static void ApplyServerOverrides(const std::vector<std::pair<std::string, bool>>& overrides);

private:
	static inline std::unordered_map<std::string, bool> ms_umServerOverrides;
	// the file's own value of each imposed switch ("\x01" = not in the file), written back by Save
	static inline std::unordered_map<std::string, std::string> ms_umOverriddenFileValues;
};