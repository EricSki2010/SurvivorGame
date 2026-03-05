#include "world.h"
#include "game_data.h"
#include <cmath>
#include <vector>
#include <algorithm>

static void setupNoise(FastNoiseLite& noise, int seed, float frequency) {
    noise.SetNoiseType(FastNoiseLite::NoiseType_Perlin);
    noise.SetSeed(seed);
    noise.SetFrequency(frequency);
}

template<typename T>
static void waitForFutures(std::vector<T>& pending) {
    for (auto& p : pending) {
        if (p.future.valid()) p.future.wait();
    }
}

World::World() {
    setupNoise(elevationNoise,  WorldSeed,     0.02f);
    setupNoise(moistureNoise,   WorldSeed * 3, 0.015f);
    setupNoise(vegetationNoise, WorldSeed * 5, 0.05f);

    // River regions generate on demand (no upfront generation)
    // Generate the starting region immediately so the player spawns with rivers
    riverSystem.generateRegion(
        SpringRiverSystem::worldToRegion(0),
        SpringRiverSystem::worldToRegion(0),
        elevationNoise);

    // Initialize all textures as empty
    for (int i = 0; i < TILE_COUNT; i++) {
        tileTextures[i] = {0};
        tileVariants[i] = 0;
        tileAtlasCols[i] = 0;
        tileAnimSpeed[i] = 0.0f;
    }

    // Resize object texture arrays to match JSON data
    objectTextures.resize(gData().objectCount, Texture2D{0});
    objectTexturesLoaded.resize(gData().objectCount, false);
}

World::~World() {
    waitForFutures(pendingRivers);
    pendingRivers.clear();

    waitForFutures(pendingChunks);
    pendingChunks.clear();

    for (int i = 0; i < TILE_COUNT; i++) {
        if (tileVariants[i] > 0) {
            UnloadTexture(tileTextures[i]);
        }
    }
    for (int i = 0; i < (int)objectTextures.size(); i++) {
        if (objectTexturesLoaded[i]) {
            UnloadTexture(objectTextures[i]);
        }
    }
    for (int i = 0; i < WP_COUNT; i++) waterPieces[i].unload();
}

void World::loadTextures() {
    // Grass tile atlas (3 frames in 2x2 grid, 32x32 each)
    tileTextures[TILE_GRASS] = LoadTexture("assets/tiles/grassland/grass.png");
    tileVariants[TILE_GRASS] = 3;
    tileAtlasCols[TILE_GRASS] = tileTextures[TILE_GRASS].width / TILE_SIZE;
    tileAnimSpeed[TILE_GRASS] = 0.4f;  // seconds per frame

    // Water transition pieces (variants stacked vertically, auto-detected by height/pieceH)
    waterPieces[WP_STRAIGHT].load("assets/tiles/water/straightWater.png", 16, 16); // 16x96 = 6 variants
    waterPieces[WP_TURN].load("assets/tiles/water/waterTurn.png", 32, 32);         // 32x64 = 2 variants
    waterPieces[WP_CORNER].load("assets/tiles/water/waterCorner.png", 16, 16);     // 16x16 = 1 variant
    waterTransitionsLoaded = waterPieces[WP_STRAIGHT].loaded
                          || waterPieces[WP_TURN].loaded
                          || waterPieces[WP_CORNER].loaded;

    // Object textures — loaded from objects.json
    for (int i = 1; i < gData().objectCount; i++) {
        const auto& def = gData().objects[i];
        if (!def.texturePath.empty()) {
            objectTextures[i] = LoadTexture(def.texturePath.c_str());
            objectTexturesLoaded[i] = (objectTextures[i].id > 0);
        }
    }

    texturesLoaded = true;
}

// ---------------------------------------------------------------------------
// Async chunk generation
// ---------------------------------------------------------------------------

void World::integrateReadyChunks() {
    // Poll each pending future. If ready, move the Chunk into the map
    // and compute water transition styles (must happen on main thread
    // because it reads from the chunks map).
    auto it = pendingChunks.begin();
    while (it != pendingChunks.end()) {
        if (it->future.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
            Chunk chunk = it->future.get();
            int cx = it->cx;
            int cy = it->cy;
            chunks.emplace(it->key, std::move(chunk));

            // Compute water edge styles for the new chunk + neighbors
            computeWaterStyles(cx, cy);
            for (int dy = -1; dy <= 1; dy++)
                for (int dx = -1; dx <= 1; dx++)
                    if (dx != 0 || dy != 0)
                        computeWaterStyles(cx + dx, cy + dy);

            it = pendingChunks.erase(it);
        } else {
            ++it;
        }
    }
}

