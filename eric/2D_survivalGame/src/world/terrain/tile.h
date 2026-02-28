#pragma once
#include "raylib.h"

enum ObjectType {
    OBJECT_NONE = 0,
    OBJECT_ROCK,
    OBJECT_TREE,
    OBJECT_COUNT
};

// Collision box size for each object type (width, height) — {0,0} means no collision
inline const Vector2 OBJECT_COLLISION_SIZE[OBJECT_COUNT] = {
    {0, 0},       // OBJECT_NONE
    {14, 12},     // OBJECT_ROCK
    {11, 12},     // OBJECT_TREE
};

// Max random offset: floor(16 - collisionWidth/2), keeps collision inside the tile
inline int getSpawnJitter(int objId) {
    Vector2 col = OBJECT_COLLISION_SIZE[objId];
    if (col.x == 0 && col.y == 0) return 0;
    return (int)(16.0f - col.x / 2.0f);
}

enum TileType {
    TILE_DEEP_WATER = 0,
    TILE_SHALLOW_WATER,
    TILE_SAND,
    TILE_DESERT,
    TILE_DRY_GRASS,
    TILE_GRASS,
    TILE_SWAMP,
    TILE_DRY_HILLS,
    TILE_FOREST,
    TILE_DENSE_FOREST,
    TILE_BARE_STONE,
    TILE_MOSSY_STONE,
    TILE_MOUNTAIN_PEAK,
    TILE_SNOWY_PEAK,
    TILE_COUNT
};

inline const char* const TILE_NAMES[TILE_COUNT] = {
    "Deep Water",
    "Shallow Water",
    "Sand",
    "Desert",
    "Dry Grass",
    "Grass",
    "Swamp",
    "Dry Hills",
    "Forest",
    "Dense Forest",
    "Bare Stone",
    "Mossy Stone",
    "Mountain Peak",
    "Snowy Peak",
};

inline const Color TILE_COLORS[TILE_COUNT] = {
    {14, 120, 100, 255},   // deep water (darker #14a98e)
    {20, 169, 142, 255},   // shallow water (#14a98e)
    {210, 190, 130, 255},  // sand
    {194, 178, 128, 255},  // desert
    {120, 180, 60, 255},   // dry grass
    {60, 180, 60, 255},    // grass
    {30, 120, 80, 255},    // swamp
    {160, 140, 100, 255},  // dry hills
    {30, 130, 30, 255},    // forest
    {20, 80, 40, 255},     // dense forest
    {140, 130, 120, 255},  // bare stone
    {110, 130, 110, 255},  // mossy stone
    {180, 180, 180, 255},  // mountain peak
    {220, 230, 240, 255},  // snowy peak
};

struct Tile {
    int id = TILE_GRASS;
    int objectId = OBJECT_NONE;
    float offsetX = 0.0f; // random placement offset within tile
    float offsetY = 0.0f;
};

// Object spawn rules: { objectType, tileType, weight }
// For each tile, matching rules are collected and weights summed.
// A roll 0-99 picks: if roll < total weight, pick from the weighted list. Otherwise no object.
struct SpawnRule {
    int objectId;
    int tileId;
    int weight; // out of 100
};

inline const SpawnRule SPAWN_RULES[] = {
    // Grass
    { OBJECT_TREE, TILE_GRASS,        1 },
    { OBJECT_ROCK, TILE_GRASS,        3 },

    // Forest
    { OBJECT_TREE, TILE_FOREST,       20 },
    { OBJECT_ROCK, TILE_FOREST,       7 },

    // Dense Forest
    { OBJECT_TREE, TILE_DENSE_FOREST, 30 },
    { OBJECT_ROCK, TILE_DENSE_FOREST, 10 },
};

inline const int SPAWN_RULE_COUNT = sizeof(SPAWN_RULES) / sizeof(SPAWN_RULES[0]);

// Returns true if a tile blocks movement
// Nothing is solid yet — flip individual checks here as needed
inline bool isTileSolid(int tileId, int objectId) {
    (void)tileId;
    (void)objectId;
    return false;
}
// Water elevation thresholds
const float DEEP_WATER_THRESHOLD = 0.25f;
const float WATER_THRESHOLD = 0.35f;
const float SAND_THRESHOLD = 0.37f;

inline TileType getBiome(float elevation, float moisture, float vegetation) {
    if (elevation < 0.25f) return TILE_DEEP_WATER;
    if (elevation < 0.35f) return TILE_SHALLOW_WATER;
    if (elevation < 0.37f) return TILE_SAND;

    if (elevation < 0.65f) {
        if (moisture < 0.30f) return TILE_DESERT;
        if (vegetation > 0.60f && vegetation < 0.80f) return TILE_FOREST; 
        if (vegetation > 0.80f) return TILE_DENSE_FOREST;
        if (moisture < 0.50f) return TILE_DRY_GRASS;
        if (moisture < 0.70f) return TILE_GRASS;
        return TILE_SWAMP;
    }

    if (elevation < 0.75f) {
        if ((moisture < 0.50f && vegetation < 0.40f) || moisture < 0.30f) return TILE_DRY_HILLS;
        if (vegetation < 0.70f) return TILE_FOREST;
        return TILE_DENSE_FOREST;
    }

    if (elevation < 0.85f) {
        if (moisture < 0.40f) return TILE_BARE_STONE;
        return TILE_MOSSY_STONE;
    }

    if (moisture > 0.50f) return TILE_SNOWY_PEAK;
    return TILE_MOUNTAIN_PEAK;
}
