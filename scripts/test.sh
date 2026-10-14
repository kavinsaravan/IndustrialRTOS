#!/usr/bin/env bash
set -euo pipefail
project_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cmake -S "$project_dir/tests" -B "$project_dir/build-host" "$@"
cmake --build "$project_dir/build-host" --parallel 2
ctest --test-dir "$project_dir/build-host" --output-on-failure
