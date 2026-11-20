# Omitir avisos de primera muerte y arresto

Archivos: COpCodeSync.cpp, CConfigLoader.h/.cpp, UI/CModeMenu.cpp, INTERRUPTORES.md, ESTADO.md, TESTEAR_HOY.md y esta tanda.
Activación: F1, skip-death-arrest-help=false por defecto. Solo HELP local con scripts originales, claves HOSP_1–3 y BUST_1–4 y WAIT 8000 contiguo verificado en main.scm vanilla.
Casos borde: prueba con host e invitado, F1 individual y 3–4. Muerte/arresto durante misión coop deben seguir fallando para todos; desconectar no arrastra estado. Los otros mensajes y esperas de HELP siguen su flujo nativo. Desactivado = tutorial original.
Aceptación: compila y se comprueban primera muerte/arresto, recuperación de cámara/control, segundo respawn y fallo de misión. Resumen de hasta 10 líneas.

Cliente compilado. Pendiente prueba en juego.
