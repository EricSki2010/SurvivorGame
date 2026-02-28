#pragma once
#include "raylib.h"
#include "../entities/player.h"
#include "../world/world.h"

class Game {
public:
    int screenWidth;
    int screenHeight;
    Player player;
    World world;
    Camera2D camera;
    bool running;

    Game(int width, int height);
    void update();
    void draw();
    void drawUI();
};
