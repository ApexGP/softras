SHELL       := /bin/bash
.SHELLFLAGS := -o pipefail -c

CXX      := g++
CXXFLAGS := -std=c++17 -O3 -g -pthread
INCLUDES := -I.

# 渲染时长（秒），可通过命令行覆盖：make DURATION=5 test
DURATION ?= 3
FPS      := 60
KFRAMES  := $(shell expr $(DURATION) \* $(FPS))

# 源文件
QUAD_SRC     := demo/quad.cpp
CUBE_SRC     := demo/cube.cpp
SHOWCASE_SRC := demo/showcase.cpp

# 输出目录
BUILD_DIR := build
MEDIA_DIR := media

# 二进制输出
QUAD_BIN     := $(BUILD_DIR)/quad
CUBE_BIN     := $(BUILD_DIR)/cube
SHOWCASE_BIN := $(BUILD_DIR)/showcase

# MP4 文件名包含时长，DURATION 变化时自动触发重建
QUAD_MP4     := $(MEDIA_DIR)/quad-$(DURATION)s.mp4
CUBE_MP4     := $(MEDIA_DIR)/cube-$(DURATION)s.mp4
SHOWCASE_MP4 := $(MEDIA_DIR)/showcase-$(DURATION)s.mp4

# 头文件依赖（任一变动则重新编译）
RASTERIZER_HEADERS := rasterizer/math.h rasterizer/framebuffer.h \
                      rasterizer/pipeline.h rasterizer/texture.h

.PHONY: all test clean clean-media

# ────────────────────────────────────────────────
# 默认目标：仅编译
# ────────────────────────────────────────────────
all: $(QUAD_BIN) $(CUBE_BIN) $(SHOWCASE_BIN)

$(QUAD_BIN): $(QUAD_SRC) $(RASTERIZER_HEADERS) | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) $(INCLUDES) $< -o $@

$(CUBE_BIN): $(CUBE_SRC) $(RASTERIZER_HEADERS) | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) $(INCLUDES) $< -o $@

$(SHOWCASE_BIN): $(SHOWCASE_SRC) $(RASTERIZER_HEADERS) | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) $(INCLUDES) $< -o $@

# ────────────────────────────────────────────────
# 渲染 + 编码（单条 pipe 命令，零中间文件）
# ────────────────────────────────────────────────
$(QUAD_MP4): $(QUAD_BIN) | $(MEDIA_DIR)
	@echo ">>> Rendering + encoding quad (1920x1080, $(DURATION)s, $(KFRAMES) frames) ..."
	./$(QUAD_BIN) $(DURATION) | ffmpeg -y -loglevel warning \
		-f image2pipe -vcodec ppm -framerate $(FPS) -i - \
		-c:v libx264 -pix_fmt yuv420p $@

$(CUBE_MP4): $(CUBE_BIN) | $(MEDIA_DIR)
	@echo ">>> Rendering + encoding cube (960x540, $(DURATION)s, $(KFRAMES) frames) ..."
	./$(CUBE_BIN) $(DURATION) | ffmpeg -y -loglevel warning \
		-f image2pipe -vcodec ppm -framerate $(FPS) -i - \
		-c:v libx264 -pix_fmt yuv420p $@

$(SHOWCASE_MP4): $(SHOWCASE_BIN) | $(MEDIA_DIR)
	@echo ">>> Rendering + encoding showcase (960x540, $(DURATION)s, $(KFRAMES) frames) ..."
	./$(SHOWCASE_BIN) $(DURATION) | ffmpeg -y -loglevel warning \
		-f image2pipe -vcodec ppm -framerate $(FPS) -i - \
		-c:v libx264 -pix_fmt yuv420p $@

# ────────────────────────────────────────────────
# make test：编译 + 渲染 + 编码
# ────────────────────────────────────────────────
test: all $(QUAD_MP4) $(CUBE_MP4) $(SHOWCASE_MP4)
	@echo "Done. (DURATION=$(DURATION)s, $(KFRAMES) frames)"
	@echo "  $(QUAD_MP4)      (1920x1080, $(FPS)fps, $(DURATION)s)"
	@echo "  $(CUBE_MP4)      (960x540,   $(FPS)fps, $(DURATION)s)"
	@echo "  $(SHOWCASE_MP4)  (960x540,   $(FPS)fps, $(DURATION)s)"

# 按需创建目录
$(BUILD_DIR) $(MEDIA_DIR):
	mkdir -p $@

# ────────────────────────────────────────────────
# make clean：删除 build/（保留视频）
# make clean-media：同时删除 media/
# ────────────────────────────────────────────────
clean:
	rm -rf $(BUILD_DIR)

clean-media:
	rm -rf $(BUILD_DIR) $(MEDIA_DIR)
