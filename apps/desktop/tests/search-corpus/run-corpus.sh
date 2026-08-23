#!/usr/bin/env bash
set -euo pipefail

if [[ $# -ne 2 ]]; then
  echo "usage: $0 <zenpdf_search_corpus_probe> <fixture-directory>" >&2
  exit 64
fi

probe=$1
fixtures=$2
script_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)

test -x "$probe"
test -d "$fixtures"
(cd -- "$fixtures" && sha256sum -c "$script_dir/SHA256SUMS")

run_probe() {
  local milliseconds=$1
  local fixture=$2
  local query=$3
  shift 3
  timeout --signal=KILL "$(((milliseconds + 4999) / 1000))s" \
    env ZENPDF_PROBE_MS="$milliseconds" \
    "$probe" "$fixtures/$fixture" "$query" "$@"
}

expect_results() {
  local expected=$1
  shift
  local output
  output=$(run_probe "$@")
  printf '%s\n' "$output"
  grep -Eq "results=${expected}( |$)" <<<"$output"
}

expect_load_failure() {
  local expected_error=$1
  shift
  local output
  local status
  set +e
  output=$(run_probe "$@")
  status=$?
  set -e
  printf '%s\n' "$output"
  test "$status" -eq 2
  grep -Eq "load_error=${expected_error} pages=0( |$)" <<<"$output"
}

observe_probe() {
  run_probe "$@"
}

expect_results 3 3000 unicode-search-independent.pdf quokka
expect_results 1 3000 unicode-search-independent.pdf café
expect_results 1 3000 unicode-search-independent.pdf Привет
expect_results 0 3000 image-only-no-text-layer.pdf wombat
expect_load_failure 5 3000 encrypted-aes256-user-reader.pdf quokka wrong-password
expect_results 3 3000 encrypted-aes256-user-reader.pdf quokka reader
expect_load_failure 4 3000 malformed-truncated.pdf quokka
expect_results 3 3000 malformed-bad-startxref.pdf quokka
expect_results 1 3000 hostile-flate-expansion-32m.pdf flate-expansion-control
expect_results 1 3000 hostile-huge-page-box.pdf huge-page-box-control
observe_probe 10000 many-results-4000-matches.pdf needle
observe_probe 25000 long-search-400-pages.pdf long-search-target-399

(cd -- "$fixtures" && sha256sum -c "$script_dir/SHA256SUMS")
