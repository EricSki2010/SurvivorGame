#include "game.h"
#include "game_data.h"
#include "collision.h"
#include <filesystem>
#include <fstream>
#include <sstream>
#include <ctime>
#include <algorithm>

namespace fs = std::filesystem;

// ---------------------------------------------------------------------------
// Small helpers to reduce repetition
// ---------------------------------------------------------------------------

static bool isNameChar(int ch) {
    return (ch>='a'&&ch<='z')||(ch>='A'&&ch<='Z')||(ch>='0'&&ch<='9')||ch==' '||ch=='_'||ch=='-';
}
static bool isDigitChar(int ch) { return ch >= '0' && ch <= '9'; }
static bool isAnyChar(int)      { return true; }

// Consume all pending key presses, appending allowed chars, and handle backspace
static void handleTextInput(std::string& text, size_t maxLen, bool (*filter)(int)) {
    int ch = GetCharPressed();
    while (ch > 0) {
        if (filter(ch) && text.size() < maxLen) text += (char)ch;
        ch = GetCharPressed();
    }
    if (IsKeyPressed(KEY_BACKSPACE) && !text.empty()) text.pop_back();
}

static const Rectangle BACK_BUTTON = {20, 20, 90, 35};

static void drawBackButton(Vector2 mouse) {
    Color c = CheckCollisionPointRec(mouse, BACK_BUTTON) ? DARKGREEN : Color{0, 100, 0, 200};
    DrawRectangleRec(BACK_BUTTON, c);
    DrawText("< Back", 30, 29, 18, WHITE);
}

// XOR is symmetric: same function encrypts and decrypts
static std::string xorCrypt(const std::string& data) {
    static const char key[] = "Sv!v0rG@m3_K3y#2025$xZ";
    static const size_t keyLen = sizeof(key) - 1;
    std::string out = data;
    for (size_t i = 0; i < out.size(); i++)
        out[i] ^= key[i % keyLen];
    return out;
}

// ---------------------------------------------------------------------------
// Constructor
// ---------------------------------------------------------------------------

Game::Game(int width, int height)
    : screenWidth(width),
      screenHeight(height),
      player(width / 2.0f, height / 2.0f),
      running(true) {
    camera = {0};
    camera.offset = {width / 2.0f, height / 2.0f};
    camera.target = player.position;
    camera.zoom = 2.0f * ((float)height / 800.0f);
}

void Game::toggleFullscreen() {
    ToggleFullscreen();
    screenWidth  = GetRenderWidth();
    screenHeight = GetRenderHeight();
    camera.offset = {screenWidth / 2.0f, screenHeight / 2.0f};
    // Scale zoom so tiles stay the same physical size at any resolution
    camera.zoom = 2.0f * ((float)screenHeight / 800.0f);
    if (world) world->renderDistance = (int)(DEFAULT_RENDER_DISTANCE / camera.zoom) + 1;
}

// ---------------------------------------------------------------------------
// Save / Load
// ---------------------------------------------------------------------------

void Game::createSaveFolders(const std::string& name) {
    fs::create_directories("saves/" + name + "/world");
    fs::create_directories("saves/" + name + "/players");
    fs::create_directories("saves/" + name + "/progress");
}

std::string Game::resolveSaveName(const std::string& baseName) {
    if (!fs::exists("saves/" + baseName)) return baseName;
    int n = 2;
    while (true) {
        std::string candidate = baseName + " (" + std::to_string(n) + ")";
        if (!fs::exists("saves/" + candidate)) return candidate;
        n++;
    }
}

void Game::scanSaves() {
    saveList.clear();
    if (!fs::exists("saves")) return;
    for (auto& entry : fs::directory_iterator("saves")) {
        if (entry.is_directory())
            saveList.push_back(entry.path().filename().string());
    }
    // Newest first (timestamp is part of name, so reverse alphabetical = newest first)
    std::sort(saveList.begin(), saveList.end(), std::greater<std::string>());
}

static void writeEncrypted(const std::string& path, const std::string& content) {
    std::string enc = xorCrypt(content);
    std::ofstream f(path, std::ios::binary);
    f.write(enc.data(), (std::streamsize)enc.size());
}

void Game::saveGame() {
    createSaveFolders(currentSaveName);

    writeEncrypted("saves/" + currentSaveName + "/world/world.dat",
        "seed=" + std::to_string(WorldSeed) + "\n");

    writeEncrypted("saves/" + currentSaveName + "/players/player.dat",
        "x="      + std::to_string(player.position.x) + "\n" +
        "y="      + std::to_string(player.position.y) + "\n" +
        "health=" + std::to_string(player.health)     + "\n");

    // Reserved for future use
    writeEncrypted("saves/" + currentSaveName + "/progress/progress.dat", "");

    saveMessageTimer = 2.0f;
}

