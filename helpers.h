/*******************************************************************************
The MIT License (MIT)

Copyright (c) 2015 Dmitry "Dima" Korolev <dmitry.korolev@gmail.com>

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
*******************************************************************************/

// TODO(dkorolev): Merge repositories and relative paths.

#ifndef HELPERS_H
#define HELPERS_H

#include "../../Current/Bricks/cerealize/cerealize.h"
#include "../../Current/Bricks/strings/printf.h"

template <typename T, typename D>
std::unique_ptr<T> CloneSerializable(const std::unique_ptr<T, D>& immutable_input) {
  // Yay for JavaScript! CC @sompylasar.
  // TODO(dkorolev): Use binary, not string, serialization, at least.
  // TODO(dkorolev): Unify this code.
  // TODO(dkorolev): Move it to, well, Current.
  return ParseJSON<std::unique_ptr<T>>(JSON(immutable_input));
}

inline std::string MillisecondIntervalAsString(uint64_t dt,
                                               const std::string& just_now = "just now",
                                               const std::string& not_just_now_prefix = "") {
  using bricks::strings::Printf;
  dt /= 1000;
  if (dt) {
    std::string result;
    result = Printf("%02ds", static_cast<int>(dt % 60)) + result, dt /= 60;
    if (dt) {
      result = Printf("%02dm ", static_cast<int>(dt % 60)) + result, dt /= 60;
      if (dt) {
        result = Printf("%dh ", static_cast<int>(dt % 24)) + result, dt /= 24;
        if (dt) {
          result = Printf("%dd ", static_cast<int>(dt % 7)) + result, dt /= 7;
          if (dt) {
            result = Printf("%dw ", static_cast<int>(dt)) + result;
          }
        }
      }
    }
    return not_just_now_prefix + result;
  } else {
    return just_now;
  }
}

#endif  // HELPERS_H
