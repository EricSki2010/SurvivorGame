#pragma once
#include "raylib.h"
#include <vector>
#include <string>
#include <unordered_map>

// Object definition (loaded from objects.json)
struct ObjectDef {
    int id;
    std::string name;
    float health;
    Vector2 collisionSize;
    std::string texturePath;
};

// Item definition (loaded from items.json)
struct ItemDef {
    int id;
    std::string name;
    int maxStack;
    std::string texturePath;
};

// Loot table entry (loaded from loot_tables.json)
struct LootEntry {
    int itemId;
    int minCount;
    int maxCount;
    int weight;
};

struct LootTable {
    int objectId;
    std::vector<LootEntry> entries;
};

// Spawn rule (loaded from spawn_rules.json)
struct SpawnRuleDef {
    int objectId;
    int tileId;
    int weight;
};

class GameData {
public:
    std::vector<ObjectDef> objects;
    int objectCount = 0;

    std::vector<ItemDef> items;
    int itemCount = 0;

    std::unordered_map<int, LootTable> lootTables;

    std::vector<SpawnRuleDef> spawnRules;
    int spawnRuleCount = 0;

    bool loadAll(const char* basePath = "assets/data/");

    Vector2 getCollisionSize(int objectId) const;
    int getSpawnJitter(int objectId) const;

private:
    bool loadObjects(const std::string& path);
    bool loadItems(const std::string& path);
    bool loadLootTables(const std::string& path);
    bool loadSpawnRules(const std::string& path);
};

GameData& gData();
