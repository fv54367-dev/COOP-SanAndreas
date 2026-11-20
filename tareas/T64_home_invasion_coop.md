# Tarea T64: Home Invasion (GUNS1) jugable por el invitado
Leé `tareas\COMUN_reglas.md`, `AGENTS.md`, `tareas\TANDA_2026-11-10_sacoop_karimov.md` y **lo que dejó T63** (`mission-guest-counts`, en la misma tanda y en su commit): esta tarea se apoya en esa. **No toques `main.scm` ni `scm/`**: C++ sobre scripts originales.

## Pedido del dueño (2026-11-10)
En Home Invasion el invitado no puede levantar las cajas, no ve los blips de las cajas y no camina en sigilo: la misión solo funciona para el dueño.

## Análisis previo (Codex del amigo, 2026-11-10, verificalo)
Diferencias con sa-coop (MIT) para `GUNS1`:
| Parte | sa-coop | Nuestro |
|---|---|---|
| Recoger cajas | objeto de misión con identidad de red y la propiedad "se puede levantar"; sincroniza quién la levanta, la lleva y la suelta | sin capa de objetos de misión ni dueño de cada caja |
| Entregar en la furgoneta | las comprobaciones del script reconocen al invitado que lleva la caja, su posición y su botón | el script pregunta solo por el CJ del dueño |
| Blips de las cajas | atados a la copia local de cada caja | el espejo no identifica objetos por red: blips por coordenadas |
| Sigilo | aplica la ropa de misión (balaclava) al invitado, que activa la marcha sigilosa del juego; después restaura la suya | no aplicamos ropa de misión al invitado |
| Ruido y alarma | barra de ruido propia por invitado; si se llena avisa al dueño y puede despertar al coronel | sin integración |
Código: `_refs\sa-coop-karimov\src\client\features\mission-objects\Objects.cpp` (~línea 56), `features\story\missions\Missions.cpp` (cajas y entrega ~703, ropa ~743, ruido ~1421). Leé esos tramos y lo que llaman.

**Blips:** no está claro por qué el invitado no ve ninguno: `CMirrorBlips` captura `ADD_BLIP_FOR_OBJECT` y hay registros de captura en guns1. Primero averiguá qué recibe y qué dibuja el invitado (logs `mirror-blips`, código de recepción) y arreglá esa causa.

## Cambio
Interruptor `coop-home-invasion` (cliente, default **true**, `INTERRUPTORES.md`). Apagado: como hoy.
1. Objetos de misión levantables de GUNS1 con identidad de red (reusá lo que haya: `CMirrorPickups`, sync de objetos; si hace falta una capa general de "objetos de misión", hacela reutilizable y decilo). Un solo dueño por caja a la vez (quien la levanta); los demás la ven en sus manos.
2. Entrega: con T63, la comprobación de "CJ lleva caja cerca de la furgoneta + botón" también acepta al invitado que la lleva. La caja entregada cuenta una sola vez.
3. Blips de las cajas que siguen al objeto en el invitado.
4. Ropa de misión (balaclava) en el invitado mientras dure la misión, con la marcha sigilosa; al terminar, fallar o abandonar, vuelve su ropa propia (regla: ropa de cada uno).
5. Ruido: barra propia del invitado; si se llena, se avisa al dueño y su script reacciona como con el ruido del dueño.

## Casos borde
- 3–4 jugadores: cada uno lleva una caja; dos que agarran la misma a la vez: gana uno, el otro no la recibe.
- Invitado que se desconecta o muere con una caja en la mano: la caja cae en su lugar y vuelve a poder levantarse (y, por la regla, si muere la misión falla para todos).
- Invitado en individual: no participa.
- Fin de misión: ropa restaurada, objetos y blips borrados en todos.

## Aceptación
Compilan client/server/testbot (versión si hay protocolo). CRLF. Cita MIT. 4 pruebas simples numeradas para GUNS1 en `TESTEAR_HOY.md`, `INTERRUPTORES.md`, `docs/MISIONES_MODO.md`, resumen en la tanda. No instales. Resumen ≤10 líneas.
