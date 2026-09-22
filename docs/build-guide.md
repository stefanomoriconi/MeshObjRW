# Build guide

`objrw` builds with **CMake ≥ 3.16** and any **C++17** compiler. It
produces a shared library, a CLI tool, and (optionally) a C test suite.

## Output artifacts

| Artifact | Windows (MSVC) | macOS | Linux |
|----------|----------------|-------|-------|
| Shared library | `objrw.dll` (+ `objrw.lib` import lib) | `libobjrw.dylib` | `libobjrw.so` |
| CLI | `objrw_cli.exe` | `objrw_cli` | `objrw_cli` |
| Tests | `objrw_tests.exe` | `objrw_tests` | `objrw_tests` |

## CMake options

| Option | Default | Description |
|--------|---------|-------------|
| `OBJRW_BUILD_TESTS` | `ON` | Build the C test suite (`objrw_tests`). |
| `OBJRW_BUILD_CLI` | `ON` | Build `objrw_cli`. |
| `OBJRW_ENABLE_OPENMP` | `ON` | Parallelise the ASCII OBJ reader with OpenMP. Degrades gracefully when OpenMP is absent. |
| `OBJRW_ENABLE_CUDA` | `OFF` | Compile the optional CUDA kernels (`src/objrw_cuda.cu`). Requires a CUDA toolkit. |

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

Produces `build/libobjrw.dylib`. Run the tests:

```sh
./build/objrw_tests
```

To use `libobjrw.dylib` from another binary in the same tree, either run
with `DYLD_LIBRARY_PATH` set, or set an `@rpath` / `install_name_tool` entry
at install time:

```sh
# (example) fix the dylib install name after install
install_name_tool -id @rpath/libobjrw.dylib <path>/libobjrw.dylib
```

---

## Linux

Prerequisites: a C++17 compiler (`g++` or `clang++`), CMake, `make` or
`ninja`.

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
```

Produces `build/libobjrw.so`. Run the tests:

```sh
./build/objrw_tests
```

For a self-contained binary that finds `libobjrw.so` at runtime, either:

- add the build dir to `LD_LIBRARY_PATH`, or
- set an `RPATH` at link/install time, e.g. pass
  `-DCMAKE_INSTALL_RPATH="$ORIGIN"` for a binary colocated with the library.

---

## OpenMP

`OBJRW_ENABLE_OPENMP=ON` (default) links the platform OpenMP runtime:

- **Windows MSVC:** `/openmp` (bundled with the compiler).
- **Linux GCC/Clang:** `-fopenmp`.
- **macOS:** no OpenMP in the system Clang — the option is detected as
  absent and the library builds **without** OpenMP (fully functional, just
  single-threaded in the parallel regions).

You can verify at runtime:

```sh
objrw_cli version
# e.g. "objrw 1.0.0 | compiler: ... | OpenMP: yes | CUDA: not compiled in | platform: linux"
```

---

## CUDA (optional)

`OBJRW_ENABLE_CUDA=ON` requires a CUDA toolkit and adds
`src/objrw_cuda.cu` to the build. It is **OFF by default** and is not
required for the CPU library or its tests.

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DOBJRW_ENABLE_CUDA=ON
cmake --build build --parallel
```

`objrw_cli version` will then report `CUDA: compiled in`. At run time the
geometry helpers automatically use the GPU when a CUDA device is present,
and transparently fall back to the CPU path otherwise. If no CUDA toolkit
(`nvcc`) is found, configure fails with a clear message.

---

## Verifying a build

```sh
# C tests — expect "PASS: 53   FAIL: 0"
./build/objrw_tests            # or build/Release/objrw_tests.exe

# CLI smoke test
./build/objrw_cli version

# Python end-to-end (library must be discoverable, see python-usage.md)
python python/test_objrw_io.py
```
