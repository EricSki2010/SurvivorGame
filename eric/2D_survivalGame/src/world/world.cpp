#include "world.h"
#include <cmath>
#include <vector>
#include <algorithm>

World::World() {
    // Elevation layer
    elevationNoise.SetNoiseType(FastNoiseLite::NoiseType_Perlin);
    elevationNoise.SetSeed(WorldSeed);
    elevationNoise.SetFrequency(0.02f);

    // Moisture layer (different seed so it's independent)
    moistureNoise.SetNoiseType(FastNoiseLite::NoiseType_Perlin);
    moistureNoise.SetSeed(WorldSeed * 3);
    moistureNoise.SetFrequency(0.015f);

    // Vegetation layer (different seed so it's independent)
    vegetationNoise.SetNoiseType(FastNoiseLite::NoiseType_Perlin);
    vegetationNoise.SetSeed(WorldSeed * 5);
    vegetationNoise.SetFrequency(0.05f);  // higher frequency = smaller patches of vegetation

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
}

World::~World() {
    for (int i = 0; i < TILE_COUNT; i++) {
        if (tileVariants[i] > 0) {
            UnloadTexture(tileTextures[i]);
        }
    }
    for (int i = 0; i < OBJECT_COUNT; i++) {
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

    // Water transition pieces (each file has variants stacked vertically)
    waterPieces[WP_EDGE].load("assets/tiles/water/water_edge_left.png", 32, 32);
    waterPieces[WP_CORNER_NE].load("assets/tiles/water/water corner ne.png", 32, 32);
    waterPieces[WP_CORNER_NW].load("assets/tiles/water/water corner nw.png", 32, 32);
    waterPieces[WP_CORNER_SE].load("assets/tiles/water/water corner se.png", 32, 32);
    waterPieces[WP_CORNER_SW].load("assets/tiles/water/water corner sw.png", 32, 32);
    waterPieces[WP_EDGE_WE].load("assets/tiles/water/water_edge_we.png", 32, 32);
    waterPieces[WP_INNER_CORNER].load("assets/tiles/water/water inner corner.png", 32, 32);
    waterTransitionsLoaded = waterPieces[WP_EDGE].loaded;

    // Object textures
    objectTextures[OBJECT_ROCK] = LoadTexture("assets/objects/rocks/rock.png");
    objectTexturesLoaded[OBJECT_ROCK] = true;

    objectTextures[OBJECT_TREE] = LoadTexture("assets/objects/trees/Pine_Tree.png");
    objectTexturesLoaded[OBJECT_TREE] = true;

    texturesLoaded = true;
}

void World::update(Vector2 playerPos) {
    animTimer += GetFrameTime();
    // Wrap to prevent float precision loss over long sessions
    if (animTimer > 3600.0f) animTimer -= 3600.0f;

    // Figure out which chunk the player is in
    int playerChunkX = (int)floor(playerPos.x / (CHUNK_SIZE * TILE_SIZE));
    int playerChunkY = (int)floor(playerPos.y / (CHUNK_SIZE * TILE_SIZE));

    // Load chunks within render distance
    for (int cy = playerChunkY - renderDistance; cy <= playerChunkY + renderDistance; cy++) {
        for (int cx = playerChunkX - renderDistance; cx <= playerChunkX + renderDistance; cx++) {
            int64_t key = chunkKey(cx, cy);
            if (chunks.find(key) == chunks.end()) {
                loadChunk(cx, cy);
            }
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

bool World::isWaterTile(int worldTileX, int worldTileY) {
    Tile* t = getTile(worldTileX, worldTileY);
    return t && (t->id == TILE_DEEP_WATER || t->id == TILE_SHALLOW_WATER);
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
                    int v = ((worldTileX * 7 + worldTileY * 13) & 0x7FFFFFFF);

                    // Cardinal neighbors
                    Tile* tn = getTile(worldTileX, worldTileY - 1);
                    Tile* ts = getTile(worldTileX, worldTileY + 1);
                    Tile* te = getTile(worldTileX + 1, worldTileY);
                    Tile* tw = getTile(worldTileX - 1, worldTileY);
                    bool landN = tn && tn->id != TILE_DEEP_WATER && tn->id != TILE_SHALLOW_WATER;
                    bool landS = ts && ts->id != TILE_DEEP_WATER && ts->id != TILE_SHALLOW_WATER;
                    bool landE = te && te->id != TILE_DEEP_WATER && te->id != TILE_SHALLOW_WATER;
                    bool landW = tw && tw->id != TILE_DEEP_WATER && tw->id != TILE_SHALLOW_WATER;

                    // Diagonal neighbors
                    Tile* tne = getTile(worldTileX + 1, worldTileY - 1);
                    Tile* tnw = getTile(worldTileX - 1, worldTileY - 1);
                    Tile* tse = getTile(worldTileX + 1, worldTileY + 1);
                    Tile* tsw = getTile(worldTileX - 1, worldTileY + 1);
                    bool landNE = tne && tne->id != TILE_DEEP_WATER && tne->id != TILE_SHALLOW_WATER;
                    bool landNW = tnw && tnw->id != TILE_DEEP_WATER && tnw->id != TILE_SHALLOW_WATER;
                    bool landSE = tse && tse->id != TILE_DEEP_WATER && tse->id != TILE_SHALLOW_WATER;
                    bool landSW = tsw && tsw->id != TILE_DEEP_WATER && tsw->id != TILE_SHALLOW_WATER;

                    // Corners (land on two adjacent cardinal sides)
                    bool cornerNE = landN && landE;
                    bool cornerNW = landN && landW;
                    bool cornerSE = landS && landE;
                    bool cornerSW = landS && landW;

                    if (cornerNE) waterPieces[WP_CORNER_NE].draw(tileX, tileY, 0, 0, v);
                    if (cornerNW) waterPieces[WP_CORNER_NW].draw(tileX, tileY, 0, 0, v);
                    if (cornerSE) waterPieces[WP_CORNER_SE].draw(tileX, tileY, 0, 0, v);
                    if (cornerSW) waterPieces[WP_CORNER_SW].draw(tileX, tileY, 0, 0, v);

                    // Edges (land on one cardinal side, not part of a corner)
                    bool edgeW = landW && !cornerNW && !cornerSW;
                    bool edgeE = landE && !cornerNE && !cornerSE;
                    bool edgeN = landN && !cornerNE && !cornerNW;
                    bool edgeS = landS && !cornerSE && !cornerSW;

                    if (edgeW && edgeE) {
                        waterPieces[WP_EDGE_WE].draw(tileX, tileY, 0, 0, v);
                    } else {
                        if (edgeW) waterPieces[WP_EDGE].drawRotated(tileX, tileY, 0, 0, 0.0f, v);
                        if (edgeE) waterPieces[WP_EDGE].drawRotated(tileX, tileY, 0, 0, 180.0f, v);
                    }
                    if (edgeN && edgeS) {
                        waterPieces[WP_EDGE_WE].drawRotated(tileX, tileY, 0, 0, 90.0f, v);
                    } else {
                        if (edgeN) waterPieces[WP_EDGE].drawRotated(tileX, tileY, 0, 0, 90.0f, v);
                        if (edgeS) waterPieces[WP_EDGE].drawRotated(tileX, tileY, 0, 0, 270.0f, v);
                    }

                    // Inner corners (land diagonal only, no cardinal land on that side)
                    if (landNE && !landN && !landE) waterPieces[WP_INNER_CORNER].drawRotated(tileX, tileY, 0, 0, 90.0f, v);
                    if (landNW && !landN && !landW) waterPieces[WP_INNER_CORNER].draw(tileX, tileY, 0, 0, v);
                    if (landSE && !landS && !landE) waterPieces[WP_INNER_CORNER].drawRotated(tileX, tileY, 0, 0, 180.0f, v);
                    if (landSW && !landS && !landW) waterPieces[WP_INNER_CORNER].drawRotated(tileX, tileY, 0, 0, 270.0f, v);
                }
            }
        }

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
                    Vector2 colSize = OBJECT_COLLISION_SIZE[objId];
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
}

bool World::checkRiverGeneration(Vector2 playerPos) {
    // Check the player's current tile and neighboring regions
    int playerTileX = (int)floor(playerPos.x / TILE_SIZE);
    int playerTileY = (int)floor(playerPos.y / TILE_SIZE);
    int playerRegionX = SpringRiverSystem::worldToRegion(playerTileX);
    int playerRegionY = SpringRiverSystem::worldToRegion(playerTileY);

    bool generated = false;

    // Check 3x3 grid of regions around the player
    for (int ry = playerRegionY - 1; ry <= playerRegionY + 1; ry++) {
        for (int rx = playerRegionX - 1; rx <= playerRegionX + 1; rx++) {
            int regionTileX = rx * RIVER_REGION_SIZE;
            int regionTileY = ry * RIVER_REGION_SIZE;
            if (!riverSystem.isRegionGenerated(regionTileX, regionTileY)) {
                riverSystem.generateRegion(rx, ry, elevationNoise);
                invalidateChunksInRegion(rx, ry);
                generated = true;
            }
        }
    }

    return generated;
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
}
