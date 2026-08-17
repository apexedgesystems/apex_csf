# ==============================================================================
# demos/apex_spec_demo - the spec-driven demo
#
# Every data structure, the command dispatch, and the component
# skeletons originate from specs: the sensor's inline [[fields]] in
# sensor/apex_data.toml, the actuator's referenced
# actuator/spec_actuator.proto. The demo exists to prove the
# spec-driven path end to end -- both authoring formats -- and to
# serve as the reference for building components from specs.
# ==============================================================================

add_subdirectory(sensor)
add_subdirectory(actuator)
add_subdirectory(bus_driver)
add_subdirectory(matrix)
add_subdirectory(limits)
add_subdirectory(proto_max)
add_subdirectory(channel)
add_subdirectory(exec)
