# aethercli
This is a shortcut manager that takes the form of a famous CLI.

## Compilation and Installation

The project uses CMake and requires a C++17 compliant compiler.

```bash
# 1. Create a build directory
mkdir -p build
cd build

# 2. Configure and compile optimized for your system
cmake -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/usr/local ..
make -j$(nproc)

# 3. Install binaries and configurations (requires sudo)
sudo make install
```

For advanced instructions, configuration, security options, and user management, please refer to [RTFM.md](RTFM.md).
