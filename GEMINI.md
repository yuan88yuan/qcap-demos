# GEMINI.md

## Project Overview

This project, `qcap-demos`, is a C++ application designed to demonstrate and test the `qcap` library. The `qcap` library appears to be a video and audio capture library, with an API for creating and managing capture devices, setting video and audio inputs, and controlling the capture process.

The project is structured as follows:

*   `src/qdemo`: The main demonstration application.
*   `src/zzlab`: A utility library providing logging, timing, and other helper functions.
*   `src/tests`: Contains test applications.
*   `mkfiles`: Contains the makefile-based build system, which supports a wide variety of platforms and libraries, including:
    *   NVIDIA CUDA
    *   FFmpeg
    *   GStreamer
    *   Various Linux distributions (Ubuntu, CentOS)
    *   Embedded platforms (L4T, HiSilicon)

## Building and Running

The project is built using `make`. The build system is complex and highly configurable via the `.mk` files in the `mkfiles` directory.

**To build the project:**

It is not immediately clear from the provided files what the default make target is. To build the project, you will likely need to specify a target.

```bash
# TODO: Add build commands here.
# It is likely that you need to run make with a specific target,
# for example:
# make l4t-r36-2
```

**To run the application:**

After a successful build, an executable should be created in the `_objs` directory.

```bash
# TODO: Add running commands here.
# The executable is likely in the _objs directory, for example:
# ./_objs/l4t-r36-2/qdemo
```

## Development Conventions

*   **Coding Style:** The code follows a consistent C++ style. It uses namespaces to organize code and classes for major components.
*   **Logging:** A custom logging framework (`ZzLog`) is used, with macros like `LOGI`, `LOGD`, and `LOGE` for logging messages at different severity levels.
*   **Resource Management:** A `free_stack` class is used for resource management. This is a stack of functions that are called to free resources when an object goes out of scope.
*   **Testing:** The `src/tests` directory contains test applications. The build system has a `tests.mk` file, suggesting a dedicated build target for tests.
