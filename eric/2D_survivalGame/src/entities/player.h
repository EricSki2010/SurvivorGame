#pragma once
#include "raylib.h"

class World;  // forward declaration

class Player {
public:
    Vector2 position;
    Vector2 size;
    float speed;
    int health;
    int maxHealth;
    Color color;

    Player(float x, float y);
    void update(float deltaTime, World& world);
    void draw();
    Rectangle getRect();
};
