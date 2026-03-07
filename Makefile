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

# ────────────────────────────────────────────────
# 渲染 stamp 文件（文件名嵌入 DURATION，解决时长变短时残留旧帧的问题）
#
# 工作原理：
#   - stamp 命名为 .rendered-$(DURATION)s，如 .rendered-3s / .rendered-5s
#   - DURATION 改变 → 旧 stamp 与当前目标名不匹配 → Make 触发重建
#   - 重建时先清空所有 PPM 帧和旧 stamp，再渲染，再 touch 新 stamp
#   - DURATION 不变且二进制未变 → stamp 比二进制新 → 直接跳过渲染
# ────────────────────────────────────────────────
QUAD_STAMP     := $(QUAD_PPM_DIR)/.rendered-$(DURATION)s
CUBE_STAMP     := $(CUBE_PPM_DIR)/.rendered-$(DURATION)s
SHOWCASE_STAMP := $(SHOWCASE_PPM_DIR)/.rendered-$(DURATION)s

$(QUAD_STAMP): $(QUAD_BIN) | $(QUAD_PPM_DIR)
	@echo ">>> Purging $(QUAD_PPM_DIR) (old frames + stamps) ..."
	@rm -f $(QUAD_PPM_DIR)/frame-*.ppm $(QUAD_PPM_DIR)/.rendered-*
	@echo ">>> Rendering quad (1920x1080, $(DURATION)s, $(KFRAMES) frames) ..."
	./$(QUAD_BIN) $(DURATION)
	@touch $@

$(CUBE_STAMP): $(CUBE_BIN) | $(CUBE_PPM_DIR)
	@echo ">>> Purging $(CUBE_PPM_DIR) (old frames + stamps) ..."
	@rm -f $(CUBE_PPM_DIR)/frame-*.ppm $(CUBE_PPM_DIR)/.rendered-*
	@echo ">>> Rendering cube (960x540, $(DURATION)s, $(KFRAMES) frames) ..."
	./$(CUBE_BIN) $(DURATION)
	@touch $@

$(SHOWCASE_STAMP): $(SHOWCASE_BIN) | $(SHOWCASE_PPM_DIR)
	@echo ">>> Purging $(SHOWCASE_PPM_DIR) (old frames + stamps) ..."
	@rm -f $(SHOWCASE_PPM_DIR)/frame-*.ppm $(SHOWCASE_PPM_DIR)/.rendered-*
	@echo ">>> Rendering showcase (960x540, $(DURATION)s, $(KFRAMES) frames) ..."
	./$(SHOWCASE_BIN) $(DURATION)
	@touch $@

# ────────────────────────────────────────────────
# make test：先确保二进制是最新的（显式依赖 all），再渲染 → 合成视频
#   - all 是 phony，每次都会检查源码/头文件时间戳，按需重编译
#   - stamp 比二进制新且 DURATION 未变 → 跳过渲染
#   - MP4 比 stamp 新 → 跳过编码
# ────────────────────────────────────────────────
test: all $(MEDIA_DIR)/quad.mp4 $(MEDIA_DIR)/cube.mp4 $(MEDIA_DIR)/showcase.mp4
	@echo "Done. (DURATION=$(DURATION)s, $(KFRAMES) frames)"
	@echo "  $(MEDIA_DIR)/quad.mp4      (1920x1080, $(FPS)fps, $(DURATION)s)"
	@echo "  $(MEDIA_DIR)/cube.mp4      (960x540,   $(FPS)fps, $(DURATION)s)"
	@echo "  $(MEDIA_DIR)/showcase.mp4  (960x540,   $(FPS)fps, $(DURATION)s)"

# ── 编码阶段（依赖 stamp；MP4 比 stamp 新则跳过）──
$(MEDIA_DIR)/quad.mp4: $(QUAD_STAMP) | $(MEDIA_DIR)
	@echo ">>> Encoding $@ ..."
	ffmpeg -y -framerate $(FPS) -i $(QUAD_PPM_DIR)/frame-%02d.ppm \
		-c:v libx264 -pix_fmt yuv420p $@

$(MEDIA_DIR)/cube.mp4: $(CUBE_STAMP) | $(MEDIA_DIR)
	@echo ">>> Encoding $@ ..."
	ffmpeg -y -framerate $(FPS) -i $(CUBE_PPM_DIR)/frame-%02d.ppm \
		-c:v libx264 -pix_fmt yuv420p $@

$(MEDIA_DIR)/showcase.mp4: $(SHOWCASE_STAMP) | $(MEDIA_DIR)
	@echo ">>> Encoding $@ ..."
	ffmpeg -y -framerate $(FPS) -i $(SHOWCASE_PPM_DIR)/frame-%03d.ppm \
		-c:v libx264 -pix_fmt yuv420p $@

# 按需创建目录
$(BUILD_DIR) $(QUAD_PPM_DIR) $(CUBE_PPM_DIR) $(SHOWCASE_PPM_DIR) $(MEDIA_DIR):
	mkdir -p $@

# ────────────────────────────────────────────────
# make clean：删除 build/ 目录（保留 PPM 和视频）
# ────────────────────────────────────────────────
clean:
	rm -rf $(BUILD_DIR)
