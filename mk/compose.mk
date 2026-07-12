# ==============================================================================
# mk/compose.mk - Docker Compose build wrappers
#
# Runs make targets inside the correct Docker Compose service so developers
# don't have to remember which service maps to which build.
#
# Usage:
#   make compose-debug                          Native debug via dev-cuda
#   make compose-stm32                          STM32 firmware via dev-stm32
#   make compose-stm32 CMAKE_EXTRA_ARGS="-DAPEX_USE_FREERTOS=ON"
#   make compose-stm32-flash STM32_FIRMWARE=stm32_encryptor
#   make compose-jetson-release                 Jetson cross-compile via dev-jetson
# ==============================================================================

ifndef COMPOSE_MK_GUARD
COMPOSE_MK_GUARD := 1

# ------------------------------------------------------------------------------
# Internal Helpers
# ------------------------------------------------------------------------------

# _compose_run: Run a make target inside a docker compose service
# Usage: $(call _compose_run,display_name,service,target[,extra_make_args])
define _compose_run
	$(call log,compose,$(1) [$(2)])
	@docker compose run --rm -T $(2) make $(3) \
	  VERBOSE=$(VERBOSE) CMAKE_EXTRA_ARGS="$(CMAKE_EXTRA_ARGS)" $(4)
endef

# _compose_target: Generate a compose-<suffix> target
# $(1) = target suffix, $(2) = display name, $(3) = service, $(4) = make target
define _compose_target
.PHONY: compose-$(1)
compose-$(1):
	$$(call _compose_run,$(2),$(3),$(4))
endef

# ------------------------------------------------------------------------------
# Native Builds (dev-cuda)
# ------------------------------------------------------------------------------

$(eval $(call _compose_target,debug,native debug,dev-cuda,debug))
$(eval $(call _compose_target,release,native release,dev-cuda,release))
$(eval $(call _compose_target,cuda-debug,native CUDA debug,dev-cuda,cuda-debug))
$(eval $(call _compose_target,cuda-release,native CUDA release,dev-cuda,cuda-release))
$(eval $(call _compose_target,docs,documentation,dev-cuda,docs))
$(eval $(call _compose_target,docs-check,documentation (zero-warning gate),dev-cuda,docs-check))
$(eval $(call _compose_target,profile-configure,configure profile,dev-cuda,profile-configure))
$(eval $(call _compose_target,profile-build,build profile,dev-cuda,profile-build))

# ------------------------------------------------------------------------------
# Testing and Quality (dev-cuda)
# ------------------------------------------------------------------------------

$(eval $(call _compose_target,test,tests (serial),dev-cuda,test))
$(eval $(call _compose_target,testp,tests (parallel),dev-cuda,testp))
$(eval $(call _compose_target,testp-cpu,tests (parallel, no CUDA),dev-cuda,testp-cpu))
$(eval $(call _compose_target,testp-cuda,CUDA tests (parallel),dev-cuda,testp-cuda))
$(eval $(call _compose_target,coverage,coverage,dev-cuda,coverage))
$(eval $(call _compose_target,format,format (auto-fix),dev-cuda,format))
$(eval $(call _compose_target,format-check,format (check only),dev-cuda,format-check))

# Compose wrappers for every registered check (mk/checks.mk), plus sbom and
# notices, are generated from the registry: each `make <check>` gets a matching
# `make compose-<check>` automatically, so adding a check needs no edit here.
# Covers the sanitizers, static analyzers, coverage, and security scanners.
$(foreach c,$(CHECKS_ALL) sbom notices,$(eval $(call _compose_target,$(c),$(c) (dev-cuda),dev-cuda,$(c))))

# ------------------------------------------------------------------------------
# Tools (dev-cuda)
# ------------------------------------------------------------------------------

$(eval $(call _compose_target,tools,all tools,dev-cuda,tools))
$(eval $(call _compose_target,tools-cpp,C++ tools,dev-cuda,tools-cpp))
$(eval $(call _compose_target,tools-py,Python tools,dev-cuda,tools-py))
$(eval $(call _compose_target,tools-rust,Rust tools,dev-cuda,tools-rust))

