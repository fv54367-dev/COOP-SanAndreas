# Auditoría: misiones coop — cinemáticas, blips y busted

Solo lectura. HEAD `1a16fa0` vs tag `base` `87d8b3b`. Drive-Thru = `SWEET3`, lanzada desde `SWEET.txt:48`.

## A. Cadena de cinemática / cámara / fade

1. Hoy el único camino es el sync de opcodes: `do_fade 0x016A`, `load_cutscene 0x02E4`, `start_cutscene 0x02E7`, `clear_cutscene 0x02EA` (`COpCodeSync.cpp:100-105`). El packet dedicado `START_CUTSCENE` está desactivado (`PacketHandlers/scripts.cpp:142-158`) y el hook `CCutsceneMgr::StartCutscene` no está enganchado (`GameHooks.cpp:188`; código en `:108-118`).
2. El emisor solo envía si es autoridad del script (`COpCodeSync.cpp:384`); el receptor descarta el opcode solo si **él** es autoridad (`:924`), así que un participante normal sí lo procesa.
3. Punto frágil: al recibir `start_cutscene` se exige `CCutsceneMgr::ms_cutsceneLoadStatus == 2`; si no, se marca `ms_bLoadingCutscene` y **se descarta** (`COpCodeSync.cpp:1249-1255`). El único reintento está en `Main.cpp:124-129` y solo corre para `!IsScriptAuthority()` y si `ms_cutsceneName[0]` ya está puesto. Si `load_cutscene` no llegó o no fijó el nombre, `start_cutscene` no se recupera nunca.
5. La cámara no cubre el fallo: `CCameraMirror` no espeja mientras `CCutsceneMgr::ms_running` (`CCameraMirror.cpp:72`), y sin cutscene no hay cámara de escena que sincronizar.
6. Diff `base..HEAD`: no hay cambio estructural; F solo sustituyó `m_bIsHost` por `IsScriptAuthority()` (`Main.cpp:124/133/147`, `GameHooks.cpp:111`, `MissionAudioHooks.cpp:13/21/52`, `CCameraMirror.cpp:56/108`). El fallo aparece cuando el dueño es invitado: la autoridad dejó de coincidir con el host y el handshake de dos pasos queda expuesto.
7. La puerta cerrada (`SWEET3.txt:239-270`) es consecuencia de A.3: si la cinemática no corre en el invitado, su copia local del mundo no ejecuta esos cambios.

## B. Blips de auto y destino

8. Auto: `SWEET3.txt:361` lo crea y `:374` llama `Coop.UpdateCarBlipForNetworkPlayer`. `CNetworkEntityBlip::UpdateEntityBlip` no guarda estado pendiente: si `GetVehicle` o `m_pVehicle` son nulos, sale sin crear blip (`CNetworkEntityBlip.cpp:24-36`). El paquete es one-shot; si llega antes de que el vehículo esté streameado (`CNetworkVehicle.cpp:24-28`), el blip se pierde. El receptor ignora el paquete si es autoridad (`blips.cpp:9-13`), correcto para el dueño.
9. Destino: `Coop.UpdateCheckpointForNetworkPlayer` (`SWEET3.txt:580`) solo crea zona resaltada, no blip de radar (`CNetworkCheckpoint.cpp:4-30`). El blip real es `Blip.AddForCoord` (`SWEET3.txt:601`), que no está en `syncedOpcodes`. Los blips de misión se excluyen del snapshot estático (`CNetworkStaticBlip.cpp:72-74`) y el snapshot solo lo manda el host (`Main.cpp:353`). Solo lo ve quien ejecuta el script.
10. El checkpoint sí se ve en ambos porque viaja por `UPDATE_CHECKPOINT`, no por radar.

## C. Busted / wasted

