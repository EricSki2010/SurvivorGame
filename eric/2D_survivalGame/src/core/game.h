#pragma once
#include "raylib.h"
#include "../entities/player.h"
#include "../world/world.h"
#include <memory>
#include <string>
#include <vector>

enum class GameState {
    MAIN_MENU,
    SEED_INPUT,
    LOAD_MENU,
    PAUSED,
    PLAYING
};

class Game {
public:
    int screenWidth;
    int screenHeight;
    Player player;
    std::unique_ptr<World> world;
    Camera2D camera;
    bool running;
    bool showDebug = true;
    bool inventoryOpen = false;
    int hotbarSlot = 0;
    ItemStack heldItem;
    GameState state = GameState::MAIN_MENU;
    std::string seedInput;
    std::string nameInput;
    int focusedField = 0;   // 0 = name, 1 = seed
    std::string currentSaveName;
    std::vector<std::string> saveList;
    float saveMessageTimer = 0.0f;
    float autoSaveTimer = 0.0f;
    int loadScrollOffset = 0;
    int contextMenuIndex = -1;  // which save has ... open (-1 = none)
    int deleteIndex = -1;       // which save is pending deletion (-1 = none)
    std::string deleteConfirmInput;
    int renameIndex = -1;       // which save is being renamed (-1 = none)
    std::string renameInput;

    std::vector<Texture2D> itemTextures;
    std::vector<bool> itemTexturesLoaded;

    Game(int width, int height);
    void update();
    void draw();
    void drawUI();
    void drawGameWorld();
    void startGame(int seed);
    bool loadGame(const std::string& saveName);
    void saveGame();
    void scanSaves();
    void createSaveFolders(const std::string& name);
    std::string resolveSaveName(const std::string& baseName);
    void toggleFullscreen();
};
