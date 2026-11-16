@echo off
setlocal
rem COOP San Andreas - usa los scripts ORIGINALES del juego (el coop lo hace la DLL, fase 6).
rem Ponelo en la carpeta de tu GTA (donde esta gta_sa.exe) y hacele doble clic con el GTA cerrado.
rem Las partidas de este modo van aparte (carpeta UserFilesOriginal): empezar partida NUEVA.
rem Para volver: MODO_SCRIPTS_COOP.bat (tus partidas de siempre quedan intactas en Documentos).

set "GTA=%~dp0"
if not exist "%GTA%gta_sa.exe" (echo Este .bat tiene que estar en la carpeta del GTA ^(donde esta gta_sa.exe^). & goto error)
if not exist "%GTA%data\script\main.scm" (echo Falta data\script\main.scm del juego. & goto error)
for %%F in ("%GTA%data\script\main.scm") do if not "%%~zF"=="3079599" (
    echo AVISO: tu data\script\main.scm no es el original ^(%%~zF bytes, el original pesa 3079599^).
    echo Si tenes otro mod que cambia los scripts, sacalo o pedi ayuda antes de jugar.
)
if not exist "%GTA%CoopAndreas" mkdir "%GTA%CoopAndreas"
if not exist "%GTA%UserFilesOriginal" mkdir "%GTA%UserFilesOriginal"

rem the first time, bring the game options and the coop name from the usual user files
for /f "usebackq delims=" %%D in (`powershell -NoProfile -Command "[Environment]::GetFolderPath('MyDocuments')"`) do set "DOCS=%%D\GTA San Andreas User Files"
for %%F in (gta_sa.set coopandreas.ini) do (
    if exist "%DOCS%\%%F" if not exist "%GTA%UserFilesOriginal\%%F" copy /Y "%DOCS%\%%F" "%GTA%UserFilesOriginal\%%F" >nul
)

set "INI=%GTA%CoopAndreas\coopandreas.ini"
if exist "%INI%" (findstr /v /i /b /c:"vanilla-scripts=" /c:"user-dir=" /c:"[config]" "%INI%" > "%INI%.tmp") else (type nul > "%INI%.tmp")
(
    echo [config]
    echo vanilla-scripts=true
    echo user-dir=UserFilesOriginal
    type "%INI%.tmp"
) > "%INI%"
del "%INI%.tmp"

echo.
echo Listo: scripts ORIGINALES activados. Empeza una partida NUEVA.
echo Los dos jugadores tienen que estar en el mismo modo.
pause
exit /b 0

:error
echo.
echo ERROR. Cerra el GTA y reintenta.
pause
exit /b 1
