Añadir una opcion en el f1 para solamente reproducir cinematicas con un solo CJ, y no varios. se puede sin romper cosas? y lo mismo otra opcion para si reproducir la cinematica con varios CJ, si entendes a lo que me refiero? si lo entendes confirmame y si no haceme preguntas asi te explico bien.

8-Track:
Ala hora de entrar en el marcador debería de asignarle ala tecla 4, la función de poder salir del marcador en caso de meterse sinquerer, y otra cosa, cuando un jugador sea descalificado que no vuelva al viejo menu, sino que se conserve el que estamos usando, también podrías incluir, que cuando un jugador sea descalificado aparezca tachado en rojo en nuestra tabla de clasificacion.

Carreras:
1.Usar nuestra tabla de clasificaciones/posiciones para todas, eliminar la del gta nativo, solamente usar la nuestra.
2.Que nuestra tabla de clasificaciones/posiciones detecte automaticamente si estas jugando solo contra NPCS, tambien clasifica a los npcs y a ti, y lo mismo si hay jugadores + npcs, que clasifique a todos, y solamente contra jugadores lo mismo solo jugadores.

Home Invasion:
1.Al empezar la mision si son mas de 3 jugadores invitados spawnear un greenwood cerca del vehiculo de la mision para que se puedan subir.
---
Aclaraciones del usuario (2026-11-16):
- Cinemáticas, opción "un solo CJ": cada jugador se ve a sí mismo como el CJ de la cinemática (opción a). Es local: cada uno según su propia opción de F1, aunque estén en la misma misión.
- Home Invasion: el Greenwood encaja con la regla general de autos de invitados (asientos libres del host primero, después 1 auto de 4 puertas).

---
Hecho (Claude, rama `tanda-domingo`, sin probar):
1. Cinemáticas: F1 "Un solo CJ / Varios CJ" (`cutscene-show-others`, CCutscenePartners esconde las copias en esta pantalla).
2. 8-Track/VS: tecla 4 sale del marcador (CVsLobby); la tabla sigue hasta el fin de la carrera local; descalificado tachado en rojo (CVsRace).
3. Carreras: tabla en todas, solo contra NPC incluido (CVsRace::ms_bSoloBoard).
4. Home Invasion: Greenwood compartido si los invitados no entran en la BOXBURG (CMissionSyncRules + regla "home-invasion").
Duda: "descalificado → no vuelve al viejo menú" se interpretó como el panel viejo de puesto/vueltas que volvía al quedar uno solo corriendo.
