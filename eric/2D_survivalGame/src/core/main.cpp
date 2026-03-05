#include "raylib.h"
#include "game.h"

int main() {
    const int screenWidth = 1200;
    const int screenHeight = 800;

    SetConfigFlags(FLAG_VSYNC_HINT);
    InitWindow(screenWidth, screenHeight, "2D Survival Game");
    SetExitKey(KEY_NULL); // disable raylib's default ESC-closes-window behaviour
    ToggleFullscreen();   // start in fullscreen; Game gets actual monitor dimensions

    {
        Game game(GetRenderWidth(), GetRenderHeight());

        // Main game loop
        while (!WindowShouldClose() && game.running) {
            game.update();
            game.draw();
        }

        // Save if the window was closed mid-game
        if (game.world) game.saveGame();
    } // game destroyed here, textures unloaded while OpenGL context is still alive

    CloseWindow();
    return 0;
}
