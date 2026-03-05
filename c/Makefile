# Symphony in C - Makefile
# Uses Cosmopolitan libc for Actually Portable Executables
#
# Build modes:
#   make               - Build with Cosmopolitan (APE binary)
#   make native        - Build with system GCC (for testing)
#   make cosmo         - Explicitly build with Cosmopolitan

# Directories
BUILD_DIR := build
SRC_DIR := src
INCLUDE_DIR := include
VENDORS_DIR := vendors
WEB_DIR := web
COSMO_DIR := $(BUILD_DIR)/cosmopolitan

# Cosmopolitan toolchain
COSMOCC_VERSION := 4.0.2
COSMOCC_URL := https://cosmo.zip/pub/cosmocc/cosmocc-$(COSMOCC_VERSION).zip
COSMOCC_ZIP := $(BUILD_DIR)/cosmocc.zip
COSMOCC := $(COSMO_DIR)/bin/cosmocc
COSMOAR := $(COSMO_DIR)/bin/cosmocaar

# Output binaries
TARGET_APE := symphony.com
TARGET_NATIVE := symphony

# Source files
SRCS := $(wildcard $(SRC_DIR)/*.c)
OBJS_APE := $(SRCS:$(SRC_DIR)/%.c=$(BUILD_DIR)/ape/%.o)
OBJS_NATIVE := $(SRCS:$(SRC_DIR)/%.c=$(BUILD_DIR)/native/%.o)

# Common flags
CFLAGS_COMMON := -Wall -Wextra -Wno-unused-parameter -std=c11 -I$(INCLUDE_DIR) \
                 -D_POSIX_C_SOURCE=200809L -D_GNU_SOURCE

# Cosmopolitan flags
CFLAGS_APE := $(CFLAGS_COMMON) -Os
LDFLAGS_APE := -s

# Native (GCC) flags - for testing
CC_NATIVE := gcc
CFLAGS_NATIVE := $(CFLAGS_COMMON) -g -O0 -DNATIVE_BUILD
LDFLAGS_NATIVE := -lcurl -lpthread

.PHONY: all clean setup download-cosmocc cosmo native check zip-assets

# Default: try native build first (faster for development)
all: native

# Build with Cosmopolitan
cosmo: setup $(TARGET_APE)

# Build with system GCC
native: $(TARGET_NATIVE)

# Download Cosmopolitan toolchain
$(COSMOCC_ZIP):
	@mkdir -p $(BUILD_DIR)
	@echo "Downloading Cosmopolitan toolchain..."
	@curl -fsSL -o $(COSMOCC_ZIP) $(COSMOCC_URL)

$(COSMOCC): $(COSMOCC_ZIP)
	@echo "Extracting Cosmopolitan toolchain..."
	@mkdir -p $(COSMO_DIR)
	@unzip -q -o $(COSMOCC_ZIP) -d $(COSMO_DIR)
	@touch $(COSMOCC)

download-cosmocc: $(COSMOCC)

setup: $(COSMOCC)
	@mkdir -p $(BUILD_DIR)/ape $(BUILD_DIR)/native

# Compile source files (Cosmopolitan)
$(BUILD_DIR)/ape/%.o: $(SRC_DIR)/%.c $(COSMOCC)
	@mkdir -p $(BUILD_DIR)/ape
	@echo "CC [APE] $<"
	@$(COSMOCC) $(CFLAGS_APE) -c $< -o $@

# Compile source files (Native)
$(BUILD_DIR)/native/%.o: $(SRC_DIR)/%.c
	@mkdir -p $(BUILD_DIR)/native
	@echo "CC [native] $<"
	@$(CC_NATIVE) $(CFLAGS_NATIVE) -c $< -o $@

# Link APE executable
$(TARGET_APE): $(OBJS_APE)
	@echo "LD [APE] $(TARGET_APE)"
	@$(COSMOCC) $(CFLAGS_APE) $(LDFLAGS_APE) -o $@ $^

# Link native executable
$(TARGET_NATIVE): $(OBJS_NATIVE)
	@echo "LD [native] $(TARGET_NATIVE)"
	@$(CC_NATIVE) $(CFLAGS_NATIVE) -o $@ $^ $(LDFLAGS_NATIVE)

# Zip web assets into the binary
zip-assets: $(TARGET_APE)
	@echo "Zipping web assets..."
	@cd $(WEB_DIR) && zip -r ../$(TARGET_APE) . -x ".*"

# Check syntax only (fast)
check:
	@echo "Checking syntax..."
	@for f in $(SRCS); do \
		echo "  $$f"; \
		$(CC_NATIVE) $(CFLAGS_NATIVE) -fsyntax-only $$f || exit 1; \
	done
	@echo "All files OK"

clean:
	rm -rf $(BUILD_DIR)
	rm -f $(TARGET_APE) $(TARGET_NATIVE)

# Development helpers
.PHONY: run run-native test lint

run: $(TARGET_APE)
	./$(TARGET_APE)

run-native: $(TARGET_NATIVE)
	./$(TARGET_NATIVE)

test: $(TARGET_NATIVE)
	@echo "Running tests..."
	./$(TARGET_NATIVE) --help

lint:
	@echo "Linting with cppcheck..."
	@command -v cppcheck >/dev/null 2>&1 && cppcheck --enable=warning $(SRC_DIR)/*.c || echo "cppcheck not installed"
