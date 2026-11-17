# Junta origin/main con los PR abiertos para el canal "todo junto". Solo vive en el CI: nunca se pushea.
$ErrorActionPreference = 'Stop'

$prs = gh pr list --state open --json number,headRefName | ConvertFrom-Json
if (-not $prs) {
    gh release delete canal-todo --yes --cleanup-tag *> $null
    "hay=false" >> $env:GITHUB_OUTPUT
    exit 0
}

git config user.name 'CI'
git config user.email 'ci@localhost'
# docs and committed binaries never decide whether two fixes can be played together
git config merge.ours.driver true
Add-Content -LiteralPath '.git/info/attributes' -Value "dist/** merge=ours`n*.md merge=ours"
git checkout --detach origin/main
if ($LASTEXITCODE -ne 0) { throw 'No se pudo pasar a origin/main' }

$entraron = @(); $saltados = @()
foreach ($pr in ($prs | Sort-Object number)) {
    git merge --no-edit --no-ff "origin/$($pr.headRefName)"
    if ($LASTEXITCODE -eq 0) {
        $entraron += "#$($pr.number)"
    } else {
        git merge --abort
        $saltados += "#$($pr.number)"
    }
}

if (-not $entraron) {
    gh release delete canal-todo --yes --cleanup-tag *> $null
    "hay=false" >> $env:GITHUB_OUTPUT
    exit 0
}

$titulo = "Todo junto: main + $($entraron -join ' ')"
if ($saltados) { $titulo += " (chocan y quedan afuera: $($saltados -join ' '))" }
"hay=true" >> $env:GITHUB_OUTPUT
"titulo=$titulo" >> $env:GITHUB_OUTPUT
exit 0
