CXX = g++
CXXFLAGS = -std=c++17 -Wall -Isrc/core -Isrc/entities -Isrc/world -Isrc/world/terrain -Isrc/world/rivers
LDFLAGS = -lraylib -lglfw3 -lopengl32 -lgdi32 -lwinmm
SHELL = cmd.exe

SRC_DIR = src
BUILD_DIR = build
TARGET = $(BUILD_DIR)\game.exe

SRCS = $(wildcard $(SRC_DIR)/core/*.cpp) $(wildcard $(SRC_DIR)/entities/*.cpp) $(wildcard $(SRC_DIR)/world/*.cpp) $(wildcard $(SRC_DIR)/world/terrain/*.cpp) $(wildcard $(SRC_DIR)/world/rivers/*.cpp)
OBJS = $(patsubst $(SRC_DIR)/%.cpp, $(BUILD_DIR)/%.o, $(SRCS))

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CXX) $(OBJS) -o $(TARGET) $(LDFLAGS)

$(BUILD_DIR)/%.o: $(SRC_DIR)/%.cpp
	@if not exist "$(dir $@)" mkdir "$(dir $@)"
	$(CXX) $(CXXFLAGS) -c $< -o $@

run: $(TARGET)
	$(TARGET)

clean:
	@if exist $(BUILD_DIR) rmdir /s /q $(BUILD_DIR)

.PHONY: all run clean
