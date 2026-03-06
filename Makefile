CXX      := g++
CXXFLAGS := -std=c++17 -O3 -g -pthread
INCLUDES := -I.

# 源文件
QUAD_SRC := demo/quad.cpp
CUBE_SRC := demo/cube.cpp

# 输出目录
BUILD_DIR    := build
QUAD_PPM_DIR := assets/quad
CUBE_PPM_DIR := assets/cube
MEDIA_DIR    := media

# 二进制输出（放在 build/ 下）
QUAD_BIN := $(BUILD_DIR)/quad
CUBE_BIN := $(BUILD_DIR)/cube

# 头文件依赖（任一变动则重新编译）
RASTERIZER_HEADERS := rasterizer/math.h rasterizer/framebuffer.h \
                      rasterizer/pipeline.h rasterizer/texture.h

.PHONY: all test clean

# ────────────────────────────────────────────────
# 默认目标：仅编译
# ────────────────────────────────────────────────
all: $(QUAD_BIN) $(CUBE_BIN)

$(QUAD_BIN): $(QUAD_SRC) $(RASTERIZER_HEADERS) | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) $(INCLUDES) $< -o $@

$(CUBE_BIN): $(CUBE_SRC) $(RASTERIZER_HEADERS) | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) $(INCLUDES) $< -o $@

# ────────────────────────────────────────────────
# make test：编译 → 渲染 → 合成视频
# PPM 帧写入各自子目录，不预先清空（不强制覆盖旧内容）
# MP4 输出至 media/
# ────────────────────────────────────────────────
test: all | $(QUAD_PPM_DIR) $(CUBE_PPM_DIR) $(MEDIA_DIR)
	@echo ">>> Rendering quad (1920x1080, 180 frames) ..."
	./$(QUAD_BIN)
	@echo ">>> Encoding $(MEDIA_DIR)/output_quad.mp4 ..."
	ffmpeg -framerate 60 -i $(QUAD_PPM_DIR)/frame-%02d.ppm \
		-c:v libx264 -pix_fmt yuv420p $(MEDIA_DIR)/quad.mp4
	@echo ">>> Rendering cube (960x540, 180 frames) ..."
	./$(CUBE_BIN)
	@echo ">>> Encoding $(MEDIA_DIR)/output_cube.mp4 ..."
	ffmpeg -framerate 60 -i $(CUBE_PPM_DIR)/frame-%02d.ppm \
		-c:v libx264 -pix_fmt yuv420p $(MEDIA_DIR)/cube.mp4
	@echo "Done."
	@echo "  $(MEDIA_DIR)/output_quad.mp4  (1920x1080, 60fps, 3s)"
	@echo "  $(MEDIA_DIR)/output_cube.mp4  (960x540,   60fps, 3s)"

# 按需创建目录
$(BUILD_DIR) $(QUAD_PPM_DIR) $(CUBE_PPM_DIR) $(MEDIA_DIR):
	mkdir -p $@

# ────────────────────────────────────────────────
# make clean：删除 build/ 目录（保留 PPM 和视频）
# ────────────────────────────────────────────────
clean:
	rm -rf $(BUILD_DIR)
