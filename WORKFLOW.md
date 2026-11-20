# WORKFLOW — tareas, compilación y pruebas

> Leelo antes de correr tareas, compilar SCM, coopificar misiones, usar el testbot o portar parches.

## Reglas de trabajo
- Nunca escribir fuera de `E:\juegos\gtasacoopnuevo`. `D:\GTA SERVER` y `...\claude code\CoopAndreas` son solo lectura.
- Un commit por punto, mensaje en español, con `Co-Authored-By:` de la IA que realmente escribió el cambio (ver "Convención anti-pisada" en `AGENTS.md`). Push a `origin` (repo privado `fv54367-dev/GTASAHISTORIAONLINE`); rama o `main` lo decide el usuario (`AGENTS.md`). El token del amigo nunca va en commits.
- Cada sistema nuevo lleva **interruptor** (server `server-config.ini`, cliente `UserFiles\coopandreas.ini`); apagado = comportamiento anterior. Documentarlo en `INTERRUPTORES.md`.
- Compilar antes de commitear. CRLF en `.cpp/.h` y en `scm/scripts/*.txt`. Estilo `CXxx`/`m_`; paquetes nuevos al final de `packet_types.h`.
- Scripts de MAIN: se pueden tocar si hace falta (avisar: las partidas guardadas viejas crashean). Dinero y estrellas siempre por jugador.
- Anotá pruebas y resultados en `TESTEAR_HOY.md`. Usá `ESTADO.md` para diagnósticos y contexto técnico. Si cambia un paquete o el protocolo, subí la versión de `shared/config.h` (cliente y server tienen que coincidir; recompilá también el `testbot` con `xmake build -r testbot`).

## Flujo de una sesión (el de la sesión del 2026-10-20, que funcionó)
1. Verificá la rama y las marcas locales de `TESTEAR_HOY.md` siguiendo «Sesión nueva» de `AGENTS.md`.
2. **Evidencia antes de tocar nada**: logs de `GTA_JUGADOR*` (`CoopAndreas_mirror.log`, `_cutscene.log`, `CoopAndreas_crashes`), log del server (`GTA_JUGADOR1\SERVIDOR\logs`), memoria viva de los GTA abiertos con `python -I tools\leer_estado_vivo.py` (contador de Sweet, `$onmission`, pickups de guardado) y `_refs\gta-reversed` para entender la función del motor. Desensamblar con `capstone`/`pefile` lo que esté protegido.
3. **Un cambio chico, una prueba.** Para ediciones con comillas o CRLF, escribir un script Python en el scratchpad con `Write` y correrlo (los heredocs de bash se rompen). Editar siempre preservando CRLF.
4. **Subagentes** (Agent, `Explore`) para mapear código en paralelo mientras se implementa otra cosa; revisar siempre lo que devuelven.
5. **Cerrar el ciclo**: `xmake build -r client` (+ server/testbot y `tools\correr_testbot.bat` solo si cambió el protocolo o el server; la CI pública lo corre igual) → `python -I tools\check_main_layout.py` → leer los logs y los crashes → commit + push → `sh tools/publicar.sh` → esperar el CI → cerrar GTA/server → `ACTUALIZAR.bat` con `CANAL=main` en `GTA_JUGADOR1/2`. **No abrir los GTA** (desde 2026-11-14): el usuario lanza `PROBAR_SOLO.bat`.
6. Contarle al usuario, en español y corto: qué se hizo, qué quedó **sin probar** y qué tiene que probar. "Compilado" no es "arreglado".
7. Al cierre, actualizá `TESTEAR_HOY.md` y conservá las marcas nuevas hasta que estén registradas en Git. Actualizá `ESTADO.md` si cambió el contexto técnico y `docs/REQUISITOS.md` si hubo una decisión de producto.

## opencode (gratis)
Codex no se usa desde 2026-11-11 (`correr_tareas.py` y `codex_resumen.py` quedan como historia).
- Modelo directo opencode: `opencode run -m opencode/deepseek-v4.1-flash-free --auto "<tarea>"` desde el repo. Para revisiones, ver el punto 7 de la convención en `AGENTS.md`. Nunca modelos pagos; si se cuelga sin escribir, cortalo y reintentá con `timeout 1500`.

## Compilar C++
```
cd E:\juegos\gtasacoopnuevo\coopandreas-server
xmake build client
xmake build server
xmake build testbot
```
Salida en `build\windows\x86\release`. Si un build tardó ~1 s, usá `-r` (client ~25 s, server ~4 s). Un build a la vez. Filtrar: `xmake build client 2>&1 | Select-String "error|warning C"`.

