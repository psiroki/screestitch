CC = clang++
CFLAGS = -std=c++20 -nostdlib --target=wasm32 -fno-exceptions -g3
LDFLAGS = -Wl,--no-entry -Wl,--export-all -Wl,--allow-undefined-file=wasm/external.syms
SRC_DIR = wasm
BUILD_DIR = build
WEB_DIR = web
TARGET = screestitch

all: $(WEB_DIR)/$(TARGET).wasm

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

$(BUILD_DIR)/$(TARGET).wasm: $(BUILD_DIR) $(SRC_DIR)/$(TARGET).cc
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $(SRC_DIR)/$(TARGET).cc

$(WEB_DIR)/$(TARGET).wasm: $(BUILD_DIR)/$(TARGET).wasm
	cp $(BUILD_DIR)/$(TARGET).wasm $(WEB_DIR)/$(TARGET).wasm

clean:
	rm -rf $(BUILD_DIR)

.PHONY: all clean
