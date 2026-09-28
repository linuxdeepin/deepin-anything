# AGENTS.md

## Build, Test, and Lint Commands

### Build Commands
```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)     # Build with CMake
make -C src/kernelmod              # Build kernel module
```

### Test Commands
```bash
./autotests/run-ut.sh              # configure (Debug) + build + run ut-anything-index
./autotests/run-ut.sh --coverage   # same, plus lcov HTML report in build-autotests/coverage/
./autotests/run-ut.sh --run-only   # run existing binaries without rebuilding
# Manual variant: unit tests are built in Debug configs only
cmake -B build-dbg -DCMAKE_BUILD_TYPE=Debug && cmake --build build-dbg -j$(nproc) && cd build-dbg && ctest
# Force tests into any configuration: -DOPT_ENABLE_BUILD_UT=ON
```

### Lint and Static Analysis
```bash
# cppcheck runs automatically on all PRs via .github/workflows/cppcheck.yml
```

## Code Style Guidelines

### Language Standards
- **Server/Logger/Dispatcher (src/server, src/logger, src/dispatcher)**: C with `-Wall -Wextra -pedantic -Werror`
- **Index service (src/index/)**: C++17 with Qt6 (`-Wall -Wextra`)
- **Extractor (src/extractor/)**: C++17 with Qt6 (`-Wall -Wextra`)
- **Kernel Module (src/kernelmod/)**: C99 with `-std=gnu99 -Wall -O3`

### File Structure
```
src/
├── server/      # System-level server component (C)
├── kernelmod/   # Linux kernel module (C99, DKMS)
├── logger/      # Logging subsystem (C)
├── dispatcher/  # Event dispatcher library (C)
├── common/      # Code shared by index & extractor (C++)
├── index/       # deepin-anything-index user service (C++17/Qt6, migrated textindex)
└── extractor/   # deepin-anything-extractor subprocess + plugins (C++17/Qt6)
autotests/
└── index/       # ut-anything-index (gtest + stub-ext, white-box; run-ut.sh + coverage)
```

Two test conventions coexist (mirroring upstream layouts): C++ suites live in
root-level `autotests/` (gated by `OPT_ENABLE_BUILD_UT`, one-command runs via
`autotests/run-ut.sh`); the legacy C suites keep their `src/*/tests`
locations. See `docs/design/textindex-migration.md` and `CONTEXT.md` for the
index service architecture and frozen D-Bus/dconfig contracts (do not rename
bus names, object paths, dconfig schemas, or index data directories).

### Naming Conventions
- Functions: `snake_case` (C functions, C++ methods)
- Classes: `CamelCase` (e.g., `EventHandlerConfig`, `EventLogger`)
- Variables: `snake_case` (e.g., `config_ptr`, `event_handler`)
- Constants: `UPPER_CASE` (e.g., `MAX_INPUT_MINOR`, `INFO_LEVELS`)
- Private members: Prefix with `_` (e.g., `_private_ptr`)

### Formatting
- Indentation: 4 spaces (not tabs)
- Braces: Opening on same line (K&R style)
- Spacing: Single space after commas, no space before parentheses
- Line length: 80-120 characters
- No trailing whitespace

### Error Handling
- Return values: 0 for success, non-zero for failure
- C functions: Use `errno` for detailed errors
- C modules: Use `Glib` logging (`g_log()`)
- C++ services: use bare `qDebug/qInfo/qWarning/qCritical` (no logging categories)
- Always check return values of system calls

### C++ Style (C++17/20)
- Use `std::mutex`, `std::lock_guard`, `std::shared_mutex` for synchronization
- Use `std::unique_ptr` for exclusive ownership, `std::shared_ptr` for shared
- Use `std::vector` and `std::string` for collections
- Use `const` references for function parameters
- Prefer explicit lambda captures `[x, &y]`

### C Style (C99)
- Use `#pragma once` for header guards
- Use `SPDX-License-Identifier` and copyright headers
- Use `struct name { ... };` (no typedef for anonymous structs)
- Always check `malloc()`/`calloc()` return values
- Use `strncpy()` and `snprintf()` for strings
- Use `g_strdup()` and `g_strfreev()` in Glib code

### Documentation
- Use C-style `/* */` for block comments, `//` for single-line
- Include SPDX headers: `// Copyright (C) <year> UOS Technology Co., Ltd.`

### Testing Conventions
- Tests located in `src/*/tests/` directories
- Use CMake with CTest framework
- Test files named `test_*.c` or `test_*.cpp`
- Aim for >80% code coverage
