#!/usr/bin/env python3
"""patch_text.py SPEC.py            apply the exact-match edits a spec file describes
   patch_text.py FILE OLD NEW       one edit: replace the text in file OLD with the text in file NEW

Edits for this repo, whose sources are CRLF, from a tool that cannot be trusted with
backslashes on its command line. Matching is done on LF-normalised text, the file is
written back with the line ending it had, and an OLD text that is not found exactly once
aborts the whole spec with nothing written.

A spec is a Python file that defines EDITS = {"path/relative/to/repo": [(OLD, NEW), ...]}
with triple-quoted strings; write it with your editor or the Write tool, never through a
shell heredoc (the Bash tool turns a double backslash into a single one, so "\\n" in a
heredoc reaches Python as a real newline).
"""
import sys, os, runpy


def apply(path, pairs):
    raw = open(path, 'rb').read()
    crlf = raw.count(b'\r\n') > raw.count(b'\n') // 2
    text = raw.replace(b'\r\n', b'\n').decode('utf-8')
    for old, new in pairs:
        old = old.replace('\r\n', '\n'); new = new.replace('\r\n', '\n')
        n = text.count(old)
        if n != 1:
            raise SystemExit(f"{path}: OLD text found {n} times, expected 1:\n{old[:200]}")
        text = text.replace(old, new)
    out = text.encode('utf-8')
    if crlf:
        out = out.replace(b'\n', b'\r\n')
    return out, crlf


def main():
    if len(sys.argv) == 4:
        path, oldp, newp = sys.argv[1:4]
        old = open(oldp, 'rb').read().decode('utf-8')
        new = open(newp, 'rb').read().decode('utf-8')
        out, crlf = apply(path, [(old, new)])
        open(path, 'wb').write(out)
        print(f"{path}: 1 edit ({'CRLF' if crlf else 'LF'})")
        return
    if len(sys.argv) != 2:
        print(__doc__); sys.exit(1)
    spec = runpy.run_path(sys.argv[1])
    edits = spec.get('EDITS')
    if not isinstance(edits, dict):
        sys.exit("spec defines no EDITS dict")
    root = spec.get('ROOT') or os.getcwd()
    results = []
    for rel, pairs in edits.items():           # verify everything before writing anything
        path = os.path.join(root, rel)
        results.append((path, apply(path, pairs), len(pairs)))
    for path, (out, crlf), n in results:
        open(path, 'wb').write(out)
        print(f"{path}: {n} edit(s) ({'CRLF' if crlf else 'LF'})")


if __name__ == '__main__':
    main()
