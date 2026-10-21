#!/usr/bin/env bash
# Build a stable Arch package (.pkg.tar.zst) for a tagged release.
# Usage: pkg/arch/build-release-package.sh [version]
#   version defaults to cpp/CMakeLists.txt PROJECT_VERSION, or $VERSION.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
ARCH_DIR="${ROOT}/pkg/arch"
OUT_DIR="${OUT_DIR:-${ARCH_DIR}}"
PKGNAME="keyboard-volume-app"

if [[ -n "${1:-}" ]]; then
  VERSION="$1"
elif [[ -n "${VERSION:-}" ]]; then
  :
else
  VERSION="$(sed -n 's/^project(keyboard-volume-app VERSION \([0-9.]*\).*/\1/p' \
    "${ROOT}/cpp/CMakeLists.txt")"
fi

if [[ -z "${VERSION}" ]]; then
  echo "Could not determine version" >&2
  exit 1
fi

TAG="v${VERSION}"
TARBALL="${ARCH_DIR}/${PKGNAME}-${VERSION}.tar.gz"

cd "${ROOT}"
if git rev-parse -q --verify "refs/tags/${TAG}" >/dev/null; then
  REF="${TAG}"
elif git rev-parse -q --verify "${TAG}" >/dev/null; then
  REF="${TAG}"
else
  REF="HEAD"
  echo "Warning: tag ${TAG} not found locally; archiving HEAD" >&2
fi

git archive --format=tar.gz --prefix="${PKGNAME}-${VERSION}/" \
  -o "${TARBALL}" "${REF}"

WORKDIR="$(mktemp -d)"
cleanup() { rm -rf "${WORKDIR}"; }
trap cleanup EXIT

cp "${ARCH_DIR}/PKGBUILD.stable" "${WORKDIR}/PKGBUILD"
cp "${TARBALL}" "${WORKDIR}/"

SUM="$(sha256sum "${WORKDIR}/${PKGNAME}-${VERSION}.tar.gz" | awk '{print $1}')"
sed -i \
  -e "s/^pkgver=.*/pkgver=${VERSION}/" \
  -e "s/^sha256sums=.*/sha256sums=('${SUM}')/" \
  "${WORKDIR}/PKGBUILD"

(
  cd "${WORKDIR}"
  makepkg -f --noconfirm
)

mkdir -p "${OUT_DIR}"
shopt -s nullglob
for pkg in "${WORKDIR}"/*.pkg.tar.zst; do
  cp -f "${pkg}" "${OUT_DIR}/"
  echo "Built ${OUT_DIR}/$(basename "${pkg}")"
done
