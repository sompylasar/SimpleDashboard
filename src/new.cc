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

#include "stdin_parse.h"

#include "../../Sherlock/sherlock.h"   // TODO(dkorolev): Submodule.
#include "../../Sherlock/yoda/yoda.h"  // TODO(dkorolev): Submodule.

#include "../Bricks/strings/printf.h"
#include "../Bricks/dflags/dflags.h"

// Structured iOS events structure to follow.
#define COMPILE_MIDICHLORIANS_AS_SERVER_SIDE_CODE
#include "../MidichloriansBeta/Current/Midichlorians.h"

DEFINE_int32(initial_tick_wait_ms, 1000, "");
DEFINE_int32(tick_interval_ms, 100, "");
DEFINE_int32(port, 3000, "Port to spawn the dashboard on.");
DEFINE_string(route, "/", "The route to serve the dashboard on.");

using bricks::strings::Printf;

typedef EventWithTimestamp<MidichloriansEvent> MidichloriansEventWithTimestamp;
CEREAL_REGISTER_TYPE(MidichloriansEventWithTimestamp);

int main(int argc, char** argv) {
  ParseDFlags(&argc, &argv);

  // "raw" is a raw events stream. Not a Yoda API, not exposed via HTTP.
  // "raw" has tick events interleaved.
  // "raw" is to be internally listened to.
  auto raw = sherlock::Stream<std::unique_ptr<MidichloriansEventWithTimestamp>>("raw");
  HTTP(FLAGS_port).Register(FLAGS_route + "ok", [](Request r) { r("OK\n"); });

  // "db" is a structured Yoda storage of processed events, sessions, and so on.
  // "db" is exposed via HTTP.
  using yoda::API;
  using yoda::Dictionary;
  typedef API<Dictionary<MidichloriansEventWithTimestamp>> LogsAPI;
  LogsAPI db("db");

  // Expose events, without timestamps, under "/log" for subscriptions, and under "/e" for browsing.
  db.ExposeViaHTTP(FLAGS_port, FLAGS_route + "log");
  HTTP(FLAGS_port).Register(FLAGS_route + "e", [&db](Request r) {
    db.GetWithNext(static_cast<EID>(bricks::strings::FromString<uint64_t>(r.url.query["q"])), std::move(r));
  });

  // Event listening logic.
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

    bool Entry(std::unique_ptr<MidichloriansEventWithTimestamp>& entry, size_t, size_t) {
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
        // Also, this results in the output of the "current" sessions to actually be Current!
        ++c2;
      }
      // printf(".");
      return true;
    }
  };
  Listener listener;
  auto scope = raw.SyncSubscribe(listener);

  // Read from standard input forever.
  // The rest of the logic is handled asynchronously, by the corresponding listeners.
  BlockingParseLogEventsAndInjectIdleEventsFromStandardInput<MidichloriansEvent,
                                                             MidichloriansEventWithTimestamp>(
      raw, db, FLAGS_initial_tick_wait_ms, FLAGS_tick_interval_ms, FLAGS_port, FLAGS_route);

  // Production code should never reach this point.
  // For non-production code, print an explanatory message before terminating.
  // Not terminating would be a bad idea, since it sure will break production one day. -- D.K.
  std::cerr << "Note: This binary is designed to run forever, and/or be restarted in an infinite loop.\n";
  std::cerr << "In test mode, to run against a small subset of data, consider `tail -f`-ing the input file.\n";
  return -1;
}
