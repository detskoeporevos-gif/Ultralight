#pragma once

#include <cstddef>
#include <string>

#include <Ultralight/String.h>

namespace escfr {

/// Ultralight's String exposes a String8 view (data/length), not std::string.
inline std::string ToStd(const ultralight::String& value) {
  const ultralight::String8& utf8 = value.utf8();
  if (!utf8.data() || utf8.length() == 0)
    return std::string();
  return std::string(utf8.data(), utf8.length());
}

inline ultralight::String FromStd(const std::string& value) {
  return ultralight::String(value.c_str(), value.size());
}

}  // namespace escfr
