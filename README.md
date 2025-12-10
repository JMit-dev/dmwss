# DMWSS - Game Boy Emulator

**D**ot **M**atrix **W**ith **S**tereo **S**ound - A high-performance, modular Game Boy emulator written in modern C++20.

## Features

- High-performance cached interpreter with block caching
- Software fastmem for optimized memory access
- Event-driven scheduler for cycle-accurate timing
- Modular architecture with clean separation of concerns
- Cross-platform support (Linux, Windows, macOS)

## Architecture Highlights

- **CPU**: Cached interpreter with instruction block caching
- **Memory**: Software fastmem with page tables (256-byte pages)
- **PPU**: Accurate rendering with mode timing
- **APU**: Four audio channels with miniaudio backend
- **Scheduler**: Event-driven timing system
- **UI**: Qt6-based GUI with OpenGL rendering

## Building

### Prerequisites

#### Linux
```bash
sudo apt-get install qt6-base-dev libgl1-mesa-dev cmake build-essential
```

#### macOS
```bash
brew install qt@6 cmake
```

#### Windows
- Install Qt6 from [Qt online installer](https://www.qt.io/download)
- Install CMake and Visual Studio 2022

### Build Instructions

```bash
# Clone with submodules
git clone --recursive https://github.com/yourusername/dmwss.git
cd dmwss

# Configure
cmake -B build -DCMAKE_BUILD_TYPE=Release

# Build
cmake --build build --config Release

# Run
./build/dmwss
```

## Project Status

🚧 **In Active Development** - Phase 0: Project Setup Complete

See [CLAUDE.md](CLAUDE.md) for detailed development roadmap.

## Dependencies

- **Qt6** - GUI framework
- **GLFW** - Window/input handling
- **GLAD** - OpenGL loader
- **miniaudio** - Audio backend
- **fmt** - String formatting
- **spdlog** - Logging
- **nlohmann/json** - JSON configuration

## License

TBD

## Development

This project follows a phased development approach:
- Phase 0: Project Setup ✅
- Phase 1: Core Types and Utilities (In Progress)
- Phase 2: Event Scheduler
- Phase 3: Memory System with Fastmem
- Phase 4: CPU with Cached Interpreter
- Phase 5: PPU Implementation
- Phase 6: APU Implementation
- Phase 7: Machine Integration
- Phase 8: Platform Abstraction
- Phase 9: Qt GUI Implementation
- Phase 10: Shaders
- Phase 11: Save States
- Phase 12: Testing and Optimization
- Phase 13: Documentation
