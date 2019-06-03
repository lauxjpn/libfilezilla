#ifndef LIBFILEZILLA_UTIL_HEADER
#define LIBFILEZILLA_UTIL_HEADER

#include "libfilezilla.hpp"
#include "time.hpp"

#include <cstdint>

/** \file
 * \brief Various utility functions
 */

namespace fz {

/** \brief Sleep current thread for the specified \ref duration.
 *
 * Alternative to \c std::this_thread::sleep_for which unfortunately isn't implemented on
 * MinGW.
 *
 * \note May wake up early, e.g. due to a signal. You can use \ref monotonic_clock
 * to check elapsed time and sleep again if needed.
 */
void FZ_PUBLIC_SYMBOL sleep(duration const& d);

/** \brief Get a secure random integer uniformly distributed in the closed interval [min, max]
 */
int64_t FZ_PUBLIC_SYMBOL random_number(int64_t min, int64_t max);

/** \brief Get random uniformly distributed bytes
 */
std::vector<uint8_t> FZ_PUBLIC_SYMBOL random_bytes(size_t size);

/** \brief Returns index of the least-significant set bit
 *
 * \example bitscan(12) returns 2
 *
 * Undefined if called with 0
 */
uint64_t FZ_PUBLIC_SYMBOL bitscan(uint64_t v);

/** \brief Returns index of the most-significant set bit
 *
 * \example bitscan(12) returns 3
 *
 * Undefined if called with 0
 */
uint64_t FZ_PUBLIC_SYMBOL bitscan_reverse(uint64_t v);

}

#endif
