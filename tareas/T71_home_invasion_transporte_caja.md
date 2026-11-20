# Tarea T71: Home Invasion, el invitado lleva y carga la caja (lo que faltó de T69)
Leé `AGENTS.md`, `tareas\T69_home_invasion_caja_unica.md` y **`tareas\TANDA_2026-11-10_codex_t69.md`** ("Qué no quedó resuelto", puntos 1–4). Ropa, sigilo y ruido ya están (`client/src/CHomeInvasion.*`): no los rehagas. No toques `main.scm` ni `scm/`. Interruptor `coop-home-invasion`.

## Cómo resolver cada punto (decisión del orquestador; portá `_refs\sa-coop-karimov\src\client\features\mission-objects\Objects.cpp`, MIT: `Held`, `TakeUp`, `HandleHold`, `FollowHolder`, `PartnerHolds`)
1. **Escrituras de `CObjectSync`:** la caja que lleva alguien queda marcada "en mano de <jugador>" (por identidad de red + época de la misión). Mientras tanto `CObjectSync::HandleSync`/`ObjectSync` **no escriben** matriz ni área de esa caja en ningún juego. Arbitraje: el invitado pide levantarla (`TakeUp`), el **dueño de la misión** confirma a uno solo (el primero), y recién ahí el invitado la levanta con la tarea nativa en su juego. En el juego del dueño, la caja real sigue a la mano de la copia de red del portador (`FollowHolder`, cada frame).
2. **Área que escribe el script (BAA62/BAD13):** después de esos comandos, si la caja está en mano de un invitado, el C++ le vuelve a poner el área del portador (es una corrección después del comando, no se fuerza nada del script).
3. **Soltar y borrar (BD666 / DELETE_OBJECT BD67A):** antes de que el dueño borre la caja (el borrado de un objeto en mano de otro), el portador suelta su copia con `DropEntityThatThisPedIsHolding` (gta-reversed 0x5E0360) al recibir el borrado; el borrado de red ya existente la elimina después. Desconexión, F1 individual, muerte o fallo con la caja en la mano: la caja vuelve a quedar libre donde estaba el portador (el dueño la suelta de la copia y vuelve a escribirla `CObjectSync`). Nada de punteros guardados: siempre por identidad.
4. **Ruido con el dueño afuera de la casa:** **se acepta la limitación** (no se fuerza 94@). Dejalo escrito en `TESTEAR_HOY.md` como comportamiento esperado.

Con eso, las respuestas de `CrateAnswer` (0x0737, 0x0475, 0x00FF de la furgoneta con `kGunsTruck`, 0x00E1 0 15) contestan por el portador **confirmado**, por el camino de `UpdateCompareFlag` (gancho T63).

## Casos borde
Los de T69. Además: el dueño levanta la caja mientras un invitado la tiene pedida → gana el que el dueño confirmó primero.

## Aceptación
Compilan client/server/testbot (versión siguiente a la de `shared/config.h` si hay protocolo; paquetes al final). `tools\correr_testbot.bat` TODO PASS. CRLF. Cita MIT. Pruebas de GUNS1 en `TESTEAR_HOY.md` (invitado levanta la caja, la lleva a la furgoneta y la carga; dueño carga la siguiente; dos invitados piden la misma), `INTERRUPTORES.md`, `docs/MISIONES_MODO.md`, tanda T69. Si algún punto no cierra, implementá los demás y explicá ese. No instales. Resumen ≤10 líneas.
