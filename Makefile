.NOTPARALLEL:
.PHONY: all build stage1 users check-env run run-gui run-stage1 run-stage1-gui test test-stage1 test-system test-topology-host test-scheduler-host test-smp test-cooperation test-storage-host test-memory-host test-memory-user test-shell-host test-ipc test-syscall-boundaries test-network-host test-network test-log-host test-performance benchmark test-page-fault test-invalid-opcode update-tools reset-data clean distclean

.PHONY: test-parallel-reduction
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
test: test-stage1 test-system test-topology-host test-scheduler-host test-smp test-cooperation test-parallel-reduction test-storage-host test-memory-host test-memory-user test-shell-host test-ipc test-syscall-boundaries test-network-host test-network test-log-host test-performance test-page-fault test-invalid-opcode
	@$(MAKE) --no-print-directory build

test-stage1: build
	@bash scripts/test-stage1.sh

test-system: build
	@bash scripts/test-system.sh

test-topology-host:
	@bash scripts/test-topology-host.sh

test-scheduler-host:
	@bash scripts/test-scheduler-host.sh

test-smp: build
	@bash scripts/test-smp.sh

test-cooperation: build
	@bash scripts/test-cooperation.sh

test-parallel-reduction: build
	@bash scripts/test-parallel-reduction.sh

test-storage-host: build
	@bash scripts/test-storage-host.sh

test-memory-host:
	@bash scripts/test-memory-host.sh

test-memory-user: build
	@bash scripts/test-memory-user.sh

test-shell-host:
	@bash scripts/test-shell-host.sh

test-ipc: build
	@bash scripts/test-ipc.sh

test-syscall-boundaries: build
	@bash scripts/test-syscall-boundaries.sh

test-network-host:
	@bash scripts/test-network-host.sh

test-network: build
	@bash scripts/test-network.sh

test-log-host:
	@bash scripts/test-log-host.sh

test-performance: build
	@bash scripts/test-performance.sh

# Uses explicitly prepared Linux kernel/initramfs; not part of CI.
benchmark: build
	@bash scripts/benchmark.sh

# Update only /bin, with a complete backup; stop QEMU before running this target.
update-tools: build
	@python3 scripts/update-tools.py

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