static std::string readDecrypted(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f.is_open()) return "";
    std::string enc((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    return xorCrypt(enc);
}

bool Game::loadGame(const std::string& saveName) {
    std::string worldData = readDecrypted("saves/" + saveName + "/world/world.dat");
    if (worldData.empty()) return false;

    int seed = 58;
    std::istringstream ws(worldData);
    std::string line;
    while (std::getline(ws, line)) {
        if (line.rfind("seed=", 0) == 0)
            seed = std::stoi(line.substr(5));
    }

    float px = screenWidth / 2.0f, py = screenHeight / 2.0f;
    int health = 100;
    std::string playerData = readDecrypted("saves/" + saveName + "/players/player.dat");
    if (!playerData.empty()) {
        std::istringstream ps(playerData);
        while (std::getline(ps, line)) {
            if      (line.rfind("x=",      0) == 0) px     = std::stof(line.substr(2));
            else if (line.rfind("y=",      0) == 0) py     = std::stof(line.substr(2));
            else if (line.rfind("health=", 0) == 0) health = std::stoi(line.substr(7));
        }
    }

    currentSaveName = saveName;
    startGame(seed);
    player.position = {px, py};
    player.health = health;
    camera.target = player.position;
    return true;
}

// ---------------------------------------------------------------------------
// Game init
// ---------------------------------------------------------------------------

void Game::startGame(int seed) {
    if (gData().objectCount == 0) {
        if (!gData().loadAll("assets/data/")) return;
    }
    WorldSeed = seed;
    world = std::make_unique<World>();
    world->loadTextures();

    // Load item textures
    itemTextures.resize(gData().itemCount, Texture2D{0});
    itemTexturesLoaded.resize(gData().itemCount, false);
    for (int i = 0; i < gData().itemCount; i++) {
        const auto& def = gData().items[i];
        if (!def.texturePath.empty()) {
            itemTextures[i] = LoadTexture(def.texturePath.c_str());
            itemTexturesLoaded[i] = (itemTextures[i].id > 0);
        }
    }

    // Starting items for testing
    player.hotbar[0] = {0, 3};  // 3 wood in first hotbar slot

    state = GameState::PLAYING;
}

// ---------------------------------------------------------------------------
// Update
// ---------------------------------------------------------------------------

void Game::update() {
    if (IsKeyPressed(KEY_F11)) { toggleFullscreen(); return; }

    // --- MAIN_MENU ---
    if (state == GameState::MAIN_MENU) {
        const int btnW = 250, btnH = 50;
        int btnX = screenWidth / 2 - btnW / 2;
        int createY = screenHeight / 2 - 80;
        int loadY   = screenHeight / 2 - 10;
        int exitY   = screenHeight / 2 + 60;
        Rectangle createRect = {(float)btnX, (float)createY, (float)btnW, (float)btnH};
        Rectangle loadRect   = {(float)btnX, (float)loadY,   (float)btnW, (float)btnH};
        Rectangle exitRect   = {(float)btnX, (float)exitY,   (float)btnW, (float)btnH};

        if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
            Vector2 mouse = GetMousePosition();
            if (CheckCollisionPointRec(mouse, createRect)) {
                seedInput.clear();
                nameInput.clear();
                focusedField = 0;
                state = GameState::SEED_INPUT;
            }
            if (CheckCollisionPointRec(mouse, loadRect)) {
                scanSaves();
                loadScrollOffset = 0;
                state = GameState::LOAD_MENU;
            }
            if (CheckCollisionPointRec(mouse, exitRect)) running = false;
        }

        return;
    }

    // --- SEED_INPUT ---
    if (state == GameState::SEED_INPUT) {
        const int boxW = 250, boxH = 40;
        int boxX = screenWidth / 2 - boxW / 2;
        int nameBoxY = screenHeight / 2 - 75;
        int seedBoxY = screenHeight / 2 + 5;
        Rectangle nameBoxRect = {(float)boxX, (float)nameBoxY, (float)boxW, (float)boxH};
        Rectangle seedBoxRect = {(float)boxX, (float)seedBoxY, (float)boxW, (float)boxH};

        // Click to focus a field or hit back
        if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
            Vector2 mouse = GetMousePosition();
            if      (CheckCollisionPointRec(mouse, BACK_BUTTON))  { state = GameState::MAIN_MENU; return; }
            else if (CheckCollisionPointRec(mouse, nameBoxRect))  focusedField = 0;
            else if (CheckCollisionPointRec(mouse, seedBoxRect))  focusedField = 1;
        }

        // Tab switches focus
        if (IsKeyPressed(KEY_TAB)) focusedField = 1 - focusedField;

        // Character input + backspace
        if (focusedField == 0)
            handleTextInput(nameInput, 30, isNameChar);
        else
            handleTextInput(seedInput, 9, isDigitChar);

        bool canCreate = !nameInput.empty() && !seedInput.empty();

        auto doCreate = [&]() {
            int seed = std::stoi(seedInput);
            currentSaveName = resolveSaveName(nameInput);
            createSaveFolders(currentSaveName);
            startGame(seed);
        };

        if (IsKeyPressed(KEY_ENTER) && canCreate) { doCreate(); return; }

        const int btnW = 150, btnH = 45;
        int btnX = screenWidth / 2 - btnW / 2;
        int btnY = screenHeight / 2 + 65;
        Rectangle btnRect = {(float)btnX, (float)btnY, (float)btnW, (float)btnH};

        if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && canCreate) {
            Vector2 mouse = GetMousePosition();
            if (CheckCollisionPointRec(mouse, btnRect)) doCreate();
        }

        if (IsKeyPressed(KEY_ESCAPE)) state = GameState::MAIN_MENU;
        return;
    }

    // --- LOAD_MENU ---
    if (state == GameState::LOAD_MENU) {
        const int rowH = 55;
        const int listStartY = 120;
        int visibleRows = (screenHeight - listStartY - 40) / rowH;

        // --- Delete confirmation popup ---
        if (deleteIndex >= 0) {
            handleTextInput(deleteConfirmInput, 50, isAnyChar);

            const int popW = 480, popH = 260;
            int popX = screenWidth / 2 - popW / 2, popY = screenHeight / 2 - popH / 2;
            Rectangle confirmRect = {(float)(popX + 20),        (float)(popY + popH - 60), 180, 40};
            Rectangle cancelRect  = {(float)(popX + popW - 200),(float)(popY + popH - 60), 180, 40};
            bool matches = deleteConfirmInput == saveList[deleteIndex];

            if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
                Vector2 mouse = GetMousePosition();
                if (matches && CheckCollisionPointRec(mouse, confirmRect)) {
                    fs::remove_all("saves/" + saveList[deleteIndex]);
                    saveList.erase(saveList.begin() + deleteIndex);
                    deleteIndex = -1; deleteConfirmInput.clear(); return;
                }
                if (CheckCollisionPointRec(mouse, cancelRect)) { deleteIndex = -1; deleteConfirmInput.clear(); return; }
            }
            if (IsKeyPressed(KEY_ESCAPE)) { deleteIndex = -1; deleteConfirmInput.clear(); }
            return;
        }

        // --- Rename popup ---
        if (renameIndex >= 0) {
            handleTextInput(renameInput, 30, isNameChar);

            const int popW = 400, popH = 200;
            int popX = screenWidth / 2 - popW / 2, popY = screenHeight / 2 - popH / 2;
            Rectangle confirmRect = {(float)(popX + 20),        (float)(popY + popH - 60), 160, 40};
            Rectangle cancelRect  = {(float)(popX + popW - 180),(float)(popY + popH - 60), 160, 40};
            bool canRename = !renameInput.empty();
            bool enterHit  = IsKeyPressed(KEY_ENTER);

            auto doRename = [&]() {
                std::string oldName = saveList[renameIndex];
                if (renameInput != oldName) {
                    std::string newName = resolveSaveName(renameInput);
                    fs::rename("saves/" + oldName, "saves/" + newName);
                    if (currentSaveName == oldName) currentSaveName = newName;
                    saveList[renameIndex] = newName;
                }
                renameIndex = -1; renameInput.clear();
            };

            if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
                Vector2 mouse = GetMousePosition();
                if (canRename && CheckCollisionPointRec(mouse, confirmRect)) { doRename(); return; }
                if (CheckCollisionPointRec(mouse, cancelRect)) { renameIndex = -1; renameInput.clear(); return; }
            }
            if (enterHit && canRename) { doRename(); return; }
            if (IsKeyPressed(KEY_ESCAPE)) { renameIndex = -1; renameInput.clear(); }
            return;
        }

        // --- Context menu (... clicked) ---
        if (contextMenuIndex >= 0) {
            int visRowI = contextMenuIndex - loadScrollOffset;
            int rowY = listStartY + visRowI * rowH;
            const int ctxW = 140, ctxItemH = 40;
            int ctxX = screenWidth - 20 - ctxW;
            int ctxY = rowY + (rowH - 5);
            if (ctxY + ctxItemH * 2 > screenHeight - 20) ctxY = rowY - ctxItemH * 2;
            Rectangle renameOptRect = {(float)ctxX, (float)ctxY,              (float)ctxW, (float)ctxItemH};
            Rectangle deleteOptRect = {(float)ctxX, (float)(ctxY+ctxItemH),   (float)ctxW, (float)ctxItemH};

            if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
                Vector2 mouse = GetMousePosition();
                if (CheckCollisionPointRec(mouse, renameOptRect)) {
                    renameIndex = contextMenuIndex; renameInput = saveList[contextMenuIndex]; contextMenuIndex = -1;
                } else if (CheckCollisionPointRec(mouse, deleteOptRect)) {
                    deleteIndex = contextMenuIndex; deleteConfirmInput.clear(); contextMenuIndex = -1;
                } else {
                    contextMenuIndex = -1;
                }
            }
            if (IsKeyPressed(KEY_ESCAPE)) contextMenuIndex = -1;
            return;
        }

        // --- Normal scroll + click ---
        float wheel = GetMouseWheelMove();
        if (wheel != 0) {
            loadScrollOffset -= (int)wheel;
            if (loadScrollOffset < 0) loadScrollOffset = 0;
            int maxScroll = (int)saveList.size() - visibleRows;
            if (loadScrollOffset > maxScroll && maxScroll >= 0) loadScrollOffset = maxScroll;
        }

        if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
            Vector2 mouse = GetMousePosition();
            if (CheckCollisionPointRec(mouse, BACK_BUTTON)) { state = GameState::MAIN_MENU; return; }
            for (int i = 0; i < visibleRows; i++) {
                int idx = i + loadScrollOffset;
                if (idx >= (int)saveList.size()) break;
                int rowY = listStartY + i * rowH;
                Rectangle dotRect = {(float)(screenWidth - 55), (float)(rowY + (rowH - 5 - 30) / 2), 45, 30};
                Rectangle rowRect = {20, (float)rowY, (float)(screenWidth - 110), (float)(rowH - 5)};
                if (CheckCollisionPointRec(mouse, dotRect)) { contextMenuIndex = idx; return; }
                if (CheckCollisionPointRec(mouse, rowRect)) { loadGame(saveList[idx]); return; }
            }
        }

        if (IsKeyPressed(KEY_ESCAPE)) state = GameState::MAIN_MENU;
        return;
    }

    // --- PAUSED ---
    if (state == GameState::PAUSED) {
        const int btnW = 200, btnH = 45;
        int btnX = screenWidth / 2 - btnW / 2;
        int resumeY   = screenHeight / 2 - 55;
        int saveExitY = screenHeight / 2 + 15;
        Rectangle resumeRect   = {(float)btnX, (float)resumeY,   (float)btnW, (float)btnH};
        Rectangle saveExitRect = {(float)btnX, (float)saveExitY, (float)btnW, (float)btnH};

        if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
            Vector2 mouse = GetMousePosition();
            if (CheckCollisionPointRec(mouse, resumeRect))   state = GameState::PLAYING;
            if (CheckCollisionPointRec(mouse, saveExitRect)) { saveGame(); world.reset(); state = GameState::MAIN_MENU; }
        }

        if (IsKeyPressed(KEY_ESCAPE)) state = GameState::PLAYING;
        return;
    }

    // --- PLAYING ---

    float dt = GetFrameTime();

    autoSaveTimer += dt;
    if (autoSaveTimer >= 20.0f) {
        saveGame();
        autoSaveTimer = 0.0f;
    }

    clearCollisionBoxes();
    float centerX = player.position.x + player.size.x / 2.0f;
    float centerY = player.position.y + player.size.y / 2.0f;
    int playerTileXi = (int)floor(centerX / TILE_SIZE);
    int playerTileYi = (int)floor(centerY / TILE_SIZE);
    for (int ty = playerTileYi - 1; ty <= playerTileYi + 1; ty++) {
        for (int tx = playerTileXi - 1; tx <= playerTileXi + 1; tx++) {
            Tile* t = world->getTile(tx, ty);
            if (!t || t->objectId == OBJECT_NONE) continue;
            if (!world->objectTexturesLoaded[t->objectId]) continue;
            Vector2 colSize = gData().getCollisionSize(t->objectId);
            if (colSize.x == 0 && colSize.y == 0) continue;
            float objW = colSize.x;
            float objH = colSize.y;
            float objX = tx * TILE_SIZE + (TILE_SIZE - objW) / 2.0f + t->offsetX;
            float objY = ty * TILE_SIZE + (TILE_SIZE - objH) / 2.0f + t->offsetY;
            std::string id = std::to_string(tx) + "," + std::to_string(ty);
            addCollisionBox(id, {objX, objY, objW, objH});
        }
    }

    player.update(dt, *world);
    world->update(player.position);

    bool zoomChanged = false;
    if (IsKeyPressed(KEY_UP))   { camera.zoom *= 1.5f; if (camera.zoom > 6.0f) camera.zoom = 6.0f; zoomChanged = true; }
    if (IsKeyPressed(KEY_DOWN)) { camera.zoom /= 1.5f; if (camera.zoom < 0.1f) camera.zoom = 0.1f; zoomChanged = true; }
    if (zoomChanged)
        world->renderDistance = (int)(DEFAULT_RENDER_DISTANCE / camera.zoom) + 1;

    if (IsKeyPressed(KEY_Z) && IsKeyDown(KEY_LEFT_SHIFT)) {
        showDebug = !showDebug;
        world->drawChunkBorders = showDebug;
    }

    // Inventory toggle
    if (IsKeyPressed(KEY_E)) {
        if (inventoryOpen) {
            // Auto-place held item back into inventory
            if (heldItem.itemId >= 0) {
                int maxStack = (heldItem.itemId < gData().itemCount) ? gData().items[heldItem.itemId].maxStack : 64;
                // Try merging into existing stacks (inventory then hotbar)
                for (int i = 0; i < Player::INV_SIZE && heldItem.count > 0; i++) {
                    if (player.inventory[i].itemId == heldItem.itemId && player.inventory[i].count < maxStack) {
                        int space = maxStack - player.inventory[i].count;
                        int add = (heldItem.count < space) ? heldItem.count : space;
                        player.inventory[i].count += add;
                        heldItem.count -= add;
                    }
                }
                for (int i = 0; i < Player::HOTBAR_SIZE && heldItem.count > 0; i++) {
                    if (player.hotbar[i].itemId == heldItem.itemId && player.hotbar[i].count < maxStack) {
                        int space = maxStack - player.hotbar[i].count;
                        int add = (heldItem.count < space) ? heldItem.count : space;
                        player.hotbar[i].count += add;
                        heldItem.count -= add;
                    }
                }
                // Place remainder in first empty slot
                if (heldItem.count > 0) {
                    for (int i = 0; i < Player::INV_SIZE; i++) {
                        if (player.inventory[i].itemId < 0) { player.inventory[i] = heldItem; heldItem = {}; break; }
                    }
                }
                if (heldItem.count > 0) {
                    for (int i = 0; i < Player::HOTBAR_SIZE; i++) {
                        if (player.hotbar[i].itemId < 0) { player.hotbar[i] = heldItem; heldItem = {}; break; }
                    }
                }
                if (heldItem.count <= 0) heldItem = {};
                // If still holding, refuse to close
                if (heldItem.itemId >= 0) goto skipClose;
            }
            inventoryOpen = false;
        } else {
            inventoryOpen = true;
        }
        skipClose:;
    }

    // Inventory click handling
    bool leftClick  = inventoryOpen && IsMouseButtonPressed(MOUSE_BUTTON_LEFT);
    bool rightClick = inventoryOpen && IsMouseButtonPressed(MOUSE_BUTTON_RIGHT);
    if (leftClick || rightClick) {
        Vector2 mouse = GetMousePosition();
        const int slotSize = 48, slotGap = 4;
        const int numSlots = 10, invCols = 10, invRows = 5;
        int hotbarW = numSlots * (slotSize + slotGap) - slotGap;
        int hotbarX = screenWidth / 2 - hotbarW / 2;
        int panelW = invCols * slotSize + (invCols - 1) * slotGap + 16;
        int panelH = invRows * slotSize + (invRows - 1) * slotGap + 36;
        int panelX = screenWidth / 2 - panelW / 2;
        int panelY = screenHeight / 2 - (panelH + slotGap + slotSize) / 2;
        int hotbarY = panelY + panelH + slotGap;

        ItemStack* clickedSlot = nullptr;
        bool clickedInHotbar = false;

        // Check hotbar slots
        for (int i = 0; i < numSlots; i++) {
            int sx = hotbarX + i * (slotSize + slotGap);
            Rectangle r = {(float)sx, (float)hotbarY, (float)slotSize, (float)slotSize};
            if (CheckCollisionPointRec(mouse, r)) { clickedSlot = &player.hotbar[i]; clickedInHotbar = true; break; }
        }

        // Check inventory slots
        if (!clickedSlot) {
            for (int row = 0; row < invRows; row++) {
                for (int col = 0; col < invCols; col++) {
                    int sx = panelX + 8 + col * (slotSize + slotGap);
                    int sy = panelY + 26 + row * (slotSize + slotGap);
                    Rectangle r = {(float)sx, (float)sy, (float)slotSize, (float)slotSize};
                    if (CheckCollisionPointRec(mouse, r)) { clickedSlot = &player.inventory[row * invCols + col]; break; }
                }
                if (clickedSlot) break;
            }
        }

        if (clickedSlot && leftClick && IsKeyDown(KEY_LEFT_SHIFT)) {
            // Shift-click: move to the other area
            if (clickedSlot->itemId >= 0) {
                // Determine destination arrays
                ItemStack* destArr = clickedInHotbar ? player.inventory : player.hotbar;
                int destSize = clickedInHotbar ? Player::INV_SIZE : Player::HOTBAR_SIZE;
                int maxStack = (clickedSlot->itemId < gData().itemCount) ? gData().items[clickedSlot->itemId].maxStack : 64;

                // Try merging into existing stacks first
                for (int i = 0; i < destSize && clickedSlot->count > 0; i++) {
                    if (destArr[i].itemId == clickedSlot->itemId && destArr[i].count < maxStack) {
                        int space = maxStack - destArr[i].count;
                        int add = (clickedSlot->count < space) ? clickedSlot->count : space;
                        destArr[i].count += add;
                        clickedSlot->count -= add;
                    }
                }
                // Then try empty slots
                for (int i = 0; i < destSize && clickedSlot->count > 0; i++) {
                    if (destArr[i].itemId < 0) {
                        destArr[i] = *clickedSlot;
                        clickedSlot->count = 0;
                    }
                }
                if (clickedSlot->count <= 0) *clickedSlot = {};
            }
        } else if (clickedSlot && leftClick) {
            // Left-click: pick up / place / merge / swap
            if (heldItem.itemId < 0) {
                if (clickedSlot->itemId >= 0) {
                    heldItem = *clickedSlot;
                    *clickedSlot = {};
                }
            } else if (clickedSlot->itemId < 0) {
                *clickedSlot = heldItem;
                heldItem = {};
            } else if (clickedSlot->itemId == heldItem.itemId) {
                int maxStack = (heldItem.itemId < gData().itemCount) ? gData().items[heldItem.itemId].maxStack : 64;
                int space = maxStack - clickedSlot->count;
                int add = (heldItem.count < space) ? heldItem.count : space;
                clickedSlot->count += add;
                heldItem.count -= add;
                if (heldItem.count <= 0) heldItem = {};
            } else {
                ItemStack temp = *clickedSlot;
                *clickedSlot = heldItem;
                heldItem = temp;
            }
        } else if (clickedSlot && rightClick) {
            if (heldItem.itemId >= 0) {
                // Right-click while holding: place 1 item
                if (clickedSlot->itemId < 0) {
                    *clickedSlot = {heldItem.itemId, 1};
                    heldItem.count--;
                    if (heldItem.count <= 0) heldItem = {};
                } else if (clickedSlot->itemId == heldItem.itemId) {
                    int maxStack = (heldItem.itemId < gData().itemCount) ? gData().items[heldItem.itemId].maxStack : 64;
                    if (clickedSlot->count < maxStack) {
                        clickedSlot->count++;
                        heldItem.count--;
                        if (heldItem.count <= 0) heldItem = {};
                    }
                }
            } else {
                // Right-click while empty hand: pick up half (or 1 if only 1)
                if (clickedSlot->itemId >= 0) {
                    int half = (clickedSlot->count + 1) / 2; // round up
                    heldItem = {clickedSlot->itemId, half};
                    clickedSlot->count -= half;
                    if (clickedSlot->count <= 0) *clickedSlot = {};
                }
            }
        }
    }

    // Hotbar: keys 1-9 and 0
    for (int i = 0; i < 9; i++)
        if (IsKeyPressed(KEY_ONE + i)) hotbarSlot = i;
    if (IsKeyPressed(KEY_ZERO)) hotbarSlot = 9;

    // Scroll wheel cycles hotbar slots (only when inventory is closed)
    if (!inventoryOpen) {
        float scroll = GetMouseWheelMove();
        if (scroll != 0)
            hotbarSlot = ((hotbarSlot - (int)scroll) % 10 + 10) % 10;
    }

    if (IsKeyPressed(KEY_ESCAPE)) {
        if (inventoryOpen) {
            inventoryOpen = false;
        } else {
            state = GameState::PAUSED;
        }
    }
}

