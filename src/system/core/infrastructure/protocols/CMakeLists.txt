# ==============================================================================
# Protocols
# ==============================================================================
# Communication protocol implementations: framing, network, serial, fieldbus,
# CCSDS space protocols, and the Apex wire protocol (aproto).
# ==============================================================================

add_subdirectory(common)
add_subdirectory(aproto)
add_subdirectory(framing)
add_subdirectory(ccsds)
add_subdirectory(fieldbus)
add_subdirectory(i2c)
add_subdirectory(network)
add_subdirectory(serial)
add_subdirectory(spi)
add_subdirectory(wireless)
