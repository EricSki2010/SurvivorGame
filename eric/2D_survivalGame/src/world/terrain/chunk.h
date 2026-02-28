#pragma once
#include "tile.h"
#include "FastNoiseLite.h"

const int CHUNK_SIZE = 16;

class SpringRiverSystem;  // forward declaration

class Chunk {
public:
    int chunkX;
    int chunkY;
    Tile tiles[CHUNK_SIZE][CHUNK_SIZE];

    Chunk();
    Chunk(int cx, int cy, FastNoiseLite& elevationNoise, FastNoiseLite& moistureNoise, FastNoiseLite& vegetationNoise, SpringRiverSystem* riverSystem, int tileSize);
    Tile& getTile(int localX, int localY);
};
