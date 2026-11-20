# Tanda 2026-11-10 Sonnet T88: César coop completo

Parte de un WIP de Codex (sin cuota). Revisado: compila client/server/testbot; se conservó todo.

## Qué cambia
- `client/src/CCoopLowrider.cpp/.h`, `CMissionMirror.cpp` (hook `AfterScripts`), `server/src/PacketHandlers/scripts.cpp`, `shared/network/packets/scripts.h`, `shared/config.h` (0.4.42-alpha, único bump).
- COOP_LOWRIDER kind 3: el dueño emite cada 200 ms posición/color del lowrider, apuesta y árbitro del garaje. No crea sesión de concurso en el servidor.
- Subtítulos: `AUDIOL` llama a PRINT_NOW (0x00BC) con clave `LOWR_*`; el dueño la reenvía como AddMessageGXT.
- Apuesta: el invitado dibuja el panel con las variables 274/275/276 del dueño (se dibuja tras los scripts).
- Auto: cada invitado crea su lowrider+chica en un lugar libre (suelo 0x5696C0 y TestSphereAgainstWorld), por slot ordenado de ids; el concurso lo reutiliza.
- Garaje: el dueño elige un jugador a <23 m (histéresis 28 m); los demás tienen MODLAST desactivado y ven "Garaje ocupado".
- Concurso: TIME se envía siempre (reloj), el invitado sale si se baja del auto.

## Casos borde
3-4 jugadores: slots separados; F1/`coop-lowrider=false`: nada; auto destruido: se cancela su concurso; desconexión/fin: ClearPreparation borra auto, chica, blip y reactiva garaje; interior/lejos (>100 m): no se entrega.

## Citas gta-reversed
CWorld::TestSphereAgainstWorld, CWorld::FindGroundZFor3DCoord (0x5696C0), CGarages (ACTIVATE/DEACTIVATE_GARAGE 0x0299/0x02B9), CRunningScript opcodes.

## Sin probar / dudas
No se verificó en juego por qué faltaba la chica del invitado (se la entrega ahora con el auto). Separación entre autos ~4,8 m, no 2 m, por seguridad.
