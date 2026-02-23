# Build System Documentation

This project, `qcap-demos`, uses a sophisticated, custom-built modular build system based on **GNU Make**. It is designed to handle cross-platform development for various embedded and desktop Linux systems, including NVIDIA Jetson (L4T), IGX, and HiSilicon platforms.

## 1. High-Level Architecture
The build system is highly decoupled and follows a functional programming style within Make. It separates core build logic from platform-specific configurations and individual module definitions.

*   **Entry Point**: Root-level `.mk` files (e.g., `l4t-r36-2.mk`) act as wrappers for specific target environments.
*   **Orchestration (`mkfiles/rules.mk`)**: The engine that defines global paths, includes all modules, and dynamically generates compilation and linking rules.
*   **Functional Macros (`mkfiles/funcs.mk`)**: A library of Make macros (`decl_mod`, `add_mod`, `compile_file`, etc.) that act as an internal API for defining project structure.

## 2. Key Concepts

### Modules
The project is divided into **Modules** (e.g., `ZZLAB`, `QCAP`). A module can be a static library, a shared library, or a set of source files.
*   **Isolation**: Each module defines its own source files (`SRCS`), dependencies (`DEPS`), and flags (`I`, `F`, `L`).
*   **Propagation**: The system automatically propagates flags from dependencies. If Module B depends on Module A, Module B automatically receives Module A's include paths and flags.

### Platforms
Platform files in `mkfiles/` define the specific environment:
*   Compilers (`CXX`, `CC`, `NVCC`).
*   Architecture-specific flags (e.g., `GENCODE_FLAGS` for CUDA).
*   System library paths.

### Feature Flags
Root `.mk` files use boolean flags (e.g., `BUILD_WITH_ZZLAB?=ON`) to enable or disable features. These flags are used to conditionally include source files or define preprocessor macros.

## 3. Usage

### Standard Commands
Run `make` using the specific platform file as the makefile:

```bash
# Build everything for a specific platform
make -f l4t-r36-2.mk

# Build utilities only
make -f l4t-r36-2.mk utils

# Build tests only
make -f l4t-r36-2.mk tests

# Clean build artifacts for a platform
make -f l4t-r36-2.mk clean
```

### Build Artifacts
All artifacts are organized under the `_objs/` directory to keep the source tree clean:
*   **Object Files**: `_objs/<platform>/<module>.dir/...`
*   **Libraries**: `_objs/<platform>/lib/`
*   **Binaries**: `_objs/<platform>/bin/`

## 4. Advanced Features
*   **CUDA Support**: Native handling of `.cu` files using `NVCC` with support for multiple GPU architectures (SM 5.3 to SM 8.7).
*   **Dependency Tracking**: Uses `-MMD` flags to generate `.d` files, ensuring that only modified files and their dependents are recompiled.
*   **Modular Flags (`mkfiles/flags.mk`)**: A centralized system to manage platform-specific preprocessor flags across all modules.
