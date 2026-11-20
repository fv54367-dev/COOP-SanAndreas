# Tarea T19_recompensa_vigilante: recompensa de Vigilante nivel 12 para los invitados con scripts originales
Leé `tareas\COMUN_reglas.md`, `tareas\TANDA_2026-11-08d.md`, `scm/scripts/COPCAR.txt:~2522` y `client/src/Commands/Commands/CCommandGrantOddjobRewardToNetworkPlayers.cpp`.

En el modo coop, el script agrega `GrantOddjobRewardToNetworkPlayers`. En originales nadie lo llama. Encontrá el comando nativo que da la recompensa (aumento de chaleco máximo) en el `COPCAR` original y, en `CMissionSyncRules::OnCommandExecuted` o donde corresponda, cuando la autoridad lo ejecute en una secundaria coop de Vigilante, reutilizá el mismo envío y receptor existentes. Solo con `ms_bVanillaScripts`. Una sola entrega por misión. Solo a jugadores incluidos. Log `rule-oddjob-reward`.

## Aceptación
Compilan client y server. Diff acotado + `TESTEAR_HOY.md`. Resumen ≤6 líneas.