void World::waitForPendingChunks() {
    for (auto& p : pendingChunks) {
        if (p.future.valid()) p.future.wait();
    }
    integrateReadyChunks();
}

// ---------------------------------------------------------------------------
// Update
// ---------------------------------------------------------------------------

void World::update(Vector2 playerPos) {
    animTimer += GetFrameTime();
    // Wrap to prevent float precision loss over long sessions
    if (animTimer > 3600.0f) animTimer -= 3600.0f;

    // Figure out which chunk the player is in
    int playerChunkX = (int)floor(playerPos.x / (CHUNK_SIZE * TILE_SIZE));
    int playerChunkY = (int)floor(playerPos.y / (CHUNK_SIZE * TILE_SIZE));

    // Poll completed river regions and invalidate affected chunks
    integrateReadyRivers();

    // Dispatch async river generation for nearby regions
    dispatchRiverGeneration(playerPos);

    // Integrate any chunks that finished generating on worker threads
    integrateReadyChunks();

    // Request missing chunks — dispatch to worker threads via std::async
    for (int cy = playerChunkY - renderDistance; cy <= playerChunkY + renderDistance; cy++) {
        for (int cx = playerChunkX - renderDistance; cx <= playerChunkX + renderDistance; cx++) {
            int64_t key = chunkKey(cx, cy);
            if (chunks.find(key) != chunks.end()) continue;

            // Already being generated?
            bool isPending = false;
            for (auto& p : pendingChunks) {
                if (p.key == key) { isPending = true; break; }
            }
            if (isPending) continue;

            // Cap concurrent worker threads so we don't overwhelm the CPU
            if ((int)pendingChunks.size() >= MAX_PENDING_CHUNKS) continue;

            // Launch async chunk generation on a worker thread.
            // Chunk constructor does noise sampling + object placement —
            // all read-only on the noise generators, so thread-safe.
            PendingChunk pc;
            pc.key = key;
            pc.cx = cx;
            pc.cy = cy;
            pc.future = std::async(std::launch::async, [this, cx, cy]() {
                return Chunk(cx, cy, elevationNoise, moistureNoise,
                             vegetationNoise, &riverSystem, TILE_SIZE);
            });
            pendingChunks.push_back(std::move(pc));
        }
    }

    // Unload chunks that are too far away
    unloadDistantChunks(playerChunkX, playerChunkY);
}

Chunk* World::getChunk(int chunkX, int chunkY) {
    auto it = chunks.find(chunkKey(chunkX, chunkY));
    if (it != chunks.end()) {
        return &it->second;
    }
    return nullptr;
}

Tile* World::getTile(int worldX, int worldY) {
    int cx = (int)floor((float)worldX / CHUNK_SIZE);
    int cy = (int)floor((float)worldY / CHUNK_SIZE);

    Chunk* chunk = getChunk(cx, cy);
    if (!chunk) return nullptr;

    // Calculate local position within the chunk
    int localX = ((worldX % CHUNK_SIZE) + CHUNK_SIZE) % CHUNK_SIZE;
    int localY = ((worldY % CHUNK_SIZE) + CHUNK_SIZE) % CHUNK_SIZE;

    return &chunk->getTile(localX, localY);
}

int World::removeObject(int worldTileX, int worldTileY, bool dropLoot) {
    Tile* tile = getTile(worldTileX, worldTileY);
    if (!tile || tile->objectId == OBJECT_NONE) return OBJECT_NONE;

    int removedId = tile->objectId;
    tile->objectId = OBJECT_NONE;
    tile->offsetX = 0.0f;
    tile->offsetY = 0.0f;

    if (dropLoot) {
        // TODO: roll loot from gData().lootTables[removedId] and spawn drops
    }

    return removedId;
}

bool World::isWaterTile(int worldTileX, int worldTileY) {
    Tile* t = getTile(worldTileX, worldTileY);
    return t && (t->id == TILE_DEEP_WATER || t->id == TILE_SHALLOW_WATER);
}

bool World::isWaterAt(int worldTileX, int worldTileY) {
    Tile* t = getTile(worldTileX, worldTileY);
    if (t) return (t->id == TILE_DEEP_WATER || t->id == TILE_SHALLOW_WATER);
    // Fallback for unloaded chunks: check elevation noise
    float elev = sampleNoise(elevationNoise, (float)worldTileX, (float)worldTileY);
    return elev < WATER_THRESHOLD;
}

