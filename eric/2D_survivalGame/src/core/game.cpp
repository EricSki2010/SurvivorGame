#include "game.h"
#include "collision.h"

Game::Game(int width, int height)
    : screenWidth(width),
      screenHeight(height),
      player(width / 2.0f, height / 2.0f),
      running(true) {
    camera = {0};
    camera.offset = {width / 2.0f, height / 2.0f};
    camera.target = player.position;
    camera.zoom = 2.0f;
    world.loadTextures();
}

void Game::update() {
    // Check if player is near an ungenerated river region
    if (world.checkRiverGeneration(player.position)) {
        // Show loading screen, pause game time
        BeginDrawing();
        ClearBackground(BLACK);
        DrawText("Generating rivers...", screenWidth / 2 - 110, screenHeight / 2 - 10, 20, WHITE);
        EndDrawing();
        // Discard the frame time that elapsed during generation
        GetFrameTime();
        return;  // skip this frame's game update
    }

    float dt = GetFrameTime();

    // Rebuild collision boxes from nearby objects
    clearCollisionBoxes();
    float centerX = player.position.x + player.size.x / 2.0f;
    float centerY = player.position.y + player.size.y / 2.0f;
    int playerTileXi = (int)floor(centerX / TILE_SIZE);
    int playerTileYi = (int)floor(centerY / TILE_SIZE);
    for (int ty = playerTileYi - 1; ty <= playerTileYi + 1; ty++) {
        for (int tx = playerTileXi - 1; tx <= playerTileXi + 1; tx++) {
            Tile* t = world.getTile(tx, ty);
            if (!t || t->objectId == OBJECT_NONE) continue;
            if (!world.objectTexturesLoaded[t->objectId]) continue;

            Vector2 colSize = OBJECT_COLLISION_SIZE[t->objectId];
            if (colSize.x == 0 && colSize.y == 0) continue;
            float objW = colSize.x;
            float objH = colSize.y;
            float objX = tx * TILE_SIZE + (TILE_SIZE - objW) / 2.0f + t->offsetX;
            float objY = ty * TILE_SIZE + (TILE_SIZE - objH) / 2.0f + t->offsetY;

            std::string id = std::to_string(tx) + "," + std::to_string(ty);
            addCollisionBox(id, {objX, objY, objW, objH});
        }
    }

    player.update(dt, world);
    world.update(player.position);

    // Zoom in/out with arrow keys
    bool zoomChanged = false;
    if (IsKeyPressed(KEY_UP)) {
        camera.zoom *= 1.5f;
        if (camera.zoom > 6.0f) camera.zoom = 6.0f;
        zoomChanged = true;
    }
    if (IsKeyPressed(KEY_DOWN)) {
        camera.zoom /= 1.5f;
        if (camera.zoom < 0.1f) camera.zoom = 0.1f;
        zoomChanged = true;
    }
    if (zoomChanged) {
        world.renderDistance = (int)(DEFAULT_RENDER_DISTANCE / camera.zoom) + 1;
    }

    // Quit on ESC
    if (IsKeyPressed(KEY_ESCAPE)) {
        running = false;
    }
}

static Player* s_drawPlayer = nullptr;
static void drawPlayerCallback() { if (s_drawPlayer) s_drawPlayer->draw(); }

void Game::draw() {
    BeginDrawing();
    ClearBackground(DARKGREEN);

    // Update camera to follow player
    camera.target = player.position;

    s_drawPlayer = &player;
    float playerSortY = player.position.y + player.size.y;

    BeginMode2D(camera);
    world.draw(camera, screenWidth, screenHeight, playerSortY, drawPlayerCallback);
    drawCollisionBoxes();
    EndMode2D();

    drawUI();

    EndDrawing();
}

void Game::drawUI() {
    // Get elevation and moisture at player position
    float playerTileX = player.position.x / TILE_SIZE;
    float playerTileY = player.position.y / TILE_SIZE;
    float elevation = sampleNoise(world.elevationNoise, playerTileX, playerTileY);
    float moisture = sampleNoise(world.moistureNoise, playerTileX, playerTileY);
    float vegetation = sampleNoise(world.vegetationNoise, playerTileX, playerTileY);
    // Current tile coords (used for flow + tile name)
    int worldTileX = (int)floor(player.position.x / TILE_SIZE);
    int worldTileY = (int)floor(player.position.y / TILE_SIZE);
    int flowVal = world.riverSystem.getFlow(worldTileX, worldTileY);

    DrawFPS(10, 10);
    DrawText(TextFormat("Health: %d", player.health), 10, 40, 20, WHITE);
    DrawText(TextFormat("Chunks: %d", world.getLoadedChunkCount()), 10, 65, 20, YELLOW);
    DrawText(TextFormat("Elevation: %.2f", elevation), 10, 90, 20, WHITE);
    DrawText(TextFormat("Moisture: %.2f", moisture), 10, 115, 20, {100, 200, 150, 255});
    DrawText(TextFormat("Vegetation: %.2f", vegetation), 10, 140, 20, {80, 200, 80, 255});
    DrawText(TextFormat("Flow: %d", flowVal), 10, 165, 20, {80, 150, 255, 255});
    Tile* tile = world.getTile(worldTileX, worldTileY);
    const char* tileName = tile ? TILE_NAMES[tile->id] : "Unknown";
    DrawText(TextFormat("Tile: %s", tileName), 10, 190, 20, WHITE);
}
