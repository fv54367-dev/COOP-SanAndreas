# INDICE — pregunta -> archivo

> Para encontrar algo sin leer los .md grandes. Los .cpp son de `client/src/` (server: `server/src/`); interruptores en `INTERRUPTORES.md` (grep del nombre); tandas en `tareas/`; cómo funciona: `docs/SISTEMAS.md`. Pruebas y resultados en juego en [TESTEAR_HOY.md](../TESTEAR_HOY.md). Contexto técnico en [ESTADO.md](../ESTADO.md). Reglas: `AGENTS.md`.

## Misiones (reglas, esperas, invitados)
| Pregunta | .cpp | Interruptor | Tanda / doc |
|---|---|---|---|
| ¿Quién arranca/espera una misión, progreso distinto, marcadores? | `CMissionSyncRules`, `CMissionLock`, `CMissionAreaMarkers`, `CLaunchManager`; server `CMissionLock`, `CCoopMission` | `mission-sync-rules`, `mission-wait-scenes` (lanzadores: sin interruptor, `vanilla-scripts`) | `T43_*`, `T44_*`, `docs/MISIONES_MODO.md`, `docs/FASE4_ANALISIS.md` |
| ¿Cómo se replica el script del dueño? | `COpCodeSync`, `CMissionMirror`, `CMirrorBlips`, `CMirrorPickups`, `CCameraMirror` | `mirror-blips`, `mirror-pickups` | `docs/PLAN_CPP.md` |
| Avisos de espera y textos nativos | `CMissionHints`, `CMissionSceneWait` | `native-hints`, `mission-wait-notices`, `sync-local-text-vars` | `TANDA_2026-11-12_claude_prompts.md` |
| Lo que hace el invitado cuenta (daño, disparo, fotos, acciones) | `CMissionGuestCounts`, `CMissionPhotos`, `CMissionGuestActions` | `mission-guest-counts`, `coop-photos`, `mission-guest-actions` | `T63_*`, `T65_*`, `T66_*` |
| Máquinas (torreta, RC, carretilla, grúa, Cop Wheels) | `CMissionMachines`, `CMissionObjectMoves`, `CMissionWarTable` | `coop-mission-machines` | `T67_*`, `T70_*`, `T75_*`, `TANDA_2026-11-12_claude_abiertos.md` |
| Autos de invitados, "solo el host maneja" | `CMissionSyncRules` (`ParkSharedGuestCar`), `CMissionGuestActions` | `mission-guest-vehicles-self-board`, `mission-guest-car-sections` | `T40_*`, `TANDA_2026-11-11_claude_beach.md` |
| Drive-By / Los Sepulcros / Running Dog / Wrong Side / Just Business | `CMissionSyncRules`, `CJustBusinessEscort` | `coop-drive-by`, `coop-los-sepulcros`, `coop-running-dog`, `coop-wrong-side-tracks`, `coop-just-business` | `T92_*`..`T96_*` |
| Home Invasion (cajas, ruido, entrada) | `CHomeInvasion` | `coop-home-invasion`, `mission-hold-on-entry` | `T64_*`, `T69_*`, `T71_*`, `T74_*`, `T91_*` |
| Life's a Beach, César, High Stakes | `CCoopLowrider`, `CHighStakesRace` | `coop-dance`, `dance-team-score`, `coop-lowrider`, `high-stakes-coop` | `T53_*`, `T54_*`, `T88_*`, `T89_*`, `TANDA_2026-11-12_codex_dance_team_score.md` |
| Secundarias (Paramédico, Vigilante, Bombero) | `CSideMissionRules`, `CCoopMission`, `CPatientHandover` | `coop-side-missions` | `docs/DISENO_F_SECUNDARIAS.md`, `T19_*`, `T20_*` |
| Carrera VS, Mensajero | `CVsRace`, `CVsLobby`, `CMinigameVs`, `CMirrorRaceCheckpoints` | `vanilla-scripts` (el VS es del modo coop) | `ESTADO.md` (diagnósticos técnicos) |

## Cinemáticas y escenas
| Pregunta | .cpp | Interruptor | Tanda / doc |
|---|---|---|---|
| Compañero en cinemática, mocap, rutas | `CCutscenePartners`, `CScenePartner`, `CSceneMocap`, `CSceneScript`, `CSceneGame` | `cutscene-partners`, `cutscene-partner-smooth`, `scene-guests-visible` | `T58_*`, `T86_*` |
| CJ/ropa propia en cinemática | `Hooks/CutsceneClothesHooks` | `player-full-rebuild` | `TANDA_2026-11-10_cinematicas.md`, `T84_*` |
| Puerta del mapa, objetos ocultos, saltar escena | `CCutsceneHidden`, `CCutsceneSkipVote` | `cutscene-hide-extras` | `T87_*` |
| Intro / partida nueva | `CMissionLock` | `intro-independent` | `T81_*` |

