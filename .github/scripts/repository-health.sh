#!/usr/bin/env bash
set -euo pipefail

ROOT="$(git rev-parse --show-toplevel)"
cd "$ROOT"
failures=0

echo "Checking immutable GitHub Actions references."
while IFS= read -r -d '' workflow; do
    line_number=0
    while IFS= read -r line || [[ -n "$line" ]]; do
        ((line_number += 1))
        if [[ "$line" =~ ^[[:space:]]*(-[[:space:]]*)?uses:[[:space:]]*([^[:space:]#]+) ]]; then
            reference="${BASH_REMATCH[2]}"
            if [[ "$reference" == ./* ]]; then
                continue
            fi
            if [[ "$reference" == docker://* ]]; then
                if [[ ! "$reference" =~ @sha256:[0-9a-fA-F]{64}$ ]]; then
                    echo "::error file=$workflow,line=$line_number::container action must be pinned to a full SHA-256 digest: $reference"
                    failures=1
                fi
                continue
            fi
            if [[ ! "$reference" =~ @[0-9a-fA-F]{40}$ ]]; then
                echo "::error file=$workflow,line=$line_number::external action must be pinned to a full commit SHA: $reference"
                failures=1
            fi
        fi
    done < "$workflow"
done < <(find .github/workflows -maxdepth 1 -type f \( -name '*.yml' -o -name '*.yaml' \) -print0 | sort -z)

if ! command -v jq >/dev/null 2>&1; then
    echo "::error::jq is required to validate JSON files"
    exit 1
fi
while IFS= read -r -d '' file; do
    if ! jq empty "$file"; then
        echo "::error file=$file::invalid JSON"
        failures=1
    fi
done < <(find . -type f -name '*.json' \
    -not -path './.git/*' \
    -not -path './web/node_modules/*' \
    -print0)

if command -v node >/dev/null 2>&1; then
    while IFS= read -r -d '' file; do
        if ! node --check "$file"; then
            echo "::error file=$file::JavaScript syntax check failed"
            failures=1
        fi
    done < <(find web -type f \( -name '*.js' -o -name '*.mjs' \) -not -path '*/node_modules/*' -print0)
fi

while IFS= read -r -d '' file; do
    if ! bash -n "$file"; then
        echo "::error file=$file::shell syntax check failed"
        failures=1
    fi
done < <(find .github/scripts cpp/tests -type f -name '*.sh' -print0 2>/dev/null || true)

if [[ "$failures" -ne 0 ]]; then
    echo "Repository quality checks failed."
    exit 1
fi

echo "Repository quality checks passed."
