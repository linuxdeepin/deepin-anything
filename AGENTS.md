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
# Not support
```

### Lint and Static Analysis
```bash
# cppcheck runs automatically on all PRs via .github/workflows/cppcheck.yml
```

## Code Style Guidelines

### Language Standards
- **Daemon (src/daemon/)**: C++20 with `-Wall -Werror -Wextra`
- **Logger (src/logger/)**: C++17 with `-Wall -Werror -Wextra`
- **Kernel Module (src/kernelmod/)**: C99 with `-std=gnu99 -Wall -O3`

### File Structure
```
src/
├── daemon/    # Main daemon service (C++20)
├── server/    # Server component (C++)
├── kernelmod/ # Linux kernel module (C99)
├── logger/    # Logging subsystem (C++17)
└── searcher/  # Search functionality (C++)
```

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
- C++ daemon: Use `spdlog` for logging
- C modules: Use `Glib` logging (`g_log()`)
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
