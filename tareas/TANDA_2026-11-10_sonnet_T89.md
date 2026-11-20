# T89 (sonnet): Life's a Beach, pantalla negra en el invitado
Archivos: client/src/CCameraMirror.{h,cpp}, CCoopLowrider.cpp, COpCodeSync.cpp, CConfigLoader.{h,cpp}, INTERRUPTORES.md, TESTEAR_HOY.md, ESTADO.md. Sin cambios de protocolo (no se toca la version).

## Causa (con evidencia, SIN PROBAR en juego)
- MUSIC1 (sincronizado) hace `Camera.DoFade(0, 1000)` antes del baile (MUSIC1.txt, label 3400): el DO_FADE 016A se replica al invitado. El fade-in lo hace el hilo DANCE (`DoFade(1,500)`, DANCE.txt:171 y 465), que NO esta sincronizado y corre solo en el host. Si el invitado no entra al baile de T54, nadie le hace el fade-in: queda en negro, pero se mueve y juega (igual que reporto el dueño).
- Log J1 (host): `dance-start session=65086641` a t=65086640. Log J2 (invitado): llega `rx-text MUS1_4` a 65086671 (mismo instante), pero NUNCA hay `dance-join`; despues llegan DNCEF1 y M_FAIL (65124531/65127625) y no aparece ningun evento de fade. El unico `lowrider-join` del invitado es el de SWEET6 (T53). El motivo exacto del rechazo no se loguea en la version anterior (distancia >40 m, area, mision local...): se agrega el log.
- En el motor, `CDraw::FadeValue` es el alfa del velo (255 = negro): gta-reversed Camera.cpp CCamera::ProcessFade (0x50B5D0), Draw.cpp (dibujo del velo), CCamera::Fade (0x50AC20), DoCamFade (CameraCommands.cpp, DO_FADE 0x47C7C7). Sin `m_bFading` nadie lo reescribe: queda negro hasta cargar o morir.

## Arreglo
- Interruptor cliente `fade-guard=true` (INTERRUPTORES.md). Apagado = comportamiento anterior.
- `CCameraMirror::OnMirroredFade()`: al replicar un 016A recibido se arma la guardia (solo invitados; el autoridad nunca).
- `ProcessFadeGuard()`: si esta armada y la pantalla esta negra y quieta (FadeValue 255, sin fade en curso) durante 4 s, sin cinematica, menu, minijuego propio (T53/T54), mision local no sincronizada, ni jugador muerto/arrestado: `TheCamera.Fade(0.5, 1)` y log `fade-stuck-restore why=watchdog`. Se desarma sola cuando la pantalla queda clara.
- Camino directo: si el invitado rechaza el PRELOAD del baile, loguea `dance-skip reason=...` y restaura al instante (`why=dance-skip`) si ya esta negro; si el fade seguia en curso, lo cubre el watchdog.
- Al terminar o cancelar un baile propio del invitado tambien se arma la guardia (por si lo cortan en medio de un fade del DANCE local).
- Prioridad: baila si puede (T54), si no mira con la pantalla visible; nunca a ciegas.

## Casos borde
- 3-4 jugadores: cada invitado tiene su guardia; el que entra al baile no la dispara (OwnContest). Individual (F1): sin fades replicados, guardia sin efecto.
- Cinematica real, menu, carga, muerte/arresto, puerta/garaje: bloquean la restauracion y reinician la cuenta de 4 s. Fade a negro legitimo de mas de 4 s con mision sincronizada y control del jugador (poco comun): se levanta igual; aceptable.
- Host que se vuelve autoridad en medio: guardia desarmada. Fade-in replicado normal: la guardia se desarma sola.

## Pendiente
- Prueba en juego: ver TESTEAR_HOY.md T54 punto 6. Confirmar con el log que razon rechazo la union (`dance-skip`) para arreglar el motivo de fondo (posible >40 m del puesto de baile cuando el host arranca).
- Compilan client, server y testbot (testbot no ejecutado, sin tocar server).
