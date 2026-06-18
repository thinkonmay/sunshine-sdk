#!/usr/bin/env bash
# Shared helpers for worker/sunshine local CI (sourced, not executed directly).
set -euo pipefail

ci_log() {
  printf '\n==> %s\n' "$*"
}

ci_os() {
  uname -s
}

ci_arch() {
  uname -m
}

ci_is_msys() {
  case "$(ci_os)" in
    MINGW*|MSYS*) return 0 ;;
    *) return 1 ;;
  esac
}

ci_require_windows() {
  if ! ci_is_msys; then
    echo "sunshine CI requires Windows MSYS2 MINGW64 (see ci/README.md)" >&2
    return 1
  fi
}

ci_init_submodules() {
  if [[ ! -f third-party/build-deps/dist/Windows-AMD64/lib/libavcodec.a ]]; then
    ci_log "initializing git submodules (build-deps + nvapi)"
    git submodule update --init --recursive
  fi
}

ci_cmake_configure() {
  local build_dir="${1:-build}"
  local build_type="${2:-Release}"
  mkdir -p "${build_dir}"
  cmake -S . -B "${build_dir}" \
    -DCMAKE_BUILD_TYPE="${build_type}" \
    -G Ninja \
    -DBUILD_TESTS=ON
}

ci_run_standalone_tests() {
  local build_dir="${1:-build-standalone}"
  ci_log "standalone IVSHMEM protocol tests (non-Windows)"
  cmake -S tests/standalone -B "${build_dir}"
  cmake --build "${build_dir}"
  "${build_dir}/test_ivshmem_protocol"
}

ci_run_tests() {
  local build_dir="${1:-build}"
  if [[ -x "${build_dir}/tests/test_ivshmem_protocol" ]]; then
    "${build_dir}/tests/test_ivshmem_protocol"
    return $?
  fi
  if [[ -x "${build_dir}/tests/test_ivshmem_protocol.exe" ]]; then
    "${build_dir}/tests/test_ivshmem_protocol.exe"
    return $?
  fi
  (cd "${build_dir}" && ctest --output-on-failure -R ivshmem_protocol)
}