void World::computeWaterStyles(int cx, int cy) {
    Chunk* chunk = getChunk(cx, cy);
    if (!chunk) return;

    for (int ty = 0; ty < CHUNK_SIZE; ty++) {
        for (int tx = 0; tx < CHUNK_SIZE; tx++) {
            Tile& tile = chunk->tiles[ty][tx];
            tile.waterDecoCount = 0;

            if (tile.id != TILE_SHALLOW_WATER && tile.id != TILE_DEEP_WATER)
                continue;

            int wx = cx * CHUNK_SIZE + tx;
            int wy = cy * CHUNK_SIZE + ty;

            // Cardinal neighbors: is there land?
            bool landN = !isWaterAt(wx, wy - 1);
            bool landS = !isWaterAt(wx, wy + 1);
            bool landE = !isWaterAt(wx + 1, wy);
            bool landW = !isWaterAt(wx - 1, wy);

            // Diagonal neighbors
            bool landNW = !isWaterAt(wx - 1, wy - 1);
            bool landNE = !isWaterAt(wx + 1, wy - 1);
            bool landSW = !isWaterAt(wx - 1, wy + 1);
            bool landSE = !isWaterAt(wx + 1, wy + 1);

            // 2x2 slot grid — each slot is a 16x16 quadrant of the 32x32 tile:
            //   [0]=NW(0,0)  [1]=NE(16,0)
            //   [2]=SW(0,16) [3]=SE(16,16)
            bool filled[4] = {};
            int count = 0;

            // Deterministic seed for random variant selection per tile
            int seed = ((wx * 7 + wy * 13) & 0x7FFFFFFF);

            // Helper: add a decoration piece with a random variant from its sprite sheet
            auto addDeco = [&](int pieceId, float rot, float ox, float oy, int seedOff) {
                if (count >= 4) return;
                int vars = waterPieces[pieceId].variants;
                tile.waterDecos[count].pieceId = pieceId;
                tile.waterDecos[count].variant = vars > 0 ? (seed + seedOff) % vars : 0;
                tile.waterDecos[count].rotation = rot;
                tile.waterDecos[count].offX = ox;
                tile.waterDecos[count].offY = oy;
                count++;
                tile.waterDecoCount = count;
            };

            // --- Step 1: Turns (adjacent cardinal land pairs) ---
            bool nInTurn = false, sInTurn = false, eInTurn = false, wInTurn = false;

            if (landN && landW) {
                addDeco(WP_TURN, 0, 0, 0, 0);
                filled[0] = filled[1] = filled[2] = true;
                nInTurn = wInTurn = true;
            }
            if (landN && landE) {
                addDeco(WP_TURN, 90, 0, 0, 1);
                filled[0] = filled[1] = filled[3] = true;
                nInTurn = eInTurn = true;
            }
            if (landS && landE) {
                addDeco(WP_TURN, 180, 0, 0, 2);
                filled[1] = filled[2] = filled[3] = true;
                sInTurn = eInTurn = true;
            }
            if (landS && landW) {
                addDeco(WP_TURN, 270, 0, 0, 3);
                filled[0] = filled[2] = filled[3] = true;
                sInTurn = wInTurn = true;
            }

            // --- Step 2: Flat/edge pieces for remaining cardinal land ---
            bool edgeN = landN && !nInTurn;
            bool edgeS = landS && !sInTurn;
            bool edgeE = landE && !eInTurn;
            bool edgeW = landW && !wInTurn;

            if (edgeW) {
                if (!filled[0]) { addDeco(WP_STRAIGHT, 0,   0,  0, 4); filled[0] = true; }
                if (!filled[2]) { addDeco(WP_STRAIGHT, 0,   0, 16, 5); filled[2] = true; }
            }
            if (edgeE) {
                if (!filled[1]) { addDeco(WP_STRAIGHT, 180, 16,  0, 6); filled[1] = true; }
                if (!filled[3]) { addDeco(WP_STRAIGHT, 180, 16, 16, 7); filled[3] = true; }
            }
            if (edgeN) {
                if (!filled[0]) { addDeco(WP_STRAIGHT, 90,   0, 0, 8); filled[0] = true; }
                if (!filled[1]) { addDeco(WP_STRAIGHT, 90,  16, 0, 9); filled[1] = true; }
            }
            if (edgeS) {
                if (!filled[2]) { addDeco(WP_STRAIGHT, 270,  0, 16, 10); filled[2] = true; }
                if (!filled[3]) { addDeco(WP_STRAIGHT, 270, 16, 16, 11); filled[3] = true; }
            }

            // --- Step 3: Inner corners for unfilled diagonal slots ---
            if (!filled[0] && landNW && !landN && !landW)
                addDeco(WP_CORNER, 0,    0,  0, 12);
            if (!filled[1] && landNE && !landN && !landE)
                addDeco(WP_CORNER, 90,  16,  0, 13);
            if (!filled[3] && landSE && !landS && !landE)
                addDeco(WP_CORNER, 180, 16, 16, 14);
            if (!filled[2] && landSW && !landS && !landW)
                addDeco(WP_CORNER, 270,  0, 16, 15);
        }
    }
}

