#pragma once
#include "raylib.h"
#include "chunk.h"
#include "spring_river_system.h"
#include "FastNoiseLite.h"
#include <unordered_map>
#include <vector>
#include <cstdint>
#include <future>

const int TILE_SIZE = 32;
const int DEFAULT_RENDER_DISTANCE = 2;
inline int WorldSeed = 58;

// Noise origin offset to avoid Perlin symmetry artifacts near (0,0)
const float NOISE_OFFSET = 10000.0f;

// Sample noise and normalize to [0, 1]
inline float sampleNoise(FastNoiseLite& noise, float worldX, float worldY) {
    return (noise.GetNoise(worldX + NOISE_OFFSET, worldY + NOISE_OFFSET) + 1.0f) / 2.0f;
}

// Pack two int32s into a single int64 key for fast hashing
inline int64_t chunkKey(int cx, int cy) {
    return ((int64_t)cx << 32) | (uint32_t)cy;
}

// Cardinal flow directions
enum FlowDir { FLOW_NONE = -1, FLOW_N = 0, FLOW_E = 1, FLOW_S = 2, FLOW_W = 3 };

// Cardinal offsets: N, E, S, W
const int FLOW_DX[] = { 0, 1, 0, -1 };
const int FLOW_DY[] = { -1, 0, 1, 0 };

// Opposite direction: N<->S, E<->W
inline FlowDir oppositeDir(FlowDir d) {
    if (d == FLOW_NONE) return FLOW_NONE;
    return (FlowDir)((d + 2) % 4);
}

class World {
public:
    std::unordered_map<int64_t, Chunk> chunks;
    FastNoiseLite elevationNoise;
    FastNoiseLite moistureNoise;
    FastNoiseLite vegetationNoise;
    SpringRiverSystem riverSystem;
    Texture2D tileTextures[TILE_COUNT];
    int tileVariants[TILE_COUNT];       // how many variants per tile type
    int tileAtlasCols[TILE_COUNT];      // columns in spritesheet (derived from texture width)
    float tileAnimSpeed[TILE_COUNT];    // seconds per frame (0 = static)
    std::vector<Texture2D> objectTextures;
    std::vector<bool> objectTexturesLoaded;
    // Water transition piece: a small image drawn at an offset within a water tile
    struct WaterPiece {
        Texture2D tex = {};
        int pieceW = 0, pieceH = 0; // size of one variant frame
        int variants = 0;           // auto-detected: tex height / pieceH
        bool loaded = false;

        void load(const char* path, int w, int h) {
            tex = LoadTexture(path);
            if (tex.id > 0) {
                if (tex.height >= h) {
                    // Normal: image has one or more variants stacked vertically
                    pieceW = w;
                    pieceH = h;
                    variants = tex.height / h;
                } else {
                    // Image smaller than expected piece size — use actual dimensions as one variant
                    pieceW = tex.width;
                    pieceH = tex.height;
                    variants = 1;
                }
                loaded = (variants > 0);
            } else {
                pieceW = w;
                pieceH = h;
                variants = 0;
                loaded = false;
            }
        }
        void unload() { if (loaded) { UnloadTexture(tex); loaded = false; } }

        // Get source rect for a given variant index
        Rectangle srcRect(int variant) const {
            int v = variant % variants;
            return {0, (float)(v * pieceH), (float)pieceW, (float)pieceH};
        }

        // Draw at a position within the tile (no rotation)
        void draw(float tileX, float tileY, float offX, float offY, int variant) const {
            if (!loaded) return;
            DrawTextureRec(tex, srcRect(variant), {tileX + offX, tileY + offY}, WHITE);
        }

        // Draw with rotation (rotates around piece center)
        void drawRotated(float tileX, float tileY, float offX, float offY, float rotation, int variant) const {
            if (!loaded) return;
            float cx = offX + pieceW / 2.0f;
            float cy = offY + pieceH / 2.0f;
            Rectangle src = srcRect(variant);
            Rectangle dst = {tileX + cx, tileY + cy, (float)pieceW, (float)pieceH};
            Vector2 orig = {pieceW / 2.0f, pieceH / 2.0f};
            DrawTexturePro(tex, src, dst, orig, rotation, WHITE);
        }
    };

    // Water transition pieces — indexed by enum below
    enum WaterPieceId {
        WP_STRAIGHT,      // straight edge (16x16, rotated per direction)
        WP_TURN,          // outer corner/turn (32x32, rotated per direction)
        WP_CORNER,        // inner corner (16x16, rotated per direction)
        WP_COUNT
    };
    WaterPiece waterPieces[WP_COUNT];
    bool waterTransitionsLoaded = false;
    bool texturesLoaded = false;
    bool drawChunkBorders = true;
    float animTimer = 0.0f;
    int renderDistance = DEFAULT_RENDER_DISTANCE;

    World();
    ~World();
    World(const World&) = delete;
    World& operator=(const World&) = delete;
    void loadTextures();
    void update(Vector2 playerPos);
    // playerSortY: the Y value to sort the player at (bottom edge). -1 = don't include player.
    // drawPlayerFn: called at the right Y-sort position to draw the player.
    void draw(const Camera2D& camera, int screenWidth, int screenHeight,
              float playerSortY = -1.0f, void (*drawPlayerFn)() = nullptr);
    Chunk* getChunk(int chunkX, int chunkY);
    Tile* getTile(int worldX, int worldY);
    int getLoadedChunkCount();
    FlowDir getFlowDir(int worldTileX, int worldTileY);
    bool isWaterTile(int worldTileX, int worldTileY);
    bool isWaterAt(int worldTileX, int worldTileY); // like isWaterTile but uses noise fallback for unloaded chunks
    void computeWaterStyles(int cx, int cy);

    // Remove the object on a tile. Returns the objectId that was removed (OBJECT_NONE if empty).
    // If dropLoot is true, the caller should use the returned id to roll loot.
    int removeObject(int worldTileX, int worldTileY, bool dropLoot);

    // Async river region generation
    struct PendingRiver {
        int regionX, regionY;
        std::future<void> future;
    };
    std::vector<PendingRiver> pendingRivers;

    void dispatchRiverGeneration(Vector2 playerPos);
    void integrateReadyRivers();
    void invalidateChunksInRegion(int regionX, int regionY);

    // --- Async chunk generation ---
    // Each pending entry holds a future that produces a Chunk on a worker thread.
    // The main thread polls these each frame and inserts completed chunks.
    struct PendingChunk {
        int64_t key;
        int cx, cy;
        std::future<Chunk> future;
    };
    std::vector<PendingChunk> pendingChunks;
    static const int MAX_PENDING_CHUNKS = 8;

    void integrateReadyChunks();
    void waitForPendingChunks();

private:
    void loadChunk(int cx, int cy);
    void unloadDistantChunks(int playerChunkX, int playerChunkY);
};
