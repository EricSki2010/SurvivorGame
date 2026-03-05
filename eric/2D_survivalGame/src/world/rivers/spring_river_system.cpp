#include "spring_river_system.h"
#include "world.h"
#include "tile.h"
#include <algorithm>
#include <cstdio>
#include <queue>
#include <cmath>

int SpringRiverSystem::worldToRegion(int worldTile) {
    // Floor division that works for negative numbers
    if (worldTile >= 0) return worldTile / RIVER_REGION_SIZE;
    return (worldTile + 1) / RIVER_REGION_SIZE - 1;
}

bool SpringRiverSystem::isRegionGenerated(int worldTileX, int worldTileY) const {
    std::shared_lock lock(regionMutex);
    int rx = worldToRegion(worldTileX);
    int ry = worldToRegion(worldTileY);
    return regions.count(regionKey(rx, ry)) > 0;
}

const SpringRiverSystem::Region* SpringRiverSystem::findRegionTile(
        int worldTileX, int worldTileY, int& index) const {
    int rx = worldToRegion(worldTileX);
    int ry = worldToRegion(worldTileY);
    auto it = regions.find(regionKey(rx, ry));
    if (it == regions.end()) return nullptr;
    int lx = worldTileX - rx * RIVER_REGION_SIZE;
    int ly = worldTileY - ry * RIVER_REGION_SIZE;
    index = ly * RIVER_REGION_SIZE + lx;
    return &it->second;
}

bool SpringRiverSystem::isRiver(int worldTileX, int worldTileY) const {
    std::shared_lock lock(regionMutex);
    int i;
    const Region* r = findRegionTile(worldTileX, worldTileY, i);
    return r ? r->river[i] : false;
}

bool SpringRiverSystem::isBank(int worldTileX, int worldTileY) const {
    std::shared_lock lock(regionMutex);
    int i;
    const Region* r = findRegionTile(worldTileX, worldTileY, i);
    return r ? r->bank[i] : false;
}

int SpringRiverSystem::getFlow(int worldTileX, int worldTileY) const {
    std::shared_lock lock(regionMutex);
    int i;
    const Region* r = findRegionTile(worldTileX, worldTileY, i);
    return r ? r->flow[i] : 0;
}

