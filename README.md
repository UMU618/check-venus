# Venus Requirements Checker

## Overview

This utility verifies whether the system meets the requirements for Venus. Please note that this tool was generated with AI assistance and may not detect all Venus dependencies with complete accuracy.

## Building and Running

### Quick Check

This method uses a bash script to check Venus requirements:

```sh
bash ./check-venus.sh
```

The script relies on `vulkaninfo` to detect Vulkan. If `vulkaninfo` is unavailable on your system, you will need to compile the C code instead using one of the methods below.

### Using Make

```sh
make
sudo ./check-venus
```

### Using Meson

If Meson is installed:

```sh
meson setup build
meson compile -C build
sudo ./build/check-venus
```
