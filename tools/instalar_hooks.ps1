# Instala los hooks versionados de .githooks/ en este clon.
#
# Uso:  powershell -NoProfile -ExecutionPolicy Bypass -File tools\instalar_hooks.ps1
#
# Que hace: git config core.hooksPath .githooks
# Es local al clon (no se versiona la config): cada copia lo corre una vez.
#
# Hook incluido:
#   commit-msg -> avisa si el asunto no tiene prefijo (feat:, fix:, refactor:, ...).
#                 Bloquea solo con COOP_STRICT_COMMITS=1.

$ErrorActionPreference = 'Stop'

$repo = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
$hooksDir = Join-Path $repo '.githooks'

if (-not (Test-Path -LiteralPath (Join-Path $hooksDir 'commit-msg'))) {
    throw "No existe $hooksDir\commit-msg"
}

Push-Location $repo
try {
    git config core.hooksPath .githooks
    if ($LASTEXITCODE -ne 0) { throw 'Fallo git config core.hooksPath' }
    Write-Host "Hooks instalados: core.hooksPath = $(git config core.hooksPath)"
    Write-Host 'El hook commit-msg avisa si el asunto no tiene prefijo.'
    Write-Host 'Para bloquear de verdad: set COOP_STRICT_COMMITS=1'
} finally {
    Pop-Location
}
