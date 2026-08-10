#!/usr/bin/env bash
# Part 3-7 — show failed unauthorized SSH (for screenshot).
set -eu
PORT="${SSHD_PORT:-2222}"
HOST="${1:-127.0.0.1}"

echo "=== Unauthorized SSH test (expect Permission denied) ==="
echo "Target: fakeuser@$HOST port $PORT"
echo

# Non-interactive: BatchMode fails fast without hanging on password prompt forever
set +e
ssh -p "$PORT" \
  -o StrictHostKeyChecking=no \
  -o UserKnownHostsFile=/dev/null \
  -o PreferredAuthentications=password \
  -o PubkeyAuthentication=no \
  -o NumberOfPasswordPrompts=1 \
  -o ConnectTimeout=5 \
  -o BatchMode=yes \
  "fakeuser@$HOST" true 2>&1
RC=$?
set -e

echo
echo "exit_code=$RC (non-zero expected)"
if [[ "$RC" -eq 0 ]]; then
  echo "UNEXPECTED: login succeeded"
  exit 1
fi
echo "→ rejected (good for experiment 3-7)"
echo
echo "Also try root (must fail — PermitRootLogin no):"
set +e
ssh -p "$PORT" \
  -o StrictHostKeyChecking=no \
  -o UserKnownHostsFile=/dev/null \
  -o BatchMode=yes \
  -o ConnectTimeout=5 \
  "root@$HOST" true 2>&1
echo "root_exit=$?"
set -e
echo
echo "Screenshot this terminal → report/part 3/fig/10_ssh_auth_fail.png"