## Compilar SCM (SB4)
La CLI de Sanny Builder se cuelga: se compila por la interfaz gráfica (hay acceso total a la PC).
```
python -I "E:\juegos\gtasacoopnuevo\tools\sb4_compile.py" <carpeta_capturas>
python -I "E:\juegos\gtasacoopnuevo\tools\sb4_compile.py" <carpeta_capturas> --compile
```
- Abre `scm\main.txt` y guarda `sb4_before.png` (la pestaña activa tiene que ser `main.txt`).
- Con `--compile` aprieta F6 y avisa si `main.scm` cambió. Fuerza el foco (si no, un diálogo encima se queda con el F6).
- Deja `sb4_report0.png` con el tamaño de MAIN y de la misión más grande (**máximo 69000**). Miralo siempre.
- Después: `python -I "E:\juegos\gtasacoopnuevo\tools\check_main_layout.py"` tiene que decir **OK**. Si falla, las partidas guardadas se rompen: mover el cambio fuera de MAIN.
- Si SB4 no conoce un opcode nuevo: `taskkill /IM sanny.exe /F` y recompilar. Manual de respaldo: abrir `scm\main.txt`, F6, capturar, cerrar.

## Coopificar una misión (MODO ARCHIVADO, no usar: solo historia)
> Desde el 2026-11-08 el mod va solo con scripts originales (`vanilla-scripts=true`, ver AGENTS.md). Esta sección y "Opcode `Coop.*` nuevo" quedan como historia: los arreglos de misión se hacen en C++ (`CMissionSyncRules`, `CSideMissionRules`, capa espejo), no coopificando scripts.

Usá `coopify.py` (determinista, no LLM):
```
python -I "E:\juegos\gtasacoopnuevo\tools\coopify.py" scm/scripts/X.txt
python -I "E:\juegos\gtasacoopnuevo\tools\coopify.py" scm/scripts/X.txt --choice "Nombre"   # secundaria coop/individual
python -I "E:\juegos\gtasacoopnuevo\tools\coopify.py" scm/scripts/X.txt --skip-line N        # teleport puntual
python -I "E:\juegos\gtasacoopnuevo\tools\coopify.py" scm/scripts/X.txt --loops              # formato viejo
```
Después: `check_scm_inserts.py` (inserciones dentro de `if` y CRLF) → compilar → mirar `sb4_report0.png` → revisar `git diff --stat` y las líneas agregadas.

**Patrón:** `EnableSyncingThisScript`, `CollectNetworkPlayersForTheMission`, teleport al host en cada cambio de escena, checkpoints/blips por jugador y éxito/fracaso para todos. `--choice` = el que inicia elige Y cooperativo / N o 5 s individual; en individual el script se llama `XXXSOLO` y no se sincroniza.
**Plantillas:** `scm/scripts/HOODS5.txt`, `SMOKE3.txt`, `INTRO1.txt`, `SWEET1.txt`. Si coopify no alcanza, copiar `tools\task_template_mision.md` y revisar el diff.
**Ojo:** cada script tiene ~3.000–5.000 líneas; probar con 2–4 clientes es obligatorio. Verificar teleports con `rg "TeleportAllNetworkPlayersToHost" scm/scripts/X.txt`.

## Testbot del server
```
xmake build server
xmake build testbot
E:\juegos\gtasacoopnuevo\tools\correr_testbot.bat
```
Copia el server a `_testserver` (handover=1, puerto 6799) y `_testserver_nohandover` (handover=0, puerto 6800), corre 4 escenarios y tiene que decir **TODO PASS**. Al final mata solo esos servers.

