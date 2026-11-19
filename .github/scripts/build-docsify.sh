#!/usr/bin/env bash
set -euo pipefail
root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
output="${1:?Usage: build-docsify.sh OUTPUT_DIRECTORY}"
mkdir -p "$output"
cp "$root"/docsify/index.html "$root"/docsify/_sidebar.md "$output/"
cp "$root"/{README.md,README.pl.md,ARCHITECTURE.md,CONTRIBUTING.md,CHANGELOG.md,LICENSE} "$output/"
cp "$root/LICENSE" "$output/LICENSE.md"
mkdir -p "$output/docs"
cp -R "$root/docs/en" "$root/docs/pl" "$output/docs/"
touch "$output/.nojekyll"
