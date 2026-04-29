# Use the Homebrew QEMU binary by default so the repo works on this machine
# without extra PATH setup.
QEMU ?= /opt/homebrew/bin/qemu-system-x86_64

.PHONY: all stage1 run-stage1 run-stage1-gui test-stage1 test-page-fault clean

# The default target only builds the current boot image.
all: stage1

# Assemble stage1/stage2 and lay them out into a raw disk image.
stage1:
	bash scripts/build-stage1-image.sh

# Headless run: useful when we only care about serial output in the terminal.
run-stage1: stage1
	$(QEMU) \
		-drive format=raw,file=build/disk.img,if=floppy,index=0 \
		-display none \
		-monitor none \
		-serial stdio \
		-no-reboot \
		-no-shutdown \
		-device isa-debug-exit,iobase=0xf4,iosize=0x04

# GUI run: useful when we want to see the BIOS text screen directly.
run-stage1-gui: stage1
	$(QEMU) \
		-drive format=raw,file=build/disk.img,if=floppy,index=0 \
		-serial stdio \
		-no-reboot \
		-no-shutdown \
		-device isa-debug-exit,iobase=0xf4,iosize=0x04

# Automated regression test: build the image, boot QEMU, and inspect serial log.
test-stage1: stage1
	bash scripts/test-stage1.sh

test-page-fault:
	bash scripts/test-page-fault.sh

# Remove build outputs so the next run starts from a clean image.
clean:
	rm -rf build
