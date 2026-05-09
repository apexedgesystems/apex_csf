# ==============================================================================
# Module: sim_electronics_topologies
# ==============================================================================
#
# Pre-built circuit topologies that compose the Layer 1 (algorithms) +
# Layer 2 (devices) infrastructure into ready-to-use circuit models. New
# topologies (oscillators, regulators, mixers, ADCs, ...) are added as
# additional sub-libraries here.
#
# Libraries:
#   - gates/        - CMOS logic gates: boolean truth tables and transistor-level
#   - filters/      - Analog filter circuits (RC low-pass, ...)
#   - amplifiers/   - Amplifier topologies (BJT common-emitter, ...)
#
# ==============================================================================

# ------------------------------------------------------------------------------
# Subdirectories
# ------------------------------------------------------------------------------

add_subdirectory(gates)
add_subdirectory(filters)
add_subdirectory(amplifiers)
