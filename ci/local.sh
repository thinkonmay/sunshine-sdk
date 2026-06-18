#!/usr/bin/env bash
# Local CI for worker/sunshine — mirrors .github/workflows/ci.yml
#
# Usage:
#   ./ci/local.sh              # configure + unit tests + build sunshine
#   ./ci/local.sh test         # unit tests only
#   ./ci/local.sh build        # build sunshine.exe only
#   ./ci/local.sh all          # test + build (same as default)
#
# Options (env):
#   SKIP_BUILD=1               skip sunshine.exe compile
#   SKIP_SUBMODULES=1          skip git submodule init
#   BUILD_DIR=build            cmake build directory
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "${ROOT}"

# shellcheck source=ci/lib.sh
source "${ROOT}/ci/lib.sh"

STAGE="${1:-default}"
BUILD_DIR="${BUILD_DIR:-build}"

step_submodules() {
  if [[ "${SKIP_SUBMODULES:-0}" == "1" ]]; then
    return 0
  fi
  ci_init_submodules
}

step_configure() {
  ci_require_windows
  step_submodules
  ci_log "cmake configure"
  ci_cmake_configure "${BUILD_DIR}"
}

step_tests() {
  if ! ci_is_msys; then
    ci_run_standalone_tests "${BUILD_DIR}-standalone"
    return $?
  fi
  if [[ ! -f "${BUILD_DIR}/build.ninja" ]]; then
    step_configure
  fi
  ci_log "build unit tests"
  cmake --build "${BUILD_DIR}" --target test_ivshmem_protocol
  ci_log "run IVSHMEM protocol unit tests"
  ci_run_tests "${BUILD_DIR}"
}

step_build() {
  if ! ci_is_msys; then
    ci_log "skip sunshine.exe build on $(ci_os) (Windows only)"
    return 0
  fi
  if [[ "${SKIP_BUILD:-0}" == "1" ]]; then
    ci_log "skip build (SKIP_BUILD=1)"
    return 0
  fi
  if [[ ! -f "${BUILD_DIR}/build.ninja" ]]; then
    step_configure
  fi
  ci_log "build sunshine"
  cmake --build "${BUILD_DIR}" --target sunshine
}

run_default() {
  step_tests
  step_build
}

run_all() {
  run_default
}

case "${STAGE}" in
  default|"") run_default ;;
  all)        run_all ;;
  test)       step_tests ;;
  build)      step_configure; step_build ;;
  configure)  step_configure ;;
  *)
    echo "unknown stage: ${STAGE}" >&2
    echo "usage: $0 [default|all|test|build|configure]" >&2
    exit 2
    ;;
esac

ci_log "local CI finished OK"