# ------------------------------------------------------------------------------
# Vernier bench wrapper
#
# Source the build's .env so `bench` (and the other Vernier CLI tools) are on
# PATH, then forward BENCH_ARGS verbatim. Example:
#   make compose-bench BENCH_ARGS='doctor build/hosted-x86_64-debug/bin/ptests/SLIPFraming_PTEST'
#   make compose-bench BENCH_ARGS='profile-all SLIPFraming_PTEST --quick \
#                                  --out _slip_runs --profilers gperf,callgrind'
#   make compose-bench BENCH_ARGS='run SLIPFraming_PTEST -- --profile massif \
#                                  --cycles 100 --gtest_filter=*EncodeClean*'
#
# Bypasses _compose_run because the inner invocation is a sourced shell
# command, not `make <target>`. Cwd is the project root so bench's
# short-name resolver finds binaries under build/*/bin/{ptests,tests,examples}.
#
# Session prep (idempotent, best-effort) makes the privileged backends ready
# without manual setup: lower perf_event_paranoid (perf) and mount debugfs +
# tracefs (bpftrace, offcpu). The container is privileged with NOPASSWD sudo.
#
# Backends that need the bench PROCESS itself to run as root -- bpftrace,
# offcpu, rapl -- take BENCH_SUDO=1, which runs bench under sudo (env + tool
# PATH preserved). Everything else runs as the mapped user.
#   make compose-bench BENCH_SUDO=1 BENCH_ARGS='run Foo --profile offcpu -- ...'
# ------------------------------------------------------------------------------

ifdef BENCH_SUDO
  _bench_cmd  := sudo -E $$(command -v bench)
  # bench ran as root -> hand artifacts back to the host user.
  _bench_post := ; sudo chown -R $(HOST_UID):$(HOST_GID) docs bench-out $(BUILD_DIR) 2>/dev/null || true
else
  _bench_cmd  := bench
  _bench_post :=
endif

.PHONY: compose-bench
compose-bench:
	$(call log,compose,bench [dev-cuda])
	@docker compose run --rm -T dev-cuda bash -c \
	  'sudo sysctl -w kernel.perf_event_paranoid=1 >/dev/null 2>&1 || true; \
	   sudo mount -t debugfs none /sys/kernel/debug 2>/dev/null || true; \
	   sudo mount -t tracefs nodev /sys/kernel/debug/tracing 2>/dev/null || true; \
	   . $(BUILD_DIR)/.env && $(_bench_cmd) $(BENCH_ARGS)$(_bench_post)'

# ------------------------------------------------------------------------------
# Cross-compile + firmware compose wrappers (generated from the platform registry)
# ------------------------------------------------------------------------------
# compose-<target> runs `make <target>` inside the platform's dev service. Host
# wrappers (compose-debug/release/cuda-*) are in the Native Builds section above.

$(foreach p,$(filter-out cpu cuda,$(PLAT_BUILDERS)),\
  $(eval $(call _compose_target,$(P_$(p)_TARGET),$(P_$(p)_DISPLAY),$(P_$(p)_SERVICE),$(P_$(p)_TARGET)))\
  $(eval $(call _compose_target,$(patsubst %-release,%,$(P_$(p)_TARGET))-debug,$(P_$(p)_DISPLAY) (debug),$(P_$(p)_SERVICE),$(patsubst %-release,%,$(P_$(p)_TARGET))-debug)))

# ------------------------------------------------------------------------------
# Size Analysis (bloaty)
# ------------------------------------------------------------------------------
# bloaty lives in apex.base. Route through dev-cuda.

.PHONY: compose-size compose-size-stm32 compose-size-arduino compose-size-pico
.PHONY: compose-size-esp32 compose-size-c2000 compose-size-app compose-size-diff

