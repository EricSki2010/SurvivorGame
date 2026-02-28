#include "raylib.h"
#include "game.h"

int main() {
    const int screenWidth = 1200;
    const int screenHeight = 800;

    InitWindow(screenWidth, screenHeight, "2D Survival Game");
    SetTargetFPS(60);

    {
        Game game(screenWidth, screenHeight);

        // Main game loop
        while (!WindowShouldClose() && game.running) {
            game.update();
            game.draw();
        }
    } // game destroyed here, textures unloaded while OpenGL context is still alive

    CloseWindow();
    return 0;
}
