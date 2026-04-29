QEMU ?= /opt/homebrew/bin/qemu-system-x86_64

.PHONY: all stage1 run-stage1 run-stage1-gui test-stage1 clean

all: stage1

stage1:
	bash scripts/build-stage1-image.sh

run-stage1: stage1
	$(QEMU) \
		-drive format=raw,file=build/disk.img \
		-display none \
		-monitor none \
		-serial stdio \
		-no-reboot \
		-no-shutdown \
		-device isa-debug-exit,iobase=0xf4,iosize=0x04

run-stage1-gui: stage1
	$(QEMU) \
		-drive format=raw,file=build/disk.img \
		-serial stdio \
		-no-reboot \
		-no-shutdown \
		-device isa-debug-exit,iobase=0xf4,iosize=0x04

test-stage1: stage1
	bash scripts/test-stage1.sh

clean:
	rm -rf build
