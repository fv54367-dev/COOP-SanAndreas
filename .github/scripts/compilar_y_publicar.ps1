# Compila el commit actual y lo publica como canal (release canal-<nombre>) para dist/ACTUALIZAR.bat.
# Sin parametros deduce el canal de la rama: main -> canal-main, otra rama -> canal-<rama>.
param([string]$Canal = '', [string]$Titulo = '', [string]$Prefijo = '')

$ErrorActionPreference = 'Stop'
$sha = (git rev-parse --short=7 HEAD).Trim()
$pr = $null

if (-not $Canal) {
    $rama = $env:GITHUB_REF_NAME
    $Canal = ($rama.ToLower() -replace '[^a-z0-9]+', '-').Trim('-')
    if ($rama -eq 'main') {
        $Titulo = 'Principal'
        $Prefijo = 'main'
    } elseif ($rama -eq 'todo' -or $rama -match '^pr\d+$') {
        # public repo: tools/publicar.ps1 pushes the private PRs and their merge with the title as the commit subject
        $Titulo = (git log -1 --format=%s).Trim()
        $Prefijo = $rama
    } else {
        $abiertos = gh pr list --state open --head $rama --json number,title | ConvertFrom-Json
        if ($abiertos) {
            $pr = $abiertos[0]
            $Titulo = "PR $($pr.number): $($pr.title)"
            $Prefijo = "pr$($pr.number)"
        } else {
            $Titulo = "Rama $rama"
            $Prefijo = 'rama'
        }
    }
}
$etiqueta = "$Prefijo-$sha"
$tag = "canal-$Canal"

# the build label comes from the commit, branches never bump it by hand
$config = 'shared/build_label.h'
$texto = [IO.File]::ReadAllText($config)
$nuevo = $texto -replace '#define COOPSA_BUILD "[^"]*"', "#define COOPSA_BUILD `"$etiqueta`""
if ($nuevo -eq $texto) { throw 'No se pudo poner la etiqueta en shared/build_label.h' }
[IO.File]::WriteAllText($config, $nuevo)

foreach ($target in 'client', 'server', 'testbot', 'proxy', 'launcher') {
    xmake build $target
    if ($LASTEXITCODE -ne 0) { throw "Fallo el build de $target" }
}

$bld = 'build/windows/x86/release'
$stage = Join-Path $env:RUNNER_TEMP 'paquete'
New-Item -ItemType Directory -Force -Path $stage | Out-Null
foreach ($f in "$bld/CoopAndreasSA.dll", "$bld/CoopSA.exe", "$bld/CoopSA.exe.manifest",
               "$bld/server.exe", 'scm/main.scm', 'scm/script.img',
               'scm/reglas_misiones.txt', 'dist/MODO_SCRIPTS_ORIGINALES.bat', 'dist/ACTUALIZAR.bat') {
    Copy-Item -LiteralPath $f -Destination $stage
}
Copy-Item -LiteralPath "$bld/proxy.dll" -Destination (Join-Path $stage 'eax.dll')

$zip = Join-Path $env:RUNNER_TEMP 'coopandreas.zip'
Compress-Archive -Path (Join-Path $stage '*') -DestinationPath $zip -Force
$manifest = Join-Path $env:RUNNER_TEMP 'manifest.json'
@{
    canal     = $Canal
    etiqueta  = $etiqueta
    commit    = (git rev-parse HEAD).Trim()
    zipSha256 = (Get-FileHash -LiteralPath $zip -Algorithm SHA256).Hash
} | ConvertTo-Json | Set-Content -LiteralPath $manifest -Encoding ascii

gh release view $tag *> $null
if ($LASTEXITCODE -ne 0) {
    # the combined merge only exists inside the CI, so its tag points at main
    $destino = if ($Canal -eq 'todo' -and $env:GITHUB_REF_NAME -ne 'todo') { (git rev-parse origin/main).Trim() } else { (git rev-parse HEAD).Trim() }
    gh release create $tag --prerelease --target $destino --title "$Titulo [$etiqueta]" --notes 'Canal automatico.'
    if ($LASTEXITCODE -ne 0) { throw 'No se pudo crear el release' }
}
gh release upload $tag $zip $manifest dist/ACTUALIZAR.bat --clobber
if ($LASTEXITCODE -ne 0) { throw 'No se pudieron subir los archivos' }
gh release edit $tag --title "$Titulo [$etiqueta]"
if ($LASTEXITCODE -ne 0) { throw 'No se pudo actualizar el titulo' }

if ($pr) {
    $cuerpo = "Build lista para probar: **$etiqueta**. En ACTUALIZAR.bat elegi este PR o Todo junto."
    gh pr comment $pr.number --edit-last --body $cuerpo *> $null
    if ($LASTEXITCODE -ne 0) { gh pr comment $pr.number --body $cuerpo }
}
exit 0
