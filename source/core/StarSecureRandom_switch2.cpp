#include "StarSecureRandom.hpp"
#include <switch.h>

namespace Star {
ByteArray secureRandomBytes(size_t size) {
  ByteArray bytes;
  bytes.resize(size);
  randomGet(bytes.ptr(), size);
  return bytes;
}
}
