# Native Cross-Compilation with Meson for ARM64 Linux

*A complete guide for compiling code on macOS for Rockchip/Allwinner ARM64 SBCs*

------

## Table of Contents

1. Introduction
2. Prerequisites
3. Understanding the Components
4. Setting Up the Sysroot
5. Creating the Meson Cross File
6. Building Your Project
7. Advanced Configurations
8. Common Issues and Solutions
9. Complete Example Project
10. Reference

------

## Introduction

### What is Cross-Compilation?

Cross-compilation is the process of compiling code on one platform (the **host**) to run on a different platform (the **target**). This guide covers compiling on **macOS** (host) for **ARM64 Linux** (target), specifically for SBCs like the Radxa Rock 3C and Cubie A7S.

### Why Clang + Meson?

| Tool      | Role           | Why Choose It                                                |
| :-------- | :------------- | :----------------------------------------------------------- |
| **Clang** | Compiler       | Native cross-compilation support; one binary targets any architecture |
| **Meson** | Build system   | Excellent cross-compilation support; fast; declarative syntax |
| **Ninja** | Build executor | Fast, minimal, works well with Meson                         |

### What You'll Build

Code on your Mac → Cross-compiled ARM64 binary → Deployed to Rock 3C/Cubie A7S

------

## Prerequisites

### On Your Mac

bash

```
# Install required tools
brew install llvm meson ninja

# Verify installation
clang --version
meson --version
ninja --version

# Ensure LLVM tools are in PATH (add to ~/.zshrc or ~/.bash_profile)
export PATH="/opt/homebrew/opt/llvm/bin:$PATH"
```



### On Your Target Device (Rock 3C / Cubie A7S)

bash

```
# Ensure SSH access is enabled
sudo systemctl enable --now ssh

# Get your device's IP address
ip addr show | grep inet

# Create a directory for sysroot transfer (optional)
mkdir -p ~/sysroot
```



------

## Understanding the Components

### The Sysroot

The **sysroot** is a directory containing the target system's headers and libraries. It tells Clang what environment the compiled code will run in.

text

```
sysroot/
├── usr/
│   ├── include/     # Header files (.h)
│   ├── lib/         # Shared libraries (.so)
│   └── lib64/       # 64-bit libraries
└── lib/             # Base libraries
```



### The Cross File

A Meson **cross file** describes the target platform and tells Meson which tools to use.

### The Toolchain

- **Clang**: Compiles C/C++ code
- **LLVM tools**: `llvm-ar`, `llvm-strip`, `llvm-objcopy`
- **LLD**: LLVM's linker (recommended for cross-compilation)

------

## Setting Up the Sysroot

### Option 1: Copy from Target Device (Recommended)

bash

```
# Create sysroot directory
mkdir -p ~/rock3c-sysroot
cd ~/rock3c-sysroot

# Copy system root from your Rock 3C
# Replace 'rock3c.local' with your device's IP
scp -r radxa@rock3c.local:/usr/include ./usr/
scp -r radxa@rock3c.local:/usr/lib/aarch64-linux-gnu ./usr/lib/
scp -r radxa@rock3c.local:/lib/aarch64-linux-gnu ./lib/
scp -r radxa@rock3c.local:/usr/lib/aarch64-linux-gnu/pkgconfig ./usr/lib/pkgconfig/

# For the Cubie A7S, use:
scp -r radxa@cubie-a7s.local:/usr/include ./usr/
scp -r radxa@cubie-a7s.local:/usr/lib/aarch64-linux-gnu ./usr/lib/
```



### Option 2: Use Homebrew Cross Toolchain Sysroot

bash

```
# Install cross toolchain (includes sysroot)
brew install aarch64-unknown-linux-gnu

# The sysroot is located at:
ls /opt/homebrew/Cellar/aarch64-unknown-linux-gnu/*/sysroot/

# Create a symlink for convenience
ln -s /opt/homebrew/Cellar/aarch64-unknown-linux-gnu/*/sysroot ~/rock3c-sysroot
```



### Option 3: Minimal Sysroot for Static Binaries

For statically linked binaries, you only need headers:

bash

```
mkdir -p ~/rock3c-sysroot/usr/include
scp -r radxa@rock3c.local:/usr/include/* ~/rock3c-sysroot/usr/include/
```



