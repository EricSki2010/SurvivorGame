#include "game_data.h"
#include "json.hpp"
#include <fstream>
#include <iostream>

using json = nlohmann::json;

GameData& gData() {
    static GameData instance;
    return instance;
}

static bool loadJsonFile(const std::string& path, json& out) {
    std::ifstream file(path);
    if (!file.is_open()) {
        std::cerr << "Failed to open: " << path << std::endl;
        return false;
    }
    out = json::parse(file);
    return true;
}

bool GameData::loadAll(const char* basePath) {
    std::string base(basePath);
    if (!loadObjects(base + "objects.json")) return false;
    if (!loadItems(base + "items.json")) return false;
    if (!loadLootTables(base + "loot_tables.json")) return false;
    if (!loadSpawnRules(base + "spawn_rules.json")) return false;
    return true;
}

bool GameData::loadObjects(const std::string& path) {
    json j;
    if (!loadJsonFile(path, j)) return false;
    objects.clear();
    for (auto& entry : j) {
        ObjectDef def;
        def.id = entry["id"].get<int>();
        def.name = entry["name"].get<std::string>();
        def.health = entry.value("health", 0.0f);
        def.collisionSize = {
            entry.value("collisionWidth", 0.0f),
            entry.value("collisionHeight", 0.0f)
        };
        def.texturePath = entry.value("texture", std::string(""));
        objects.push_back(def);
    }
    objectCount = (int)objects.size();
    return true;
}

bool GameData::loadItems(const std::string& path) {
    json j;
    if (!loadJsonFile(path, j)) return false;
    items.clear();
    for (auto& entry : j) {
        ItemDef def;
        def.id = entry["id"].get<int>();
        def.name = entry["name"].get<std::string>();
        def.maxStack = entry.value("maxStack", 64);
        def.texturePath = entry.value("texture", std::string(""));
        items.push_back(def);
    }
    itemCount = (int)items.size();
    return true;
}

bool GameData::loadLootTables(const std::string& path) {
    json j;
    if (!loadJsonFile(path, j)) return false;
    lootTables.clear();
    for (auto& entry : j) {
        LootTable table;
        table.objectId = entry["objectId"].get<int>();
        for (auto& drop : entry["drops"]) {
            LootEntry lootEntry;
            lootEntry.itemId = drop["itemId"].get<int>();
            lootEntry.minCount = drop.value("min", 1);
            lootEntry.maxCount = drop.value("max", 1);
            lootEntry.weight = drop.value("weight", 100);
            table.entries.push_back(lootEntry);
        }
        lootTables[table.objectId] = table;
    }
    return true;
}

bool GameData::loadSpawnRules(const std::string& path) {
    json j;
    if (!loadJsonFile(path, j)) return false;
    spawnRules.clear();
    for (auto& entry : j) {
        SpawnRuleDef rule;
        rule.objectId = entry["objectId"].get<int>();
        rule.tileId = entry["tileId"].get<int>();
        rule.weight = entry["weight"].get<int>();
        spawnRules.push_back(rule);
    }
    spawnRuleCount = (int)spawnRules.size();
    return true;
}

Vector2 GameData::getCollisionSize(int objectId) const {
    if (objectId >= 0 && objectId < objectCount)
        return objects[objectId].collisionSize;
    return {0, 0};
}

int GameData::getSpawnJitter(int objectId) const {
    Vector2 col = getCollisionSize(objectId);
    if (col.x == 0 && col.y == 0) return 0;
    return (int)(16.0f - col.x / 2.0f);
}
