#!/bin/sh
# Publica el codigo del mod en el repo publico (fv54367-dev/COOP-SanAndreas); su CI compila y arma los releases.
# Va solo la lista blanca SOLO (menos NUNCA) y lo de publico/, que va a la raiz (README, INSTALACION, CI, .gitignore).
# Ramas publicas: main, pr<N> por cada PR abierto de este repo y todo (main + los PR que mezclan sin chocar).
# Las de PR cerrados se borran con su release. La historia publica es propia: un commit por publicacion.
# Uso: sh tools/publicar.sh            publica todo
#      FUENTE=origin/<rama> sh tools/publicar.sh   usa esa rama privada como main publico (para probar el CI)
#      sh tools/publicar.sh --probar   lista lo que iria en main y no sube nada
set -eu
cd "$(dirname "$0")/.."

URL=https://github.com/fv54367-dev/COOP-SanAndreas.git
PUB=fv54367-dev/COOP-SanAndreas
export SOLO='^((client|server|shared|proxy|launcher|scm|sdk|third_party|dist|tools/testbot)/|(xmake\.lua|LICENSE|\.clang-format|\.clang-tidy|\.gitattributes|\.editorconfig|\.github/scripts/.*\.ps1)$)'
export NUNCA='(\.aps$|^scm/\.vscode/|^(client|server|shared|proxy|launcher|scm|sdk|dist|tools)/.*\.md$)'
RUTAS='client server shared proxy launcher scm sdk third_party dist tools/testbot xmake.lua .github/scripts publico'

# the public history never carries the personal e-mail
export GIT_AUTHOR_NAME=fv54367-dev GIT_COMMITTER_NAME=fv54367-dev
export GIT_AUTHOR_EMAIL=283286675+fv54367-dev@users.noreply.github.com GIT_COMMITTER_EMAIL=283286675+fv54367-dev@users.noreply.github.com

git remote get-url publico >/dev/null 2>&1 || git remote add publico "$URL"
git fetch -q origin
git fetch -q --prune publico '+refs/heads/*:refs/publico/*' 2>/dev/null || true

IDX=$(mktemp)
trap 'rm -f "$IDX"' EXIT

# private commit -> public tree
arbol() {
    rm -f "$IDX"
    {
        git ls-tree -r -z "$1" | awk -v RS='\0' -v ORS='\0' -F '\t' '$2 ~ ENVIRON["SOLO"] && $2 !~ ENVIRON["NUNCA"]'
        git ls-tree -r -z "$1" publico/ | awk -v RS='\0' -v ORS='\0' -F '\t' '{ sub(/^publico\//, "", $2); print $1 "\t" $2 }'
    } | GIT_INDEX_FILE="$IDX" git update-index -z --index-info
    GIT_INDEX_FILE="$IDX" git write-tree
}

# publicar <rama publica> <arbol> <titulo> <commit privado de origen>
publicar() {
    padre=$(git rev-parse -q --verify "refs/publico/$1" || true)
    if [ -n "$padre" ] && [ "$(git rev-parse "$padre^{tree}")" = "$2" ]; then
        echo "$1: sin cambios"
        return
    fi
    cuerpo=''
    desde=$([ -n "$padre" ] && git log -1 --format=%B "$padre" | sed -n 's/^Origen: //p' || true)
    if [ -n "$desde" ] && git cat-file -e "$desde^{commit}" 2>/dev/null; then
        # shellcheck disable=SC2086
        cuerpo=$(git log --no-merges --format='- %s' "$desde..$4" -- $RUTAS | grep -v -E '^- docs' || true)
    fi
    if [ -n "$padre" ]; then
        c=$(printf '%s\n\n%s\n\nOrigen: %s\n' "$3" "$cuerpo" "$4" | git commit-tree "$2" -p "$padre")
    else
        c=$(printf '%s\n\nOrigen: %s\n' "$3" "$4" | git commit-tree "$2")
    fi
    git push -q publico "$c:refs/heads/$1"
    git update-ref "refs/publico/$1" "$c"
    echo "$1: publicado ($3)"
}

borrar() {
    git push -q publico --delete "$1" || true
    git update-ref -d "refs/publico/$1"
    gh release delete "canal-$1" -R "$PUB" --yes --cleanup-tag >/dev/null 2>&1 || true
    echo "$1: borrado"
}

FUENTE=${FUENTE:-origin/main}
git rev-parse -q --verify "$FUENTE^{commit}" >/dev/null || { echo "No existe $FUENTE"; exit 1; }
main=$(arbol "$FUENTE")
if [ "${1:-}" = "--probar" ]; then
    git ls-tree -r --name-only "$main" | awk -F/ '{ print ($2 == "" ? $1 : $1 "/") }' | sort | uniq -c
    echo "--- archivos sueltos y de publico/:"
    git ls-tree -r --name-only "$main" | grep -v / || true
    exit 0
fi

if [ -z "$(git rev-parse -q --verify refs/publico/main || true)" ]; then
    publicar main "$main" 'COOP San Andreas: primera version publica' "$(git rev-parse "$FUENTE")"
else
    publicar main "$main" 'Actualizacion' "$(git rev-parse "$FUENTE")"
fi

prs=$(gh pr list --state open --json number,headRefName,title --jq '.[] | "\(.number)\t\(.headRefName)\t\(.title)"' | sort -n)
todo=$main
entraron=''
saltados=''
abiertas=' main todo '
tab=$(printf '\t')
while IFS="$tab" read -r n rama titulo; do
    [ -n "$n" ] || continue
    abiertas="$abiertas pr$n "
    tp=$(arbol "origin/$rama")
    publicar "pr$n" "$tp" "PR $n: $titulo" "$(git rev-parse "origin/$rama")"
    # the merge runs on the public trees: docs never block two fixes from being played together
    cb=$(echo x | git commit-tree "$(arbol "$(git merge-base origin/main "origin/$rama")")")
    cm=$(echo x | git commit-tree "$todo")
    cp=$(echo x | git commit-tree "$tp")
    if r=$(git merge-tree --write-tree --merge-base="$cb" "$cm" "$cp" 2>/dev/null); then
        todo=$(echo "$r" | head -n 1)
        entraron="$entraron #$n"
    else
        saltados="$saltados #$n"
    fi
done <<EOF
$prs
EOF

if [ -n "$entraron" ]; then
    titulo="Todo junto: main +$entraron"
    [ -z "$saltados" ] || titulo="$titulo (chocan y quedan afuera:$saltados)"
    publicar todo "$todo" "$titulo" "$(git rev-parse origin/main)"
elif git rev-parse -q --verify refs/publico/todo >/dev/null; then
    borrar todo
fi

for ref in $(git for-each-ref --format='%(refname:lstrip=2)' refs/publico/); do
    case "$abiertas" in
        *" $ref "*) ;;
        *) borrar "$ref" ;;
    esac
done