### Verify Sysroot Completeness

bash

```
# Check for essential libraries
ls ~/rock3c-sysroot/usr/lib/libc.so*
ls ~/rock3c-sysroot/usr/include/stdio.h

# Should show libc.so and stdio.h
```



------

## Creating the Meson Cross File

Create a file named `aarch64-linux-clang.ini` in your project root or `~/.config/meson/cross/`:

ini

```
[binaries]
c = 'clang'
cpp = 'clang++'
ar = 'llvm-ar'
strip = 'llvm-strip'
ld = 'ld.lld'
pkgconfig = 'pkg-config'

[host_machine]
system = 'linux'
cpu_family = 'aarch64'
cpu = 'armv8-a'
endian = 'little'

[built-in options]
c_args = ['-target', 'aarch64-linux-gnu', '--sysroot=/Users/YOUR_USERNAME/rock3c-sysroot']
c_link_args = ['-target', 'aarch64-linux-gnu', '--sysroot=/Users/YOUR_USERNAME/rock3c-sysroot', '-fuse-ld=lld']

cpp_args = ['-target', 'aarch64-linux-gnu', '--sysroot=/Users/YOUR_USERNAME/rock3c-sysroot']
cpp_link_args = ['-target', 'aarch64-linux-gnu', '--sysroot=/Users/YOUR_USERNAME/rock3c-sysroot', '-fuse-ld=lld']

[properties]
needs_exe_wrapper = false
```



> **Important:** Replace `/Users/YOUR_USERNAME/rock3c-sysroot` with the actual path to your sysroot.

### For Cubie A7S (Same, but optional CPU tuning)

ini

```
[host_machine]
cpu = 'armv8.2-a'  # A733 supports ARMv8.2
```



### For Projects Using Dependencies

Add pkg-config wrapper to find libraries in sysroot:

bash

```
#!/bin/bash
# Save as ~/bin/aarch64-pkg-config
export PKG_CONFIG_DIR=
export PKG_CONFIG_LIBDIR=/Users/YOUR_USERNAME/rock3c-sysroot/usr/lib/pkgconfig
export PKG_CONFIG_SYSROOT_DIR=/Users/YOUR_USERNAME/rock3c-sysroot
exec pkg-config "$@"

chmod +x ~/bin/aarch64-pkg-config
```



Update the cross file:

ini

```
[binaries]
pkgconfig = '/Users/YOUR_USERNAME/bin/aarch64-pkg-config'
```



------

## Building Your Project

### Basic Build Workflow

bash

```
# 1. Configure the build directory
meson setup build --cross-file aarch64-linux-clang.ini

# 2. Compile the project
ninja -C build

# 3. Check the generated binary
file build/your-program

# Expected output:
# build/your-program: ELF 64-bit LSB executable, ARM aarch64, version 1 (SYSV)
```



### Clean Build

bash

```
# Remove build directory and reconfigure
rm -rf build
meson setup build --cross-file aarch64-linux-clang.ini
ninja -C build
```



### Build with Custom Options

bash

```
# Set build type
meson setup build --cross-file aarch64-linux-clang.ini -Dbuildtype=release

# Set optimization level
meson setup build --cross-file aarch64-linux-clang.ini -Doptimization=3

# Enable/disable features
meson setup build --cross-file aarch64-linux-clang.ini -Dmyfeature=enabled
```



### Deploy to Target Device

bash

```
# Copy binary to Rock 3C
scp build/your-program radxa@rock3c.local:~

# Run it
ssh radxa@rock3c.local ./your-program
```



------

## Advanced Configurations

### Static Linking

Create `aarch64-linux-clang-static.ini`:

ini

```
[binaries]
c = 'clang'
cpp = 'clang++'
ar = 'llvm-ar'
strip = 'llvm-strip'

[host_machine]
system = 'linux'
cpu_family = 'aarch64'
cpu = 'armv8-a'
endian = 'little'

[built-in options]
default_library = 'static'
c_args = ['-target', 'aarch64-linux-gnu', '--sysroot=/Users/YOUR_USERNAME/rock3c-sysroot', '-static']
c_link_args = ['-target', 'aarch64-linux-gnu', '--sysroot=/Users/YOUR_USERNAME/rock3c-sysroot', '-static']
cpp_args = ['-target', 'aarch64-linux-gnu', '--sysroot=/Users/YOUR_USERNAME/rock3c-sysroot', '-static']
cpp_link_args = ['-target', 'aarch64-linux-gnu', '--sysroot=/Users/YOUR_USERNAME/rock3c-sysroot', '-static']
```



