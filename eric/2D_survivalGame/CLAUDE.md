# 2D Survival Game

## Project Overview
A 2D survival game built with C++ and Raylib.

## Tech Stack
- **Language:** C++17
- **Graphics Library:** Raylib
- **Build System:** Make (via MSYS2/MinGW)
- **Platform:** Windows 11

## Project Structure
```
2D_survivalGame/
  src/
    core/
      main.cpp     - Entry point, game loop
      game.h       - Game class declaration
      game.cpp     - Game class implementation
    entities/
      player.h     - Player class declaration
      player.cpp   - Player class implementation
    world/
      world.h      - World class declaration (chunk manager)
      world.cpp    - World class implementation
      terrain/
        tile.h           - Tile struct, biome enums and colors
        chunk.h          - Chunk class declaration (16x16 tile grid)
        chunk.cpp        - Chunk class implementation
        FastNoiseLite.h  - Third-party noise library
      rivers/
        river_system.h           - Legacy fixed-map river system
        river_system.cpp
        spring_river_system.h    - Region-based on-demand river system
        spring_river_system.cpp
  Makefile         - Build configuration
```

## Build & Run
```bash
make        # compile the project
make run    # compile and run
make clean  # remove build artifacts
```

## Conventions
- Use `snake_case` for file names
- Use `PascalCase` for class names
- Use `camelCase` for variables and functions
- Keep header files (.h) for declarations, source files (.cpp) for implementations
- Use `#pragma once` for header guards
- Pass large objects by `const&` reference
- Prefer stack allocation and vectors over raw `new`/`delete`
- Game loop runs in main.cpp, game logic lives in Game class

## Save System Considerations
- The game should support saving/loading in the future — keep this in mind when adding new systems
- World terrain is fully deterministic (fixed seed + noise), so only player state and modifications need saving
- **Do not store transient/regenerable data as persistent state** — anything derivable from the seed does not need saving
- When adding player-modifiable world state (building, placing, breaking), store modifications as a **chunk overlay** (e.g., `std::unordered_map<int64_t, std::vector<TileModification>>`) that lives in `World` and persists across chunk load/unload cycles
- New gameplay state (inventory, crafting progress, NPCs, etc.) should be designed with serialization in mind — prefer plain data (ints, floats, enums, vectors of structs) over opaque runtime-only objects
- Chunk unload currently destroys all data (`chunks.erase()`); any future modification system must write changes to the overlay map before a chunk is erased

## Raylib Notes
- Window, input, drawing all handled through Raylib C functions
- Draw calls must be between `BeginDrawing()` and `EndDrawing()`
- Raylib uses simple structs like `Vector2`, `Rectangle`, `Color`
- Docs: https://www.raylib.com/cheatsheet/cheatsheet.html
