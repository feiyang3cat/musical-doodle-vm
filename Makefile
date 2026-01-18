# TinyVMM - A minimal hypervisor for macOS using Hypervisor.framework
# Educational project demonstrating VM creation on Apple Silicon

CC = clang
AS = as

CFLAGS = -Wall -Wextra -O2 -g
FRAMEWORKS = -framework Hypervisor

# Detect architecture
ARCH := $(shell uname -m)

ifeq ($(ARCH),arm64)
    TARGET = tinyvmm
    GUEST = guest.bin
else
    $(error This project only supports arm64 (Apple Silicon). Detected: $(ARCH))
endif

.PHONY: all clean run

all: $(TARGET) $(GUEST)

$(TARGET): main.c
	$(CC) $(CFLAGS) $(FRAMEWORKS) -o $@ $<
	codesign --entitlements entitlements.plist -s - $@

# Assemble guest code to raw binary
guest.o: guest.S
	$(AS) -o $@ $<

guest.bin: guest.o
	objcopy -O binary $< $@

# Alternative: compile guest inline (no external assembler needed)
# The guest code is embedded in main.c, so this target is optional

run: all
	@echo "Running TinyVMM..."
	@echo "Note: Requires com.apple.security.hypervisor entitlement"
	./$(TARGET)

clean:
	rm -f $(TARGET) guest.o guest.bin
	rm -rf *.dSYM

help:
	@echo "TinyVMM - Minimal macOS Hypervisor Demo"
	@echo ""
	@echo "Targets:"
	@echo "  all   - Build the VMM and guest code"
	@echo "  run   - Build and run the VMM"
	@echo "  clean - Remove build artifacts"
	@echo ""
	@echo "Requirements:"
	@echo "  - macOS 11.0+ on Apple Silicon"
	@echo "  - Xcode Command Line Tools"
	@echo "  - com.apple.security.hypervisor entitlement (for signed builds)"