## NPC, jugadores y sync
| Pregunta | .cpp | Interruptor | Tanda / doc |
|---|---|---|---|
| Peds/NPC (dueño, copia, daño, animaciones, velocidad) | `CNetworkPed`, `CNetworkPedManager`, `CPedDamageSync`, `CAimSync`; `PacketHandlers/peds.cpp` | `ped-damage-authority`, `npc-anim-mirror`, `npc-velocity-sync` | `TANDA_2026-11-10_sync_visual.md`, `docs/DISENO_BCD_POBLACION.md` |
| Policías duplicados, ambulancias | `CSpawnWatch`, `Hooks/CCarPedGenHooks` | `spawn-watch`, `emergency-dedupe` | `T50` en `TANDA_2026-11-10_sync_visual.md` |
| Interiores (población, puertas, marcadores) | `CInteriorOwnership`, `CEntryExitMarkerSync` | `interior-population-sync`, `interior-handover`, `mission-interior-safety` | `T83_*`, `T18_*` |
| Jugador remoto (animaciones, ropa, revivir) | `CPlayerAnimMirror`, `CNetworkPlayer`, `CNetworkPlayerManager`, `CPersistenceSync` | `player-anim-mirror`, `remote-revive-auto` | `TANDA_2026-11-12_claude_abiertos.md` |
| Robo de vehículo | `CJackSync`, `CPassengerEnter` | `player-jack-native` | `T72_*`, `T77_*` |
| Vehículos y trenes | `CNetworkVehicle`, `CNetworkVehicleManager`, `CTrainSync` | `train-sync`, `train-sync-rail` | `T62_*`, `T76_*`, `T80_*` |
| Reclutas, pandillas | `CGangSync` | `recruit-dismiss-sync` | `TANDA_2026-11-12_codex_pendientes_coop.md` |
| Blips propios del mapa | `CNetworkStaticBlip`, `CNetworkEntityBlip` | `own-static-blips` | `TANDA_2026-11-12_claude_abiertos.md` |
| Armas, memoria, FPS | `CUtil` (`GiveWeaponByPacket`), `CPerfWatch` | `weapon-model-release`, `perf-telemetry` | `ESTADO.md` (caída de FPS) |
| Crashes y congelamientos | `CCrashLog`, `CHangWatchdog`, `Hooks/CrashfixHooks` | — | `AGENTS.md` (trampas), `docs/SISTEMAS.md` (evidencia) |

## Mundo, UI y extras
| Pregunta | .cpp | Interruptor | Tanda / doc |
|---|---|---|---|
| Garajes | `CGarageSync` | `garage-sync` | `T90_*` |
| GPS, TP con pedido, animaciones F9 | `CGpsRoute`, `CSocialActions`, `CChatCommands` | `gps-route`, `tp-request`, `emotes` | `T57_*`, `T60_*`, `T78_*` |
| Billar, desafíos, voz | `CCoopPool`, `CActivityChallenge`, `CVoice` | `pool-two-players`, `activity-challenge`, `voice-chat` | `T55_*`, `T56_*`, `T59_*` |
| Menús (F1, F8, F9), chat | `UI/`, `CCustomMenuManager`, `CChatCommands` | `cheat-menu-all`, `menus-estilo-sa` | `TANDA_2026-11-12_claude_prompts.md` |
| Guardado, progreso por jugador, coleccionables | `CSaveMenu`, `CStoryProgress`, `CCollectibleSync`; server `CSaveManager`, `CStorySave` | — | `docs/SISTEMAS.md` |
| Conexión (UPnP, código), server | server `CInternet`, `ConfigManager` | `upnp` | `T61_*` |
| Launcher CoopSA | `launcher/src/` (`main.rs`, `update.rs`) | — | `TANDA_2026-11-11_glm_launcher_rust.md` |
| Compilar, instalar, testbot, SB4 | `tools/` | — | `WORKFLOW.md`, `AGENTS.md` |

## Otros documentos
Lo que quiere el dueño: `docs/REQUISITOS.md`. Rumbo: `docs/PLAN_CPP.md`. Licencias/otros proyectos: `docs/REFERENCIAS_OPENSOURCE.md`, `NOTICE.md`. Auditorías: `tareas/AUDIT_*` y `tareas/archivo/`. Tandas viejas (antes del 2026-11-08): `tareas/archivo/`.
