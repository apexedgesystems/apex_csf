# ==============================================================================
# Module: sim_electronics_chips
# ==============================================================================
#
# Chip-scale assemblies -- complete simulation stacks for specific ICs. Each
# chip can ship multiple fidelity tiers (behavioral / gate / transistor) and
# whatever auxiliary infrastructure that chip needs (netlist parser, ROM
# format, peripheral models, etc.).
#
# Subdirectories:
#   - intel4004/   - Intel 4004 microprocessor (L0 / Lg / L1 / L2)
#
# Adding a new chip: create a sub-directory mirroring intel4004's layout
# (behavioral/, grid/, netlist/, optionally gate/), with each tier its own
# library, then register here.
#
# ==============================================================================

# ------------------------------------------------------------------------------
# Subdirectories
# ------------------------------------------------------------------------------

add_subdirectory(intel4004)
