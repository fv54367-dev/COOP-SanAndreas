<#
.SYNOPSIS
    Corre el testbot del server (tools/testbot) contra un server local.

.DESCRIPTION
    Un server fresco por escenario (aislamiento: cada escenario arranca con el server
    limpio) y un proceso del testbot por escenario (si uno falla, se ve cual, y el
    estado de un escenario no contamina al siguiente).

    Los escenarios a, b, c, e, f, g, h, i, j, k, l, m, n, o necesitan handover = 1;
    el d necesita handover = 0 (ver PrintUsage del testbot).

    Si un escenario falla se reintenta UNA vez. Pasar
    al reintento se informa como PASS (FLAKY) y no rompe la CI, pero queda avisado y
    contado en el resumen. Solo es falla si falla las dos veces.

    El server se cierra por su propio objeto de proceso: NO se busca por ruta, asi que
    nunca se toca otro server.exe del equipo (el del juego, otro testbot, etc).

    Los logs quedan en <ServerDir>\logs\server-<esc>.log y testbot-<esc>.log: en la CI
    se suben como artefacto cuando algo falla.

    Sale con 0 si todos los escenarios pasan, con 1 si alguno falla o se cuelga.

.PARAMETER Build
    Carpeta con server.exe y testbot.exe. Por defecto build/windows/x86/release.

.PARAMETER Port
    Puerto del server de prueba.

.PARAMETER ServerDir
    Carpeta de trabajo del server (se recrea). Por defecto build/testbot.

.PARAMETER TimeoutSec
    Segundos maximos por escenario antes de darlo por colgado.

.PARAMETER Escenarios
    Lista separada por comas para correr solo esos (ej: a,d). Sin el parametro corren todos.

.EXAMPLE
    pwsh -File .github/scripts/correr_testbot.ps1

.EXAMPLE
    pwsh -File .github/scripts/correr_testbot.ps1 -Escenarios a,b,c
#>
[CmdletBinding()]
param(
    [string]$Build = '',
    [int]$Port = 6811,
    [string]$ServerDir = '',
    [int]$TimeoutSec = 240,
    [string]$Escenarios = ''
)

$ErrorActionPreference = 'Stop'

# $PSScriptRoot no existe en los defaults de param en PS 5.1: se resuelve en el cuerpo.
# El script vive en .github/scripts/: la raiz del repo son dos niveles arriba.
$scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$repoRoot = Split-Path -Parent (Split-Path -Parent $scriptDir)
if (-not $Build) { $Build = Join-Path $repoRoot 'build\windows\x86\release' }
if (-not $ServerDir) { $ServerDir = Join-Path $repoRoot 'build\testbot' }

$serverExe = Join-Path $Build 'server.exe'
$testbotExe = Join-Path $Build 'testbot.exe'
foreach ($f in @($serverExe, $testbotExe)) {
    if (-not (Test-Path -LiteralPath $f)) { throw "Falta $f - compila antes: xmake build server testbot" }
}

# escenarios -> handover que necesitan
$todos = [ordered]@{
    'a' = 1; 'b' = 1; 'c' = 1; 'e' = 1; 'f' = 1; 'g' = 1; 'h' = 1; 'i' = 1
    'j' = 1; 'k' = 1; 'l' = 1; 'm' = 1; 'n' = 1; 'o' = 1; 'd' = 0
}

# -Escenarios a,b,c corre solo esos (util para probar un cambio puntual sin esperar
# los ~5 minutos de la tanda completa). Sin el parametro corren todos.
$aCorrer = [ordered]@{}
if ($Escenarios) {
    $pedidos = $Escenarios -split '[,;\s]+' | Where-Object { $_ }
    foreach ($e in $pedidos) {
        if (-not $todos.Contains($e)) { throw "Escenario desconocido: '$e' (validos: $($todos.Keys -join ', '))" }
        $aCorrer[$e] = $todos[$e]
    }
} else {
    foreach ($k in $todos.Keys) { $aCorrer[$k] = $todos[$k] }
}
$total = $aCorrer.Count

