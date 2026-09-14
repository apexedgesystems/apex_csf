#ifndef APEX_STM32_ENCRYPTOR_BOARD_HPP
#define APEX_STM32_ENCRYPTOR_BOARD_HPP
/**
 * @file Board.hpp
 * @brief Selects the board description for the configured NUCLEO.
 *
 * CMake maps APEX_STM32_BOARD to exactly one APEX_BOARD_* definition and
 * this header pulls in the matching description. Everything board-specific
 * (LED, the two USARTs and their pins, clock tree, caches, key-store page)
 * lives in the selected header; main.cpp and KeyStore name only the
 * encryptor::board symbols.
 */

#if defined(APEX_BOARD_NUCLEO_L476RG)
#include "boards/nucleo_l476rg.hpp"
#elif defined(APEX_BOARD_NUCLEO_F767ZI)
#include "boards/nucleo_f767zi.hpp"
#elif defined(APEX_BOARD_NUCLEO_F446RE)
#include "boards/nucleo_f446re.hpp"
#else
#error "No board selected: CMake defines one APEX_BOARD_NUCLEO_* from APEX_STM32_BOARD."
#endif

#endif // APEX_STM32_ENCRYPTOR_BOARD_HPP
