#pragma once
#include <optional>
namespace Star {
// Acknowledgement follows successful persistence. Missing destinations and
// failed writes must retry, even when the desired content has not changed.
template <typename T, typename Exists, typename Write>
bool persistChanged(std::optional<T>& persisted, T const& value, Exists exists, Write write) {
  if(persisted && *persisted==value && exists())return false;
  write(value);
  persisted=value;
  return true;
}
}
