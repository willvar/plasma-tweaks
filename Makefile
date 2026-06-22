BUILD_DIR := build

all:
	@cmake -B $(BUILD_DIR) -GNinja -Wno-dev 2>&1 | tail -1
	@ninja -C $(BUILD_DIR)

clean:
	rm -rf $(BUILD_DIR)

run: all
	./$(BUILD_DIR)/plasma-tweaks

.PHONY: all clean run