void SpringRiverSystem::generateRegion(int regionX, int regionY, FastNoiseLite& elevationNoise) {
    // Already generated? (check under shared lock)
    {
        std::shared_lock lock(regionMutex);
        if (regions.count(regionKey(regionX, regionY))) return;
    }
    // Heavy computation below runs without any lock held

    // The region covers world tiles [regionX*512, regionX*512+511] x [regionY*512, regionY*512+511]
    // We sample a padded area for river tracing: 256 tiles of padding on each side
    int regionWorldX = regionX * RIVER_REGION_SIZE;  // world tile origin of region
    int regionWorldY = regionY * RIVER_REGION_SIZE;
    int sampleOriginX = regionWorldX - RIVER_REGION_PADDING; // world tile origin of sample area
    int sampleOriginY = regionWorldY - RIVER_REGION_PADDING;

    int sampleTotal = RIVER_SAMPLE_SIZE * RIVER_SAMPLE_SIZE;
    std::vector<float> elevation(sampleTotal);

    // Local indexing for the sample grid
    auto sampleIndex = [](int sx, int sy) { return sy * RIVER_SAMPLE_SIZE + sx; };

    const float noiseOffset = NOISE_OFFSET;
    const int dx4[] = {-1, 1, 0, 0};
    const int dy4[] = {0, 0, -1, 1};
    const int dx8[] = {-1, 0, 1, -1, 1, -1, 0, 1};
    const int dy8[] = {-1, -1, -1, 0, 0, 1, 1, 1};

    // ---- Sample elevation for padded area ----
    for (int sy = 0; sy < RIVER_SAMPLE_SIZE; sy++) {
        for (int sx = 0; sx < RIVER_SAMPLE_SIZE; sx++) {
            float worldX = (float)(sampleOriginX + sx) + noiseOffset;
            float worldY = (float)(sampleOriginY + sy) + noiseOffset;
            elevation[sampleIndex(sx, sy)] = (elevationNoise.GetNoise(worldX, worldY) + 1.0f) / 2.0f;
        }
    }

    // ---- Spring placement (within region only, not padding) ----
    const float SPRING_THRESHOLD = 0.55f;
    const float MIN_SPRING_ELEVATION = 0.55f;
    const int LOCAL_MAX_RADIUS = 3;

    FastNoiseLite springNoise;
    springNoise.SetNoiseType(FastNoiseLite::NoiseType_Perlin);
    springNoise.SetSeed(WorldSeed * 7919);
    springNoise.SetFrequency(0.05f);

    struct Spring { int sx, sy; };  // sample-grid coords
    std::vector<Spring> springs;

    // Springs are only placed within the region (padding offset to sample-grid coords)
    int innerStart = RIVER_REGION_PADDING;  // = 256
    int innerEnd = RIVER_REGION_PADDING + RIVER_REGION_SIZE;  // = 768

    for (int sy = innerStart; sy < innerEnd; sy++) {
        for (int sx = innerStart; sx < innerEnd; sx++) {
            float worldX = (float)(sampleOriginX + sx) + noiseOffset;
            float worldY = (float)(sampleOriginY + sy) + noiseOffset;
            float springVal = (springNoise.GetNoise(worldX, worldY) + 1.0f) / 2.0f;

            if (springVal < SPRING_THRESHOLD) continue;
            if (elevation[sampleIndex(sx, sy)] < MIN_SPRING_ELEVATION) continue;

            // Local maximum check
            bool isLocalMax = true;
            for (int dy = -LOCAL_MAX_RADIUS; dy <= LOCAL_MAX_RADIUS && isLocalMax; dy++) {
                for (int dx = -LOCAL_MAX_RADIUS; dx <= LOCAL_MAX_RADIUS && isLocalMax; dx++) {
                    if (dx == 0 && dy == 0) continue;
                    int nx = sx + dx;
                    int ny = sy + dy;
                    if (nx < 0 || nx >= RIVER_SAMPLE_SIZE || ny < 0 || ny >= RIVER_SAMPLE_SIZE) continue;
                    float nwx = (float)(sampleOriginX + nx) + noiseOffset;
                    float nwy = (float)(sampleOriginY + ny) + noiseOffset;
                    float neighborVal = (springNoise.GetNoise(nwx, nwy) + 1.0f) / 2.0f;
                    if (neighborVal > springVal) isLocalMax = false;
                }
            }
            if (!isLocalMax) continue;

            springs.push_back({sx, sy});
        }
    }

    // ---- Trace rivers (within full sample area) ----
    // Track river tiles in sample-grid space
    std::vector<bool> sampleRiver(sampleTotal, false);
    std::vector<int> sampleFlow(sampleTotal, 0);

    const int MAX_RIVER_LENGTH = 2000;
    const int border = 2;
    std::vector<int> bfsParent(sampleTotal, -1);

    int convergentCount = 0;
    int discardedCount = 0;

    for (auto& spring : springs) {
        int cx = spring.sx;
        int cy = spring.sy;
        std::vector<int> path;
        bool reachedWater = false;

        while ((int)path.size() < MAX_RIVER_LENGTH) {
            int ci = sampleIndex(cx, cy);

            if (elevation[ci] < WATER_THRESHOLD) {
                reachedWater = true;
                break;
            }
            if (sampleRiver[ci]) {
                reachedWater = true;  // connected to a validated river
                break;
            }

            path.push_back(ci);

            // Find lowest D4 neighbor
            float lowestElev = elevation[ci];
            int bestNx = -1, bestNy = -1;

            for (int d = 0; d < 4; d++) {
                int nx = cx + dx4[d];
                int ny = cy + dy4[d];
                if (nx < border || nx >= RIVER_SAMPLE_SIZE - border ||
                    ny < border || ny >= RIVER_SAMPLE_SIZE - border) continue;
                float ne = elevation[sampleIndex(nx, ny)];
                if (ne < lowestElev) {
                    lowestElev = ne;
                    bestNx = nx;
                    bestNy = ny;
                }
            }

            // Stuck — BFS to find escape
            if (bestNx < 0) {
                float stuckElev = elevation[ci];
                std::queue<int> bfs;
                std::vector<int> visited;
                bfsParent[ci] = ci;
                bfs.push(ci);
                visited.push_back(ci);

                int escapeIdx = -1;
                const int BFS_MAX = 5000;

                while (!bfs.empty() && (int)visited.size() < BFS_MAX) {
                    int bi = bfs.front();
                    bfs.pop();
                    int bx = bi % RIVER_SAMPLE_SIZE;
                    int by = bi / RIVER_SAMPLE_SIZE;

                    for (int d = 0; d < 4; d++) {
                        int nx = bx + dx4[d];
                        int ny = by + dy4[d];
                        if (nx < border || nx >= RIVER_SAMPLE_SIZE - border ||
                            ny < border || ny >= RIVER_SAMPLE_SIZE - border) continue;
                        int ni = sampleIndex(nx, ny);
                        if (bfsParent[ni] >= 0) continue;

                        bfsParent[ni] = bi;
                        visited.push_back(ni);

                        if (elevation[ni] < stuckElev || elevation[ni] < WATER_THRESHOLD) {
                            escapeIdx = ni;
                            break;
                        }
                        bfs.push(ni);
                    }
                    if (escapeIdx >= 0) break;
                }

                if (escapeIdx >= 0) {
                    std::vector<int> escape;
                    int trace = escapeIdx;
                    while (trace != ci) {
                        escape.push_back(trace);
                        trace = bfsParent[trace];
                    }
                    std::reverse(escape.begin(), escape.end());

                    for (int ei : escape) {
                        if ((int)path.size() >= MAX_RIVER_LENGTH) break;
                        path.push_back(ei);
                    }

                    cx = escapeIdx % RIVER_SAMPLE_SIZE;
                    cy = escapeIdx / RIVER_SAMPLE_SIZE;
                } else {
                    break;  // truly stuck, river didn't reach water
                }

                for (int vi : visited) bfsParent[vi] = -1;
            } else {
                cx = bestNx;
                cy = bestNy;
            }
        }

        // Only keep rivers that reached water AND pass through land (elevation >= 0.35)
        bool touchesLand = false;
        for (int pi : path) {
            if (elevation[pi] >= 0.35f) { touchesLand = true; break; }
        }

        if (reachedWater && touchesLand && (int)path.size() >= 30) {
            convergentCount++;
            for (int i = 0; i < (int)path.size(); i++) {
                int pi = path[i];
                sampleRiver[pi] = true;
                sampleFlow[pi] = std::max(sampleFlow[pi], i + 1);
            }
        } else {
            discardedCount++;
        }
    }

    // ---- Widen rivers ----
    std::vector<bool> widened = sampleRiver;
    for (int sy = border; sy < RIVER_SAMPLE_SIZE - border; sy++) {
        for (int sx = border; sx < RIVER_SAMPLE_SIZE - border; sx++) {
            int i = sampleIndex(sx, sy);
            if (!sampleRiver[i]) continue;

            int dist = sampleFlow[i];
            int radius = 0;
            if (dist >= 60) radius = 3;
            else if (dist >= 30) radius = 2;
            else if (dist >= 10) radius = 1;

            for (int dy = -radius; dy <= radius; dy++) {
                for (int dx = -radius; dx <= radius; dx++) {
                    int nx = sx + dx;
                    int ny = sy + dy;
                    if (nx < 0 || nx >= RIVER_SAMPLE_SIZE || ny < 0 || ny >= RIVER_SAMPLE_SIZE) continue;
                    int ni = sampleIndex(nx, ny);
                    if (elevation[ni] > DEEP_WATER_THRESHOLD) {
                        widened[ni] = true;
                    }
                }
            }
        }
    }
    sampleRiver = widened;

    // ---- Mark banks in sample space ----
    std::vector<bool> sampleBank(sampleTotal, false);
    for (int sy = 1; sy < RIVER_SAMPLE_SIZE - 1; sy++) {
        for (int sx = 1; sx < RIVER_SAMPLE_SIZE - 1; sx++) {
            int i = sampleIndex(sx, sy);
            if (sampleRiver[i]) continue;
            if (elevation[i] < SAND_THRESHOLD) continue;
            if (elevation[i] > 0.85f) continue;

            for (int d = 0; d < 8; d++) {
                int ni = sampleIndex(sx + dx8[d], sy + dy8[d]);
                if (sampleRiver[ni]) {
                    sampleBank[i] = true;
                    break;
                }
            }
        }
    }

    // ---- Copy inner region results into stored Region ----
    Region region;
    int regionTotal = RIVER_REGION_SIZE * RIVER_REGION_SIZE;
    region.river.resize(regionTotal, false);
    region.bank.resize(regionTotal, false);
    region.flow.resize(regionTotal, 0);

    for (int ly = 0; ly < RIVER_REGION_SIZE; ly++) {
        for (int lx = 0; lx < RIVER_REGION_SIZE; lx++) {
            int sx = lx + RIVER_REGION_PADDING;
            int sy = ly + RIVER_REGION_PADDING;
            int si = sampleIndex(sx, sy);
            int ri = ly * RIVER_REGION_SIZE + lx;
            region.river[ri] = sampleRiver[si];
            region.bank[ri] = sampleBank[si];
            region.flow[ri] = sampleFlow[si];
        }
    }

    // ---- Remove river blobs that touch the region edge ----
    // These are rivers that got cut off at the boundary and look broken.
    auto regionIndex = [](int x, int y) { return y * RIVER_REGION_SIZE + x; };
    std::vector<bool> visited(regionTotal, false);

    for (int ly = 0; ly < RIVER_REGION_SIZE; ly++) {
        for (int lx = 0; lx < RIVER_REGION_SIZE; lx++) {
            int ri = regionIndex(lx, ly);
            if (!region.river[ri] || visited[ri]) continue;

            // BFS to find the connected blob
            std::queue<int> q;
            std::vector<int> blob;
            bool touchesEdge = false;

            q.push(ri);
            visited[ri] = true;

            while (!q.empty()) {
                int ci = q.front(); q.pop();
                blob.push_back(ci);

                int cx = ci % RIVER_REGION_SIZE;
                int cy = ci / RIVER_REGION_SIZE;

                if (cx == 0 || cx == RIVER_REGION_SIZE - 1 ||
                    cy == 0 || cy == RIVER_REGION_SIZE - 1) {
                    touchesEdge = true;
                }

                for (int d = 0; d < 4; d++) {
                    int nx = cx + dx4[d];
                    int ny = cy + dy4[d];
                    if (nx < 0 || nx >= RIVER_REGION_SIZE || ny < 0 || ny >= RIVER_REGION_SIZE) continue;
                    int ni = regionIndex(nx, ny);
                    if (!region.river[ni] || visited[ni]) continue;
                    visited[ni] = true;
                    q.push(ni);
                }
            }

            if (touchesEdge) {
                for (int bi : blob) {
                    region.river[bi] = false;
                    region.bank[bi] = false;
                    region.flow[bi] = 0;
                }
                // Also clear banks adjacent to removed river tiles
                for (int bi : blob) {
                    int bx = bi % RIVER_REGION_SIZE;
                    int by = bi / RIVER_REGION_SIZE;
                    for (int d = 0; d < 8; d++) {
                        int nx = bx + dx8[d];
                        int ny = by + dy8[d];
                        if (nx < 0 || nx >= RIVER_REGION_SIZE || ny < 0 || ny >= RIVER_REGION_SIZE) continue;
                        int ni = regionIndex(nx, ny);
                        if (region.bank[ni]) region.bank[ni] = false;
                    }
                }
            }
        }
    }

    {
        std::unique_lock lock(regionMutex);
        regions[regionKey(regionX, regionY)] = std::move(region);
    }
    printf("RIVERS: Region (%d, %d) — %d springs, %d rivers kept, %d discarded\n",
           regionX, regionY, (int)springs.size(), convergentCount, discardedCount);
    fflush(stdout);
}
