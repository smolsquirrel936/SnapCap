#!/usr/bin/env python3
"""SnapCap i18n consistency check (build/CI gate).

Validates that the editable language files stay honest:
  1. Every TR("...") / t("...") key used in C++ source exists in the default locale.
  2. Every non-default locale has exactly the default locale's keys
     (reports missing + orphan keys).

Usage:
    python tools/i18n_check.py [--lang-dir lang] [--src-dir .] [--default en]

Exit code is non-zero if any problem is found, so it can fail a CI step.
"""
from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path

try:
    import yaml  # PyYAML
except ImportError:
    sys.exit("i18n_check: PyYAML required ->  pip install pyyaml")

KEY_RE = re.compile(r'(?:\bTR|\.t)\s*\(\s*(?:QStringLiteral\s*\(\s*)?"([^"]+)"')


def flatten(node, prefix="", out=None):
    """Flatten nested YAML maps into dotted keys (mirrors I18n::flatten)."""
    if out is None:
        out = {}
    if isinstance(node, dict):
        for k, v in node.items():
            nxt = f"{prefix}.{k}" if prefix else str(k)
            flatten(v, nxt, out)
    else:
        out[prefix] = node
    return out


def load_locale(path: Path) -> dict:
    with path.open(encoding="utf-8") as fh:
        data = yaml.safe_load(fh) or {}
    flat = flatten(data)
    # meta.* are bookkeeping, not translatable UI keys.
    return {k: v for k, v in flat.items() if not k.startswith("meta.")}


def collect_source_keys(src_dir: Path, exclude: set[str]) -> set[str]:
    keys: set[str] = set()
    for ext in ("*.cpp", "*.h", "*.hpp", "*.cc"):
        for f in src_dir.rglob(ext):
            if exclude.intersection(f.parts):
                continue  # skip tests/build/etc. — they use fixture keys
            keys.update(KEY_RE.findall(f.read_text(encoding="utf-8", errors="ignore")))
    return keys


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--lang-dir", default="lang")
    ap.add_argument("--src-dir", default=".")
    ap.add_argument("--default", default="en")
    ap.add_argument(
        "--exclude",
        nargs="*",
        default=["test", "build", "third_party", ".git"],
        help="Directory names to skip (e.g. tests use synthetic fixture keys).",
    )
    args = ap.parse_args()

    lang_dir = Path(args.lang_dir)
    locales = {p.stem: load_locale(p) for p in sorted(lang_dir.glob("*.yaml"))}
    if args.default not in locales:
        sys.exit(f"i18n_check: default locale '{args.default}' not found in {lang_dir}")

    base = locales[args.default]
    base_keys = set(base)
    problems = 0

    # 1. Source keys must exist in the default locale.
    used = collect_source_keys(Path(args.src_dir), set(args.exclude))
    missing_in_base = sorted(used - base_keys)
    if missing_in_base:
        problems += len(missing_in_base)
        print(f"[FAIL] keys used in code but missing from {args.default}.yaml:")
        for k in missing_in_base:
            print(f"        {k}")

    # 2. Each other locale must match the default locale's key set.
    for code, flat in locales.items():
        if code == args.default:
            continue
        keys = set(flat)
        missing = sorted(base_keys - keys)
        orphan = sorted(keys - base_keys)
        if missing:
            problems += len(missing)
            print(f"[FAIL] {code}.yaml missing {len(missing)} key(s):")
            for k in missing:
                print(f"        {k}")
        if orphan:
            problems += len(orphan)
            print(f"[WARN] {code}.yaml has {len(orphan)} orphan key(s) not in {args.default}.yaml:")
            for k in orphan:
                print(f"        {k}")

    if problems == 0:
        print(f"[OK] i18n consistent: {len(locales)} locale(s), {len(base_keys)} keys each.")
        return 0
    print(f"\ni18n_check found {problems} problem(s).")
    return 1


if __name__ == "__main__":
    raise SystemExit(main())
