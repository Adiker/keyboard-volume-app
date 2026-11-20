#!/usr/bin/env bash
set -euo pipefail
root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
output="${1:?Usage: build-docsify.sh OUTPUT_DIRECTORY}"
mkdir -p "$output"
cp "$root"/docsify/{index.html,_sidebar.md,_404.md,theme.css,motion.css,app.js,effects.js,palette.js,resize.js} "$output/"
cp "$root"/{README.md,README.pl.md,ARCHITECTURE.md,CONTRIBUTING.md,CHANGELOG.md,LICENSE} "$output/"
cp "$root/LICENSE" "$output/LICENSE.md"
cp "$root/resources/icon.png" "$output/icon.png"
mkdir -p "$output/docs"
cp -R "$root/docs/en" "$root/docs/pl" "$output/docs/"
touch "$output/.nojekyll"

# Cache-bust the static assets with the current commit (or a timestamp outside git).
version="$(git -C "$root" rev-parse --short HEAD 2>/dev/null || date +%s)"
sed -i "s/?v=dev/?v=${version}/g" "$output/index.html"