compose-size:
	$(call _compose_run,size,dev-cuda,size,FILE="$(FILE)" DIM="$(DIM)" N="$(N)")

compose-size-stm32:
	$(call _compose_run,size-stm32,dev-cuda,size-stm32,FW="$(FW)" DIM="$(DIM)" N="$(N)")

compose-size-arduino:
	$(call _compose_run,size-arduino,dev-cuda,size-arduino,FW="$(FW)" DIM="$(DIM)" N="$(N)")

compose-size-pico:
	$(call _compose_run,size-pico,dev-cuda,size-pico,FW="$(FW)" DIM="$(DIM)" N="$(N)")

compose-size-esp32:
	$(call _compose_run,size-esp32,dev-cuda,size-esp32,FW="$(FW)" DIM="$(DIM)" N="$(N)")

compose-size-c2000:
	$(call _compose_run,size-c2000,dev-cuda,size-c2000,FW="$(FW)" DIM="$(DIM)" N="$(N)")

compose-size-app:
	$(call _compose_run,size-app,dev-cuda,size-app,APP="$(APP)" DIM="$(DIM)" N="$(N)")

compose-size-diff:
	$(call _compose_run,size-diff,dev-cuda,size-diff,NEW="$(NEW)" OLD="$(OLD)" DIM="$(DIM)" N="$(N)")

# ------------------------------------------------------------------------------
# Firmware Flash and Reset
# ------------------------------------------------------------------------------
# Kept explicit because each platform passes different device-selector variables.

.PHONY: compose-stm32-flash compose-stm32-reset
compose-stm32-flash:
	$(call _compose_run,STM32 flash,dev-stm32,stm32-flash,STM32_FIRMWARE="$(STM32_FIRMWARE)" STM32_SERIAL="$(STM32_SERIAL)")
compose-stm32-reset:
	$(call _compose_run,STM32 reset,dev-stm32,stm32-reset,STM32_SERIAL="$(STM32_SERIAL)")

.PHONY: compose-arduino-flash compose-arduino-reset
compose-arduino-flash:
	$(call _compose_run,Arduino flash,dev-arduino,arduino-flash,ARDUINO_FIRMWARE="$(ARDUINO_FIRMWARE)" ARDUINO_PORT="$(ARDUINO_PORT)")
compose-arduino-reset:
	$(call _compose_run,Arduino reset,dev-arduino,arduino-reset,ARDUINO_PORT="$(ARDUINO_PORT)")

.PHONY: compose-pico-flash compose-pico-reset
compose-pico-flash:
	$(call _compose_run,Pico flash,dev-pico,pico-flash,PICO_FIRMWARE="$(PICO_FIRMWARE)" PICO_ADDRESS="$(PICO_ADDRESS)")
compose-pico-reset:
	$(call _compose_run,Pico reset,dev-pico,pico-reset,PICO_ADDRESS="$(PICO_ADDRESS)")

.PHONY: compose-esp32-flash compose-esp32-reset
compose-esp32-flash:
	$(call _compose_run,ESP32 flash,dev-esp32,esp32-flash,ESP32_FIRMWARE="$(ESP32_FIRMWARE)" ESP32_PORT="$(ESP32_PORT)")
compose-esp32-reset:
	$(call _compose_run,ESP32 reset,dev-esp32,esp32-reset,ESP32_PORT="$(ESP32_PORT)")

.PHONY: compose-c2000-flash compose-c2000-reset
compose-c2000-flash:
	$(call _compose_run,C2000 flash,dev-c2000,c2000-flash,C2000_FIRMWARE="$(C2000_FIRMWARE)" C2000_CCXML="$(C2000_CCXML)" C2000_SERIAL="$(C2000_SERIAL)")
compose-c2000-reset:
	$(call _compose_run,C2000 reset,dev-c2000,c2000-reset,C2000_CCXML="$(C2000_CCXML)" C2000_SERIAL="$(C2000_SERIAL)")

endif  # COMPOSE_MK_GUARD