FlowDir World::getFlowDir(int worldTileX, int worldTileY) {
    float elev = sampleNoise(elevationNoise, (float)worldTileX, (float)worldTileY);
    float steepest = 0.0f;
    FlowDir best = FLOW_NONE;

    for (int d = 0; d < 4; d++) {
        float nElev = sampleNoise(elevationNoise,
            (float)(worldTileX + FLOW_DX[d]),
            (float)(worldTileY + FLOW_DY[d]));
        float drop = elev - nElev;
        if (drop > steepest) {
            steepest = drop;
            best = (FlowDir)d;
        }
    }
    return best;
}

void World::draw(const Camera2D& camera, int screenWidth, int screenHeight,
                 float playerSortY, void (*drawPlayerFn)()) {
    int chunkPixelSize = CHUNK_SIZE * TILE_SIZE;

    // Calculate visible bounds in world space for culling
    Vector2 topLeft = GetScreenToWorld2D({0, 0}, camera);
    Vector2 bottomRight = GetScreenToWorld2D({(float)screenWidth, (float)screenHeight}, camera);
    int minCX = (int)floor(topLeft.x / chunkPixelSize) - 1;
    int maxCX = (int)floor(bottomRight.x / chunkPixelSize) + 1;
    int minCY = (int)floor(topLeft.y / chunkPixelSize) - 1;
    int maxCY = (int)floor(bottomRight.y / chunkPixelSize) + 1;

    // Pass 1: Draw all ground tiles and water transitions
    for (auto& [key, chunk] : chunks) {
        if (chunk.chunkX < minCX || chunk.chunkX > maxCX ||
            chunk.chunkY < minCY || chunk.chunkY > maxCY) continue;

        float chunkWorldX = chunk.chunkX * chunkPixelSize;
        float chunkWorldY = chunk.chunkY * chunkPixelSize;

        int chunkTileX = chunk.chunkX * CHUNK_SIZE;
        int chunkTileY = chunk.chunkY * CHUNK_SIZE;
        if (!riverSystem.isRegionGenerated(chunkTileX, chunkTileY)) {
            DrawRectangle((int)chunkWorldX, (int)chunkWorldY, chunkPixelSize, chunkPixelSize, {10, 10, 15, 255});
            continue;
        }

        for (int ty = 0; ty < CHUNK_SIZE; ty++) {
            for (int tx = 0; tx < CHUNK_SIZE; tx++) {
                float tileX = chunkWorldX + tx * TILE_SIZE;
                float tileY = chunkWorldY + ty * TILE_SIZE;
                int tileId = chunk.tiles[ty][tx].id;
                int worldTileX = chunk.chunkX * CHUNK_SIZE + tx;
                int worldTileY = chunk.chunkY * CHUNK_SIZE + ty;

                bool isWater = (tileId == TILE_SHALLOW_WATER || tileId == TILE_DEEP_WATER);

                bool useTexture = (tileVariants[tileId] > 0 && !isWater);
                if (useTexture) {
                    int variant;
                    if (tileAnimSpeed[tileId] > 0.0f) {
                        variant = (int)(animTimer / tileAnimSpeed[tileId]) % tileVariants[tileId];
                    } else {
                        variant = ((worldTileX * 7 + worldTileY * 13) & 0x7FFFFFFF) % tileVariants[tileId];
                    }
                    int cols = tileAtlasCols[tileId];
                    int atlasCol = variant % cols;
                    int atlasRow = variant / cols;
                    Rectangle src = {(float)(atlasCol * TILE_SIZE), (float)(atlasRow * TILE_SIZE), (float)TILE_SIZE, (float)TILE_SIZE};
                    DrawTextureRec(tileTextures[tileId], src, {tileX, tileY}, WHITE);
                } else {
                    DrawRectangle((int)tileX, (int)tileY, TILE_SIZE, TILE_SIZE, TILE_COLORS[tileId]);
                }

                if (isWater && waterTransitionsLoaded) {
                    const Tile& t = chunk.tiles[ty][tx];
                    for (int d = 0; d < t.waterDecoCount; d++) {
                        const WaterDeco& deco = t.waterDecos[d];
                        if (deco.pieceId < 0 || deco.pieceId >= WP_COUNT) continue;
                        const WaterPiece& piece = waterPieces[deco.pieceId];
                        if (!piece.loaded) continue;
                        if (deco.rotation == 0.0f)
                            piece.draw(tileX, tileY, deco.offX, deco.offY, deco.variant);
                        else
                            piece.drawRotated(tileX, tileY, deco.offX, deco.offY, deco.rotation, deco.variant);
                    }
                }
            }
        }

        if (drawChunkBorders)
            DrawRectangleLines(chunkWorldX, chunkWorldY, chunkPixelSize, chunkPixelSize, {0, 0, 0, 60});
    }

    // Pass 2: Collect all visible objects, sort by Y, then draw
    struct ObjDraw { int objId; float drawX; float drawY; float sortY; };
    std::vector<ObjDraw> objsToDraw;

    for (auto& [key, chunk] : chunks) {
        if (chunk.chunkX < minCX || chunk.chunkX > maxCX ||
            chunk.chunkY < minCY || chunk.chunkY > maxCY) continue;

        int chunkTileX = chunk.chunkX * CHUNK_SIZE;
        int chunkTileY = chunk.chunkY * CHUNK_SIZE;
        if (!riverSystem.isRegionGenerated(chunkTileX, chunkTileY)) continue;

        float chunkWorldX = chunk.chunkX * chunkPixelSize;
        float chunkWorldY = chunk.chunkY * chunkPixelSize;

        for (int ty = 0; ty < CHUNK_SIZE; ty++) {
            for (int tx = 0; tx < CHUNK_SIZE; tx++) {
                int objId = chunk.tiles[ty][tx].objectId;
                if (objId == OBJECT_NONE || !objectTexturesLoaded[objId]) continue;

                float tileX = chunkWorldX + tx * TILE_SIZE;
                float tileY = chunkWorldY + ty * TILE_SIZE;
                Texture2D& objTex = objectTextures[objId];
                float offX = chunk.tiles[ty][tx].offsetX;
                float offY = chunk.tiles[ty][tx].offsetY;
                float objX = tileX + (TILE_SIZE - objTex.width) / 2.0f + offX;
                float objY;
                if (objId == OBJECT_TREE) {
                    Vector2 colSize = gData().getCollisionSize(objId);
                    float boxBottom = tileY + (TILE_SIZE + colSize.y) / 2.0f + offY;
                    objY = boxBottom - objTex.height;
                } else {
                    objY = tileY + (TILE_SIZE - objTex.height) / 2.0f + offY;
                }
                // Sort by tile bottom edge so lower objects draw on top
                objsToDraw.push_back({objId, objX, objY, tileY + TILE_SIZE + offY});
            }
        }
    }

    // Insert player into the sort list (objId = -1 marks the player)
    if (drawPlayerFn) {
        objsToDraw.push_back({-1, 0, 0, playerSortY});
    }

    std::sort(objsToDraw.begin(), objsToDraw.end(),
        [](const ObjDraw& a, const ObjDraw& b) { return a.sortY < b.sortY; });

    for (auto& obj : objsToDraw) {
        if (obj.objId == -1) {
            drawPlayerFn();
        } else {
            DrawTexture(objectTextures[obj.objId], (int)obj.drawX, (int)obj.drawY, WHITE);
        }
    }
}

