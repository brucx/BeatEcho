#!/usr/bin/env bash
# Push committed work to the existing repository. Never creates a repository.
set -euo pipefail
DRY=0
if [[ ${1:-} == --dry-run ]]; then DRY=1; shift; fi
[[ $# == 0 ]] || { echo 'Usage: bash scripts/publish-github.sh [--dry-run]' >&2; exit 2; }
cd "$(dirname "${BASH_SOURCE[0]}")/.."
git rev-parse --is-inside-work-tree >/dev/null
REMOTE=$(git remote get-url origin)
case "$REMOTE" in
  https://github.com/brucx/BeatEcho|https://github.com/brucx/BeatEcho.git|git@github.com:brucx/BeatEcho.git) ;;
  *) echo 'Refused: origin must be brucx/BeatEcho.' >&2; exit 1;;
esac
BRANCH=$(git symbolic-ref --short HEAD)
[[ "$BRANCH" != main && "$BRANCH" != master ]] || { echo 'Create a feature branch before publishing changes.' >&2; exit 1; }
[[ -z $(git status --porcelain) ]] || { echo 'Review and commit your intended files first; this script never stages files.' >&2; exit 1; }
if (( DRY )); then
  printf 'Would push current committed branch: %s to %s (no force)\n' "$BRANCH" "$REMOTE"
else
  git push --set-upstream origin "$BRANCH"
fi