11. `CRunningScript__DoDeatharrestCheck_Hook` (`PlayerHooks.cpp:442-498`) corre en la autoridad. Si el jugador local está `HASDIED`/`HASBEENARRESTED` **o cualquier** ped de red está `PEDSTATE_ARRESTED`/`DEAD`/`DIE`+`bIsDyingStuck` (`:465-477`), limpia `OnAMissionFlag`, desenrolla el script y marca wasted/busted.
12. Eso hace fallar la misión para todos: se propaga por `CCoopMission::ProcessEnd` (`client/CCoopMission.cpp:389-408`) y `OnEnd` del servidor (`server/CCoopMission.cpp:237-249`).
13. Opciones:
   - (a) Falla solo el arrestado/muerto y reaparece; la misión sigue. Pro: no castiga al compañero. Contra: hay que definir reincorporación, propiedad de objetivos y qué pasa con el coche/objetos; el SCM no está escrito para seguir con un jugador menos.
   - (b) Falla solo si **todos** los participantes están caídos/arrestados. Pro: se acerca a GTA y permite continuar. Contra: exige estado y temporizadores de down por participante, mensajes y reglas de reaparición.
   - (c) Actual (fail-fast). Pro: simple y determinista, sin estados nuevos. Contra: un arresto ajeno arruina la partida de ambos, que es el síntoma reportado.
14. Recomendación: (a) para misiones coopiadas, con el arrestado en espectador hasta el fin; (b) como evolución si se quiere down/revive.

## D. Opcodes en los 100 scripts con `EnableSyncingThisScript`

15. `IsOpcodeSyncable` exige autoridad (`COpCodeSync.cpp:384`), así que una misión ejecutada por un invitado que no es dueño no manda nada; `Coop.*ForAll` exige `IsScriptAuthority() && IsScriptSynced`.

| Área | Estado | Riesgo |
|---|---|---|
| Cutscene (`0x02E4`/`0x02E7`/`0x02EA`) | Sincronizado | Alto: handshake de 2 pasos (A.3) |
| Cámara (5.477 usos) | Mayoría sincronizada; faltan `AttachToVehicleLookAtVehicle` (3), `SetInFrontOfPlayer` (2), primera persona / player-in-car, tweak de vehículo | Medio |
| Texto (4.970) | `Print`/`PrintNow`/`PrintBig`/`PrintHelp`/clear/`LoadMissionText` sincronizados; faltan `Display` (47), `ClearSmallPrints` (25), `DisplayWithNumber` (6), `PrintWithNumber` (4), `DisplayWith2Numbers` (3) | Alto: mensajes no vistos |
| Audio de misión (3.667) | load/play/clear por hooks + `CLEAR_MISSION_AUDIO`; faltan `Report...AtObject` (71), `Attach...ToChar/Car/Object` (33), `SetMissionAudioPosition` (11) | Medio |
| Blips (5.851) | Ninguno en `syncedOpcodes`; solo `Coop.Update*` de char/coche/zona | Alto |
| Checkpoint (66) | Ninguno; solo la zona de `Coop.UpdateCheckpoint...` | Medio |
| HUD (788) | radar/zona/nombres sincronizados; faltan `SetRadarZoom` (42) y primitivas de dibujo | Bajo/Medio |

## E. Arreglos mínimos priorizados (sin implementar) y pruebas

16. `COpCodeSync.cpp:1249-1264` + `Main.cpp:124-129`: reintentar `start_cutscene` con cola/timeout y sin depender de `ms_cutsceneName[0]`. Probar: dueño invitado, el otro llega segundo, cinemática completa y puerta correcta.
17. `CNetworkEntityBlip.cpp`: guardar blip pendiente por entidad y aplicarlo cuando `m_pVehicle`/`m_pPed` exista (hook de stream-in o reintento en `Process`). Probar: llegar tarde al marcador y ver la flecha azul del coche.
18. Sincronizar `Blip.AddForCoord`/`RemoveBlip` con remapeo de handle, o extender `Coop.UpdateCheckpointForNetworkPlayer` para crear blip de radar. Probar: ambos ven el blip de destino en el minimapa.
19. `PlayerHooks.cpp:465-477`: que la autoridad solo marque fracaso por su propio estado (o con todos caídos) y lo notifique; el arrestado reaparece/espectador. Probar: arrestar por separado al invitado y al dueño.
20. Regresión con 2 clientes y `base` como referencia: cinemática, blips de coche/destino, busted y "mission passed".
