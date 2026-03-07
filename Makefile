CXX      := g++
CXXFLAGS := -std=c++17 -O3 -g -pthread
INCLUDES := -I.

# 渲染时长（秒），可通过命令行覆盖：make DURATION=5 test
DURATION ?= 3
FPS      := 60
# 最后一帧帧号（0-indexed）
LAST_IDX      := $(shell expr $(DURATION) \* $(FPS) - 1)
# 格式化帧号字符串（quad/cube 用 %02d，showcase 用 %03d）
QUAD_LAST_STR := $(shell printf '%02d' $(LAST_IDX))
SHOW_LAST_STR := $(shell printf '%03d' $(LAST_IDX))

# 源文件
QUAD_SRC     := demo/quad.cpp
CUBE_SRC     := demo/cube.cpp
SHOWCASE_SRC := demo/showcase.cpp

# 输出目录
BUILD_DIR        := build
QUAD_PPM_DIR     := assets/quad
CUBE_PPM_DIR     := assets/cube
SHOWCASE_PPM_DIR := assets/showcase
MEDIA_DIR        := media

# 二进制输出（放在 build/ 下）
QUAD_BIN     := $(BUILD_DIR)/quad
CUBE_BIN     := $(BUILD_DIR)/cube
SHOWCASE_BIN := $(BUILD_DIR)/showcase

# 头文件依赖（任一变动则重新编译）
RASTERIZER_HEADERS := rasterizer/math.h rasterizer/framebuffer.h \
                      rasterizer/pipeline.h rasterizer/texture.h

.PHONY: all test clean

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

# 各渲染任务的哨兵：最后一帧 PPM（帧号随 DURATION 动态变化）
QUAD_SENTINEL     := $(QUAD_PPM_DIR)/frame-$(QUAD_LAST_STR).ppm
CUBE_SENTINEL     := $(CUBE_PPM_DIR)/frame-$(QUAD_LAST_STR).ppm
SHOWCASE_SENTINEL := $(SHOWCASE_PPM_DIR)/frame-$(SHOW_LAST_STR).ppm

# ────────────────────────────────────────────────
# make test：先确保二进制是最新的（显式依赖 all），再渲染 → 合成视频
#   - all 是 phony，每次都会检查源码/头文件时间戳，按需重编译
#   - 若哨兵帧比二进制更新，跳过渲染
#   - 若 MP4 比哨兵帧更新，跳过编码
# ────────────────────────────────────────────────
test: all $(MEDIA_DIR)/quad.mp4 $(MEDIA_DIR)/cube.mp4 $(MEDIA_DIR)/showcase.mp4
	@echo "Done. (DURATION=$(DURATION)s, $(shell expr $(DURATION) \* $(FPS)) frames)"
	@echo "  $(MEDIA_DIR)/quad.mp4      (1920x1080, $(FPS)fps, $(DURATION)s)"
	@echo "  $(MEDIA_DIR)/cube.mp4      (960x540,   $(FPS)fps, $(DURATION)s)"
	@echo "  $(MEDIA_DIR)/showcase.mp4  (960x540,   $(FPS)fps, $(DURATION)s)"

# ── 渲染阶段（依赖二进制；哨兵存在且比二进制新则跳过）──
$(QUAD_SENTINEL): $(QUAD_BIN) | $(QUAD_PPM_DIR)
	@echo ">>> Rendering quad (1920x1080, $(DURATION)s) ..."
	./$(QUAD_BIN) $(DURATION)

$(CUBE_SENTINEL): $(CUBE_BIN) | $(CUBE_PPM_DIR)
	@echo ">>> Rendering cube (960x540, $(DURATION)s) ..."
	./$(CUBE_BIN) $(DURATION)

$(SHOWCASE_SENTINEL): $(SHOWCASE_BIN) | $(SHOWCASE_PPM_DIR)
	@echo ">>> Rendering showcase (960x540, $(DURATION)s) ..."
	./$(SHOWCASE_BIN) $(DURATION)

# ── 编码阶段（依赖哨兵帧；MP4 比哨兵新则跳过）──
$(MEDIA_DIR)/quad.mp4: $(QUAD_SENTINEL) | $(MEDIA_DIR)
	@echo ">>> Encoding $@ ..."
	ffmpeg -y -framerate 60 -i $(QUAD_PPM_DIR)/frame-%02d.ppm \
		-c:v libx264 -pix_fmt yuv420p $@

$(MEDIA_DIR)/cube.mp4: $(CUBE_SENTINEL) | $(MEDIA_DIR)
	@echo ">>> Encoding $@ ..."
	ffmpeg -y -framerate 60 -i $(CUBE_PPM_DIR)/frame-%02d.ppm \
		-c:v libx264 -pix_fmt yuv420p $@

$(MEDIA_DIR)/showcase.mp4: $(SHOWCASE_SENTINEL) | $(MEDIA_DIR)
	@echo ">>> Encoding $@ ..."
	ffmpeg -y -framerate 60 -i $(SHOWCASE_PPM_DIR)/frame-%03d.ppm \
		-c:v libx264 -pix_fmt yuv420p $@

# 按需创建目录
$(BUILD_DIR) $(QUAD_PPM_DIR) $(CUBE_PPM_DIR) $(SHOWCASE_PPM_DIR) $(MEDIA_DIR):
	mkdir -p $@

# ────────────────────────────────────────────────
# make clean：删除 build/ 目录（保留 PPM 和视频）
# ────────────────────────────────────────────────
clean:
	rm -rf $(BUILD_DIR)