if (Test-Path -LiteralPath $ServerDir) { Remove-Item -LiteralPath $ServerDir -Recurse -Force }
$logsDir = Join-Path $ServerDir 'logs'
New-Item -ItemType Directory -Force -Path $logsDir | Out-Null
Copy-Item -LiteralPath $serverExe -Destination $ServerDir

function Stop-Server($proc) {
    if ($null -eq $proc) { return }
    try {
        if (-not $proc.HasExited) { $proc.Kill() }
        $proc.WaitForExit(5000) | Out-Null
    } catch { }
}

function Escribir-Config([int]$handover) {
    $cfg = @(
        "port = $Port",
        'maxplayers = 8',
        'persistence = 0',
        'name = CI testbot',
        'test-commands = 0',
        'sim-latency-ms = 0',
        'sim-jitter-ms = 0',
        'sim-loss-percent = 0',
        "handover = $handover",
        'handover-ped-radius = 100',
        'handover-vehicle-radius = 200'
    )
    Set-Content -LiteralPath (Join-Path $ServerDir 'server-config.ini') -Value $cfg -Encoding ascii
}

# Un escenario = un server fresco + un proceso del testbot. Devuelve el veredicto como
# objeto en vez de decidir aca, para poder reintentarlo una vez. $sufijo va al nombre de
# los logs: el reintento NO pisa el log del primer intento (es el que se mira cuando un
# escenario sale flaky).
function Invoke-Escenario([string]$esc, [int]$handover, [string]$sufijo = '') {
    $logServer = Join-Path $logsDir "server-$esc$sufijo.log"
    $logTestbot = Join-Path $logsDir "testbot-$esc$sufijo.log"

    Escribir-Config -handover $handover
    $procServer = Start-Process -FilePath (Join-Path $ServerDir 'server.exe') -WorkingDirectory $ServerDir `
        -WindowStyle Hidden -RedirectStandardOutput $logServer -RedirectStandardError "$logServer.err" -PassThru
    $procBot = $null
    try {
        Start-Sleep -Milliseconds 1200

        $procBot = Start-Process -FilePath $testbotExe -ArgumentList @('127.0.0.1', "$Port", $esc) `
            -WorkingDirectory $ServerDir -WindowStyle Hidden -RedirectStandardOutput $logTestbot `
            -RedirectStandardError "$logTestbot.err" -PassThru

        $terminado = $procBot.WaitForExit($TimeoutSec * 1000)
        if (-not $terminado) {
            return [pscustomobject]@{
                Ok = $false; Timeout = $true; Motivo = "timeout (${TimeoutSec}s)"
                Fails = @(); Resumen = ''; LogServer = $logServer; LogTestbot = $logTestbot
            }
        }

        # PS 5.1 con Start-Process -PassThru + redireccion deja ExitCode vacio: el
        # veredicto se toma de la salida del testbot ("RESULTADO: ... TODO PASS").
        # Si el ExitCode SI esta disponible, tambien tiene que dar 0.
        $procBot.WaitForExit()
        $salida = (Get-Content -LiteralPath $logTestbot -Raw -ErrorAction SilentlyContinue)
        $resumen = ($salida -split "`r?`n" | Where-Object { $_ -match 'RESULTADO' } | Select-Object -Last 1)

        $codigoTxt = ''
        try { $codigoTxt = "$($procBot.ExitCode)" } catch { }
        $salidaOk = ($salida -match 'TODO PASS') -and ($salida -notmatch 'HAY FALLAS')
        $exitOk = [string]::IsNullOrWhiteSpace($codigoTxt) -or ($codigoTxt -eq '0')
        $motivo = if (-not [string]::IsNullOrWhiteSpace($codigoTxt)) { "exit $codigoTxt" } else { 'sin TODO PASS' }
        $fails = @($salida -split "`r?`n" | Where-Object { $_ -match '\[FAIL\]' } | Select-Object -First 20)

        return [pscustomobject]@{
            Ok = ($salidaOk -and $exitOk); Timeout = $false; Motivo = $motivo
            Fails = $fails; Resumen = $resumen; LogServer = $logServer; LogTestbot = $logTestbot
        }
    } finally {
        Stop-Server $procBot
        Stop-Server $procServer
    }
}

$fallas = @()
$flaky = @()
$ok = 0

foreach ($kv in $aCorrer.GetEnumerator()) {
    $esc = $kv.Key
    $handover = $kv.Value
    Write-Host ''
    Write-Host "=== escenario ($esc) - handover $handover ==="

    $r = Invoke-Escenario $esc $handover
    if ($r.Resumen) { Write-Host $r.Resumen }

    if ($r.Ok) {
        Write-Host ">>> escenario ($esc): PASS"
        $ok++
        continue
    }

    # Falla: se reintenta UNA vez. Se observo intermitencia el 2026-11-17
    # en (f), "INPUT del segundo conserva mouse y teclas": fallo una vez y paso al
    # reintentar). Pasar al reintento = FLAKY: se avisa y se cuenta, pero no rompe la CI.
    # Dos fallos hacen fallar el runner; la causa de la intermitencia sigue pendiente.
    if ($r.Timeout) {
        Write-Host ">>> escenario ($esc): TIMEOUT - el testbot se colgo; se reintenta una vez"
    } else {
        Write-Host ">>> escenario ($esc): FAIL ($($r.Motivo)) - se reintenta una vez"
    }
    if ($r.Fails.Count -gt 0) { Write-Host ($r.Fails -join "`n") }
    Write-Host "--- intento 1: ultimas lineas del server ($esc) ---"
    Get-Content -LiteralPath $r.LogServer -Tail 40 -ErrorAction SilentlyContinue | ForEach-Object { "    $_" }

    $r2 = Invoke-Escenario $esc $handover '-reintento'
    if ($r2.Resumen) { Write-Host $r2.Resumen }

    if ($r2.Ok) {
        Write-Host ">>> escenario ($esc): PASS (FLAKY) - fallo el primer intento y paso al reintentar"
        Write-Host "    AVISO: ($esc) es flaky; el log del primer intento quedo en $($r.LogTestbot)"
        $flaky += $esc
        $ok++
        continue
    }

    if ($r2.Timeout) {
        Write-Host ">>> escenario ($esc): FALLA - segundo intento con TIMEOUT"
    } else {
        Write-Host ">>> escenario ($esc): FALLA - segundo intento ($($r2.Motivo))"
    }
    if ($r2.Fails.Count -gt 0) { Write-Host ($r2.Fails -join "`n") }
    Write-Host "--- intento 2: ultimas lineas del server ($esc) ---"
    Get-Content -LiteralPath $r2.LogServer -Tail 40 -ErrorAction SilentlyContinue | ForEach-Object { "    $_" }
    $fallas += "$esc ($($r2.Motivo))"
}

Write-Host ''
if ($flaky.Count -gt 0) {
    Write-Host "AVISO: $($flaky.Count) escenario(s) FLAKY (fallaron una vez y pasaron al reintentar): $($flaky -join ', ')"
    Write-Host "       La CI no falla por esto, pero hay que mirarlo: logs testbot-<esc>.log (primer intento) y testbot-<esc>-reintento.log"
}
if ($fallas.Count -eq 0) {
    $detalleFlaky = if ($flaky.Count -gt 0) { ", $($flaky.Count) flaky" } else { '' }
    Write-Host "TODO PASS - $ok escenarios$detalleFlaky"
    exit 0
}
Write-Host "HAY FALLAS: $($fallas.Count) de $total escenarios -> $($fallas -join ', ')"
Write-Host "Logs: $logsDir"
exit 1
