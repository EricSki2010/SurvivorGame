#include "chunk.h"
#include "world.h"
#include "spring_river_system.h"
#include <random>

Chunk::Chunk() : chunkX(0), chunkY(0) {}

Chunk::Chunk(int cx, int cy, FastNoiseLite& elevationNoise, FastNoiseLite& moistureNoise, FastNoiseLite& vegetationNoise, SpringRiverSystem* riverSystem, int tileSize)
    : chunkX(cx), chunkY(cy) {

    for (int y = 0; y < CHUNK_SIZE; y++) {
        for (int x = 0; x < CHUNK_SIZE; x++) {
            float worldX = (float)(chunkX * CHUNK_SIZE + x);
            float worldY = (float)(chunkY * CHUNK_SIZE + y);

            float elevation = sampleNoise(elevationNoise, worldX, worldY);
            float moisture = sampleNoise(moistureNoise, worldX, worldY);
            float vegetation = sampleNoise(vegetationNoise, worldX, worldY);
            tiles[y][x].id = getBiome(elevation, moisture, vegetation);

            // Rivers from flow accumulation (two-pass system)
            int worldTileX = chunkX * CHUNK_SIZE + x;
            int worldTileY = chunkY * CHUNK_SIZE + y;
            if (riverSystem) {
                if (riverSystem->isRiver(worldTileX, worldTileY)) {
                    // Don't override existing deep water (ocean)
                    if (tiles[y][x].id != TILE_DEEP_WATER) {
                        tiles[y][x].id = TILE_SHALLOW_WATER;
                    }
                } else if (riverSystem->isBank(worldTileX, worldTileY)) {
                    tiles[y][x].id = TILE_SAND;
                }
            }
        }
    }

    // Object placement pass — deterministic seeded RNG per chunk
    uint32_t chunkSeed = (uint32_t)(WorldSeed ^ (cx * 374761393) ^ (cy * 668265263));
    std::mt19937 rng(chunkSeed);
    std::uniform_int_distribution<int> chance(0, 99);

    for (int y = 0; y < CHUNK_SIZE; y++) {
        for (int x = 0; x < CHUNK_SIZE; x++) {
            int tileId = tiles[y][x].id;
            int roll = chance(rng);

            // Sum weights of all matching rules for this tile
            int totalWeight = 0;
            for (int r = 0; r < SPAWN_RULE_COUNT; r++) {
                if (SPAWN_RULES[r].tileId == tileId) totalWeight += SPAWN_RULES[r].weight;
            }
            if (roll >= totalWeight) continue; // no object

            // Pick from the weighted list
            int acc = 0;
            for (int r = 0; r < SPAWN_RULE_COUNT; r++) {
                if (SPAWN_RULES[r].tileId != tileId) continue;
                acc += SPAWN_RULES[r].weight;
                if (roll < acc) {
                    int objId = SPAWN_RULES[r].objectId;
                    tiles[y][x].objectId = objId;
                    int jitter = getSpawnJitter(objId);
                    if (jitter > 0) {
                        std::uniform_int_distribution<int> jitterDist(-jitter, jitter);
                        tiles[y][x].offsetX = (float)jitterDist(rng);
                        tiles[y][x].offsetY = (float)jitterDist(rng);
                    }
                    break;
                }
            }
        }
    }
}

Tile& Chunk::getTile(int localX, int localY) {
    return tiles[localY][localX];
}
