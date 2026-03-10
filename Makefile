SHELL       := /bin/bash
.SHELLFLAGS := -o pipefail -c

CXX      := g++
CXXFLAGS := -std=c++17 -O3 -g -pthread
INCLUDES := -I.

# Render duration in seconds; override on command line: make DURATION=5 test
DURATION ?= 3
FPS      := 60

# Source files
QUAD_SRC     := demo/quad.cpp
CUBE_SRC     := demo/cube.cpp
SHOWCASE_SRC := demo/showcase.cpp
PPM2MP4_SRC  := tools/ppm2mp4.cpp

# Output directories
BUILD_DIR := build
MEDIA_DIR := media

# Binary outputs
QUAD_BIN     := $(BUILD_DIR)/quad
CUBE_BIN     := $(BUILD_DIR)/cube
SHOWCASE_BIN := $(BUILD_DIR)/showcase
PPM2MP4_BIN  := $(BUILD_DIR)/ppm2mp4

# MP4 filenames include duration so changing DURATION triggers a rebuild
QUAD_MP4     := $(MEDIA_DIR)/quad-$(DURATION)s.mp4
CUBE_MP4     := $(MEDIA_DIR)/cube-$(DURATION)s.mp4
SHOWCASE_MP4 := $(MEDIA_DIR)/showcase-$(DURATION)s.mp4

# Header dependencies (any change triggers recompilation)
RASTERIZER_HEADERS := rasterizer/math.h rasterizer/framebuffer.h \
                      rasterizer/pipeline.h rasterizer/texture.h

.PHONY: all test clean clean-media

# ────────────────────────────────────────────────
# Default target: compile only
# ────────────────────────────────────────────────
all: $(QUAD_BIN) $(CUBE_BIN) $(SHOWCASE_BIN) $(PPM2MP4_BIN)

$(QUAD_BIN): $(QUAD_SRC) $(RASTERIZER_HEADERS) | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) $(INCLUDES) $< -o $@

$(CUBE_BIN): $(CUBE_SRC) $(RASTERIZER_HEADERS) | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) $(INCLUDES) $< -o $@

$(SHOWCASE_BIN): $(SHOWCASE_SRC) $(RASTERIZER_HEADERS) | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) $(INCLUDES) $< -o $@

$(PPM2MP4_BIN): $(PPM2MP4_SRC) tools/mp4mux.h | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) $(INCLUDES) $< -o $@ -lx264

# ────────────────────────────────────────────────
# Render + encode (single pipe command, no intermediate files)
# ────────────────────────────────────────────────
$(QUAD_MP4): $(QUAD_BIN) $(PPM2MP4_BIN) | $(MEDIA_DIR)
	@echo ">>> Rendering + encoding quad (1920x1080, $(DURATION)s, $(FPS)fps) ..."
	./$(QUAD_BIN) $(DURATION) | ./$(PPM2MP4_BIN) --fps $(FPS) --duration $(DURATION) -o $@

$(CUBE_MP4): $(CUBE_BIN) $(PPM2MP4_BIN) | $(MEDIA_DIR)
	@echo ">>> Rendering + encoding cube (960x540, $(DURATION)s, $(FPS)fps) ..."
	./$(CUBE_BIN) $(DURATION) | ./$(PPM2MP4_BIN) --fps $(FPS) --duration $(DURATION) -o $@

$(SHOWCASE_MP4): $(SHOWCASE_BIN) $(PPM2MP4_BIN) | $(MEDIA_DIR)
	@echo ">>> Rendering + encoding showcase (960x540, $(DURATION)s, $(FPS)fps) ..."
	./$(SHOWCASE_BIN) $(DURATION) | ./$(PPM2MP4_BIN) --fps $(FPS) --duration $(DURATION) -o $@

# ────────────────────────────────────────────────
# make test: compile + render + encode
# ────────────────────────────────────────────────
test: all $(QUAD_MP4) $(CUBE_MP4) $(SHOWCASE_MP4)
	@echo "Done. (DURATION=$(DURATION)s, FPS=$(FPS))"
	@echo "  $(QUAD_MP4)      (1920x1080, $(FPS)fps, $(DURATION)s)"
	@echo "  $(CUBE_MP4)      (960x540,   $(FPS)fps, $(DURATION)s)"
	@echo "  $(SHOWCASE_MP4)  (960x540,   $(FPS)fps, $(DURATION)s)"

# Create directories on demand
$(BUILD_DIR) $(MEDIA_DIR):
	mkdir -p $@

# ────────────────────────────────────────────────
# make clean:       remove build/ (keep media)
# make clean-media: also remove media/
# ────────────────────────────────────────────────
clean:
	rm -rf $(BUILD_DIR)

clean-media:
	rm -rf $(BUILD_DIR) $(MEDIA_DIR)
