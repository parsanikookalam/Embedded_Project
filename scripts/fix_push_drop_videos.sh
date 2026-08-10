#!/usr/bin/env bash
# Remove oversized videos from unpushed history, keep files on disk, push.
set -euo pipefail
cd "$(dirname "$0")/.."

echo "==> Fetch origin"
git fetch origin

echo "==> Current tip vs origin/main"
git log --oneline origin/main..HEAD || true

echo "==> Soft reset to origin/main (keeps ALL file changes staged)"
git reset --soft origin/main

echo "==> Untrack videos (keep on disk)"
git rm -r --cached --ignore-unmatch \
  "report/part 1/fig/03_autostart.mp4" \
  "report/part 2/fig/08_swagger_api_demo.mp4" \
  "report/part 4/fig/01_guard_mode.mp4" \
  "report/part 4/fig/05_watchdog.mp4" \
  2>/dev/null || true

# Any other mp4 still staged?
while IFS= read -r -d '' f; do
  git rm --cached --ignore-unmatch "$f" 2>/dev/null || true
done < <(git diff --cached --name-only -z --diff-filter=A 2>/dev/null | tr '\0' '\n' | grep -iE '\.(mp4|mov|avi|mkv)$' | tr '\n' '\0' || true)

# Also unstage by pathspec if still listed
git diff --cached --name-only | grep -iE '\.(mp4|mov|avi|mkv)$' | while read -r f; do
  git rm --cached --ignore-unmatch -- "$f" || true
done

echo "==> Ensure .gitignore ignores videos"
grep -q '^\*.mp4' .gitignore 2>/dev/null || cat >> .gitignore <<'EOF'

# Large media (GitHub >100MB limit)
*.mp4
*.mov
*.avi
*.mkv
EOF
git add .gitignore

echo "==> Staged files that look like video (must be empty):"
git diff --cached --name-only | grep -iE '\.(mp4|mov|avi|mkv)$' && {
  echo "ERROR: videos still staged — abort"
  exit 1
} || echo "(none — good)"

echo "==> Create clean commit"
git commit -m "$(cat <<'EOF'
Add project reports and fixes without large video files.

Demo videos stay local (gitignored); attach separately for TAs if needed.
EOF
)" || {
  echo "Nothing to commit or commit failed — check git status"
  git status -sb
  exit 1
}

echo "==> Verify no blob >90MB in commits to push"
git rev-list --objects origin/main..HEAD \
  | git cat-file --batch-check='%(objecttype) %(objectname) %(objectsize) %(rest)' \
  | awk '/^blob/ && $3 > 90000000 {printf "TOO BIG: %.1f MB  %s\n", $3/1024/1024, $4; bad=1} END{exit bad+0}' \
  && echo "(all blobs under 90MB — good)" \
  || {
    echo "ERROR: large blob still in range to push"
    exit 1
  }

echo "==> Push"
git push -u origin HEAD

echo "Done. Videos still on disk if present:"
find report -name '*.mp4' 2>/dev/null || true
