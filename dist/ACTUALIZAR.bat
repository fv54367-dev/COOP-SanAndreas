@echo off
setlocal
rem COOP San Andreas - actualizador. Ponelo junto a gta_sa.exe y hacele doble clic: elegis que version instalar.
set "GTA_DIR=%~dp0"
set "SELF=%~f0"
powershell.exe -NoProfile -Command "iex (((Get-Content -Raw -LiteralPath $env:SELF) -split ('#PS'+'-START'),2)[1])"
set "RC=%errorlevel%"
rem started from the CoopSA update button: on success reopen the launcher instead of pausing
if "%COOPSA_LANZADOR%%RC%"=="10" (start "" "%GTA_DIR%CoopSA.exe") else pause
rem replace this updater last, on one line: cmd keeps reading the file after it changes
if exist "%SELF%.new" (move /Y "%SELF%.new" "%SELF%" >nul & exit /b %RC%)
exit /b %RC%
#PS-START
$ErrorActionPreference = 'Stop'
$repo = 'fv54367-dev/COOP-SanAndreas'
$gta = $env:GTA_DIR
$version = Join-Path $gta 'CoopAndreas_version.txt'

function Falla([string]$texto) {
    Write-Host ''
    Write-Host "ERROR: $texto" -ForegroundColor Red
    exit 1
}

function Bajar([string]$url, [string]$destino) {
    & curl.exe -fsSL -H 'Accept: application/octet-stream' -o $destino $url
    if ($LASTEXITCODE -ne 0) { Falla 'Fallo la descarga. Revisa la conexion y reintenta.' }
}

if (-not (Test-Path -LiteralPath (Join-Path $gta 'gta_sa.exe'))) { Falla 'Este .bat tiene que estar junto a gta_sa.exe.' }
if ($env:COOPSA_LANZADOR) {
    # the launcher closes itself after starting this .bat; CoopSA.exe can't be replaced while it runs
    for ($i = 0; $i -lt 40 -and (Get-Process CoopSA -ErrorAction SilentlyContinue); $i++) { Start-Sleep -Milliseconds 250 }
}
if (Get-Process gta_sa,server -ErrorAction SilentlyContinue) { Falla 'Cerra el GTA y el servidor y volve a abrir este .bat.' }

$json = & curl.exe -fsSL -H 'Accept: application/vnd.github+json' "https://api.github.com/repos/$repo/releases?per_page=100"
if ($LASTEXITCODE -ne 0) { Falla 'No se pudo consultar GitHub. Revisa la conexion y reintenta.' }
$canales = @(($json -join "`n" | ConvertFrom-Json) | Where-Object { $_.tag_name -like 'canal-*' } |
    Sort-Object @{ e = { if ($_.tag_name -eq 'canal-main') { 0 } elseif ($_.tag_name -eq 'canal-todo') { 1 } else { 2 } } }, tag_name)
if ($canales.Count -eq 0) { Falla 'Todavia no hay versiones publicadas.' }

if (Test-Path -LiteralPath $version) { Write-Host "Tenes instalado: $(Get-Content -LiteralPath $version -TotalCount 1)" }
Write-Host ''
for ($i = 0; $i -lt $canales.Count; $i++) { Write-Host "  $($i + 1). $($canales[$i].name)" }
Write-Host ''

if ($env:CANAL) {
    $elegido = @($canales | Where-Object { $_.tag_name -eq "canal-$($env:CANAL)" })[0]
    if (-not $elegido) { Falla "No existe el canal $($env:CANAL)." }
} else {
    $resp = Read-Host 'Que version instalo? (Enter = 1)'
    if (-not $resp) { $resp = '1' }
    $n = 0
    if (-not [int]::TryParse($resp, [ref]$n) -or $n -lt 1 -or $n -gt $canales.Count) { Falla 'Opcion invalida.' }
    $elegido = $canales[$n - 1]
}

$zipAsset = @($elegido.assets | Where-Object { $_.name -eq 'coopandreas.zip' })[0]
$manAsset = @($elegido.assets | Where-Object { $_.name -eq 'manifest.json' })[0]
if (-not $zipAsset -or -not $manAsset) { Falla 'Ese canal todavia se esta publicando. Reintenta en un minuto.' }

$respaldo = Join-Path $gta ('_backup_mod\canal-' + (Get-Date -Format 'yyyyMMdd-HHmmss'))
$stage = Join-Path $respaldo 'descarga'
New-Item -ItemType Directory -Force -Path $stage | Out-Null

Write-Host "Descargando $($elegido.name)..."
$zip = Join-Path $stage 'coopandreas.zip'
$man = Join-Path $stage 'manifest.json'
Bajar $zipAsset.url $zip
Bajar $manAsset.url $man
$manifest = Get-Content -Raw -LiteralPath $man | ConvertFrom-Json
if ((Get-FileHash -LiteralPath $zip -Algorithm SHA256).Hash -ne $manifest.zipSha256) { Falla 'El archivo bajado no coincide con su hash. No se instalo nada. Reintenta en un minuto.' }
$archivos = Join-Path $stage 'archivos'
Expand-Archive -LiteralPath $zip -DestinationPath $archivos -Force
foreach ($viejo in @('LaunchCoopAndreas.exe', 'LaunchCoopAndreas.exe.manifest')) {
    $renombre = 'CoopSA' + $viejo.Substring('LaunchCoopAndreas'.Length)
    if (-not (Test-Path -LiteralPath (Join-Path $archivos $renombre)) -and (Test-Path -LiteralPath (Join-Path $archivos $viejo))) {
        Move-Item -LiteralPath (Join-Path $archivos $viejo) -Destination (Join-Path $archivos $renombre) -Force
    }
}

