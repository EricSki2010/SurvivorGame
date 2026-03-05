#pragma once
#include "raylib.h"

class World;  // forward declaration

struct ItemStack {
    int itemId = -1;   // -1 = empty
    int count  = 0;
};

class Player {
public:
    Vector2 position;
    Vector2 size;
    float speed;
    int health;
    int maxHealth;
    Color color;

    static const int HOTBAR_SIZE = 10;
    static const int INV_ROWS = 5;
    static const int INV_COLS = 10;
    static const int INV_SIZE = INV_ROWS * INV_COLS; // 50

    ItemStack hotbar[HOTBAR_SIZE];
    ItemStack inventory[INV_SIZE];

    Player(float x, float y);
    void update(float deltaTime, World& world);
    void draw();
    Rectangle getRect();
};
