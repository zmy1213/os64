.NOTPARALLEL:
.PHONY: all build stage1 users check-env run run-gui run-stage1 run-stage1-gui test test-stage1 test-system test-storage-host test-page-fault test-invalid-opcode reset-data clean distclean

all: build
build stage1:
	@bash scripts/build-stage1-image.sh

users:
	@bash scripts/build-user.sh

check-env:
	@bash scripts/check-env.sh

run run-stage1: build
	@bash scripts/run-qemu.sh

run-gui run-stage1-gui: build
	@bash scripts/run-qemu.sh --gui

# Old milestones use the immutable RAM fixture; the system suite uses its own
# temporary IDE data disk and never writes build/data.img.
test: test-stage1 test-system test-storage-host test-page-fault test-invalid-opcode
	@$(MAKE) --no-print-directory build

test-stage1: build
	@bash scripts/test-stage1.sh

test-system: build
	@bash scripts/test-system.sh

test-storage-host: build
	@bash scripts/test-storage-host.sh

test-page-fault:
	@bash scripts/test-page-fault.sh

test-invalid-opcode:
	@bash scripts/test-invalid-opcode.sh

# Reset is explicit. Ordinary build and clean preserve the persistent data disk.
reset-data: build
	@cp build/data_volume.bin build/data.img
	@echo 'Persistent data disk reset to the current template.'

clean:
	@python3 scripts/clean-build.py

distclean:
	@rm -rf build
