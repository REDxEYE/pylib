//
// Created by i.getsman on 30.03.2020.
//

#ifndef SOURCEIOTEXTUREUTILS_UTILS_HPP

#include <limits>
#define TCB_SPAN_NO_CONTRACT_CHECKING
#include <span.hpp>


#	ifdef __WINDOWS__
#		define PYLIB_DLL_EXPORT __declspec(dllexport)
#	else
#		define PYLIB_DLL_EXPORT __attribute__((visibility("default")))
#	endif

//#ifdef __GNUC__
//#define PYLIB_DLL_EXPORT __attribute__((dllexport))
//#else
//#define PYLIB_DLL_EXPORT __declspec(dllexport)
//#define PYLIB_DLL_IMPORT __declspec(dllimport)
//#endif

#define MIN(a, b) ((a) < (b) ? (a) : (b))
#define MAX(a, b) ((a) > (b) ? (a) : (b))

inline long long round_up(long long value, long long step) {
  return (value + step - 1) / (step * step);
}
static tcb::span<uint8_t>
slice(const tcb::span<uint8_t> &data, uint32_t start,
      uint32_t len = std::numeric_limits<uint32_t>::max()) {
  return data.subspan(start, len);
}

#define SOURCEIOTEXTUREUTILS_UTILS_HPP

#endif // SOURCEIOTEXTUREUTILS_UTILS_HPP
