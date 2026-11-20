# Tarea T86: dónde van (o no van) los invitados en ciertas cinemáticas

Leé `AGENTS.md`, `tareas\COMUN_reglas.md` y `tareas\TANDA_2026-11-10_pruebas_b.md`.

Código:
- `client/src/CScenePartner.cpp`: cómo se ubican los invitados en la escena y quién decide si salen en plano.
- `CSceneMocap.cpp`
- `SceneCompat/cutscene_ways.txt`
- `CSceneScript.cpp`

Si ya hay un mecanismo de reglas o excepciones por escena, usalo. Si no, creá uno chico y declarativo: una tabla por nombre de escena o por misión, con rango de tiempo o plano.

## Qué pidió el dueño
1. **Big Smoke (intro1):** después de CJ y Smoke en la casa, cuando se suben al auto, la copia del invitado persigue el auto corriendo. Que vaya dentro del auto si hay asiento; si no, que no aparezca.
2. **Big Smoke, cementerio:** caminando hacia la salida del cementerio, el invitado se queda duro (no tiene recorrido ni animación propios), aunque en las otras partes sí los tiene. Que tenga recorrido y animación, o que no aparezca en ese tramo.
3. **Cleaning the Hood (SWEET1B):**
   - Plano con la cámara desde el piso mirando hacia arriba a CJ y Ryder: los invitados se ven raros. Sacalos de plano o dales una animación decente.
   - Final de la misión, ya en Grove Street: sacalos de plano directamente.
4. **Drive-Thru, cinemática del Cluckin' Bell (pidiendo la comida):** los invitados se ven raros. Verificá el nombre del script (¿SWEET3?).
   - Si entraron a la misión en un auto: ponelos en un auto unos 2 m detrás del auto del jugador de la misión (detrás, preferentemente). Tiene sentido porque al terminar la cinemática los invitados aparecen dentro de un auto.
   - Si no entraron en auto: que no aparezcan en el plano.

5. **Nines and AKs (SWEET2), cinemática de Emmet** ("Y no lo olvidéis: ¡cuando hace falta un arma, Emmet es la solución!"): CJ y Smoke caminan por el callejón y los invitados quedan quietos. Que caminen igual que los otros, con recorrido y animación de caminar.

Regla general que se desprende: cuando la escena no tiene un lugar coherente para el invitado (persecución, plano de cerca del protagonista, toma muy cerrada), es mejor no mostrarlo que mostrarlo raro.

## Aceptación
Compilan client/server/testbot, testbot TODO PASS, CRLF. Prueba simple en `TESTEAR_HOY.md` por cada punto y "Pendiente" de `ESTADO.md`. No instales. Resumen ≤10 líneas.