## Pruebas automáticas en juego (solo si el dueño lo pide)
Desde el 2026-11-13 NO se corren por defecto antes de instalar: tardan demasiado. La herramienta queda para cuando se pida.
Con los GTA de prueba abiertos (`PROBAR_SOLO.bat`, cada uno con `sa_bridge.asi`, puerto `47311+slot`) corré `python tools\prueba_auto.py` (`--escenarios conexion,jugadores,npc,animaciones,capturas`, `--lanzar` para que corra el .bat y espere, `--salida <carpeta>`; por defecto `_pruebas_auto\<fecha_hora>` en la raíz con `informe.txt` de una línea por escenario y `datos.json`). Exit 0 si todo PASS (SKIP no falla). Sin el juego: `python tools\test_prueba_auto.py` (bridge falso).
- **Herramienta `sa_debug_json`:** el cliente exporta `sa_debug_json(char* buf, uint32_t cap)` (`client/src/CDebugExport.cpp`, la llama el bridge con `plugin_query` en el hilo del juego, módulo `CoopAndreasSA.dll`; también la usan `compare_instances`/`sync_trace` de sa-mcp). JSON: `net_id, version, connected, authenticated, is_host, area, mission{id,coop_running,coop_owner,script_authority,title}, local_position, local_anims[], remotes[]{net_id,name,position,net_position,net_on_foot,age_ms,has_ped,area,in_vehicle,dead,anims[]}, peds{mine,copies}, ped_list[] (60 más cercanos: ped_id,mine,model,has_ped,streamed_out,position y, si es copia, net_position+age_ms), vehicles{mine,copies}`. Si `cap` no alcanza devuelve el largo necesario sin escribir.
- **Mide:** `conexion` (2 instancias conectadas, se ven entre sí, misma versión, sin `CoopAndreas_crashes\*.log` nuevos); `jugadores` (CJ 0 corre 10 s, error entre su posición real y su copia en la otra a 10 Hz, FAIL si p95 > 1.5 m); `npc` (dueño vs copia por `ped_id`, p95 y copias faltantes); `animaciones` (anim DANCING de script en el CJ 0 aparece en `remotes[].anims` de la otra: `player-anim-mirror`); `capturas` (PNG de cada instancia).
- **NO puede probar:** cinemáticas, misiones completas de punta a punta, lo visual fino (pose, ropa, iluminación, sensación de lag): para eso están las capturas y TESTEAR_HOY.md con ojos humanos.

## Instalar y probar en los GTA
- Normal: `ACTUALIZAR.bat` del release con `CANAL=main` en `GTA_JUGADOR1/2` (después de `publicar.sh` y del CI).
- Respaldo sin CI: `tools\actualizar_copiar.ps1 -Base E:\juegos\gtasacoopnuevo` (lo usa `ACTUALIZAR_GTA_SERVER.bat`): copia client, server, SCM y datos a `GTA_JUGADOR1`, `GTA_JUGADOR2`, `GTA_JUGADOR3` (si existe), `GTA_JUGADOR1\SERVIDOR` y `coopandreas-server\dist`, y borra los `CoopAndreas_*.log`.
- `E:\juegos\gtasacoopnuevo\PROBAR_SOLO.bat` (lo lanza el usuario, no las IAs): server + dos GTA en ventana en esta PC (`JUGADORES=3` arriba del `.bat` abre también `GTA_JUGADOR3`). Lista de pruebas: `TESTEAR_HOY.md`.

## Parches antiguos
`parches\<etapa>\*.patch` (en la raíz del proyecto, fuera del repo) es el histórico de las etapas A–G (ya no se usa: se hace push directo a `origin`).

## Opcode `Coop.*` nuevo (modo archivado, no usar)
1. Entrada en `sa_coop.json` **y** `sa_coop.db`.
2. En `tools\sb4\data\sa_sbl_coopandreas\` **y** en `coopandreas-server\sdk\Sanny Builder 4\data\sa_sbl_coopandreas\`.
3. El usuario reinicia SB4.
4. Implementación en `client/src/Commands/Commands/` (una clase `CCommand*.cpp` por opcode) y registro en `CCustomCommandRegistrar.h`.

API actual (definición en `sdk/Sanny Builder 4/data/sa_sbl_coopandreas/sa_coop.json`): `EnableSyncingThisScript`, `CollectNetworkPlayersForTheMission`, `IsNetworkPlayerActorValid`, `GetNetworkPlayerChar`, `GetNetworkPlayerInternalId`, `LocateAllPlayersOnFoot3D`, `Update/RemoveCheckpointForNetworkPlayer`, `Update/RemoveCarBlipForNetworkPlayer`, `Update/RemoveCharBlipForNetworkPlayer`, `ClearAllEntityBlipsForNetworkPlayer`, `Print/PrintBig/PrintNow/PrintHelp ForNetworkPlayer`, `ClearThisPrintForNetworkPlayer`, `AddChatMessage`, `IsHost`, `TeleportPlayersToHostSafely`, `GetPedInAreaWithModel`, `ClaimPedOnRelease`, `CancelPedClaim`, `IsSyncingThisPed`, `PedResetAllClaims`, `PedTakeHost`, `LaunchMissionForNetworkPlayers`, `MinigameVsReport`, `ChooseSideMissionMode`, `IsCoopMission`.

## Resumen de arranque y control de docs
- `python -I tools/resumen_sesion.py`: rama, protocolo, último tag, git, PR abiertos, marcas locales de la lista de pruebas y AVISOS si `ESTADO.md` pasa de 2500 palabras, `TESTEAR_HOY.md` de 250 líneas, aparece "SIN INSTALAR" o hay links rotos. En esta PC lo corre el hook SessionStart (`.claude/settings.json` de la raíz). Si hay avisos, compactar antes de seguir.
