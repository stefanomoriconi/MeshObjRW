# Build guide

`czmesh` builds with **CMake ≥ 3.16** and any **C++17** compiler. It
produces a shared library, a CLI tool, and (optionally) a C test suite.

## Output artifacts

| Artifact | Windows (MSVC) | macOS | Linux |
|----------|----------------|-------|-------|
| Shared library | `czmesh.dll` (+ `czmesh.lib` import lib) | `libczmesh.dylib` | `libczmesh.so` |
| CLI | `czmesh_cli.exe` | `czmesh_cli` | `czmesh_cli` |
| Tests | `czmesh_tests.exe` | `czmesh_tests` | `czmesh_tests` |

## CMake options

| Option | Default | Description |
|--------|---------|-------------|
| `CZMESH_BUILD_TESTS` | `ON` | Build the C test suite (`czmesh_tests`). |
| `CZMESH_BUILD_CLI` | `ON` | Build `czmesh_cli`. |
| `CZMESH_ENABLE_OPENMP` | `ON` | Parallelise the reader/writer with OpenMP. Degrades gracefully when OpenMP is absent. |
| `CZMESH_ENABLE_CUDA` | `OFF` | Compile the optional CUDA kernels (`src/czmesh_cuda.cu`). Requires a CUDA toolkit. |

---

## Windows

Prerequisites: Visual Studio 2019/2022 with the **"Desktop development with
C++"** workload, and CMake.

### MSVC (multi-config)

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64 -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release --parallel
```

Artifacts land in `build/Release/`.

### Clang on Windows (optional)

```powershell
cmake -S . -B build -G "Ninja" `
      -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++ `
      -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

> If building with MinGW/`-shared`, also add
> `-DCMAKE_CXX_FLAGS="-static-libstdc++ -static-libgcc"` if you want a
> self-contained DLL.

---

## macOS

Prerequisites: Xcode command line tools (`xcode-select --install`), CMake.

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
```

Produces `build/libczmesh.dylib`. Run the tests:

```sh
./build/czmesh_tests
```

To use `libczmesh.dylib` from another binary in the same tree, either run
with `DYLD_LIBRARY_PATH` set, or set an `@rpath` / `install_name_tool` entry
at install time:

```sh
# (example) fix the dylib install name after install
install_name_tool -id @rpath/libczmesh.dylib <path>/libczmesh.dylib
```

---

## Linux

Prerequisites: a C++17 compiler (`g++` or `clang++`), CMake, `make` or
`ninja`.

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
```

Produces `build/libczmesh.so`. Run the tests:

```sh
./build/czmesh_tests
```

For a self-contained binary that finds `libczmesh.so` at runtime, either:

- add the build dir to `LD_LIBRARY_PATH`, or
- set an `RPATH` at link/install time, e.g. pass
  `-DCMAKE_INSTALL_RPATH="$ORIGIN"` for a binary colocated with the library.

---

## OpenMP

`CZMESH_ENABLE_OPENMP=ON` (default) links the platform OpenMP runtime:

- **Windows MSVC:** `/openmp` (bundled with the compiler).
- **Linux GCC/Clang:** `-fopenmp`.
- **macOS:** no OpenMP in the system Clang — the option is detected as
  absent and the library builds **without** OpenMP (fully functional, just
  single-threaded in the parallel regions).

You can verify at runtime:

```sh
czmesh_cli version
# e.g. "czmesh 1.0.0 | compiler: ... | OpenMP: yes | CUDA: not compiled in | platform: linux"
```

---

## CUDA (optional)

`CZMESH_ENABLE_CUDA=ON` requires a CUDA toolkit and adds
`src/czmesh_cuda.cu` to the build. It is **OFF by default** and is not
required for the CPU library or its tests.

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DCZMESH_ENABLE_CUDA=ON
cmake --build build --parallel
```

`czmesh_cli version` will then report `CUDA: enabled (sm_XX)`. If no CUDA
toolkit is found, configure fails with a clear message.

---

## Verifying a build

```sh
# C tests — expect "PASS: 53   FAIL: 0"
./build/czmesh_tests            # or build/Release/czmesh_tests.exe

# CLI smoke test
./build/czmesh_cli version

# Python end-to-end (library must be discoverable, see python-usage.md)
python python/test_czmesh_io.py
```
