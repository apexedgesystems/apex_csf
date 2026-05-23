# ==============================================================================
# Module: sim_electronics_chips_intel4004
#
# Intel 4004 microprocessor model. Ships at two production fidelity levels:
#
#   L0: Behavioral CPU (Intel4004Cpu) -- functional reference for all 46
#       opcodes, registers, stack, and carry flag.
#
#   L1: Component hybrid (Intel4004GridLevel1) -- 1305 NOR gates use Level 1
#       Shichman-Hodges physics validated 0.0000V vs ngspice per-gate, 222
#       pass gates and 610 dynamic storage transistors use binary switch,
#       behavioral timing injection drives clocks and machine states.
#
# Sub-libraries:
#   netlist     - SPICE netlist parser (lajos-4004.spice, 2,242 PMOS)
#   behavioral  - L0 functional CPU model (golden reference)
#   grid        - L1 transistor circuit (binary switch + Level 1 hybrid)
#   gate        - Gate-level extraction utilities (NOR/pass gate topology)
# ==============================================================================

# ------------------------------------------------------------------------------
# Subdirectories
# ------------------------------------------------------------------------------

add_subdirectory(netlist)
add_subdirectory(behavioral)
add_subdirectory(grid)
add_subdirectory(gate)
