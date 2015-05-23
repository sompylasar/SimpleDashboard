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

#include <atomic>
#include <iostream>
#include <algorithm>

#include "html.h"

#include "../../Sherlock/sherlock.h"   // TODO(dkorolev): Submodule.
#include "../../Sherlock/yoda/yoda.h"  // TODO(dkorolev): Submodule.

#include "../Bricks/time/chrono.h"
#include "../Bricks/strings/printf.h"

#include "../Bricks/template/metaprogramming.h"
#include "../Bricks/dflags/dflags.h"

// Stored log event structure, to parse the JSON-s.
#include "../SimpleServer/log_collector.h"

// Structured iOS events structure to follow.
#define COMPILE_MIDICHLORIANS_AS_SERVER_SIDE_CODE
#include "../MidichloriansBeta/Current/Midichlorians.h"

DEFINE_int32(initial_tick_wait_ms, 1000, "");
DEFINE_int32(tick_interval_ms, 100, "");

DEFINE_int32(port, 3000, "Port to spawn the dashboard on.");
DEFINE_string(route, "/", "The route to serve the dashboard on.");

using bricks::strings::Printf;

std::string MillisecondIntervalAsString(uint64_t dt) {
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
    return result;
  } else {
    return "just now";
  }
}

template <typename T, typename D>
std::unique_ptr<T> CloneSerializable(const std::unique_ptr<T, D>& immutable_input) {
  // Yay for JavaScript! CC @sompylasar.
  // TODO(dkorolev): Use binary, not string, serialization, at least.
  // TODO(dkorolev): Unify this code.
  return ParseJSON<std::unique_ptr<T>>(JSON(immutable_input));
}

// `EVENT_KEY` is a monotonically increasing microsecond timestamp,
// computed as "multiply the millisecond timestamp by 1000, keep adding one as necessary."
enum class EVENT_KEY : uint64_t;

struct EventWithTimestamp : yoda::Padawan {
  virtual ~EventWithTimestamp() {}

  EVENT_KEY key = static_cast<EVENT_KEY>(0);
  uint64_t ms = 0;
  std::unique_ptr<MidichloriansEvent> e;
  bricks::time::EPOCH_MILLISECONDS ExtractTimestamp() const {
    return static_cast<bricks::time::EPOCH_MILLISECONDS>(ms);
  }

  EventWithTimestamp() : key(static_cast<EVENT_KEY>(-1)) {}
  EventWithTimestamp(const EventWithTimestamp& rhs) : key(rhs.key), ms(rhs.ms), e(CloneSerializable(rhs.e)) {}

  // Real event.
  EventWithTimestamp(uint64_t ms, std::unique_ptr<MidichloriansEvent>&& e)
      : key(static_cast<EVENT_KEY>(-1)), ms(ms), e(std::move(e)) {}

  // Tick event.
  EventWithTimestamp(uint64_t ms) : key(static_cast<EVENT_KEY>(-1)), ms(ms) {}

  void operator=(const EventWithTimestamp& rhs) {
    key = rhs.key;
    ms = rhs.ms;
    e = CloneSerializable(rhs.e);
  }

  template <typename A>
  void serialize(A& ar) {
    ar(CEREAL_NVP(key), CEREAL_NVP(ms), CEREAL_NVP(e));
  }
};
CEREAL_REGISTER_TYPE(EventWithTimestamp);

