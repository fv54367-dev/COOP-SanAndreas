# Tarea T88: High Stakes Lowrider (César) cooperativa de verdad (sigue a T53)

Leé `AGENTS.md`, `tareas\COMUN_reglas.md`, `tareas\T53_cesar_coop_lowrider.md` y su salida `T53_cesar_coop_lowrider_salida.txt`. Código: `client/src/CCoopLowrider.*`, lo que espeja textos y HUD del host al invitado (`rx-text`, `CMissionMirror`, `CMissionGuestCounts`), script `_refs\project_gta_sa_coop\CoopAndreas\scm\scripts` (CESAR1 / lowrider), sin tocar `main.scm`.

## Qué reportó el dueño (prueba 2026-11-10 noche, `coop-lowrider` como viene)
1. **Subtítulos:** en la cinemática corta donde le dan el auto al host ("¡Con unos muelles a medida puedes irte a casa dando brincos! Pruébalo.") el invitado no ve el subtítulo.
2. **HUD de la apuesta:** en la parte de apostar, el host ve el panel "Apuesta" (esta apuesta, mínima, máxima) y el invitado no. Mostrale el mismo panel y la ayuda; si el invitado no apuesta, que vea el del host.
3. **Auto para los invitados:** cuando le dan el lowrider al host, a cada invitado se le da uno igual, a unos 2 m (lo necesario para no chocar), a la vista y con su marcador. Que no aparezca dentro de estructuras ni del terreno: usá la búsqueda de lugar libre del juego (gta-reversed `CWorld::TestSphereAgainstWorld`, `ThePaths` nodos de auto o lo que ya use el proyecto para spawnear autos de invitado).
4. **Garaje de tuneo (Loco Low Co.):** que entre un solo jugador a la vez, para que no se bugee. Los demás esperan afuera con un mensaje.
5. **La competencia de saltos con la chica:** hoy el invitado no tiene su auto con la chica de acompañante como el host. El invitado tiene que llegar con el auto que se le dio, con su propia chica sentada al lado, y participar (la idea original de T53). Averiguá por qué no se armó (logs `E:\juegos\gtasacoopnuevo\_logs_2026-11-10\J*_mirror_cesar_beach.log`).

## Casos borde
- 3–4 jugadores: un auto para cada uno, todos separados.
- Invitado en individual (F1).
- El invitado se baja del auto o lo destruye.
- Desconexión a mitad.
- `coop-lowrider=false`: igual que antes.

## Aceptación
Compilan client/server/testbot, testbot TODO PASS, CRLF. Prueba simple en `TESTEAR_HOY.md` (sección T53) y "Pendiente" de `ESTADO.md`. No instales. Resumen ≤10 líneas.
