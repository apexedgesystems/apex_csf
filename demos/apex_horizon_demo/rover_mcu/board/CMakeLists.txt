# ==============================================================================
# apex_horizon_demo_rover_board
#
# The rover controller as it runs on the NUCLEO: the wire protocol, the board
# controller (shared guidance law + mode / adoption / halt / strobe) and, on
# the host, the link driver that speaks it. The same controller sources link
# into the firmware (board/firmware) and into these host tests.
# ==============================================================================

include(apex/All)

apex_module(apex_horizon_demo_rover_board)

apex_add_library(
  NAME
  ${LIB_NAME}
  TYPE
  STATIC
  INC
  "${INC_DIR}"
  SRC
  "${SRC_DIR}/RoverBoardController.cpp"
  DEPS_PUBLIC
  apex_horizon_demo_rover_controller # RoverGuidance.hpp
  NO_PCH
)

apex_standard_optins(${LIB_NAME})

apex_add_test_subdirs()
