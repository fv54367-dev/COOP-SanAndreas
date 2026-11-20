# TANDA 2026-11-12 (Claude orquesta, subagentes Sonnet escriben)

Origen: `tareas/PROMPTS_2026-11-12.md` (pedidos del dueño tras probar). Todo queda **SIN PROBAR** hasta que el dueño confirme.

## Reparto (cada uno en su worktree; Claude revisa y mergea a main)
| Grupo | Puntos | Archivos probables |
|---|---|---|
| A | High Stakes Lowrider 1-7 (espera en marcador para invitados que llegan en su auto, aviso "te esperan", auto del invitado que desaparece, carrera coop sin menú VS, blips/marcadores y posición para invitados, tabla de clasificación estilo SA abajo a la derecha, final cuando llegan todos con aviso de quién falta, ícono que no desaparece al terminar) | `CHighStakesRace.*`, `CMinigameVs.*`, marcadores/espera (`CMissionLock`, `CMissionAreaMarkers`) |
| B | Just Business 1-3 (spawn dentro del interior, moto con Grove por invitado siguiendo a Smoke con 1/2/3 s de retraso, sin espera en marcadores solo en esta misión) + cinemática robótica del compañero (Wrong Side of the Tracks) | `CMissionSyncRules.cpp` (bloque de SMOKE), `CCutscenePartners.*`/`CSceneMocap.*` |
| C | Catalyst 1-3 (primera cinemática y mini-cinemáticas esperan a invitados; los dos tirando cajas arriba del tren) + Home Invasion 1-4 (esperar a todos para la cinemática del interior y para salir; sacar máscara/sigilo a invitados al terminar; recorrido de invitados en la cinemática del garaje) | `CMissionSyncRules.cpp` (bloques RYDER), espera de cinemáticas |
| D | Life's a Beach (puntaje de invitados sumado, meta 2500×jugadores) + billar F7 que no anda + F8 con TODOS los trucos | `CMissionGuestCounts.*`/reglas de LAB, billar (`CMinigameVs`/T55), `CCheatSync.*`, menú F8 |
| Claude | Launcher: botón de actualización arriba a la derecha (círculo; flecha = hay actualización, tilde = al día) y sacar "Mod cooperativo no oficial" | `launcher/src/*`, `dist/ACTUALIZAR.bat` |

## Restricciones
- CRLF en `.cpp/.h`. Comentarios en inglés, estilo vecino. Interruptor para todo sistema nuevo (`INTERRUPTORES.md`).
- Protocolo: si un grupo necesita cambiarlo, sube `shared/config.h` y lo dice; Claude renumera al mergear.
- Generalizar (memoria del dueño): esperas a 12 m y espera de cinemáticas valen para todas las misiones salvo excepción pedida (Just Business sin espera en marcadores).
- Incluir a los invitados (visibles, animados, con aviso) antes que esconderlos o excluirlos.

## Casos borde
- 3-4 jugadores: retrasos/órdenes/tabla escalan por índice de jugador.
- Uno en individual (F1): no cuenta para esperas ni puntajes.
- Desconexión o muerte a mitad: la espera no queda colgada (se recalcula con los conectados); caer = falla para todos (regla fija).
- Invitado en otro interior / otra punta: las esperas avisan quién falta.
- Interruptor apagado = comportamiento anterior.

## Resultado
(cada grupo agrega acá su resumen al mergear)

