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

// Parse input events from stdin into a Sherlock stream.
// Inject `tick` events based on the timer, not input stream.
// Note: This should be handled by the server receiving events, and by Sherlock. -- D.K.

#ifndef STDIN_PARSE_H
#define STDIN_PARSE_H

#include <atomic>
#include <iostream>

#include "types.h"
#include "helpers.h"

#include "../Current/Bricks/dflags/dflags.h"
#include "../Current/Bricks/strings/printf.h"
#include "../Current/Bricks/template/metaprogramming.h"
#include "../Current/Bricks/time/chrono.h"
#include "../Current/Bricks/waitable_atomic/waitable_atomic.h"
#include "../Current/Sherlock/sherlock.h"
#include "../Current/Sherlock/yoda/yoda.h"

// Stored log event structure, to parse the JSON-s.
#include "../Current/SimpleServer/log_collector.h"

// Parses events from standard input. Expects them to be of type `LogEntry`,
// see `../Current/SimpleServer/log_collector.h`.
struct State {
  const uint64_t start_ms;
  uint64_t last_event_ms = 0;
  size_t total_events = 0;
  size_t total_ticks = 0;

  State() : start_ms(static_cast<uint64_t>(bricks::time::Now())) {}

  template <typename A>
  void save(A& ar) const {
    const uint64_t now_ms = static_cast<uint64_t>(bricks::time::Now());
    ar(cereal::make_nvp("uptime", MillisecondIntervalAsString(now_ms - start_ms)),
       cereal::make_nvp("uptime_ms", now_ms - start_ms),
       CEREAL_NVP(last_event_ms),
       cereal::make_nvp("last_event_age", MillisecondIntervalAsString(now_ms - last_event_ms)),
       cereal::make_nvp("last_event_age_ms", now_ms - last_event_ms),
       CEREAL_NVP(total_events),
       CEREAL_NVP(total_ticks));
  }
};

// `ENTRY_TYPE` should have a two-parameter constructor, from { `timestamp`, `std::move(event)` }.
template <typename HTTP_BODY_BASE_TYPE, typename ENTRY_TYPE, typename YODA>
void BlockingParseLogEventsAndInjectIdleEventsFromStandardInput(sherlock::StreamInstance<EID>& raw,
                                                                YODA& db,
                                                                int port = 0,
                                                                const std::string& route = "") {
  // Maintain and report the state.
  bricks::WaitableAtomic<State> state;
  if (port) {
    HTTP(port)
        .Register(route + "stats", [&state](Request r) { state.ImmutableUse([&r](const State& s) { r(s); }); });
  }

  // A generic way to publish events, interleaved with ticks.
  typedef std::function<void(std::unique_ptr<ENTRY_TYPE> && )> PUBLISH_F;
  uint64_t last_key = 0;
  PUBLISH_F publish_f = [&raw, &db, &last_key, &state](std::unique_ptr<ENTRY_TYPE>&& e0) {
    // Own the event.
    std::unique_ptr<ENTRY_TYPE> e = std::move(e0);

    // Update the state.
    state.MutableUse([&e](State& s) {
      if (e->e) {
        ++s.total_events;
      } else {
        ++s.total_ticks;
      }
      s.last_event_ms = e->ms;
    });

    // Use microseconds timestamp as log entry key.
    // Increment it by one in case the millisecond timestamp of the entry is the same.
    // I think it's safe to assume we're quite a bit under 1M QPS. -- D.K.
    // TODO(dkorolev): "Time went back" logging.
    last_key = std::max(last_key + 1, e->ms * 1000);
    if (!e->e) {
      // Make sure ticks end with '999'.
      // Totally unnecessary, except for convenience and human readability. -- D.K.
      last_key = ((last_key / 1000) * 1000) + 999;
    }
    // const auto eid = static_cast<EID>(8e18 + last_key);  // "800*" is our convention. -- D.K.
    const auto eid = static_cast<EID>(last_key);
    e->key = eid;

    // If it's not the `tick` one, add it to the DB.
    if (e->e) {
      db.Add(*e);
    }

    // Always publish to the raw stream, be it the event or the tick.
    raw.Publish(eid);
  };

  // Parse log events as JSON from standard input until EOF.
  std::string log_entry_as_string;
  LogEntry log_entry;
  std::unique_ptr<HTTP_BODY_BASE_TYPE> log_event;
  while (std::getline(std::cin, log_entry_as_string)) {
    try {
      ParseJSON(log_entry_as_string, log_entry);
      const uint64_t timestamp = log_entry.t;
      if (log_entry.m == "TICK") {
        publish_f(make_unique<ENTRY_TYPE>(timestamp));
      } else {
        try {
          ParseJSON(log_entry.b, log_event);
          publish_f(make_unique<ENTRY_TYPE>(timestamp, std::move(log_event)));
        } catch (const bricks::ParseJSONException&) {
          // TODO(dkorolev): Error logging and stats over sliding windows.
        }
      }
    } catch (const bricks::ParseJSONException&) {
      // TODO(dkorolev): Error logging and stats over sliding windows.
    }
  }
}

#endif  // STDIN_PARSE_H
