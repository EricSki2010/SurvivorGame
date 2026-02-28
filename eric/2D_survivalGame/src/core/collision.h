#pragma once
#include "raylib.h"
#include <unordered_map>
#include <string>

// A collision box in world space
struct CollisionBox {
    Rectangle rect;
};

// Registry of all active solid collision boxes, keyed by a unique ID
inline std::unordered_map<std::string, CollisionBox>& getCollisionBoxes() {
    static std::unordered_map<std::string, CollisionBox> boxes;
    return boxes;
}

inline void addCollisionBox(const std::string& id, Rectangle rect) {
    getCollisionBoxes()[id] = {rect};
}

inline void removeCollisionBox(const std::string& id) {
    getCollisionBoxes().erase(id);
}

inline void clearCollisionBoxes() {
    getCollisionBoxes().clear();
}

// Check if two line segments (a1->a2) and (b1->b2) intersect
inline bool lineSegmentsIntersect(Vector2 a1, Vector2 a2, Vector2 b1, Vector2 b2) {
    float d1x = a2.x - a1.x, d1y = a2.y - a1.y;
    float d2x = b2.x - b1.x, d2y = b2.y - b1.y;
    float cross = d1x * d2y - d1y * d2x;
    if (cross == 0.0f) return false; // parallel

    float dx = b1.x - a1.x, dy = b1.y - a1.y;
    float t = (dx * d2y - dy * d2x) / cross;
    float u = (dx * d1y - dy * d1x) / cross;

    return t >= 0.0f && t <= 1.0f && u >= 0.0f && u <= 1.0f;
}

// Check if a line segment (p1 -> p2) intersects with a rectangle
inline bool lineIntersectsRect(Vector2 p1, Vector2 p2, Rectangle rect) {
    float left = rect.x;
    float right = rect.x + rect.width;
    float top = rect.y;
    float bottom = rect.y + rect.height;

    // Either endpoint inside the rectangle
    if (p1.x >= left && p1.x <= right && p1.y >= top && p1.y <= bottom) return true;
    if (p2.x >= left && p2.x <= right && p2.y >= top && p2.y <= bottom) return true;

    // Line crosses any edge
    Vector2 tl = {left, top};
    Vector2 tr = {right, top};
    Vector2 bl = {left, bottom};
    Vector2 br = {right, bottom};

    if (lineSegmentsIntersect(p1, p2, tl, tr)) return true;  // top
    if (lineSegmentsIntersect(p1, p2, bl, br)) return true;  // bottom
    if (lineSegmentsIntersect(p1, p2, tl, bl)) return true;  // left
    if (lineSegmentsIntersect(p1, p2, tr, br)) return true;  // right

    return false;
}

// Check if moving to a new position would collide with any registered box
inline bool checkMovementCollision(Vector2 from, Vector2 to) {
    for (auto& [id, box] : getCollisionBoxes()) {
        if (lineIntersectsRect(from, to, box.rect)) return true;
    }
    return false;
}

// Returns how far (0.0 to 1.0) along the line from->to before hitting a box.
// 1.0 means no collision (full movement is safe).
// moverSize: the size of the moving box (e.g. player 32x32).
// Expands each collision box by half the mover size so a point sweep is equivalent to box-vs-box.
inline float sweepMovement(Vector2 from, Vector2 to, Vector2 moverSize = {0, 0}) {
    float halfW = moverSize.x / 2.0f;
    float halfH = moverSize.y / 2.0f;
    float closest = 1.0f;
    for (auto& [id, box] : getCollisionBoxes()) {
        Rectangle r = box.rect;
        // Expand collision box by half the mover's size
        float left = r.x - halfW, right = r.x + r.width + halfW;
        float top = r.y - halfH, bottom = r.y + r.height + halfH;

        Vector2 edges[4][2] = {
            {{left, top}, {right, top}},     // top
            {{left, bottom}, {right, bottom}}, // bottom
            {{left, top}, {left, bottom}},   // left
            {{right, top}, {right, bottom}}, // right
        };

        float d1x = to.x - from.x, d1y = to.y - from.y;
        for (int i = 0; i < 4; i++) {
            float d2x = edges[i][1].x - edges[i][0].x;
            float d2y = edges[i][1].y - edges[i][0].y;
            float cross = d1x * d2y - d1y * d2x;
            if (cross == 0.0f) continue;

            float dx = edges[i][0].x - from.x;
            float dy = edges[i][0].y - from.y;
            float t = (dx * d2y - dy * d2x) / cross;
            float u = (dx * d1y - dy * d1x) / cross;

            if (t >= 0.0f && t <= 1.0f && u >= 0.0f && u <= 1.0f) {
                if (t < closest) closest = t;
            }
        }

        // Also check if 'from' is already inside the box
        if (from.x >= left && from.x <= right && from.y >= top && from.y <= bottom) {
            closest = 0.0f;
        }
    }

    // Leave a tiny gap so we don't sit exactly on the edge
    if (closest < 1.0f) closest = (closest > 0.01f) ? closest - 0.01f : 0.0f;
    return closest;
}

// Draw red outlines around all registered collision boxes
inline void drawCollisionBoxes() {
    for (auto& [id, box] : getCollisionBoxes()) {
        DrawRectangleLinesEx(box.rect, 1.0f, RED);
    }
}
