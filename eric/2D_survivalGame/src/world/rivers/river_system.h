#pragma once
#include "FastNoiseLite.h"
#include <vector>

const int RIVER_MAP_SIZE = 2048;   // grid cells per axis (1 cell = 1 tile)
// Covers ±1024 tiles from origin = ±64 chunks
const int RIVER_THRESHOLD = 60;    // minimum flow accumulation to be a river

class RiverSystem {
public:
    std::vector<float> elevation;
    std::vector<int> flow;
    std::vector<bool> riverTile;   // precomputed: this tile is water
    std::vector<bool> riverBank;   // precomputed: this tile is sand (next to river)

    void generate(FastNoiseLite& elevationNoise);
    bool isRiver(int worldTileX, int worldTileY) const;
    bool isBank(int worldTileX, int worldTileY) const;
    int getFlow(int worldTileX, int worldTileY) const;

private:
    int toGrid(int worldTile) const;
    int idx(int gx, int gy) const { return gy * RIVER_MAP_SIZE + gx; }
    // Returns grid index or -1 if out of bounds
    int safeIdx(int worldTileX, int worldTileY) const;
};
