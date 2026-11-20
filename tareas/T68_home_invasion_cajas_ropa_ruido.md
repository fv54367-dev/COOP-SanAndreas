# Tarea T68: Home Invasion, segunda parte (cajas, entrega, balaclava, ruido)
Leé `tareas\COMUN_reglas.md`, `AGENTS.md`, `tareas\T64_home_invasion_coop.md` (el pedido completo) y **`tareas\TANDA_2026-11-10_codex_t64.md`** (lo que ya se hizo y todo lo que se encontró en GUNS1: offsets de holding BD021/BD119/BD63F, entrega LOCATE BD1DE + botón BD264, ruido BAD5A/BAD66, estado único 36@/60@, `tools\inspect_t64.py`). No toques `main.scm` ni `scm/`.

## Qué ya está
Los blips de las cajas siguen al objeto en el invitado (T64, `coop-home-invasion`). **Falta todo lo demás de T64**; hacelo ahora, con el mismo interruptor.

## Diseño que tenés que respetar (la duda de T64)
GUNS1 lleva **una sola caja activa** (36@/60@). No hace falta que el script maneje varias a la vez:
- Cada jugador puede levantar una caja **en su juego** (la caja es objeto de misión con identidad `CObjectSync`; quien la levanta pasa a ser su dueño y los demás la ven en sus manos). Dos que agarran la misma: gana el primero que el **dueño de la misión** confirme.
- **Entrega serializada:** cuando un invitado llega a la furgoneta con su caja y aprieta el botón, se lo manda al dueño de la misión; el dueño, en el momento en que el script pregunta por la entrega (BD1DE/BD264) y **no** hay una entrega propia en curso, responde "sí" por ese invitado (vía T63) y deja que el script haga su tramo BD271–BD697 (que no mueve a CJ: actualiza 35@/50@/48@/47@, Ryder y borra la caja). Para que el tramo borre la caja correcta, poné en 36@/60@ la caja del invitado justo antes y restaurá la del dueño después (si el dueño tenía una en la mano). Si hay cola, de a una por vez.
- Si serializar no se puede hacer limpio, **frená y explicá en la tanda** qué impide hacerlo (no lo fuerces).

## Además
- **Balaclava y sigilo** en el invitado mientras dure GUNS1 (ropa de misión → marcha sigilosa del juego); al terminar, fallar o abandonar, vuelve su ropa propia (regla: ropa de cada uno). Referencia MIT: `_refs\sa-coop-karimov\src\client\features\story\missions\Missions.cpp` (~743).
- **Ruido propio** del invitado (~1421 en la misma referencia): su barra; si se llena, se le avisa al dueño y su script reacciona como con su propio ruido (BAD5A/BAD66).

## Casos borde
Los de T64 (3–4 jugadores, caja en la mano al desconectarse/morir, individual, fin de misión con ropa restaurada y objetos/blips borrados en todos).

## Aceptación
Compilan client/server/testbot (si hay protocolo, la versión siguiente a la de `shared/config.h`; paquetes al final). CRLF. Cita MIT. Completá las 4 pruebas de GUNS1 en `TESTEAR_HOY.md`, `INTERRUPTORES.md`, `docs/MISIONES_MODO.md`, la tanda de T64. No instales. Resumen ≤10 líneas.