// ---------------------------------------------------------------------------
// Draw helpers
// ---------------------------------------------------------------------------

static Player* s_drawPlayer = nullptr;
static void drawPlayerCallback() { if (s_drawPlayer) s_drawPlayer->draw(); }

void Game::drawGameWorld() {
    ClearBackground(DARKGREEN);
    camera.target = player.position;
    s_drawPlayer = &player;
    float playerSortY = player.position.y + player.size.y;
    BeginMode2D(camera);
    world->draw(camera, screenWidth, screenHeight, playerSortY, drawPlayerCallback);
    if (showDebug) drawCollisionBoxes();
    EndMode2D();
    drawUI();
}

// ---------------------------------------------------------------------------
// Draw
// ---------------------------------------------------------------------------

void Game::draw() {
    BeginDrawing();

    // --- MAIN_MENU ---
    if (state == GameState::MAIN_MENU) {
        ClearBackground(GREEN);

        const int btnW = 250, btnH = 50;
        int btnX = screenWidth / 2 - btnW / 2;
        Vector2 mouse = GetMousePosition();

        auto drawBtn = [&](int y, const char* label, Color base) {
            Rectangle r = {(float)btnX, (float)y, (float)btnW, (float)btnH};
            DrawRectangleRec(r, CheckCollisionPointRec(mouse, r) ? DARKGREEN : base);
            int textWidth = MeasureText(label, 24);
            DrawText(label, btnX + (btnW - textWidth) / 2, y + (btnH - 24) / 2, 24, WHITE);
        };

        drawBtn(screenHeight / 2 - 80, "Create World", DARKGREEN);
        drawBtn(screenHeight / 2 - 10, "Load Game",    DARKGREEN);
        drawBtn(screenHeight / 2 + 60, "Exit Game",    {100, 50, 50, 255});

        EndDrawing();
        return;
    }

    // --- SEED_INPUT ---
    if (state == GameState::SEED_INPUT) {
        ClearBackground(GREEN);

        const char* title = "Create World";
        int titleW = MeasureText(title, 30);
        DrawText(title, screenWidth / 2 - titleW / 2, screenHeight / 2 - 140, 30, WHITE);

        const int boxW = 250, boxH = 40;
        int boxX = screenWidth / 2 - boxW / 2;
        bool blink = ((int)(GetTime() * 2) % 2) == 0;

        // --- Name field ---
        int nameBoxY = screenHeight / 2 - 75;
        DrawText("World Name", boxX, nameBoxY - 22, 18, WHITE);
        DrawRectangle(boxX, nameBoxY, boxW, boxH, WHITE);
        DrawRectangleLines(boxX, nameBoxY, boxW, boxH, focusedField == 0 ? DARKGREEN : DARKGRAY);
        const char* nameText = nameInput.c_str();
        DrawText(nameText, boxX + 10, nameBoxY + (boxH - 20) / 2, 20, BLACK);
        if (focusedField == 0 && blink) {
            int cursorX = boxX + 10 + MeasureText(nameText, 20);
            DrawText("|", cursorX, nameBoxY + (boxH - 20) / 2, 20, BLACK);
        }

        // --- Seed field ---
        int seedBoxY = screenHeight / 2 + 5;
        DrawText("Seed", boxX, seedBoxY - 22, 18, WHITE);
        DrawRectangle(boxX, seedBoxY, boxW, boxH, WHITE);
        DrawRectangleLines(boxX, seedBoxY, boxW, boxH, focusedField == 1 ? DARKGREEN : DARKGRAY);
        const char* seedText = seedInput.c_str();
        DrawText(seedText, boxX + 10, seedBoxY + (boxH - 20) / 2, 20, BLACK);
        if (focusedField == 1 && blink) {
            int cursorX = boxX + 10 + MeasureText(seedText, 20);
            DrawText("|", cursorX, seedBoxY + (boxH - 20) / 2, 20, BLACK);
        }

        // --- Create button ---
        bool canCreate = !nameInput.empty() && !seedInput.empty();
        const int btnW = 150, btnH = 45;
        int btnX = screenWidth / 2 - btnW / 2;
        int btnY = screenHeight / 2 + 65;
        DrawRectangle(btnX, btnY, btnW, btnH, canCreate ? DARKGREEN : GRAY);
        int createTextWidth = MeasureText("Create", 24);
        DrawText("Create", btnX + (btnW - createTextWidth) / 2, btnY + (btnH - 24) / 2, 24, WHITE);

        // --- Back arrow ---
        drawBackButton(GetMousePosition());

        EndDrawing();
        return;
    }

    // --- LOAD_MENU ---
    if (state == GameState::LOAD_MENU) {
        ClearBackground(GREEN);

        const char* title = "Load Game";
        int titleW = MeasureText(title, 30);
        DrawText(title, screenWidth / 2 - titleW / 2, 40, 30, WHITE);

        const int rowH = 55;
        const int listStartY = 120;
        int visibleRows = (screenHeight - listStartY - 40) / rowH;
        Vector2 mouse = GetMousePosition();

        if (saveList.empty()) {
            const char* msg = "No saves found";
            int msgW = MeasureText(msg, 24);
            DrawText(msg, screenWidth / 2 - msgW / 2, screenHeight / 2, 24, WHITE);
        } else {
            for (int i = 0; i < visibleRows; i++) {
                int idx = i + loadScrollOffset;
                if (idx >= (int)saveList.size()) break;
                int rowY = listStartY + i * rowH;
                Rectangle rowRect = {20, (float)rowY, (float)(screenWidth - 110), (float)(rowH - 5)};
                Color rowColor = CheckCollisionPointRec(mouse, rowRect) ? DARKGREEN : Color{0, 100, 0, 200};
                DrawRectangleRec(rowRect, rowColor);
                DrawText(saveList[idx].c_str(), 35, rowY + (rowH - 5 - 20) / 2, 20, WHITE);

                // "..." button — anchored to right edge of screen
                Rectangle dotRect = {(float)(screenWidth - 55), (float)(rowY + (rowH - 5 - 30) / 2), 45, 30};
                Color dotColor = CheckCollisionPointRec(mouse, dotRect) ? DARKGREEN : Color{0, 80, 0, 220};
                DrawRectangleRec(dotRect, dotColor);
                int dotsTextWidth = MeasureText("...", 18);
                DrawText("...", (int)(dotRect.x + (dotRect.width - dotsTextWidth) / 2), (int)(dotRect.y + (dotRect.height - 18) / 2), 18, WHITE);
            }

            if ((int)saveList.size() > visibleRows)
                DrawText("Scroll for more", screenWidth - 160, screenHeight - 25, 16, {200, 200, 200, 200});

            // --- Context menu ---
            if (contextMenuIndex >= 0) {
                int visRowI = contextMenuIndex - loadScrollOffset;
                int rowY = listStartY + visRowI * rowH;
                const int ctxW = 140, ctxItemH = 40;
                int ctxX = screenWidth - 20 - ctxW;
                int ctxY = rowY + (rowH - 5);
                if (ctxY + ctxItemH * 2 > screenHeight - 20) ctxY = rowY - ctxItemH * 2;

                Rectangle renameOptRect = {(float)ctxX, (float)ctxY,            (float)ctxW, (float)ctxItemH};
                Rectangle deleteOptRect = {(float)ctxX, (float)(ctxY+ctxItemH), (float)ctxW, (float)ctxItemH};

                DrawRectangleRec(renameOptRect, CheckCollisionPointRec(mouse, renameOptRect) ? DARKGREEN : Color{30,30,30,240});
                int renameTextWidth = MeasureText("Rename", 18); DrawText("Rename", ctxX + (ctxW-renameTextWidth)/2, ctxY + (ctxItemH-18)/2, 18, WHITE);

                DrawRectangleRec(deleteOptRect, CheckCollisionPointRec(mouse, deleteOptRect) ? Color{180,50,50,255} : Color{120,30,30,240});
                int deleteTextWidth = MeasureText("Delete", 18); DrawText("Delete", ctxX + (ctxW-deleteTextWidth)/2, ctxY+ctxItemH+(ctxItemH-18)/2, 18, WHITE);
            }
        }

        // --- Back arrow ---
        drawBackButton(mouse);

        // --- Rename modal ---
        if (renameIndex >= 0) {
            const int popW = 400, popH = 200;
            int popX = screenWidth / 2 - popW / 2, popY = screenHeight / 2 - popH / 2;
            DrawRectangle(0, 0, screenWidth, screenHeight, {0,0,0,160});
            DrawRectangle(popX, popY, popW, popH, {40,40,40,255});
            DrawRectangleLines(popX, popY, popW, popH, DARKGRAY);

            DrawText("Rename World", popX + 20, popY + 18, 22, WHITE);
            DrawText("New Name:", popX + 20, popY + 65, 18, LIGHTGRAY);

            int boxY = popY + 90;
            DrawRectangle(popX+20, boxY, popW-40, 40, WHITE);
            DrawRectangleLines(popX+20, boxY, popW-40, 40, DARKGREEN);
            DrawText(renameInput.c_str(), popX+30, boxY+10, 20, BLACK);
            if (((int)(GetTime()*2)%2)==0) {
                int cursorX = popX+30 + MeasureText(renameInput.c_str(), 20);
                DrawText("|", cursorX, boxY+10, 20, BLACK);
            }

            bool canRename = !renameInput.empty();
            Rectangle confirmRect = {(float)(popX+20),       (float)(popY+popH-60), 160, 40};
            Rectangle cancelRect  = {(float)(popX+popW-180), (float)(popY+popH-60), 160, 40};
            DrawRectangleRec(confirmRect, canRename ? (CheckCollisionPointRec(mouse,confirmRect)?DARKGREEN:Color{0,120,0,255}) : GRAY);
            DrawText("Rename", (int)(confirmRect.x+(160-MeasureText("Rename",18))/2), (int)(confirmRect.y+11), 18, WHITE);
            DrawRectangleRec(cancelRect, CheckCollisionPointRec(mouse,cancelRect) ? Color{80,80,80,255} : Color{60,60,60,255});
            DrawText("Cancel", (int)(cancelRect.x+(160-MeasureText("Cancel",18))/2), (int)(cancelRect.y+11), 18, WHITE);
        }

        // --- Delete confirmation modal ---
        if (deleteIndex >= 0) {
            const int popW = 480, popH = 260;
            int popX = screenWidth / 2 - popW / 2, popY = screenHeight / 2 - popH / 2;
            DrawRectangle(0, 0, screenWidth, screenHeight, {0,0,0,160});
            DrawRectangle(popX, popY, popW, popH, {40,40,40,255});
            DrawRectangleLines(popX, popY, popW, popH, {150,30,30,255});

            DrawText("Delete World", popX+20, popY+18, 22, WHITE);
            DrawText("This will permanently delete:", popX+20, popY+58, 18, LIGHTGRAY);
            const char* worldName = saveList[deleteIndex].c_str();
            DrawText(TextFormat("\"%s\"", worldName), popX+20, popY+82, 20, YELLOW);
            DrawText("Type the world's name to confirm:", popX+20, popY+118, 17, LIGHTGRAY);

            int boxY = popY + 143;
            DrawRectangle(popX+20, boxY, popW-40, 40, WHITE);
            bool matches = deleteConfirmInput == saveList[deleteIndex];
            DrawRectangleLines(popX+20, boxY, popW-40, 40, matches ? Color{200,50,50,255} : DARKGRAY);
            DrawText(deleteConfirmInput.c_str(), popX+30, boxY+10, 20, BLACK);
            if (((int)(GetTime()*2)%2)==0) {
                int cursorX = popX+30 + MeasureText(deleteConfirmInput.c_str(), 20);
                DrawText("|", cursorX, boxY+10, 20, BLACK);
            }

            Rectangle confirmRect = {(float)(popX+20),       (float)(popY+popH-60), 180, 40};
            Rectangle cancelRect  = {(float)(popX+popW-200), (float)(popY+popH-60), 180, 40};
            DrawRectangleRec(confirmRect, matches ? (CheckCollisionPointRec(mouse,confirmRect)?Color{200,40,40,255}:Color{160,30,30,255}) : Color{80,40,40,255});
            DrawText("Delete", (int)(confirmRect.x+(180-MeasureText("Delete",18))/2), (int)(confirmRect.y+11), 18, WHITE);
            DrawRectangleRec(cancelRect, CheckCollisionPointRec(mouse,cancelRect) ? Color{80,80,80,255} : Color{60,60,60,255});
            DrawText("Cancel", (int)(cancelRect.x+(180-MeasureText("Cancel",18))/2), (int)(cancelRect.y+11), 18, WHITE);
        }

        EndDrawing();
        return;
    }

    // --- PAUSED ---
    if (state == GameState::PAUSED) {
        drawGameWorld();

        // Dim overlay — use render dimensions to guarantee full framebuffer coverage
        DrawRectangle(0, 0, GetRenderWidth(), GetRenderHeight(), {0, 0, 0, 150});

        const char* pauseTitle = "PAUSED";
        int pauseTitleWidth = MeasureText(pauseTitle, 36);
        DrawText(pauseTitle, screenWidth / 2 - pauseTitleWidth / 2, screenHeight / 2 - 110, 36, WHITE);

        const int btnW = 200, btnH = 45;
        int btnX = screenWidth / 2 - btnW / 2;

        // Resume
        int resumeY = screenHeight / 2 - 55;
        DrawRectangle(btnX, resumeY, btnW, btnH, DARKGREEN);
        int resumeTextWidth = MeasureText("Resume", 24);
        DrawText("Resume", btnX + (btnW - resumeTextWidth) / 2, resumeY + (btnH - 24) / 2, 24, WHITE);

        // Save and Exit
        int saveExitY = screenHeight / 2 + 15;
        DrawRectangle(btnX, saveExitY, btnW, btnH, {100, 50, 50, 255});
        int saveExitTextWidth = MeasureText("Save and Exit", 22);
        DrawText("Save and Exit", btnX + (btnW - saveExitTextWidth) / 2, saveExitY + (btnH - 22) / 2, 22, WHITE);

        EndDrawing();
        return;
    }

    // --- PLAYING ---
    drawGameWorld();
    EndDrawing();
}