### Cross-Compiling with Dependencies (CMake Subprojects)

For projects using `cmake` subprojects (like `libpng`, `zlib`), add to your cross file:

ini

```
[cmake]
CMake_ROOT = '/Users/YOUR_USERNAME/rock3c-sysroot/usr'
CMAKE_TOOLCHAIN_FILE = '/path/to/toolchain.cmake'
```



### Hardware-Specific Optimizations

For Rock 3C (RK3568, Cortex-A55):

ini

```
c_args = ['-target', 'aarch64-linux-gnu', '--sysroot=...', '-march=armv8.2-a+crc', '-mtune=cortex-a55']
```



For Cubie A7S (A733, Cortex-A76 + A55):

ini

```
c_args = ['-target', 'aarch64-linux-gnu', '--sysroot=...', '-march=armv8.2-a+fp16+rcpc', '-mtune=cortex-a76']
```



### Using Distributed Compilation

bash

```
# Install distcc on Mac
brew install distcc

# Set up distcc (on both Mac and target)
# On target (Rock 3C):
export DISTCC_HOSTS="localhost your-mac-ip"

# In cross file:
[binaries]
c = 'distcc clang'
cpp = 'distcc clang++'
```



------

## Common Issues and Solutions

### Issue 1: `fatal error: 'stdio.h' file not found`

**Cause:** Sysroot missing or incorrect path.

**Solution:**

bash

```
# Verify sysroot path
ls ~/rock3c-sysroot/usr/include/stdio.h

# Update cross file with correct absolute path
```



### Issue 2: `ld: unknown option: --fuse-ld=lld`

**Cause:** Using system ld instead of LLVM's lld.

**Solution:**

bash

```
# Install lld
brew install lld

# Use full path in cross file
c_link_args = ['-target', 'aarch64-linux-gnu', '--sysroot=...', '-fuse-ld=/opt/homebrew/opt/llvm/bin/ld.lld']
```



### Issue 3: `cannot find -lssl`

**Cause:** OpenSSL headers/libraries missing from sysroot.

**Solution:**

bash

```
# Install OpenSSL on target, then recopy sysroot
ssh radxa@rock3c.local "sudo apt install libssl-dev"
scp -r radxa@rock3c.local:/usr/lib/aarch64-linux-gnu/libssl* ~/rock3c-sysroot/usr/lib/
scp -r radxa@rock3c.local:/usr/include/openssl ~/rock3c-sysroot/usr/include/
```



### Issue 4: Meson says `Unknown compiler(s)`

**Cause:** Meson can't find Clang.

**Solution:**

bash

```
# Ensure Clang is in PATH
which clang

# Or specify full path in cross file:
[binaries]
c = '/opt/homebrew/opt/llvm/bin/clang'
cpp = '/opt/homebrew/opt/llvm/bin/clang++'
```



### Issue 5: `pkg-config` finds host libraries instead of target

**Cause:** pkg-config isn't configured for cross-compilation.

**Solution:** Use the pkg-config wrapper shown earlier or set environment:

bash

```
export PKG_CONFIG_SYSROOT_DIR=~/rock3c-sysroot
export PKG_CONFIG_LIBDIR=~/rock3c-sysroot/usr/lib/pkgconfig
```



### Issue 6: Executable fails with `cannot execute binary file: Exec format error`

**Cause:** Binary is ARM64, trying to run on x86 Mac.

**Solution:** This is normal; the binary is correct. Run it on your target device.

------

## Complete Example Project

### Project Structure

text

```
myproject/
├── meson.build
├── src/
│   ├── main.c
│   └── utils.c
├── include/
│   └── utils.h
└── aarch64-linux-clang.ini
```



### Source Code

**`src/main.c`:**

c

```
#include <stdio.h>
#include "utils.h"

int main() {
    printf("Hello from Rock 3C!\n");
    printf("2 + 3 = %d\n", add(2, 3));
    return 0;
}
```



**`include/utils.h`:**