// Parses events from standard input. Expects them to be of type `LogEntry`,
// see `../SimpleServer/log_collector.h`
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
void BlockingParseLogEventsAndInjectIdleEventsFromStandardInput(
    sherlock::StreamInstance<std::unique_ptr<ENTRY_TYPE>>& raw,
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
  typedef std::function<void(std::unique_ptr<ENTRY_TYPE>&&)> PUBLISH_F;
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
    // I think it's safe to assume we're not running on 1M QPS. -- D.K.
    // TODO(dkorolev): "Time went back" logging.
    last_key = std::max(last_key + 1, e->ms * 1000);
    e->key = static_cast<EVENT_KEY>(last_key);

    // If it's not the `tick` one, add it to a DB.
    if (e->e) {
      db.Add(*e);
    }

    // Always publish the event.
    raw.Publish(std::move(e));
  };

  // Ensure that tick events are being sent periodically.
  struct TickSender {
    uint64_t tick_frequency = 1000 * 60;  // Ticks every minute seconds.
    uint64_t last_tick_data = 0ull;
    uint64_t last_tick_wall = 0ull;
    bool caught_up = false;
    mutable std::mutex mutex;

    PUBLISH_F publish_f;
    explicit TickSender(PUBLISH_F publish_f) : publish_f(publish_f) {}

    bool Started() const {
      std::lock_guard<std::mutex> guard(mutex);
      return last_tick_data != 0ull;
    }

    void Relax(uint64_t t, bool force) {
      const uint64_t w = static_cast<uint64_t>(bricks::time::Now());
      assert(w >= last_tick_wall);
      std::lock_guard<std::mutex> guard(mutex);
      if (force) {
        last_tick_wall = w;
      } else {
        if (!caught_up) {
          if ((w - last_tick_wall) < 10 * 1000ull) {
            // Do not start idle ticks until more than 10 seconds have passed
            // since the last entry from stdin. Yes, this is a hack.
            // TODO(dkorolev): Reconsider this logic one day.
            return;
          }
          caught_up = true;
        }
        last_tick_wall = w;
      }
      if (!last_tick_data) {
        last_tick_data = (t / tick_frequency) * tick_frequency;
      }
      if (t < last_tick_data) {
        std::cerr << "Time went back from " << last_tick_data << " to " << t << "( by " << (last_tick_data - t)
                  << " ms.)" << std::endl;
        ::exit(-1);
      } else {
        while (last_tick_data + tick_frequency < t) {
          last_tick_data += tick_frequency;
          publish_f(make_unique<ENTRY_TYPE>(last_tick_data));
        }
      }
    }
  };
  TickSender tick_sender(publish_f);

  // Send ticks events once in a while.
  std::atomic_bool stop_ticks(false);
  std::thread timer_thread([&tick_sender, &stop_ticks]() {
    std::this_thread::sleep_for(std::chrono::milliseconds(FLAGS_initial_tick_wait_ms));
    while (!stop_ticks) {
      tick_sender.Relax(static_cast<uint64_t>(bricks::time::Now()), false);
      std::this_thread::sleep_for(std::chrono::milliseconds(FLAGS_tick_interval_ms));
    }
  });

  // Parse log events as JSON from standard input until EOF.
  std::string log_entry_as_string;
  LogEntry log_entry;
  std::unique_ptr<HTTP_BODY_BASE_TYPE> log_event;
  while (std::getline(std::cin, log_entry_as_string)) {
    try {
      ParseJSON(log_entry_as_string, log_entry);
      try {
        ParseJSON(log_entry.b, log_event);
        const uint64_t timestamp = log_entry.t;
        tick_sender.Relax(timestamp, true);
        publish_f(make_unique<ENTRY_TYPE>(timestamp, std::move(log_event)));
      } catch (const bricks::ParseJSONException&) {
        // TODO(dkorolev): Error logging and stats over sliding windows.
      }
    } catch (const bricks::ParseJSONException&) {
      // TODO(dkorolev): Error logging and stats over sliding windows.
    }
  }

  // Graceful shutdown.
  stop_ticks = true;
  timer_thread.join();
}

int main(int argc, char** argv) {
  ParseDFlags(&argc, &argv);

  // "raw" is a raw events stream. Not a Yoda API, not exposed via HTTP.
  // "raw" has tick events interleaved.
  // "raw" is to be internally listened to.
  auto raw = sherlock::Stream<std::unique_ptr<EventWithTimestamp>>("raw");
  HTTP(FLAGS_port).Register(FLAGS_route + "ok", [](Request r) { r("OK\n"); });

  // "db" is a structured Yoda storage of processed events, session, and so on.
  // "db" is exposed via HTTP.
  using yoda::API;
  using yoda::Dictionary;
  typedef API<Dictionary<EventWithTimestamp>> LogsAPI;
  LogsAPI db("db");

  db.ExposeViaHTTP(FLAGS_port, FLAGS_route + "log");
  HTTP(FLAGS_port).Register(FLAGS_route + "e", [&db](Request r) {
    db.GetWithNext(static_cast<EVENT_KEY>(bricks::strings::FromString<uint64_t>(r.url.query["q"])),
                   std::move(r));
  });

  struct Listener {
    // TODO(dkorolev): The remaining two parameters should be made optional.
    // TODO(dkorolev): The return value should be made optional.
    size_t c1 = 0;
    size_t c2 = 0;

    std::set<std::string> sessions;

    Listener() {
      HTTP(FLAGS_port).Register(FLAGS_route + "listener", [this](Request r) {
        r(Printf("Total events seen: %d + %d, different keys: %d\n",
                 static_cast<int>(c1),
                 static_cast<int>(c2),
                 static_cast<int>(sessions.size())));
      });
    }

    bool Entry(std::unique_ptr<EventWithTimestamp>& entry, size_t, size_t) {
      if (entry->e) {
        // A log-entry-based event.
        // Group by key (device_id or client_id).
        // sessions.insert(entry->e->device_id);
        sessions.insert(entry->e->client_id);
        ++c1;
      } else {
        // Tick event.
        // Notify each active session whether it's interested in ending itself at this moment,
        // since some session types do use the "idle time" signal.
        // Also, this results in the output of the "current" sessions to actuay be Current!
        ++c2;
      }
      // printf(".");
      return true;
    }
  };
  Listener listener;
  auto scope = raw.SyncSubscribe(listener);

  // The rest of the logic is handled asynchronously, by the corresponding listeners.
  BlockingParseLogEventsAndInjectIdleEventsFromStandardInput<MidichloriansEvent, EventWithTimestamp>(
      raw, db, FLAGS_port, FLAGS_route);
  // sherlock::StreamInstance<std::unique_ptr<Padawan>>
}
