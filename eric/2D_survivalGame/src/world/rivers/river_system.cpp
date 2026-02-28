#include "river_system.h"
#include "tile.h"
#include <algorithm>
#include <numeric>
#include <cstdio>
#include <queue>

int RiverSystem::toGrid(int worldTile) const {
    return worldTile + RIVER_MAP_SIZE / 2;
}

int RiverSystem::safeIdx(int worldTileX, int worldTileY) const {
    int gx = toGrid(worldTileX);
    int gy = toGrid(worldTileY);
    if (gx < 0 || gx >= RIVER_MAP_SIZE || gy < 0 || gy >= RIVER_MAP_SIZE) return -1;
    return idx(gx, gy);
}

int RiverSystem::getFlow(int worldTileX, int worldTileY) const {
    int i = safeIdx(worldTileX, worldTileY);
    return i >= 0 ? flow[i] : 0;
}

bool RiverSystem::isRiver(int worldTileX, int worldTileY) const {
    int i = safeIdx(worldTileX, worldTileY);
    return i >= 0 && riverTile[i];
}

bool RiverSystem::isBank(int worldTileX, int worldTileY) const {
    int i = safeIdx(worldTileX, worldTileY);
    return i >= 0 && riverBank[i];
}

void RiverSystem::generate(FastNoiseLite& elevationNoise) {
    int total = RIVER_MAP_SIZE * RIVER_MAP_SIZE;
    elevation.resize(total);
    flow.resize(total, 1);  // each cell starts with 1 unit of rain
    riverTile.resize(total, false);
    riverBank.resize(total, false);

    // 8-directional neighbor offsets
    const int dx8[] = {-1, 0, 1, -1, 1, -1, 0, 1};
    const int dy8[] = {-1, -1, -1, 0, 0, 1, 1, 1};
    const int dx4[] = {-1, 1, 0, 0};
    const int dy4[] = {0, 0, -1, 1};

    const float noiseOffset = 10000.0f;

    // ---- Pass 1: Sample elevation ----
    for (int gy = 0; gy < RIVER_MAP_SIZE; gy++) {
        for (int gx = 0; gx < RIVER_MAP_SIZE; gx++) {
            float worldX = (float)(gx - RIVER_MAP_SIZE / 2) + noiseOffset;
            float worldY = (float)(gy - RIVER_MAP_SIZE / 2) + noiseOffset;
            elevation[idx(gx, gy)] = (elevationNoise.GetNoise(worldX, worldY) + 1.0f) / 2.0f;
        }
    }

    // ---- Sort all cells by elevation, highest first ----
    std::vector<int> sorted(total);
    std::iota(sorted.begin(), sorted.end(), 0);
    std::sort(sorted.begin(), sorted.end(), [this](int a, int b) {
        return elevation[a] > elevation[b];
    });

    // ---- D4 flow accumulation (cardinal only) + store flow direction ----
    std::vector<int> flowDir(total, -1);  // which cell does water flow to?

    for (int i : sorted) {
        int gx = i % RIVER_MAP_SIZE;
        int gy = i / RIVER_MAP_SIZE;

        float steepest = 0.0f;
        int bestIdx = -1;

        for (int d = 0; d < 4; d++) {
            int nx = gx + dx4[d];
            int ny = gy + dy4[d];
            if (nx < 0 || nx >= RIVER_MAP_SIZE || ny < 0 || ny >= RIVER_MAP_SIZE) continue;

            float drop = elevation[i] - elevation[idx(nx, ny)];
            if (drop > steepest) {
                steepest = drop;
                bestIdx = idx(nx, ny);
            }
        }

        if (bestIdx >= 0) {
            flow[bestIdx] += flow[i];
            flowDir[i] = bestIdx;  // remember where this cell drains
        }
    }

    // ---- Trace each cell to find its ultimate sink ----
    std::vector<int> drainsSink(total, -1);

    for (int i = 0; i < total; i++) {
        if (drainsSink[i] >= 0) continue;

        std::vector<int> chain;
        int curr = i;
        while (curr >= 0 && drainsSink[curr] < 0) {
            chain.push_back(curr);
            curr = flowDir[curr];
        }

        int sink;
        if (curr < 0) {
            sink = chain.back();
        } else {
            sink = drainsSink[curr];
        }

        for (int c : chain) {
            drainsSink[c] = sink;
        }
    }

    // ---- Stats ----
    int maxFlow = 0;
    for (int i = 0; i < total; i++) {
        if (flow[i] > maxFlow) maxFlow = flow[i];
    }
    printf("RIVERS: Max flow = %d, Total cells = %d\n", maxFlow, total);
    fflush(stdout);

    // ---- Mark river tiles: high flow AND drains to water ----
    const int border = 5;
    for (int gy = border; gy < RIVER_MAP_SIZE - border; gy++) {
        for (int gx = border; gx < RIVER_MAP_SIZE - border; gx++) {
            int i = idx(gx, gy);
            if (flow[i] < RIVER_THRESHOLD) continue;
            if (elevation[i] < DEEP_WATER_THRESHOLD) continue;  // don't mark ocean as river

            int sink = drainsSink[i];
            if (sink >= 0 && elevation[sink] < WATER_THRESHOLD) {
                riverTile[i] = true;
            }
        }
    }

    // ---- Widen rivers based on flow (thin rivers allowed) ----
    std::vector<bool> widened = riverTile;
    for (int gy = border; gy < RIVER_MAP_SIZE - border; gy++) {
        for (int gx = border; gx < RIVER_MAP_SIZE - border; gx++) {
            int i = idx(gx, gy);
            if (!riverTile[i]) continue;

            int radius = 0;
            if (flow[i] >= RIVER_THRESHOLD * 20) radius = 3;
            else if (flow[i] >= RIVER_THRESHOLD * 8) radius = 2;
            else if (flow[i] >= RIVER_THRESHOLD * 3) radius = 1;

            for (int dy = -radius; dy <= radius; dy++) {
                for (int dx = -radius; dx <= radius; dx++) {
                    int nx = gx + dx;
                    int ny = gy + dy;
                    if (nx < 0 || nx >= RIVER_MAP_SIZE || ny < 0 || ny >= RIVER_MAP_SIZE) continue;
                    int ni = idx(nx, ny);
                    if (elevation[ni] > DEEP_WATER_THRESHOLD) {
                        widened[ni] = true;
                    }
                }
            }
        }
    }
    riverTile = widened;

    // ---- Remove river tiles not connected to a water body ----
    // Flood-fill from natural water (elevation < 0.35) through river tiles.
    // Any river tile not cardinally reachable from water gets removed.
    std::vector<bool> connected(total, false);
    std::queue<int> bfsQueue;

    // Seed: natural water tiles adjacent to river tiles
    for (int gy = 1; gy < RIVER_MAP_SIZE - 1; gy++) {
        for (int gx = 1; gx < RIVER_MAP_SIZE - 1; gx++) {
            int i = idx(gx, gy);
            if (elevation[i] >= WATER_THRESHOLD) continue;
            for (int d = 0; d < 4; d++) {
                int ni = idx(gx + dx4[d], gy + dy4[d]);
                if (riverTile[ni] && !connected[ni]) {
                    connected[ni] = true;
                    bfsQueue.push(ni);
                }
            }
        }
    }

    // Flood fill through river tiles cardinally
    while (!bfsQueue.empty()) {
        int ci = bfsQueue.front();
        bfsQueue.pop();
        int cgx = ci % RIVER_MAP_SIZE;
        int cgy = ci / RIVER_MAP_SIZE;
        for (int d = 0; d < 4; d++) {
            int nx = cgx + dx4[d];
            int ny = cgy + dy4[d];
            if (nx < 0 || nx >= RIVER_MAP_SIZE || ny < 0 || ny >= RIVER_MAP_SIZE) continue;
            int ni = idx(nx, ny);
            if (riverTile[ni] && !connected[ni]) {
                connected[ni] = true;
                bfsQueue.push(ni);
            }
        }
    }

    // Remove disconnected river tiles
    int removed = 0;
    for (int i = 0; i < total; i++) {
        if (riverTile[i] && !connected[i]) {
            riverTile[i] = false;
            removed++;
        }
    }
    printf("RIVERS: Removed %d disconnected river tiles\n", removed);

    // ---- Connect thin rivers cardinally ----
    // For any river tile with no cardinal river neighbor, add a river tile
    // in the cardinal direction closest to its flow direction (downhill).
    bool changed = true;
    int connectCount = 0;
    while (changed) {
        changed = false;
        for (int gy = 1; gy < RIVER_MAP_SIZE - 1; gy++) {
            for (int gx = 1; gx < RIVER_MAP_SIZE - 1; gx++) {
                int i = idx(gx, gy);
                if (!riverTile[i]) continue;

                // Check if already has a cardinal river neighbor
                bool hasCardinal = false;
                for (int d = 0; d < 4; d++) {
                    int ni = idx(gx + dx4[d], gy + dy4[d]);
                    if (riverTile[ni] || elevation[ni] < WATER_THRESHOLD) {
                        hasCardinal = true;
                        break;
                    }
                }
                if (hasCardinal) continue;

                // Find the lowest cardinal neighbor and make it a river tile
                float lowestElev = 999.0f;
                int bestNi = -1;
                for (int d = 0; d < 4; d++) {
                    int nx = gx + dx4[d];
                    int ny = gy + dy4[d];
                    int ni = idx(nx, ny);
                    if (elevation[ni] < lowestElev && elevation[ni] > DEEP_WATER_THRESHOLD) {
                        lowestElev = elevation[ni];
                        bestNi = ni;
                    }
                }
                if (bestNi >= 0) {
                    riverTile[bestNi] = true;
                    changed = true;
                    connectCount++;
                }
            }
        }
    }
    printf("RIVERS: Added %d tiles for cardinal connectivity\n", connectCount);
    fflush(stdout);

    // ---- Mark river banks (sand borders) ----
    for (int gy = 1; gy < RIVER_MAP_SIZE - 1; gy++) {
        for (int gx = 1; gx < RIVER_MAP_SIZE - 1; gx++) {
            int i = idx(gx, gy);
            if (riverTile[i]) continue;
            if (elevation[i] < SAND_THRESHOLD) continue;
            if (elevation[i] > 0.85f) continue;

            for (int d = 0; d < 8; d++) {
                int ni = idx(gx + dx8[d], gy + dy8[d]);
                if (riverTile[ni]) {
                    riverBank[i] = true;
                    break;
                }
            }
        }
    }

    // Free data no longer needed after generation
    elevation.clear();
    elevation.shrink_to_fit();
}
