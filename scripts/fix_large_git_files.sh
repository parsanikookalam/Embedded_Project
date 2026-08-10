#!/usr/bin/env bash
# Diagnose / fix local commits that include files >100MB (GitHub limit).
set -euo pipefail
cd "$(dirname "$0")/.."

echo "=== status ==="
git status -sb
echo
echo "=== recent commits ==="
git log --oneline -12
echo
echo "=== remotes ==="
git remote -v || true
echo
echo "=== files >50MB in working tree (not .git) ==="
find . -type f -size +50M -not -path './.git/*' 2>/dev/null | head -50 || true
echo
echo "=== blobs >50MB in git history ==="
git rev-list --objects --all \
  | git cat-file --batch-check='%(objecttype) %(objectname) %(objectsize) %(rest)' \
  | awk '/^blob/ && $3 > 50000000 {printf "%.1f MB\t%s\n", $3/1024/1024, $4}' \
  | sort -rn | head -30
echo
echo "=== last commit files (stat) ==="
git show --stat --oneline -1 HEAD | head -80
