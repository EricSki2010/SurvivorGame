#pragma once
#include "FastNoiseLite.h"
#include <vector>
#include <unordered_map>
#include <cstdint>

const int RIVER_REGION_SIZE = 512;      // tiles per region axis
const int RIVER_REGION_PADDING = 256;   // extra elevation sampling for river tracing
const int RIVER_SAMPLE_SIZE = RIVER_REGION_SIZE + 2 * RIVER_REGION_PADDING; // 1024

class SpringRiverSystem {
public:
    // Generate rivers for a single region (called on demand)
    void generateRegion(int regionX, int regionY, FastNoiseLite& elevationNoise);

    // Check if the region containing this world tile has been generated
    bool isRegionGenerated(int worldTileX, int worldTileY) const;

    // Convert world tile coord to region coord
    static int worldToRegion(int worldTile);

    // Query tiles (returns false for ungenerated regions)
    bool isRiver(int worldTileX, int worldTileY) const;
    bool isBank(int worldTileX, int worldTileY) const;
    int getFlow(int worldTileX, int worldTileY) const;

private:
    struct Region {
        std::vector<bool> river;   // RIVER_REGION_SIZE * RIVER_REGION_SIZE
        std::vector<bool> bank;
        std::vector<int> flow;
    };

    std::unordered_map<int64_t, Region> regions;

    int64_t regionKey(int rx, int ry) const {
        return ((int64_t)rx << 32) | (uint32_t)ry;
    }
};
