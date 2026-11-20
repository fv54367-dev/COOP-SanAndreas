# AGENTS.md — reglas canónicas del proyecto (todas las IAs y personas)

> Claude Code lo carga por `CLAUDE.md`; cualquier otra IA debe empezar por este archivo.
> **Sesión nueva:** verificá la carpeta y la rama activa con `git status -sb`. Leé este archivo y el `TESTEAR_HOY.md` local, incluidas las marcas sin commit, antes de decidir qué tocar. Usá `ESTADO.md` para contexto técnico, `docs/REQUISITOS.md` para decisiones de producto y `docs/INDICE.md` para buscar referencias. Consultá la tanda pertinente a la tarea y a la rama. Para trabajo de rumbo, leé `docs/PLAN_CPP.md`.

## Proyecto
GTA San Andreas modo historia en **cooperativo de 2 a 4 jugadores** (PC, 1.0 US, `gta_sa.exe` 14.383.616 bytes). Fork propio de CoopAndreas (GPL-3.0, autor original Tornamic; ENet + plugin-sdk). Server con autoridad: dueños de entidades, traspaso, guardado central, modo prueba de 2 instancias en la misma PC.
- **Objetivo del usuario: un buen GTA SA coop.** Si hace falta REESCRIBIR lo hecho antes, se reescribe — evaluando con evidencia, no por capricho.
- **Modo scripts coop ARCHIVADO (decisión del dueño, 2026-11-08):** el mod va solo con scripts originales (`vanilla-scripts=true`). El modo viejo (main.scm del coop con `Coop.*`) no se distribuye ni se prueba más; su material queda en el historial de git y `scm/` (código C++ conservado, como el patrón de T17). Las tandas nuevas ya no llevan caso borde "scripts coop vs. originales".
- Licencia GPL-3.0; sin archivos de Rockstar. Crédito a Tornamic/CoopAndreas en `NOTICE.md`.
- **Cómo anda hoy (scripts originales):** cada cliente corre el `main.scm` original sin tocar; el script de misión corre en el **dueño** y sus opcodes se replican porque `CMissionSyncRules` registra el script por nombre (reemplaza al viejo `Coop.EnableSyncingThisScript()`; `client/src/COpCodeSync.cpp`). La capa espejo C++ (`CMissionMirror`, `CMirrorBlips`, `CMirrorPickups`, `CCameraMirror`, `CMissionAreaMarkers`) y las reglas de misión (`CMissionSyncRules`, `CSideMissionRules`) hacen el resto: **todo arreglo de misión va en C++**, no editando `scm/scripts/*.txt`. Si ves código o docs con `Coop.*`, `coopify.py`, `XXXSOLO` o "scripts coop", es del modo archivado: no lo tomes como el modo en uso. Rumbo: `docs/PLAN_CPP.md`.

