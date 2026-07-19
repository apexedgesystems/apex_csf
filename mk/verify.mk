# ==============================================================================
# verify.mk - lib.manifest support-contract verification
#
# `make lib-verify LIB=<target>` reads the library's lib.manifest and compiles
# its probe on every declared platform (hosted dialects + MCU toolchains)
# through the docker compose services. See scripts/lib-verify.sh.
# ==============================================================================

.PHONY: lib-verify

lib-verify:
	@scripts/lib-verify.sh $(LIB)
