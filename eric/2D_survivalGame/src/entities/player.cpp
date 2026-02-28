#include "player.h"
#include "collision.h"

Player::Player(float x, float y) {
    position = {x, y};
    size = {32.0f, 32.0f};
    speed = 200.0f;
    health = 100;
    maxHealth = 100;
    color = BLUE;
}

void Player::update(float deltaTime, World& world) {
    (void)world; // available for future use

    // Movement with WASD (normalized so diagonals aren't faster)
    float dx = 0.0f, dy = 0.0f;
    if (IsKeyDown(KEY_W)) dy -= 1.0f;
    if (IsKeyDown(KEY_S)) dy += 1.0f;
    if (IsKeyDown(KEY_A)) dx -= 1.0f;
    if (IsKeyDown(KEY_D)) dx += 1.0f;
    if (dx != 0.0f && dy != 0.0f) {
        dx *= 0.7071f; // 1/sqrt(2)
        dy *= 0.7071f;
    }

    Vector2 center = {position.x + size.x / 2.0f, position.y + size.y / 2.0f};
    float moveX = dx * speed * deltaTime;
    float moveY = dy * speed * deltaTime;

    // X axis: sweep to find how far we can go
    Vector2 targetX = {center.x + moveX, center.y};
    float tX = sweepMovement(center, targetX, size);
    position.x += moveX * tX;
    center.x += moveX * tX;

    // Y axis: sweep independently so the other axis still slides
    Vector2 targetY = {center.x, center.y + moveY};
    float tY = sweepMovement(center, targetY, size);
    position.y += moveY * tY;
}

void Player::draw() {
    // Draw player
    DrawRectangleV(position, size, color);

    // Draw health bar above player
    float barWidth = size.x;
    float barHeight = 4.0f;
    float barY = position.y - barHeight - 4.0f;
    float healthPercent = (float)health / (float)maxHealth;

    DrawRectangle(position.x, barY, barWidth, barHeight, DARKGRAY);
    DrawRectangle(position.x, barY, barWidth * healthPercent, barHeight, GREEN);
}

Rectangle Player::getRect() {
    return {position.x, position.y, size.x, size.y};
}