$destinos = @(
    @('CoopAndreasSA.dll', ''),
    @('CoopSA.exe', ''),
    @('CoopSA.exe.manifest', ''),
    @('MODO_SCRIPTS_ORIGINALES.bat', ''),
    @('main.scm', 'CoopAndreas'),
    @('script.img', 'CoopAndreas'),
    @('server.exe', 'SERVIDOR')
)
foreach ($d in $destinos) {
    if (-not (Test-Path -LiteralPath (Join-Path $archivos $d[0]))) { Falla "Al paquete le falta $($d[0]). No se instalo nada." }
}

foreach ($d in $destinos) {
    $actual = Join-Path (Join-Path $gta $d[1]) $d[0]
    if (Test-Path -LiteralPath $actual) {
        $carpeta = Join-Path $respaldo $d[1]
        New-Item -ItemType Directory -Force -Path $carpeta | Out-Null
        Copy-Item -LiteralPath $actual -Destination $carpeta -Force
    }
}

$copiados = @()
try {
    foreach ($d in $destinos) {
        $carpeta = Join-Path $gta $d[1]
        New-Item -ItemType Directory -Force -Path $carpeta | Out-Null
        $actual = Join-Path $carpeta $d[0]
        Copy-Item -LiteralPath (Join-Path $archivos $d[0]) -Destination $actual -Force
        $copiados += , $d
    }
} catch {
    foreach ($d in $copiados) {
        $previo = Join-Path (Join-Path $respaldo $d[1]) $d[0]
        if (Test-Path -LiteralPath $previo) { Copy-Item -LiteralPath $previo -Destination (Join-Path (Join-Path $gta $d[1]) $d[0]) -Force }
    }
    Falla "No se pudo copiar un archivo; se volvio a la version anterior. $($_.Exception.Message)"
}

# coop rules as data (CMissionRulesData); optional: older packages do not have it
$reglas = Join-Path $archivos 'reglas_misiones.txt'
if (Test-Path -LiteralPath $reglas) {
    Copy-Item -LiteralPath $reglas -Destination (Join-Path (Join-Path $gta 'CoopAndreas') 'reglas_misiones.txt') -Force
}

# the loader (proxy eax.dll, starts the mod only with --coop): the game's own eax.dll becomes eax_orig.dll the first time
$proxy = Join-Path $archivos 'eax.dll'
if (Test-Path -LiteralPath $proxy) {
    $eax = Join-Path $gta 'eax.dll'
    $orig = Join-Path $gta 'eax_orig.dll'
    if (-not (Test-Path -LiteralPath $orig) -and (Test-Path -LiteralPath $eax)) { Move-Item -LiteralPath $eax -Destination $orig }
    if (Test-Path -LiteralPath $eax) { Copy-Item -LiteralPath $eax -Destination $respaldo -Force }
    Copy-Item -LiteralPath $proxy -Destination $eax -Force
}

foreach ($viejo in @('LaunchCoopAndreas.exe', 'LaunchCoopAndreas.exe.manifest')) {
    $previoGta = Join-Path $gta $viejo
    if (Test-Path -LiteralPath $previoGta) {
        Move-Item -LiteralPath $previoGta -Destination (Join-Path $respaldo $viejo) -Force
    }
}

# Open Limit Adjuster moves the script memory unless these two are 0: the coop then misses every mission (14-11)
foreach ($ola in @((Join-Path $gta 'III.VC.SA.LimitAdjuster.ini'), (Join-Path $gta 'scripts\III.VC.SA.LimitAdjuster.ini'))) {
    if (-not (Test-Path -LiteralPath $ola)) { continue }
    $lineas = [System.Collections.Generic.List[string]]@(Get-Content -LiteralPath $ola)
    $lineas.RemoveAll([Predicate[string]]{ param($l) $l -match '^\s*(MAINsegmentSize|MissionSize)\s*=' }) | Out-Null
    $sec = $lineas.FindIndex([Predicate[string]]{ param($l) $l.Trim() -eq '[SALIMITS]' })
    if ($sec -lt 0) { $lineas.Add('[SALIMITS]'); $sec = $lineas.Count - 1 }
    $lineas.InsertRange($sec + 1, [string[]]@('MAINsegmentSize = 0', 'MissionSize = 0'))
    Set-Content -LiteralPath $ola -Value $lineas -Encoding Default
    Write-Host 'Open Limit Adjuster: memoria de scripts sin mover (MAINsegmentSize y MissionSize en 0).'
}

Set-Content -LiteralPath $version -Value $manifest.etiqueta -Encoding ascii
$nuevoBat = Join-Path $archivos 'ACTUALIZAR.bat'
if (Test-Path -LiteralPath $nuevoBat) { Copy-Item -LiteralPath $nuevoBat -Destination "$($env:SELF).new" -Force }
Remove-Item -LiteralPath $stage -Recurse -Force

Write-Host ''
Write-Host "Listo: instalado $($manifest.etiqueta)." -ForegroundColor Green
Write-Host "Tu amigo tiene que ver este mismo codigo. Respaldo: $respaldo"
exit 0
