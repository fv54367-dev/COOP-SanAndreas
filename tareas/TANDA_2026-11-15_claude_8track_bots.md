# TANDA 2026-11-15 — 8-Track: modos 1-2-3 y rechazo sin carrera (HECHO 14-11, sin probar)

Pedido (14-11): en la carrera (8-Track y demás CPRSOLO) elegir con 1 = VS solo jugadores, 2 = NPC + jugadores, 3 = solo. Si se ofrece VS y todos rechazan (o nadie llega), la carrera NO arranca (hoy arranca sola con NPC).

Hallazgo: con `vanilla-scripts=true` el CPRSOLO original no llama a `Coop.ChooseSideMissionMode` (0x1D29): `CVsLobby` nunca se usa en ese modo. Solo los scripts coop (`scm/scripts/CPRACE.txt`, `:CPRACE_COOP_CHOOSE` justo después de `script_name 'CPRSOLO'`) lo llaman.

Diseño:
1. Retener el CPRSOLO original: en `CVsLobby::Init` (gameProcessEvent.after) buscar el script de misión `CPRSOLO` (no CPRACE/High Stakes); mientras `CVsLobby::Choose(script)` dé false, `script->m_nWakeTime = CTimer::m_snTimeInMilliseconds + 100`. El primer frame ya corrió `script_name` + `wait 0` (mismo punto que el coop). Interruptor `vs-lobby-vanilla=true`.
2. Teclas en `ROLE_DECIDING`: 1/Y = VS jugadores, 2 = VS con NPC, 3/N = solo (`GetAsyncKeyState` con `CTestMode::IsGameWindowFocused`). Textos de `DrawHelpBox`/`Draw`.
3. Paquete `RaceVsLobby`: campo `uint8_t mode` (0 jugadores, 1 con NPC) en INVITE y GO; server lo reenvía igual. Protocolo 0.4.55. Testbot: chequeo del campo.
4. Rechazo: en `ROLE_WAITING`, "nadie aceptó"/"nadie llegó" -> nuevo `ROLE_ABORT`; en `Process` (fuera del script) emular RETURN del gosub del cuerpo: `if (s->m_nSP) s->m_pCurrentIP = s->m_apStack[--s->m_nSP]; s->m_nWakeTime = 0;` -> el script sigue a su limpieza normal (53@ == 0: fundido y CJ al lado del marcador) sin carrera. Mandar CANCEL; el invitado ARRIVED también aborta (hoy "corres solo").
5. Modo NPC en `CVsRace::HideNpcRacersAndSeparate`: el dueño NO oculta sus NPC y oculta las copias de NPC de otros juegos (vehículo de red que no sincroniza este juego); los invitados ocultan solo sus NPC locales y dejan las copias del dueño. Ubicación: el dueño no se mueve; los demás van detrás de la grilla (rama "no free grid slot"). Accesores `CVsLobby::WithNpcs()`, `OwnerId()`.
6. Riesgo: no se verificó que los autos de carrera NPC del dueño se sincronicen a los invitados; si no, en modo 2 los invitados no ven NPC (anotarlo al probar).

## Hecho
- El dueño elige 1/2/3 (sin Y/N); el invitado 1 = sí / 2 = no. `RaceVsLobby.mode` (protocolo 0.4.55).
- Rechazo o nadie llega: CANCEL y `ROLE_ABORT` -> el script vuelve del gosub del cuerpo a su limpieza (log `vs-lobby-abort`).
- Modo NPC en `CVsRace`: el dueño conserva sus NPC, los invitados ven las copias del dueño y arrancan detrás (log `vs-race-npc-behind`).
- El punto 1 (retener el CPRSOLO original) NO se hizo: el usuario confirmó que el menú de oferta ya aparece en el 8-Track.
- Con 3+ jugadores un invitado podría ver también copias de NPC de otro invitado (si su juego las sincroniza).
