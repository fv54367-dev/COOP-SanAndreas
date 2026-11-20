# Despedir reclutas de población remota

Archivos: Hooks/PlayerHooks.cpp, CConfigLoader.h/.cpp, INTERRUPTORES.md, ESTADO.md, TESTEAR_HOY.md y esta tanda.
Causa verificada: DisbandPlayerGroup llama a RemoveAllFollowers(true), que omite CreatedBy=2; las copias remotas se crean así aunque su registro de red indique población aleatoria. Tras la orden nativa, quitar solo copias de población de la banda local.
Casos borde: host e invitado, 3–4 jugadores, F1 individual, misión con seguidor creado por script, muerte/desconexión. No retirar miembros de otro grupo ni cambiar dueño. Apagado conserva el flujo anterior.
Aceptación: mantener H quita el blip y el miembro de población del grupo. Queda aparte el seguimiento cuando el recluta tiene otro dueño.

Cliente compilado. Pendiente prueba en juego.
