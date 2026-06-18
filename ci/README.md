# Local CI — `worker/sunshine`

Run the same checks as GitHub Actions **before pushing**, without waiting on remote runners.

**Platform:** Windows MSYS2 MINGW64 for full `sunshine.exe` build. **Unit tests** also run on macOS/Linux via `tests/standalone/` (no FFmpeg/submodules required).

Mandatory testing rules: [docs/engineering/ci_cd/testing_policy.md](../../../docs/engineering/ci_cd/testing_policy.md).

## Quick start

```bash
cd worker/sunshine
git submodule update --init --recursive
./ci/local.sh test
```

```powershell
cd worker\sunshine
git submodule update --init --recursive
.\ci\local.ps1 -Stage test
```

Default pipeline (`./ci/local.sh`):

1. Init submodules (`third-party/build-deps`, `third-party/nvapi-open-source-sdk`) if missing
2. `cmake -DBUILD_TESTS=ON` + Ninja
3. Build and run `test_ivshmem_protocol` (GoogleTest)
4. Build `sunshine.exe`

## GitHub Actions

| Location | Workflow |
|----------|----------|
| Sunshine repo | [`.github/workflows/ci.yml`](../.github/workflows/ci.yml) |
| Thinkmay monorepo | [`window.yml`](../../../.github/workflows/window.yml) job `build_sunshine` |

Both run unit tests via `ctest` / `test_ivshmem_protocol` before packaging.

## Stages

| Stage | Command | Purpose |
|-------|---------|---------|
| **default** | `./ci/local.sh` | Unit tests + build |
| **all** | `./ci/local.sh all` | Same as default |
| **test** | `./ci/local.sh test` | IVSHMEM protocol unit tests only |
| **build** | `./ci/local.sh build` | Compile `sunshine.exe` |
| **configure** | `./ci/local.sh configure` | CMake configure only |

PowerShell: `.\ci\local.ps1 -Stage test`

## Environment flags

| Variable / switch | Effect |
|-------------------|--------|
| `SKIP_BUILD=1` / `-SkipBuild` | Skip `sunshine.exe` compile |
| `SKIP_SUBMODULES=1` / `-SkipSubmodules` | Skip `git submodule update` |
| `BUILD_DIR=build` | CMake build directory |

## Test scope (CI)

| Tier | Target | Notes |
|------|--------|-------|
| **CI** | `test_ivshmem_protocol` | Queue sizes, packet append, Sunshine video wire header, ring indices (Windows full build + macOS/Linux standalone) |
| **CI (monorepo Go)** | `worker/proxy/util/memory`, `worker/daemon/utils/memory` | Host-side IVSHMEM protocol tests in Go |
| **Lab** | DXGI capture, NVENC encode, IVSHMEM driver | Not in default CI |

## Dependencies

MSYS2 MINGW64 packages (same as monorepo `window.yml`):

```bash
pacman -S mingw-w64-x86_64-toolchain mingw-w64-x86_64-cmake mingw-w64-x86_64-ninja \
  mingw-w64-x86_64-boost mingw-w64-x86_64-openssl mingw-w64-x86_64-opus \
  mingw-w64-x86_64-miniupnpc mingw-w64-x86_64-nlohmann-json mingw-w64-x86_64-onevpl
```

## Make shortcut

```bash
make ci
make test
```