## Regla dura de rutas
- **Nunca escribir fuera de `E:\juegos\gtasacoopnuevo`.** No tocar `D:\GTA SERVER`, `C:\Users\nose2\Desktop\claude code\CoopAndreas`, ni el repo `porahoranoname` (solo lectura).
- **Toda la documentación del proyecto vive dentro de este repo** (`AGENTS.md`, `CLAUDE.md`, `ESTADO.md`, `WORKFLOW.md`, `TESTEAR_HOY.md`, `INTERRUPTORES.md`, `PROMPT_NUEVA_SESION.md`, `docs/`, `tareas/`). **No** crear ni editar `.md` del proyecto en la raíz `E:\juegos\gtasacoopnuevo` (ahí solo quedaron stubs y cosas de esta PC).
- Fuera del repo quedan (específicos de esta PC): `tools\` (build/instalación/SB4), los `.bat`, `.claude\`, `.mcp.json`, los GTA de prueba `GTA_JUGADOR*\`, `_refs\`, `respaldos\`, `parches\`.
- `_refs\` = repos de referencia **solo lectura**. No commitear ni leer entero `build\`, `third_party\`, `GTA_JUGADOR*\`.

## Quién hace qué (actualizado 2026-11-17)
- **Claude Code (Opus): orquestador y escritor.** Decide, escribe lo delicado y lo rápido, reparte lo paralelizable a subagentes Claude (Sonnet; Opus solo si el usuario lo habilita en esa sesión) cada uno en su worktree, revisa, compila e instala.
- **Codex: NO USAR en esta PC (desde 2026-11-11, decisión del dueño).** Sus tandas T92-T96 tomaron malas decisiones de diseño (misiones enteras excluidas, invitados escondidos en escenas, autos que suben solos al invitado). Escriben Claude y subagentes Claude, cada uno en su worktree, y Claude revisa (el merge a `main`, con OK del usuario). Criterio: incluir a los invitados (visibles, animados, con aviso) antes que esconderlos o excluir.
- **El amigo (su PC):** trabaja en su clon, en rama, y manda PR. No puede probar con 2 GTA (eso lo hace el dueño de esta PC con `PROBAR_SOLO.bat`).
- Subagentes de Claude Code (Explore, general-purpose) permitidos y recomendados para paralelizar; **revisar siempre lo que devuelven contra estas reglas** (el 2026-10-25 un port rompía los blips por jugador y se detectó en la revisión).
- **GLM** (plan Z.ai del dueño) también puede escribir, si el dueño quiere: por la **app ZCode** (el dueño pega el prompt que le da Claude); el CLI `zcode -p` no sirve sin la app (no ve el modelo elegido). El 2026-11-10 se usó el worktree `E:\juegos\gtasacoopnuevo\wt-glm`. Otras IAs (p. ej. un subagente Fable) solo si el dueño lo pide.
- Si escriben varias a la vez, conviene que cada una tenga su worktree (`git worktree add ../wt-<ia> main` y `xmake f -p windows -a x86 -m release --vs=2026 -y`) y que Claude mergee a main (versión: la más alta +1; nombres de `ePacketType_ToString` en el mismo orden que el enum) y corra el testbot. Testbot en otro worktree: copia de `tools\correr_testbot.bat` con su `REPO`, otras carpetas/puertos y el kill limitado a esas carpetas.

## Convención anti-pisada (tandas)
1. **Archivo de tanda** `tareas/TANDA_<fecha>_<quien>.md` (objetivo, archivos a tocar, restricciones, criterio de aceptación, casos borde) **solo si** toca protocolo o reglas de misión, o si escriben varios a la vez (subagentes, el amigo): se crea antes de tocar código y quien no está en la tanda no toca esos archivos. Para el resto alcanza el mensaje del commit (desde 2026-11-17).
2. **`main` siempre compila** (cliente y server). Un commit por punto (para revertir uno solo), mensajes en español con prefijo `feat:`/`fix:`/`refactor:`/`net:`/`docs:`. `git pull --rebase` antes de pushear. **Rama o `main`, y cuándo se integra, lo decide el usuario** (desde 2026-11-17): si no lo dijo para esta tanda, preguntarle en una línea antes de empezar. Nada se mergea a `main` (ramas propias ni PR del amigo) sin su OK.
3. **El amigo trabaja en rama** y manda PR; Claude lo revisa y lo integra si el usuario dice que sí (ver "PR" abajo). `main` es siempre la instalable.
4. **Publicar (desde 2026-11-16):** se trabaja y pushea en este repo privado (`GTASAHISTORIAONLINE`, su Actions está apagado) y `sh tools/publicar.sh` copia solo el código (lista blanca + `publico/`) al repo **público** `fv54367-dev/COOP-SanAndreas`, cuyo Actions compila gratis en ~3 min y publica los releases: `main` → "Principal", cada PR abierto → `pr<N>`, "Todo junto" = `main` + PR. Docs, tareas y estas reglas NUNCA van al público. **No usar `build.ccache`** (con MSVC+PCH vuelve el CI 5-10x más lento). Sin binarios en `dist/`. `COOPSA_BUILD` (`shared/build_label.h`) lo pone el CI. En esta PC se instala corriendo el `ACTUALIZAR.bat` del release con `CANAL=main` en `GTA_JUGADOR1/2`. Si hay rama: PR (compila su canal `pr<N>`) → merge a `main` cuando el usuario lo aprueba → instalar; tag `probada-*` cuando el usuario confirma. Explicarle al usuario cada paso de git (pidió aprender).
5. **Protocolo (`shared/network`): un solo dueño por tanda.** Quien cambia el protocolo sube la versión de `shared/config.h`, lo anuncia en su tanda, y el otro rebasea antes de seguir tocando.
6. **Casos borde antes de codear** (lista para repasar; se escribe solo si hay archivo de tanda): qué pasa con 3–4 jugadores, con uno en individual (F1), si uno se desconecta o muere a mitad, si está en un interior u otra punta del mapa, si el marcador/objeto es de uno solo (silla de peluquería), y qué hace el interruptor apagado. Si el cambio toca el **protocolo**, otra IA critica la tanda antes (solo lectura: "¿qué caso no tuvo en cuenta este plan?"; mismo orden de ahorro que el punto 7); el resto se cubre con la revisión del commit.
7. **Revisión cruzada:** lo que escribe un subagente (o el amigo) lo revisa Claude (diff contra estas reglas). Lo que Claude escribe a mano, según el riesgo (cuidar la cuota; lo que más bugs agarra es la prueba del usuario): **delicado** (protocolo, reglas de misión, hooks del exe, arreglos de crash/heap) → revisión en solo lectura ("buscá bugs y errores de lógica en `git show <commit>`; no modifiques nada") primero con opencode gratis (`opencode run -m opencode/deepseek-v4.1-flash-free`, ver `WORKFLOW.md`) y subagente Sonnet solo si opencode no alcanza; **resto** (docs, una línea, arreglo obvio) → alcanza con que Claude relea el diff. Los hallazgos se verifican antes de aplicarlos (pueden equivocarse los dos). GLM queda fuera del pipeline (lo usa el dueño a mano).
8. **Pruebas en juego:** `TESTEAR_HOY.md` es la única lista activa y el único archivo que marca el usuario. `[ ]` = falta probar, `[x]` = anda, `[N]` = falla, `[I]` = a medias. Anotá una prueba por línea, en palabras simples, agrupada por tema. La nueva va primero en su sección con *(nuevo)*. Las cuatro secciones son historia (por misión, en orden del juego), todas las misiones, secundarias y minijuegos, y mundo libre.
   - Conservá las marcas del usuario, incluidas las locales. Retirá un `[x]` de la lista activa cuando la confirmación ya esté registrada en un commit de documentación en la rama donde se continuará. Un tag `probada-AAAA-MM-DD` identifica el build que se probó.
   - Las `[N]` y `[I]` son reportes para el trabajo que el usuario pidió. Las `[ ]` van a prueba en juego. Una nota antigua en `ESTADO.md`, una tanda, otra rama o Drive aporta contexto. Para reabrir un caso confirmado, hace falta un reporte nuevo o un cambio que afecte ese caso. Indicá el síntoma nuevo o el cambio en `TESTEAR_HOY.md`.
   - Leé el Google Doc del amigo al empezar la sesión y subilo una vez al terminar o cuando lo pida el usuario. Incorporá reportes nuevos con su contexto y conservá las confirmaciones locales. El comportamiento implementado se documenta en `docs/SISTEMAS.md` y el diagnóstico técnico en `ESTADO.md`.

## PR (desde 2026-11-10, reemplaza a la Red de IAs)
Decisión del dueño (2026-11-10): se deja de usar la app Red de IAs y su tablero de reservas; se vuelve a trabajar con PR.
1. **El amigo** trabaja en rama (`tanda/<fecha>-<nombre>`) y manda PR a `main`.
2. **Claude revisa los PR abiertos** al empezar cada sesión (`gh pr list`) y le cuenta al usuario qué traen; **los integra solo si el usuario dice que sí**: revisión del diff contra estas reglas, rebase sobre `main` (si el PR cambia el protocolo y `main` ya avanzó, toma la versión siguiente y su paquete va al final), compilar client/server (testbot local solo si toca protocolo o server; la CI pública lo corre igual), push a `main` y cerrar el PR con un comentario que diga qué commits quedaron y qué falta probar. Si algo ya está en `main`, se toma solo lo que falta y se explica en el PR. Un PR no queda esperando: si no se puede integrar, se le comenta por qué.
3. Rama o `main` para el trabajo de Claude: lo decide el usuario (punto 2 de la convención).
4. Para no pisarse: la tanda dice qué archivos toca; si un PR y una tanda local tocan lo mismo, gana el que llegó primero a `main` y el otro rebasea.

## Cómo trabajar
0. **Usar MUCHO `_refs\gta-reversed`** (pedido del usuario): es el C++ reescrito del exe. Antes de parchear por dirección, de hookear o de inventar, buscar la función real (`grep -rn <dirección|nombre> _refs\gta-reversed\source`), su semántica e invariantes. Preferir copiar o llamar la función del propio juego antes que dibujar/simular algo nuestro. Citarlo en commits y en `ESTADO.md`. **Para entender una misión**, leer primero su fuente original en `_refs\UndefinifiedGrove\main\` (`.sc` con nombres de variables y comentarios de Rockstar; buscar con `grep -rln -i "SCRIPT_NAME <nombre>"`) en vez del `scm/scripts/*.txt` descompilado; los offsets de MAIN siguen saliendo del binario.
1. **Evidencia primero:** un crash se investiga con el log (`GTA_JUGADOR*\CoopAndreas_crashes\*.log`, backtrace y base de nuestra DLL), el desensamblado (`capstone`/`pefile`) y gta-reversed. Dos veces se nombró mal una función por no mirarlo.
2. **Estado vivo:** con los GTA abiertos se lee su memoria (`ReadProcessMemory`; `tools\leer_memoria.py`, `tools\leer_estado_vivo.py`). Volcados `.dmp`: parsear Memory64List; EIP en `0x015xxxxx` es función protegida del exe: mirar la dirección de retorno del llamador.
3. **Un cambio chico, una prueba:** bisecar con commits. No apilar tareas de subagentes sin revisar entre medio.
4. **Antes de parchear un síntoma, preguntar si el diseño es el correcto** (ej.: sincronizar opcode por opcode vs. la capa espejo).
5. **Todo sistema nuevo lleva interruptor** (server `server-config.ini`, cliente `UserFiles\coopandreas.ini` o el ini de modo prueba): apagado = comportamiento anterior. Documentarlo en `INTERRUPTORES.md`.
6. "Compilado" no es "arreglado": contarlo siempre tal cual, y decir qué quedó sin probar.

## Código
- C++20, VS 2022+, xmake. **Seguir el estilo del código vecino** (camelCase, `m_`, Hungarian, `CXxx`). Comentarios en inglés, cortos.
- **CRLF** en `.cpp/.h` y `scm/scripts/*.txt`: editar con Python `open(..., newline='')` preservando `\r\n`; **nunca `sed -i`**.
- Agregar sincronización: `shared/network/packet_types.h` → `shared/network/packets/<area>.h` → hook en `client/src/Hooks/` → `client/src/PacketHandlers/<area>.cpp` → `server/src/PacketHandlers/<area>.cpp`. Paquetes nuevos al final de `packet_types.h`. Si cambia el protocolo, subir la versión de `shared/config.h` (cliente y server tienen que coincidir; recompilar también el `testbot`).
- Commits: la atribución `Co-Authored-By:` es de la IA que realmente escribió el cambio (nunca poner una que no participó).
- Hooks de plugin-sdk: preferir call-site hooks (no pisar otros plugins).
- Licencias: **SACoop (Parricidium) NO tiene licencia: NUNCA copiar código, solo ideas.** `project_gta_sa_coop` (GPL-3.0) sí se puede portar citando (`NOTICE.md`). Si se reutiliza código GPL, citar origen en el commit.

## Decisiones fijas (no volver a preguntar)
- **Dinero y estrellas: SIEMPRE de cada jugador.** No sincronizar pickups fijos, sobornos ni propiedades. **Coleccionables** (graffitis, herraduras, ostras, fotos): compartidos solo entre jugadores en modo coop del F1 (`CCollectibleSync`).
- No aplicar `MAX_SERVER_VEHICLES/PEDS` (255). **El mundo NUNCA se pausa** (`CTimer::StartUserPause` anulado, también al perder foco).
- **Si cae (muere o lo arrestan) cualquier jugador de una misión coop, la misión falla para todos.** Abandonar (mantener `2`) saca solo al que abandona.
- Descartado para siempre (de otros proyectos): un solo jugador arranca/avanza la misión para todos, el host que se teletransporta al invitado, estrellas compartidas, guardado del host copiado a todos.
- Solo donaciones voluntarias, sin ventajas. El token del amigo nunca va en documentos, memoria ni commits.
- Los scripts de MAIN se pueden tocar si hace falta (autorizado 2026-10-21), pero avisar antes: corren offsets y las partidas guardadas viejas crashean al cargar.

## Reglas de contexto (no reventar la ventana)
- Nunca leer entero un `scm/scripts/*.txt` (3.000–5.000 líneas) ni un `.cpp` grande: `rg`/`grep` + ±80 líneas de contexto, o resumen con `tools\codex_resumen.py`.
- No leer `build/`, `third_party/`, `.git/`, `opcodes.txt`, ni `_refs\` entero (buscar con grep).
- `git diff --stat` y solo los archivos tocados. Un build a la vez; si un build tardó ~1 s, `xmake build -r client` (~25 s).
- No releer lo ya leído ni repetir el contenido de estos `.md`.
- (Solo Claude Code tiene hook automático de lectura; las demás IAs aplican estas reglas a mano.)

## Trampas que cuestan caro
- **CRLF** (ver arriba). También: escribir `\n` dentro de un literal C++ desde Python con `chr(92)+'n'`.
- **Inline asm MSVC:** una `constexpr` dentro de `__asm` se toma como variable propia, no como valor: usar literales.
- **Heredocs de Bash** con comillas/backslashes/backticks fallan: escribir el archivo con Write (o un script Python).
- **xmake** a veces no recompila un archivo editado (1 s): usar `-r`.
- **SB4:** orden de parámetros de `Char.LocateAnyMeans3D` (la esfera va en 2º lugar; con el orden equivocado compila y nunca da verdadero). F6 siempre con `main.txt` activo; el modal "Report" bloquea F6. Misión más grande: máx 69000. Tras compilar: `python -I tools\check_main_layout.py` debe decir OK (si falla, las partidas guardadas se rompen).
- **Offsets de `main.scm` usados desde C++** (valen mientras MAIN no cambie): `$flag_sweet_mission_counter` 1808, `$onmission` 1636, `$created_save_blips` 3536, `$number_of_save_icons` 3540, `$grove_save_pickup[0]` 3460. Los nombres de scripts de MAIN llegan en minúsculas en `m_szName`: comparar sin distinguir mayúsculas.
- **Merge de upstream:** `scm/main.scm` (binario) siempre choca; quedarse con el nuestro.
- **Depurar el exe:** `capstone` y `pefile` instalados. Funciones protegidas saltan a ~0x15xxxxx: enganchar en el sitio de llamada. Bajo un depurador el juego se comporta distinto; probar lanzándolo normal (`Start-Process`). El exe sale "limpio" (exit 0) si falla el dispositivo gráfico.
- **Punteros colgantes** (`RegisterReference`) corrompen el heap y dan crashes "al azar": sospechar de esto cuando varios crashes no se parecen.
- **`GetAsyncKeyState` es global:** con 2 GTA en una PC lo ven los dos (usar `CTestMode::IsGameWindowFocused`).
- `small` es una macro de Windows (`char`): no usarla como nombre de variable. En strings C++ con `\xC2\xA1` seguido de letra hex (`a`..`f`), cortar el literal.
- `tools\actualizar_copiar.ps1` se llama desde PowerShell (desde bash se rompen las barras). Cerrar `server`, `testbot` y `gta_sa` antes de instalar; el testbot deja `server.exe` abierto.
- `FailCurrentMission` solo actúa en scripts "únicos"; para fallar la misión del dueño se usa el camino de muerte (`CCoopMission::RequestMissionFail` → hook `DoDeatharrestCheck`).

## Compilar y probar (detalle en `WORKFLOW.md`)
- Repo: `xmake build client` / `server` / `testbot` → salida `build\windows\x86\release`. Filtrar errores: `xmake build client 2>&1 | Select-String "error|warning C"`.
- SCM: `python -I "E:\juegos\gtasacoopnuevo\tools\sb4_compile.py" <capturas> --compile` (la CLI de SB4 se cuelga: va por la GUI) → `check_main_layout.py` OK → mirar `sb4_report0.png` (máx 69000).
- Testbot del server: lo corre la CI pública antes de publicar. A mano (`tools\correr_testbot.bat` → **TODO PASS**) solo si se tocó el protocolo o el server.
- Instalar (desde 2026-11-16): después de `sh tools/publicar.sh` y de que el CI publique, correr el `ACTUALIZAR.bat` del release con `CANAL=main` en `GTA_JUGADOR1/2`. Respaldo local sin CI: `tools\actualizar_copiar.ps1 -Base E:\juegos\gtasacoopnuevo` (copia a `GTA_JUGADOR1/2/3`, el server único `GTA_JUGADOR1\SERVIDOR\` y `coopandreas-server\dist\`, borra los `CoopAndreas_*.log`).
- Probar: lo hace el usuario con `PROBAR_SOLO.bat` (server + `GTA_JUGADOR1` izquierda/host + `GTA_JUGADOR2` derecha; `JUGADORES=3` abre el tercero). Las IAs **no lo lanzan** (desde 2026-11-14): solo instalan. **Las IAs no juegan: todo queda "compilado, sin probar" hasta que el usuario confirme.**
- Push: `origin` = GitHub `fv54367-dev/GTASAHISTORIAONLINE` (privado, rama `main`); remoto `publico` = `fv54367-dev/COOP-SanAndreas` (lo maneja `tools/publicar.sh`). El actualizador y el launcher bajan del público, sin token. El nombre del mod es **COOP San Andreas** (crédito GPL a CoopAndreas/Tornamic y project_gta_sa_coop en `publico/README.md` y en la consola del server; no sacarlo).
- Chat en juego: T o F6 (`/traer /ir /tp x y /caminar /mision /actualizar /igualar <jugador> /guardar /guardarsv /municion /tpm /fijar /ayuda`); F1 modo coop/individual; F8 trucos.

## Cuándo leer cada archivo
| Archivo | Cuándo |
|---|---|
| `ESTADO.md` | contexto técnico, diagnósticos y decisiones |
| `docs/INDICE.md` | **al empezar una tarea**: mapa pregunta -> sistema -> .cpp -> interruptor -> tanda (para no leer los .md grandes) |
| `docs/SISTEMAS.md` | cómo funciona cada sistema ya implementado (movido de ESTADO.md) |
| `docs/REQUISITOS.md` | siempre al empezar (lo que el usuario quiere; manda sobre el código) |
| `docs/PLAN_CPP.md` | solo si la tarea es de rumbo: hacia dónde va el proyecto (fases 0–6, scripts originales) |
| `tareas/TANDA_*.md` | antecedentes de la tarea y rama pertinentes |
| `WORKFLOW.md` | flujo de sesión, compilar, publicar e instalar, SCM, testbot |
| `TESTEAR_HOY.md` | al empezar la sesión y al registrar pruebas o resultados en juego |
| `INTERRUPTORES.md` | qué interruptor apagar para volver al comportamiento anterior |
| `docs/MISIONES_MODO.md` | modos admitidos por misión |
| `docs/MISIONES_HECHAS.md` | resumen de las adaptaciones de cada misión |
| `docs/FASE4_ANALISIS.md` | análisis de las reglas de misión en C++ |
| `docs/REFERENCIAS_OPENSOURCE.md` | cómo lo resuelven otros proyectos (licencias) |
| `docs/DISENO_BCD_POBLACION.md` / `docs/DISENO_F_SECUNDARIAS.md` | traspaso/zonas/policía/interiores · secundarias coop |
| `tareas/AUDIT_*_informe.md`, `tareas/archivo/` | hallazgos de auditorías (robo de vehículos, misiones) y tandas viejas (antes del 2026-11-08) |

## Referencias (en `_refs\`, solo lectura; verificar offsets contra el binario)
| Repo | Licencia | Para qué |
|---|---|---|
| `gta-reversed` | GPL-3.0 | C++ reescrito del exe: nombre/semántica de funciones por dirección. **La más usada** |
| `project_gta_sa_coop` (sametozbalkan) | GPL-3.0 (citar) | Fork del mismo CoopAndreas: mirror layer, world-state, mission-porting. Leer antes de diseñar sync de misiones |
| `plugin-sdk-sa` (Dryxio) | ver LICENSE | SDK con layouts/offsets verificados |
| `reagent` (Dryxio) | MIT | Agente de RE para reconstruir/validar funciones |
| `sa-mcp` (dntAtMe) | MIT | MCP para N instancias: desync, screenshots, memoria |
| `satk-gta` (qxlies) | MIT | Toolkit/MCP de análisis de crashes, opcodes |
| `UndefinifiedGrove` (Sergeanur) | **Filtración: solo leer, no copiar ni compilar** | Fuente original de los scripts (`main/story_missions/<zona>/*.sc`, `oddjobs/`, `misc/`, `mutliplayer/` = modos 2P originales). Rama `master` = PC 1.00 (la nuestra); otras versiones por tag. Leer el `.sc` de la misión antes de armar reglas de sync en C++ |
| `sacoop` (Parricidium) | **SIN LICENCIA: no copiar código** | Solo ideas |
| `koltrbtw`, `mtasa`, `gtarev` | ver cada uno | Otro fork de CoopAndreas, MTA, reversing |
