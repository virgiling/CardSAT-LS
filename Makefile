BUILD_DIR ?= build
CMAKE_FLAGS ?=

all: release

tools:
	@cd tools/maxSAT_to_KNF && bash build.sh
	@$(MAKE) -C tools/wecnf_to_wcnf

release:
	cmake -S . -B $(BUILD_DIR) -DCMAKE_BUILD_TYPE=Release $(CMAKE_FLAGS)
	cmake --build $(BUILD_DIR) --parallel

debug:
	cmake -S . -B $(BUILD_DIR) -DCMAKE_BUILD_TYPE=Debug $(CMAKE_FLAGS)
	cmake --build $(BUILD_DIR) --parallel

clean:
	@cmake -E remove_directory $(BUILD_DIR)

install: release
	@mkdir -p bin
	cp $(BUILD_DIR)/cardsat bin/cardsat

.PHONY: all tools release debug clean install