int World::getLoadedChunkCount() {
    return (int)chunks.size();
}

void World::loadChunk(int cx, int cy) {
    chunks.emplace(chunkKey(cx, cy), Chunk(cx, cy, elevationNoise, moistureNoise, vegetationNoise, &riverSystem, TILE_SIZE));
    computeWaterStyles(cx, cy);
    // Recompute neighboring chunks — their border tiles may have used the noise
    // fallback before this chunk existed, giving wrong results near rivers
    for (int dy = -1; dy <= 1; dy++)
        for (int dx = -1; dx <= 1; dx++)
            if (dx != 0 || dy != 0)
                computeWaterStyles(cx + dx, cy + dy);
}

void World::unloadDistantChunks(int playerChunkX, int playerChunkY) {
    auto it = chunks.begin();
    while (it != chunks.end()) {
        Chunk& chunk = it->second;
        int dx = abs(chunk.chunkX - playerChunkX);
        int dy = abs(chunk.chunkY - playerChunkY);

        if (dx > renderDistance || dy > renderDistance) {
            it = chunks.erase(it);
        } else {
            ++it;
        }
    }

    // Also discard pending chunks that are now out of range.
    // (The future's destructor blocks until the task completes, which is fine
    // since chunk generation is fast — just noise math for 16x16 tiles.)
    auto pit = pendingChunks.begin();
    while (pit != pendingChunks.end()) {
        int pdx = abs(pit->cx - playerChunkX);
        int pdy = abs(pit->cy - playerChunkY);
        if (pdx > renderDistance || pdy > renderDistance) {
            if (pit->future.valid()) pit->future.wait();
            pit = pendingChunks.erase(pit);
        } else {
            ++pit;
        }
    }
}