c

```
#ifndef UTILS_H
#define UTILS_H

int add(int a, int b);

#endif
```



**`src/utils.c`:**

c

```
#include "utils.h"

int add(int a, int b) {
    return a + b;
}
```



**`meson.build`:**

python

```
project('myproject', 'c',
    version: '1.0.0',
    default_options: ['c_std=c11', 'buildtype=release'])

inc = include_directories('include')

executable('myprogram',
    sources: ['src/main.c', 'src/utils.c'],
    include_directories: inc,
    install: true)

# Optional: install to /usr/local/bin on target
install_headers('include/utils.h')
```



### Build Script

**`build.sh`:**

bash

```
#!/bin/bash

# Set sysroot path (adjust as needed)
export SYSROOT="$HOME/rock3c-sysroot"

# Configure
meson setup build \
    --cross-file aarch64-linux-clang.ini \
    --prefix=/usr/local

# Build
ninja -C build

# Show binary info
echo "Built binary:"
file build/myprogram

# Optionally install to sysroot (for further development)
DESTDIR="$SYSROOT" ninja -C build install
```



### Deployment Script

**`deploy.sh`:**

bash

```
#!/bin/bash

TARGET_IP="${1:-rock3c.local}"
TARGET_USER="${2:-radxa}"

echo "Deploying to $TARGET_USER@$TARGET_IP"

# Copy binary
scp build/myprogram $TARGET_USER@$TARGET_IP:~

# Copy any additional files
# scp -r data/ $TARGET_USER@$TARGET_IP:~/data

echo "Run with: ssh $TARGET_USER@$TARGET_IP ./myprogram"
```



------

## Reference

### Cross File Options Quick Reference

| Section              | Option            | Description                  |
| :------------------- | :---------------- | :--------------------------- |
| `[binaries]`         | `c`, `cpp`        | Compiler commands            |
|                      | `ar`              | Archiver                     |
|                      | `strip`           | Strips debug symbols         |
|                      | `ld`              | Linker                       |
|                      | `pkgconfig`       | pkg-config wrapper           |
| `[host_machine]`     | `system`          | 'linux', 'windows', 'darwin' |
|                      | `cpu_family`      | 'aarch64', 'x86_64'          |
|                      | `cpu`             | 'armv8-a', 'cortex-a55'      |
|                      | `endian`          | 'little' or 'big'            |
| `[built-in options]` | `c_args`          | Extra C compiler args        |
|                      | `c_link_args`     | Extra linker args            |
|                      | `default_library` | 'shared' or 'static'         |

### Useful LLVM Tools

| Tool           | Purpose                    |
| :------------- | :------------------------- |
| `clang`        | C/C++/Objective-C compiler |
| `clang++`      | C++ compiler               |
| `ld.lld`       | Linker                     |
| `llvm-ar`      | Archiver (replaces `ar`)   |
| `llvm-strip`   | Strips symbols             |
| `llvm-objdump` | Disassemble binaries       |
| `llvm-readelf` | Read ELF information       |

### Testing Your Cross-Compilation Setup

bash

```
# Create a minimal test
cat > test.c << 'EOF'
#include <stdio.h>
int main() { printf("OK\n"); return 0; }
EOF

# Compile manually
clang --target=aarch64-linux-gnu --sysroot=~/rock3c-sysroot -o test test.c

# Check result
file test
# Should show: ELF 64-bit LSB executable, ARM aarch64

# Copy to target and run
scp test radxa@rock3c.local:~/
ssh radxa@rock3c.local ./test
# Should output: OK
```



------

## Conclusion

You now have a complete setup for native cross-compilation from macOS to ARM64 Linux using Clang and Meson. This workflow:

- ✅ Requires no Docker or virtualization
- ✅ Integrates natively with macOS
- ✅ Works for both Rock 3C and Cubie A7S
- ✅ Supports complex projects with dependencies
- ✅ Is fast and reproducible

For further reading:

- [Meson Cross-Compilation Documentation](https://mesonbuild.com/Cross-compilation.html)
- [Clang Cross-Compilation Guide](https://clang.llvm.org/docs/CrossCompilation.html)
- [Radxa Documentation](https://docs.radxa.com/)

------

*Document version: 1.0 | Last updated: 2026-06-06*