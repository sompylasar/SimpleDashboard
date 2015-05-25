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

#ifndef TYPES_H
#define TYPES_H

#include "helpers.h"

#include "../../Current/Sherlock/yoda/yoda.h"

// `EID`, "Event ID", is a monotonically increasing microsecond timestamp,
// computed as "multiply the millisecond timestamp by 1000, keep adding one as necessary".
//
// Rationale: To fully leverage Yoda for REST-ful access to events, unique event key is needed,
// and, while millisecond timestamp might not be enough (>1K QPS is possible), the microsecond one should do.
// And it still fits 64 bits.
enum class EID : uint64_t;

template <typename E = struct MidichloriansEvent>
struct EventWithTimestamp : yoda::Padawan {
  virtual ~EventWithTimestamp() {}

  EID key = static_cast<EID>(0);
  uint64_t ms = 0;
  std::unique_ptr<E> e;  // If `e` is not set, the event is a metronome tick.

  bricks::time::EPOCH_MILLISECONDS ExtractTimestamp() const {
    return static_cast<bricks::time::EPOCH_MILLISECONDS>(ms);
  }

  EventWithTimestamp() : key(static_cast<EID>(-1)) {}

  // Real event.
  EventWithTimestamp(uint64_t ms, std::unique_ptr<E>&& e)
      : key(static_cast<EID>(-1)), ms(ms), e(std::move(e)) {}

  // Tick event.
  EventWithTimestamp(uint64_t ms) : key(static_cast<EID>(-1)), ms(ms) {}

  // C++ logic to avoid / reduce copies.
  EventWithTimestamp(EventWithTimestamp&& rhs) : key(rhs.key), ms(rhs.ms), e(std::move(rhs.e)) {}
  EventWithTimestamp(const EventWithTimestamp& rhs) : key(rhs.key), ms(rhs.ms), e(CloneSerializable(rhs.e)) {}
  void operator=(const EventWithTimestamp& rhs) {
    key = rhs.key;
    ms = rhs.ms;
    e = CloneSerializable(rhs.e);
  }
  void operator=(EventWithTimestamp&& rhs) {
    key = rhs.key;
    ms = rhs.ms;
    e = std::move(rhs.e);
  }

  // Cerealization logic.
  template <typename A>
  void serialize(A& ar) {
    Padawan::serialize(ar);
    ar(CEREAL_NVP(key), CEREAL_NVP(ms), CEREAL_NVP(e));
  }
};

#endif  // TYPES_H