// ---------------------------------------------------------------------------
// HUD
// ---------------------------------------------------------------------------

void Game::drawUI() {
    float playerTileX = player.position.x / TILE_SIZE;
    float playerTileY = player.position.y / TILE_SIZE;
    float elevation  = sampleNoise(world->elevationNoise,  playerTileX, playerTileY);
    float moisture   = sampleNoise(world->moistureNoise,   playerTileX, playerTileY);
    float vegetation = sampleNoise(world->vegetationNoise, playerTileX, playerTileY);
    int worldTileX = (int)floor(player.position.x / TILE_SIZE);
    int worldTileY = (int)floor(player.position.y / TILE_SIZE);
    int flowVal = world->riverSystem.getFlow(worldTileX, worldTileY);

    DrawFPS(10, 10);
    DrawText(TextFormat("Health: %d", player.health),       10, 40,  20, WHITE);
    DrawText(TextFormat("Chunks: %d", world->getLoadedChunkCount()), 10, 65,  20, YELLOW);
    DrawText(TextFormat("Elevation: %.2f", elevation),      10, 90,  20, WHITE);
    DrawText(TextFormat("Moisture: %.2f", moisture),        10, 115, 20, {100, 200, 150, 255});
    DrawText(TextFormat("Vegetation: %.2f", vegetation),    10, 140, 20, {80, 200, 80, 255});
    Tile* tile = world->getTile(worldTileX, worldTileY);
    const char* tileName = tile ? TILE_NAMES[tile->id] : "Unknown";
    DrawText(TextFormat("Tile: %s", tileName),              10, 190, 20, WHITE);

    // --- Hotbar ---
    const int slotSize = 48;
    const int slotGap  = 4;
    const int numSlots = 10;
    const int invCols = 10, invRows = 5;
    int hotbarW = numSlots * slotSize + (numSlots - 1) * slotGap;
    int hotbarX = screenWidth  / 2 - hotbarW / 2;
    int hotbarY = screenHeight - slotSize - 10;

    // When inventory is open, move hotbar up to sit just below the panel
    int panelW = invCols * slotSize + (invCols - 1) * slotGap + 16;
    int panelH = invRows * slotSize + (invRows - 1) * slotGap + 36;
    int panelX = screenWidth  / 2 - panelW / 2;
    int panelY = screenHeight / 2 - (panelH + slotGap + slotSize) / 2;
    if (inventoryOpen) {
        hotbarY = panelY + panelH + slotGap;
    }

    Vector2 mouse = GetMousePosition();

    for (int i = 0; i < numSlots; i++) {
        int sx = hotbarX + i * (slotSize + slotGap);
        Rectangle slotRect = {(float)sx, (float)hotbarY, (float)slotSize, (float)slotSize};
        DrawRectangle(sx, hotbarY, slotSize, slotSize, {40, 40, 40, 220});

        // Draw item if slot is occupied
        ItemStack& slot = player.hotbar[i];
        if (slot.itemId >= 0 && slot.itemId < (int)itemTextures.size() && itemTexturesLoaded[slot.itemId]) {
            Texture2D& tex = itemTextures[slot.itemId];
            float scale = (float)(slotSize - 8) / (float)(tex.width > tex.height ? tex.width : tex.height);
            int drawW = (int)(tex.width * scale);
            int drawH = (int)(tex.height * scale);
            DrawTextureEx(tex, {(float)(sx + (slotSize - drawW) / 2), (float)(hotbarY + (slotSize - drawH) / 2)}, 0, scale, WHITE);
            if (slot.count > 1)
                DrawText(TextFormat("%d", slot.count), sx + slotSize - 14, hotbarY + slotSize - 16, 14, WHITE);
        }

        // Selection highlight only when inventory is closed; hover highlight when open
        bool hovered = inventoryOpen && CheckCollisionPointRec(mouse, slotRect);
        bool selected = !inventoryOpen && (i == hotbarSlot);
        Color border = hovered ? Color{200, 200, 200, 255} : selected ? Color{255, 200, 50, 255} : Color{80, 80, 80, 255};
        int thick = (hovered || selected) ? 3 : 1;
        DrawRectangleLinesEx(slotRect, thick, border);
        DrawText(TextFormat("%d", (i + 1) % 10), sx + 4, hotbarY + 4, 12, {160, 160, 160, 255});
    }

    // --- Inventory panel ---
    if (inventoryOpen) {
        DrawRectangle(panelX, panelY, panelW, panelH, {20, 20, 20, 230});
        DrawRectangleLines(panelX, panelY, panelW, panelH, {100, 100, 100, 255});
        DrawText("Inventory", panelX + 8, panelY + 6, 16, {180, 180, 180, 255});
        for (int row = 0; row < invRows; row++) {
            for (int col = 0; col < invCols; col++) {
                int sx = panelX + 8  + col * (slotSize + slotGap);
                int sy = panelY + 26 + row * (slotSize + slotGap);
                Rectangle slotRect = {(float)sx, (float)sy, (float)slotSize, (float)slotSize};
                DrawRectangle(sx, sy, slotSize, slotSize, {40, 40, 40, 220});

                // Draw item if slot is occupied
                ItemStack& slot = player.inventory[row * invCols + col];
                if (slot.itemId >= 0 && slot.itemId < (int)itemTextures.size() && itemTexturesLoaded[slot.itemId]) {
                    Texture2D& tex = itemTextures[slot.itemId];
                    float scale = (float)(slotSize - 8) / (float)(tex.width > tex.height ? tex.width : tex.height);
                    int drawW = (int)(tex.width * scale);
                    int drawH = (int)(tex.height * scale);
                    DrawTextureEx(tex, {(float)(sx + (slotSize - drawW) / 2), (float)(sy + (slotSize - drawH) / 2)}, 0, scale, WHITE);
                    if (slot.count > 1)
                        DrawText(TextFormat("%d", slot.count), sx + slotSize - 14, sy + slotSize - 16, 14, WHITE);
                }

                bool hovered = CheckCollisionPointRec(mouse, slotRect);
                Color border = hovered ? Color{200, 200, 200, 255} : Color{80, 80, 80, 255};
                int thick = hovered ? 3 : 1;
                DrawRectangleLinesEx(slotRect, thick, border);
            }
        }
    }

    // --- Held item on cursor ---
    if (heldItem.itemId >= 0 && heldItem.itemId < (int)itemTextures.size() && itemTexturesLoaded[heldItem.itemId]) {
        Vector2 mouse = GetMousePosition();
        Texture2D& tex = itemTextures[heldItem.itemId];
        float scale = (float)(slotSize - 8) / (float)(tex.width > tex.height ? tex.width : tex.height);
        int drawW = (int)(tex.width * scale);
        int drawH = (int)(tex.height * scale);
        DrawTextureEx(tex, {mouse.x - drawW / 2.0f, mouse.y - drawH / 2.0f}, 0, scale, WHITE);
        if (heldItem.count > 1)
            DrawText(TextFormat("%d", heldItem.count), (int)mouse.x + 10, (int)mouse.y + 10, 14, WHITE);
    }
}
