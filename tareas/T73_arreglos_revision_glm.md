# Tarea T73: arreglar los hallazgos de la revisión de GLM
Leé `AGENTS.md` y **`tareas\REVISION_GLM_2026-11-10.md`**. Cada hallazgo trae archivo:línea, por qué y cómo arreglarlo. **Verificá cada uno antes de tocarlo** (las líneas pueden haberse corrido; si uno es falso, decilo y no lo toques). Claude ya confirmó 1, 2 y 13.

Arreglá, un commit lógico por grupo si podés, en este orden:
- **Altos:** 1 (CInternet: npos + try/catch en el worker), 2 (CMissionMachines `Back`/`TakeAway`: no mover ni reequipar muerto, en vehículo o en cinemática; diferir hasta que reviva), 3 (CSocialActions: revalidar ped por pool + vtable antes de cada Fade/Blend), 4 (CMissionMachines `OnDamage`: `IsObjectValid` del pool antes de `InflictDamage`).
- **Medios:** 5, 6, 7, 9, 10, 11, 12, 13. El **8** (sesiones server para COOP_LOWRIDER/baile como `poolSessions` de T55) también, con tope razonable de puntos por paquete.
- **Bajos:** 15, 16, 17, 18, 20, 21, 25, 26. No toques 14, 19, 22, 23, 24 (14 es previo y cambia comportamiento: anotalo como propuesta en la revisión).

Sin cambios de comportamiento con los interruptores apagados. Si hay protocolo: versión siguiente a la de `shared/config.h`, paquetes al final.

## Aceptación
Compilan client/server/testbot; `tools\correr_testbot.bat` TODO PASS. CRLF. Al final de `tareas\REVISION_GLM_2026-11-10.md`, una sección "Arreglado (T73)" con el número de cada hallazgo y qué se hizo (o por qué no). No instales. Resumen ≤10 líneas.

## Nota (2026-11-10 11:15)
Una sesión anterior se cortó por límite de uso: **ya hay trabajo de esta tarea sin commitear en el árbol**. Mirá `git status` y `git diff`, seguí desde ahí y no rehagas lo que ya está bien.
