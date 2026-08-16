# ==============================================================================
# ApexSpecDemo - spec-driven demo executive
#
# The component set is born from sensor/apex_data.toml; the exec wires
# it into the standard executive. TPRM masters generate from
# tprm/tprm.manifest with the on-board constraint registry compiled in.
#
# Build:
#   make compose-debug
#
# Run:
#   ./build/hosted-x86_64-debug/bin/ApexSpecDemo \
#     --config build/hosted-x86_64-debug/demos/apex_spec_demo/exec/tprm/master.tprm \
#     --shutdown-after 10
# ==============================================================================

include(apex/All)

set(APP_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/src/main.cpp"
                "${CMAKE_CURRENT_SOURCE_DIR}/src/SpecExecutive.cpp"
)

apex_add_app(
  NAME
  ApexSpecDemo
  SRC
  ${APP_SOURCES}
  INC
  "${CMAKE_CURRENT_SOURCE_DIR}/inc"
  LINK
  apex_spec_demo_sensor
  apex_spec_demo_actuator
  system_core_executive
  system_core_scheduler
  system_core_support_system_monitor
  system_core_logs
  system_core_filesystem
  utilities_helpers
  utilities_concurrency
  fmt::fmt
  REQUIRES
  POSIX
)

# Deployment: single executive; the master generates from the manifest.
apex_add_tprm(
  NAME ApexSpecDemo MANIFEST "${CMAKE_CURRENT_SOURCE_DIR}/../tprm/tprm.manifest" EXEC ApexSpecDemo
)
apex_add_deployment(NAME ApexSpecDemo EXEC ApexSpecDemo TPRM ApexSpecDemo/master.tprm)
