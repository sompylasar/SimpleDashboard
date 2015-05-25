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

#include "../../Current/Bricks/template/metaprogramming.h"
#include "../../Current/Sherlock/yoda/yoda.h"

// Structured iOS events structure to follow.
#define COMPILE_MIDICHLORIANS_AS_SERVER_SIDE_CODE
#include "../MidichloriansBeta/Current/Midichlorians.h"

// `EID`, "Event ID", is a monotonically increasing microsecond timestamp,
// computed as "multiply the millisecond timestamp by 1000, keep adding one as necessary".
//
// Rationale: To fully leverage Yoda for REST-ful access to events, unique event key is needed,
// and, while millisecond timestamp might not be enough (>1K QPS is possible), the microsecond one should do.
// And it still fits 64 bits.
enum class EID : uint64_t;

typedef std::tuple<iOSIdentifyEvent,
                   iOSDeviceInfo,
                   iOSAppLaunchEvent,
                   iOSFirstLaunchEvent,
                   iOSFocusEvent,
                   iOSGenericEvent,
                   iOSBaseEvent> MIDICHLORIAN_EVENT_TYPES;

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

  // Human-friedly representation.
  std::string Description() const {
    if (!e) {
      return "Tick";
    } else {
      struct Describer {
        std::string text;
        void operator()(iOSIdentifyEvent) { text = "iOSIdentifyEvent"; }
        void operator()(const iOSDeviceInfo& e) {
          const auto cit_model = e.info.find("deviceModel");
          const auto cit_name = e.info.find("deviceName");
          text = "iOSDeviceInfo, " + (cit_model != e.info.end() ? cit_model->second : "unspecified device.") +
                 ", `" + (cit_name != e.info.end() ? cit_name->second : "Unnamed") + "`";
        }
        void operator()(const iOSAppLaunchEvent& e) {
          text = "iOSAppLaunchEvent, binary of `" + e.binary_version + "`";
        }
        void operator()(iOSFirstLaunchEvent) { text = "iOSFirstLaunchEvent"; }
        void operator()(const iOSFocusEvent& e) {
          text = std::string("iOSFocusEvent: ") + (e.gained_focus ? "gained" : "lost");
        }
        void operator()(const iOSGenericEvent& e) {
          text = "iOSGenericEvent, `" + e.event + "`, `" + e.source + "`";
        }
        void operator()(const iOSBaseEvent& e) { text = "iOSBaseEvent, `" + e.description + "`"; }
      };
      Describer d;
      // TODO(dkorolev): Have `RTTIDynamicCall` support `const unique_ptr<>` w/o `*e.get()`.
      bricks::metaprogramming::RTTIDynamicCall<MIDICHLORIAN_EVENT_TYPES>(*e.get(), d);
      return d.text;
    }
  }

  // Canonical event representation, for insights.
  std::string CanonicalDescription() const {
    if (!e) {
      return "";
    } else {
      struct CanonicalDescriber {
        std::string gist;
        void operator()(iOSIdentifyEvent) {}
        void operator()(const iOSDeviceInfo& e) {
          const auto cit_model = e.info.find("deviceModel");
          gist = "iOSDeviceInfo:" + (cit_model != e.info.end() ? cit_model->second : "UNKNOWN");
        }
        void operator()(const iOSAppLaunchEvent& e) {
          gist = "iOSAppLaunchEvent:binary_date=`" + e.binary_version + "`";
        }
        void operator()(iOSFirstLaunchEvent) {}
        void operator()(iOSFocusEvent) {}
        void operator()(const iOSGenericEvent& e) {
          if (e.event != "AppOpen" && e.event != "Backgrounded" && e.event != "MemoryWarning") {
            gist = "iOSGenericEvent:" + e.source + ":`" + e.event + "`";
          }
        }
        void operator()(const iOSBaseEvent& e) { gist = "iOSBaseEvent:`" + e.description + "`"; }
      };
      CanonicalDescriber cd;
      // TODO(dkorolev): Have `RTTIDynamicCall` support `const unique_ptr<>` w/o `*e.get()`.
      bricks::metaprogramming::RTTIDynamicCall<MIDICHLORIAN_EVENT_TYPES>(*e.get(), cd);
      return cd.gist;
    }
  }

  // Cerealization logic.
  template <typename A>
  void serialize(A& ar) {
    Padawan::serialize(ar);
    ar(CEREAL_NVP(key), CEREAL_NVP(ms), CEREAL_NVP(e));
  }
};

#endif  // TYPES_H