void World::integrateReadyRivers() {
    auto it = pendingRivers.begin();
    while (it != pendingRivers.end()) {
        if (it->future.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
            it->future.get();
            invalidateChunksInRegion(it->regionX, it->regionY);
            it = pendingRivers.erase(it);
        } else {
            ++it;
        }
    }
}

void World::dispatchRiverGeneration(Vector2 playerPos) {
    int playerTileX = (int)floor(playerPos.x / TILE_SIZE);
    int playerTileY = (int)floor(playerPos.y / TILE_SIZE);
    int playerRegionX = SpringRiverSystem::worldToRegion(playerTileX);
    int playerRegionY = SpringRiverSystem::worldToRegion(playerTileY);

    for (int ry = playerRegionY - 1; ry <= playerRegionY + 1; ry++) {
        for (int rx = playerRegionX - 1; rx <= playerRegionX + 1; rx++) {
            int regionTileX = rx * RIVER_REGION_SIZE;
            int regionTileY = ry * RIVER_REGION_SIZE;
            if (riverSystem.isRegionGenerated(regionTileX, regionTileY)) continue;

            // Already pending?
            bool isPending = false;
            for (auto& p : pendingRivers) {
                if (p.regionX == rx && p.regionY == ry) { isPending = true; break; }
            }
            if (isPending) continue;

            PendingRiver pr;
            pr.regionX = rx;
            pr.regionY = ry;
            pr.future = std::async(std::launch::async, [this, rx, ry]() {
                riverSystem.generateRegion(rx, ry, elevationNoise);
            });
            pendingRivers.push_back(std::move(pr));
        }
    }
}

void World::invalidateChunksInRegion(int regionX, int regionY) {
    // Remove all loaded chunks that fall within this river region
    // so they get regenerated with river overlays on next update
    int minTileX = regionX * RIVER_REGION_SIZE;
    int minTileY = regionY * RIVER_REGION_SIZE;
    int maxTileX = minTileX + RIVER_REGION_SIZE;
    int maxTileY = minTileY + RIVER_REGION_SIZE;

    // Convert to chunk coords
    int minCX = (int)floor((float)minTileX / CHUNK_SIZE);
    int minCY = (int)floor((float)minTileY / CHUNK_SIZE);
    int maxCX = (int)floor((float)(maxTileX - 1) / CHUNK_SIZE);
    int maxCY = (int)floor((float)(maxTileY - 1) / CHUNK_SIZE);

    auto it = chunks.begin();
    while (it != chunks.end()) {
        Chunk& chunk = it->second;
        if (chunk.chunkX >= minCX && chunk.chunkX <= maxCX &&
            chunk.chunkY >= minCY && chunk.chunkY <= maxCY) {
            it = chunks.erase(it);
        } else {
            ++it;
        }
    }

    // Also discard in-flight chunks that overlap this region —
    // they were built without river data and need to be re-requested.
    auto pit = pendingChunks.begin();
    while (pit != pendingChunks.end()) {
        if (pit->cx >= minCX && pit->cx <= maxCX &&
            pit->cy >= minCY && pit->cy <= maxCY) {
            if (pit->future.valid()) pit->future.wait();
            pit = pendingChunks.erase(pit);
        } else {
            ++pit;
        }
    }
}
