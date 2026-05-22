#!/usr/bin/env bash
# List remote branches with zero commits ahead of origin/main (likely stale after squash merge).
set -euo pipefail

git fetch origin --prune

protected='^(origin/main|origin/python-legacy|origin/cpp-rewrite|origin)$'

while IFS= read -r ref; do
    [[ "$ref" =~ $protected ]] && continue
    ahead=$(git rev-list --count origin/main.."$ref")
    if [[ "$ahead" -eq 0 ]]; then
        echo "STALE (0 ahead of main): $ref"
    fi
done < <(git for-each-ref --format='%(refname:short)' refs/remotes/origin | grep -v '^origin/HEAD$')