### Grupo C (Catalyst / Home Invasion), SIN PROBAR, sin cambio de protocolo
- Espera de escenas general (`mission-wait-scenes`): `CMissionSceneWait` retiene el script de la autoridad antes de `START_CUTSCENE`/widescreen hasta que todos los jugadores estén en el mismo lugar; aviso de quién falta. Cubre Catalyst 1 y 3 y Home Invasion 1 y 2 (y las demás misiones). Hook en `CommandHooks` (salida 0x469FFB de `CRunningScript::Process`, el IP no avanza).
- Catalyst 2 (`mission-guest-train`): el invitado se pega al vagón junto al dueño (T66 ya aceptaba lanzamientos de invitados sobre el vagón, pero nada lo subía).
- Home Invasion 3: `CleanGuest` quita la máscara con reintentos y devuelve el grupo de animación normal (sigilo).
- Home Invasion 4: en escena con barras, el invitado sale del vehículo de misión que el protagonista dejó (antes se quedaba en el asiento = adentro).
- **Grupo C, ajuste 2 (Claude Sonnet, SIN PROBAR, sin cambio de protocolo):** (1) Catalyst: `mission-scene-prelude`, la retención empieza en el primer comando de la escena (DO_FADE a negro, SELECT_INTERIOR, LOAD_CUTSCENE), antes de que se replique nada al invitado. Causa (hipótesis por lectura de RYDER3.txt y COpCodeSync.cpp:1523-1596): el script hace DO_FADE(negro), SetAreaVisible(2), Cutscene.Load y recién Cutscene.Start; la retención en 02E7 llegaba tarde y el invitado ya tenía replicado el negro/interior. (2) Home Invasion: `mission-hold-on-entry`, el script de la autoridad se detiene al entrar a un interior hasta que entren todos (genérico), sin teletransportar; la ventana de 30 s de CMissionSyncRules espera a los que vienen. (3) Home Invasion 2: `scene-guest-end-near-host`, causa (hipótesis): `g.saved` se tomaba tras el gather de la misión (adentro del garaje) y `ReleaseSceneGuests` lo restauraba; ahora el invitado queda junto al host.
- **Grupo D (Claude):** (1) Life's a Beach: el log del ultimo test (`dance-team-result participants=2 total=7160 required=5000`) muestra que 0e384ee SI calculaba el equipo; el hueco real era que el limite de +500 por paquete congelaba para siempre el puntaje de un invitado ante un paquete perdido/salto, y que nada mostraba la meta. Ahora sin tope en modo equipo y con avisos de meta/resultado en el chat. (2) Billar: no encontre causa estatica unica (server/protocolo pasan el testbot); agregue logs `pool-f7/join-tx/join-rx/start-rx/start-skip/offer-open` y aclare que el host no aprieta F7, quien se une si; falta log de una prueba real. (3) Trucos: F8 con los 91 trucos del juego (tabla por slot de CCheat), buscador, categorias, scroll, interruptor `cheat-menu-all`.
**Grupo B (Claude Sonnet, rama worktree-agent-ab843fd1d4a5fbe96, compila client/server/testbot, SIN PROBAR, sin cambio de protocolo):**
- JB1 spawn: `ReleaseSceneGuests` mandaba al invitado a la posicion anterior a la escena (afuera) con el area del interior; rompio cb987f3. Ahora guarda la posicion del primer ubicado y solo restaura en la misma area.
- JB2 motos: `CJustBusinessEscort` (interruptor `just-business-bike-escort`): moto + rider FAM1-3 por invitado, repite start/stop/speed/skip de la grabacion de la moto de Smoke con 1/2/3 s de retraso.
- JB3: DRUGS1 sin espera en marcadores (`just-business-no-marker-wait`).
- Cinematicas: `cutscene-partner-smooth` (HoldMoving): velocidad suavizada, sin flapping caminar/correr, cadencia por cuadro, primer clip con blend rapido. La causa exacta del "robotico" no se pudo verificar sin jugar: es una hipotesis por lectura de codigo.

### Grupo A - High Stakes Lowrider (rama del worktree agent-ae10bee59edd36d16, compila client/server/testbot, SIN PROBAR; protocolo 0.4.46-alpha)
- A1 invitado en su coche no veia la espera: `CHighStakesRace::FilterStart` ya no corta antes de calcular a quien se espera; el invitado usa `LocalStartWait`, el anfitrion `AuthorityWait` (todos ven "Esperando a X" / "Te esperan"). Interruptor `high-stakes-guest-notice`.
- A2 coche del invitado desaparecia: causa mas probable, el `clear_area` (0395) replicado borra vehiculos que no son de mision; el coche del jugador se marca de mision mientras corre el comando (general, todas las misiones). `guest-clear-area-keep-car`. Causa por lectura de gta-reversed, sin log que la confirme.
- A3 aviso "te esperan": mismo cambio que A1 (y aviso de lowrider para los de a pie en la grilla).
- A4 grilla sin espera ni oferta, blips de autos NPC para el invitado, puesto de invitados: `high-stakes-no-grid-wait`, `high-stakes-guest-blips`. No hay repro de la oferta VS: se agrego el log `high-stakes-race-not-story` con el motivo.
- A5 tabla de posiciones estilo SA abajo a la derecha: `high-stakes-board` (protocolo: filas en `STORY_RACE`).
- A6 aviso de quien falta en la meta: `high-stakes-finish-notice` (`MissionWaitNotice::RACE_FINISH`).
- A7 icono que no desaparece: CPRACE no llama a `register_mission_passed` ni sincroniza el blip; `high-stakes-end-cleanup`. No pude confirmar por que tampoco desaparece en el anfitrion (puede ser que no gano la carrera).
