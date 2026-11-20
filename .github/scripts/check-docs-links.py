#!/usr/bin/env python3
"""Check relative Markdown links and Docsify sidebar targets in the built site.

Usage: check-docs-links.py SITE_DIRECTORY

Only local links are verified (external URLs are skipped). Links are resolved
the way Docsify does: relative to the current page, or from the site root when
they start with "/". An optional "#anchor" or "?id=anchor" is ignored.
"""
import re
import sys
from pathlib import Path
from urllib.parse import unquote

LINK = re.compile(r"(?<!\!)\[[^\]]*\]\(([^)\s]+)(?:\s+\"[^\"]*\")?\)")
FENCE = re.compile(r"^(```|~~~)")


def targets(markdown: str):
    in_fence = False
    for line in markdown.splitlines():
        if FENCE.match(line.strip()):
            in_fence = not in_fence
            continue
        if not in_fence:
            yield from LINK.findall(line)


def main() -> int:
    site = Path(sys.argv[1]).resolve()
    errors = []
    pages = [p for p in site.rglob("*.md")]
    for page in sorted(pages):
        for href in targets(page.read_text(encoding="utf-8")):
            if re.match(r"^[a-z][a-z0-9+.-]*:", href, re.I) or href.startswith("#"):
                continue
            path = unquote(re.split(r"[#?]", href, maxsplit=1)[0])
            if not path:
                continue
            base = site if path.startswith("/") else page.parent
            resolved = (base / path.lstrip("/")).resolve()
            candidates = [resolved, resolved.with_name(resolved.name + ".md")]
            if not any(c.exists() for c in candidates):
                errors.append(f"{page.relative_to(site)}: broken link -> {href}")
    if errors:
        print("\n".join(errors))
        print(f"{len(errors)} broken link(s)")
        return 1
    print(f"Checked {len(pages)} Markdown files: no broken local links")
    return 0


if __name__ == "__main__":
    sys.exit(main())
