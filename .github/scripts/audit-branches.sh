#!/usr/bin/env bash
# List remote branches whose work is already contained in origin/main.
# Uses patch equivalence (git cherry), not commit ancestry, so squash-merged
# branches are still detected as stale.
set -euo pipefail

git fetch origin --prune

protected='^(origin/main|origin/python-legacy|origin/cpp-rewrite|origin)$'

branch_has_unique_patches() {
    local ref=$1
    # '+' = commit on branch whose patch is not in upstream
    # '-' = equivalent patch already in main (typical after squash merge)
    git cherry origin/main "$ref" | grep -q '^+'
}

while IFS= read -r ref; do
    [[ "$ref" =~ $protected ]] && continue

    ahead=$(git rev-list --count origin/main.."$ref" 2>/dev/null || echo 0)

    if [[ "$ahead" -eq 0 ]]; then
        echo "STALE (0 commits ahead of main): $ref"
        continue
    fi

    if ! branch_has_unique_patches "$ref"; then
        echo "STALE (patches already in main): $ref"
    fi
done < <(git for-each-ref --format='%(refname:short)' refs/remotes/origin | grep -v '^origin/HEAD$')
