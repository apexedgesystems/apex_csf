#ifndef APEX_MK_FIXTURES_CHECK_DATA_DB_WARNING_OTHER_HPP
#define APEX_MK_FIXTURES_CHECK_DATA_DB_WARNING_OTHER_HPP
/**
 * @file Other.hpp
 * @brief Header the warning fixture lets discovery parse; it defines an
 *        enum other than the one the manifest declares.
 */

#include <cstdint>

enum class OtherMode : std::uint8_t { A = 0 };

#endif // APEX_MK_FIXTURES_CHECK_DATA_DB_WARNING_OTHER_HPP
