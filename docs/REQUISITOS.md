# Requisitos del usuario (fuente de verdad del producto)

Dichos por el usuario (2026-10-19 y 2026-10-20). Si algo choca con el código actual, **gana el requisito**; el código se reescribe si hace falta. Preguntarle solo lo que no esté acá. Tabla misión → modos: `docs/MISIONES_MODO.md`.

## 0. Alcance
- Un buen **GTA San Andreas coop de historia** (y secundarias) para **2 a 4 jugadores**. Todo diseño debe escalar a 4. El usuario prueba con 2 GTA en su PC (`PROBAR_SOLO.bat`); con un amigo por Radmin VPN, y más adelante una comunidad de Discord probará con más jugadores.
- Se puede **reescribir** cualquier cosa hecha antes si el resultado es mejor.

## 1. Misiones: modo individual o coop (historia Y secundarias)
- **Modo por jugador** (menú F1): *coop* (por defecto) o *individual*. Se sincroniza; el chat avisa los cambios.
  - **Individual**: el marcador solo lo pide a él, puede iniciar solo y nada de su misión se transmite a los demás (cinemáticas, cámara, textos, marcadores). No cuenta para esperar a otros ni es invitado a coop.
  - **Coop**: para iniciar en el marcador hacen falta los demás jugadores que también estén en coop; los individuales no cuentan. Si todos los demás están en individual, se juega como solo.
- **Bloqueo por misión de historia**: si un jugador juega una misión de historia (individual o coop), ningún otro la inicia hasta que termine/falle/se vaya; el marcador ni arranca y muestra un cartel de 5 s con quién la juega. Las **secundarias, minijuegos y escuelas no se bloquean** (el mismo tipo lo pueden jugar varios a la vez).
- **Fallo en coop**: si un participante muere o lo arrestan, la misión se pierde para todos. **Abandonar** (mantener `2`) saca solo al que abandona; si el **dueño** cancela, termina para todos y se avisa en el chat. Fallo en individual: solo afecta a quien la juega.
- Cesar Vialpando: coop (puntaje en equipo) e individual. Solo individuales: Taxi, Chulo, escuelas (coches, motos, náutica, aviación). Paramédico y Vigilante: coop e individual. Carreras: individual **o batalla VS**, con o sin NPC según la opción elegida (participan los que aceptan la invitación, arrancan juntos y ven el HUD de posiciones y la tabla de resultados). Resto de la historia: coop e individual, y se corrigen caso por caso las que no se puedan.
- **Progreso por jugador**: lo individual avanza solo a quien lo juega. Si los de una misión coop de historia tienen progresos distintos en el marcador de Sweet, no arranca y se avisa con los nombres de las misiones. Comandos: `/mision` (ver cuál sigue) e `/igualar <jugador>` (pasar tu progreso al suyo).
- Mensajes claros siempre ("misión en curso por X", "saliste de la misión", "cancelaste…").
- **Autos de invitados en misiones (2026-11-11): el mínimo de vehículos.** Máximo 4 jugadores (host + 3). Primero se usan los asientos libres del auto del host, sin quitarle el asiento a ningún NPC de la misión. Los que no entran van en **un solo** auto compartido de 4 puertas (aunque el del host sea de 2), o de a 2 en moto. Si el host va solo en un 4 puertas, no se crea nada. Nunca un auto por cada invitado.

## 2. Todos ven lo mismo (coop)
- **Barras y contadores de una mision coop: uno solo para todos** (pedido del 2026-11-10). En Home Invasion el ruido de invitados se suma a la barra del dueno; todos ven su mismo valor y visibilidad.

En una misión coop **todos los participantes ven lo que ve el que la inició**:
- **Cinemáticas**, ángulos de cámara, fades, widescreen: iguales y a la vez. El **CJ de una cinemática lleva la ropa del host** en todos.
- **Textos**: objetivos, diálogos/subtítulos, help boxes, "mission passed/failed", HUD (reloj y contadores).
- **Blips y marcadores** de auto, personajes y destino, incluidos el minimapa/radar con el tamaño correcto.
- Objetos y estado del mundo de la misión.
Referencia: el "mission mirror layer" de `_refs\project_gta_sa_coop\docs\design\mirror-layer.md` (GPL-3.0, reutilizable con atribución).

## 3. Robo de vehículos a NPCs
Al robarle el auto a un NPC: el NPC hace la animación de ser sacado, se da cuenta (huye o pelea como en el juego normal) y se ve igual en los dos clientes.

## 4. Mundo y jugabilidad (decisiones tomadas)
- **El mundo nunca se pausa** (menú de pausa, alt-tab).
- **Dinero y estrellas de búsqueda: siempre de cada jugador.** No sincronizar pickups fijos, sobornos ni propiedades.
- **Coleccionables** (graffitis, herraduras, ostras, fotos): compartidos entre los jugadores que están en modo **coop** del F1 (cada uno recibe la recompensa y el mensaje del juego). Quien está en **individual** no recibe el progreso de los demás ni comparte el suyo (2026-10-24).
- Cada jugador tiene sus NPCs alrededor (población por zonas, traspaso entre dueños). Funciona "bastante bien".
- **Guardado**: `/guardar` abre el menú de guardado del juego (el del pickup de las casas); `/guardarsv` guarda el estado de todos en el server. El server guarda también cada 10 min y al terminar una misión.
- Modo prueba: `/traer`, `/ir`, `/tp`, `/caminar`, F8 trucos, T/F6 chat.

## 5. Calidad y estabilidad
- Los crashes son prioridad alta; buscar la causa de fondo antes de seguir agregando features, y **usar mucho `_refs\gta-reversed`** (ver `AGENTS.md`). Lo parchado sin causa de fondo: peds sin animación, sombras de estencil, partículas colgantes al cargar partida.
- Probar siempre con 2 clientes como mínimo; el usuario prueba y reporta; Claude no puede jugar.

## 6. Cómo quiere trabajar con Claude
- Respuestas cortas en español. Que Claude haga solo todo lo que pueda (instalar, compilar, analizar logs, leer la memoria de los GTA abiertos); pedirle ayuda al usuario solo para cosas puntuales (jugar).
- **Claude cierra los GTA y el server de prueba e instala** (`ACTUALIZAR.bat` con `CANAL=main`) sin preguntar, y **no los vuelve a abrir**: los lanza el usuario. Si el usuario está probando, no compilar ni instalar (le baja los FPS).
- **Rama o `main`, y si se integra a `main`, lo decide el usuario** (2026-11-17).
- La documentación debe ser corta y permitir arrancar una sesión nueva con buen contexto (lista en `AGENTS.md`, "Sesión nueva").
