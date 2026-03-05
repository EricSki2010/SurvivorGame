#include "chunk.h"
#include "world.h"
#include "game_data.h"
#include "spring_river_system.h"

static uint32_t tileHash(uint32_t seed, int x, int y, uint32_t slot) {
    uint32_t h = seed ^ ((uint32_t)x * 374761393u) ^ ((uint32_t)y * 668265263u) ^ (slot * 1234567891u);
    h ^= h >> 16;
    h *= 0x45d9f3bu;
    h ^= h >> 16;
    return h;
}

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

    // Object placement pass — deterministic hash per tile
    for (int y = 0; y < CHUNK_SIZE; y++) {
        for (int x = 0; x < CHUNK_SIZE; x++) {
            int worldTileX = chunkX * CHUNK_SIZE + x;
            int worldTileY = chunkY * CHUNK_SIZE + y;
            int tileId = tiles[y][x].id;
            int roll = (int)(tileHash(WorldSeed, worldTileX, worldTileY, 0) % 100); // slot 0 — spawn chance

            // Sum weights of all matching rules for this tile
            int totalWeight = 0;
            for (int r = 0; r < gData().spawnRuleCount; r++) {
                if (gData().spawnRules[r].tileId == tileId) totalWeight += gData().spawnRules[r].weight;
            }
            if (roll >= totalWeight) continue; // no object

            // Pick from the weighted list
            int acc = 0;
            for (int r = 0; r < gData().spawnRuleCount; r++) {
                if (gData().spawnRules[r].tileId != tileId) continue;
                acc += gData().spawnRules[r].weight;
                if (roll < acc) {
                    int objId = gData().spawnRules[r].objectId;
                    tiles[y][x].objectId = objId;
                    int jitter = gData().getSpawnJitter(objId);
                    if (jitter > 0) {
                        int range = 2 * jitter + 1;
                        tiles[y][x].offsetX = (float)((int)(tileHash(WorldSeed, worldTileX, worldTileY, 1) % range) - jitter); // slot 1 — jitter X
                        tiles[y][x].offsetY = (float)((int)(tileHash(WorldSeed, worldTileX, worldTileY, 2) % range) - jitter); // slot 2 — jitter Y
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
