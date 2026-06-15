# Building libHaru on Windows (x64)

These are the exact steps used to build the static `hpdf.lib` in this fork,
plus the `svg_demo` example. The build uses **Visual Studio 2022** and a
**self-built zlib 1.3.2**.

> Note: the `zlib-*`, `x64/`, `build*/` trees and the demo binaries are
> `.gitignore`d, so a fresh clone will **not** contain zlib or any compiled
> output. zlib must be downloaded and built first (Step 1) — libHaru's CMake
> will not configure without finding `zs.lib`.

## Prerequisites

- **Visual Studio 2022** (Community is fine) with the "Desktop development
  with C++" workload. This provides `cl.exe`, `MSBuild`, and a bundled
  `cmake.exe`.
- `cmake` and `cl` are **not on PATH**. Either:
  - run commands from a *"x64 Native Tools Command Prompt for VS 2022"*, or
  - prefix a normal shell with `vcvars64.bat` (see the `svg_demo` step), and
  - use the VS-bundled cmake at:
    `C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe`

All paths below are relative to the repo root unless noted.

## Step 1 — Download and build zlib 1.3.2

zlib is a separate dependency, **not** part of libHaru.

1. Download the official source tarball from <https://zlib.net/> —
   `zlib-1.3.2.tar.gz` (also mirrored at <https://github.com/madler/zlib>).
2. Unpack it into the repo root as `zlib-1.3.2/`.
3. Build it (out-of-source) into `zlib-x64/` with the VS 2022 x64 generator:

   ```bat
   cmake -S zlib-1.3.2 -B zlib-x64 -G "Visual Studio 17 2022" -A x64
   cmake --build zlib-x64 --config Release
   ```

This produces:

- `zlib-x64/Release/zs.lib` — the **static** library libHaru links against.
  (zlib's newer CMake names the static target `zs` via `OUTPUT_NAME zs`, so the
  file is `zs.lib`, not the older `zlibstatic.lib`.)
- `zlib-x64/Release/z.dll` / `z.lib` — shared library (not used here).

The headers come from the unpacked source tree `zlib-1.3.2/`.

## Step 2 — Configure and build the libHaru library

The `x64/` CMake build is wired to find zlib at the paths produced above:

- `ZLIB_INCLUDE_DIR = zlib-1.3.2`
- `ZLIB_LIBRARY_RELEASE = zlib-x64/Release/zs.lib`

Configure and build (Release, x64):

```bat
cmake -S . -B x64 -G "Visual Studio 17 2022" -A x64 ^
  -DZLIB_INCLUDE_DIR=zlib-1.3.2 ^
  -DZLIB_LIBRARY_RELEASE=zlib-x64/Release/zs.lib

cmake --build x64 --config Release --clean-first
```

Output: **`x64/src/Release/hpdf.lib`** — the static libHaru library.
The generated config header lands at `x64/include/hpdf_config.h`.

Notes:
- The library is built with the **`/MD`** runtime (MultiThreadedDLL) in Release.
  Anything linking `hpdf.lib` must use the same runtime.
- A `Could NOT find PNG` message during configure is expected — PNG support is
  off; zlib is the only optional library used (`HAVE_ZLIB: TRUE`).
- One pre-existing warning in `hpdf_pdfa.c` (C5286, implicit enum conversion) is
  upstream and harmless.

## Step 3 — Build the `svg_demo` example (optional)

The CMake examples are turned off, so `svg_demo` is compiled manually. It uses
`demo/svg_demo.c`, the single-header `demo/nanosvg.h`, the libHaru headers, and
the generated `hpdf_config.h`. From the repo root, with the VS x64 environment
loaded:

```bat
call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat"

cl /nologo /O2 /MD /I include /I demo /I x64\include ^
   demo\svg_demo.c /Fe:svg_demo.exe /Fo:svg_demo.obj ^
   /link x64\src\Release\hpdf.lib zlib-x64\Release\zs.lib
```

Run it against a sample SVG:

```bat
svg_demo.exe demo\images\bigjs-border-clean.svg
```

This writes `svg_demo.pdf`.

## Quick reference

| Artifact | Path |
|---|---|
| zlib source (headers) | `zlib-1.3.2/` |
| zlib static lib | `zlib-x64/Release/zs.lib` |
| libHaru static lib | `x64/src/Release/hpdf.lib` |
| Generated config header | `x64/include/hpdf_config.h` |
| Demo executable | `svg_demo.exe` |
