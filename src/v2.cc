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

#include "../../Current/Bricks/strings/printf.h"
#include "../../Current/Bricks/dflags/dflags.h"
#include "../../Current/Sherlock/sherlock.h"
#include "../../Current/Sherlock/yoda/yoda.h"

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

struct KeyedByClientID : yoda::Padawan {
  std::string row;
  uint64_t col;  // EID
  KeyedByClientID(std::string row = "", uint64_t col = 0) : row(row), col(col) {}
  // Cerealization logic.
  template <typename A>
  void serialize(A& ar) {
    Padawan::serialize(ar);
    ar(CEREAL_NVP(row), CEREAL_NVP(col));
  }
};
CEREAL_REGISTER_TYPE(KeyedByClientID);

namespace DashboardAPIType {

using yoda::API;
using yoda::Dictionary;
using yoda::MatrixEntry;
typedef API<Dictionary<MidichloriansEventWithTimestamp>, MatrixEntry<KeyedByClientID>> DB;

}  // namespace DashboardAPIType

using DashboardAPIType::DB;

// Key extraction logic.
// One of N. -- D.K.
struct Splitter {
  DB& db;

  struct SessionsListPayload {
    std::vector<std::string> sessions;
    template <typename A>
    void serialize(A& ar) {
      ar(CEREAL_NVP(sessions));
    }
  };

  struct SessionDetailsPayload {
    struct Event {
      uint64_t eid_as_uint64;
      std::string uri;
      std::string time_ago;
      std::string time_since_previous_event;
      std::string text;
      Event(const uint64_t eid_as_uint64, const std::string& uri) : eid_as_uint64(eid_as_uint64), uri(uri) {}
      template <typename A>
      void serialize(A& ar) {
        ar(CEREAL_NVP(uri), CEREAL_NVP(time_ago), CEREAL_NVP(time_since_previous_event), CEREAL_NVP(text));
      }
      bool operator<(const Event& rhs) const { return eid_as_uint64 > rhs.eid_as_uint64; }
    };
    std::string up;
    std::vector<Event> event;
    template <typename A>
    void serialize(A& ar) {
      ar(CEREAL_NVP(up), CEREAL_NVP(event));
    }
  };

  explicit Splitter(DB& db) : db(db) {
    HTTP(FLAGS_port).Register(FLAGS_route + "s", [this, &db](Request r) {
      const std::string& key = r.url.query["k"];
      if (key.empty()) {
        db.Transaction([&db](typename DB::T_DATA data) {
                         SessionsListPayload payload;
                         for (const auto cit : yoda::MatrixEntry<KeyedByClientID>::Accessor(data).Rows()) {
                           payload.sessions.push_back("http://localhost:3000/s?k=" + cit.key());
                         }
                         std::sort(std::begin(payload.sessions), std::end(payload.sessions));
                         return payload;
                       },
                       std::move(r));
      } else {
        const auto now_as_uint64 = static_cast<uint64_t>(bricks::time::Now());
        db.Transaction([key, now_as_uint64, &db](typename DB::T_DATA data) {
                         SessionDetailsPayload payload;
                         payload.up = "http://localhost:3000/s";
                         for (const auto cit : yoda::MatrixEntry<KeyedByClientID>::Accessor(data)[key]) {
                           const auto eid_as_uint64 = static_cast<uint64_t>(cit.col);
                           payload.event.push_back(SessionDetailsPayload::Event(
                               eid_as_uint64, Printf("http://localhost:3000/e?q=%llu", eid_as_uint64)));
                         }
                         if (!payload.event.empty()) {
                           std::sort(std::begin(payload.event), std::end(payload.event));
                           for (auto& e : payload.event) {
                             const auto ev = data[static_cast<EID>(e.eid_as_uint64)];
                             e.time_ago = MillisecondIntervalAsString(now_as_uint64 - ev.ms);
                             e.text = ev.Description();
                           }
                           for (size_t i = 0; i + 1 < payload.event.size(); ++i) {
                             payload.event[i].time_since_previous_event = MillisecondIntervalAsString(
                                 ((payload.event[i].eid_as_uint64 - payload.event[i + 1].eid_as_uint64) / 1000),
                                 "same second as the event below",
                                 "the event below + ");
                           }
                           payload.event.back().time_since_previous_event =
                               "a long time ago in a galaxy far far away";
                         }

                         return payload;
                       },
                       std::move(r));
      }
    });
  }

  void RealEvent(EID eid, const MidichloriansEventWithTimestamp& event, typename DB::T_DATA& data) {
    assert(event.e);
    // Start / update / end active sessions.
    const std::string& cid = event.e->client_id;
    if (!cid.empty()) {
      data.Add(KeyedByClientID(cid, static_cast<uint64_t>(eid)));
    }
  }

  void TickEvent(uint64_t ms, typename DB::T_DATA& data) {
    static_cast<void>(ms);
    static_cast<void>(data);
    // End active sessions.
  }
};

// Event listening logic.
struct Listener {
  DB& db;
  Splitter splitter;

  explicit Listener(DB& db) : db(db), splitter(db) {}

  // TODO(dkorolev): The last two parameters should be made optional.
  // TODO(dkorolev): The return value should be made optional.
  bool Entry(const EID eid, size_t, size_t) {
    db.Transaction([this, eid](typename DB::T_DATA data) {
                     // Yep, it's an extra, synchronous, lookup. But this solution is cleaner data-wise.
                     const auto entry = data.Get(eid);
                     if (entry) {
                       // Found in the DB: we have a log-entry-based event.
                       splitter.RealEvent(
                           eid, static_cast<const MidichloriansEventWithTimestamp&>(entry), data);

                     } else {
                       // Not found in the DB: we have a tick event.
                       // Notify each active session whether it's interested in ending itself at this moment,
                       // since some session types do use the "idle time" signal.
                       // Also, this results in the output of the "current" sessions to actually be Current!
                       uint64_t tmp = static_cast<uint64_t>(eid);
                       assert(tmp % 1000 == 999);

                       splitter.TickEvent(static_cast<uint64_t>(eid) / 1000, std::ref(data));
                     }
                   }).Go();
    return true;
  }
};

int main(int argc, char** argv) {
  ParseDFlags(&argc, &argv);

  // "raw" is a raw stream of event identifiers (EID-s).
  // "raw" has tick events interleaved.
  // If a given EID can be found in the database, it's a user event, otherwise it's a tick event.
  // "raw" is to be internally listened to, it is not exposed over HTTP.
  auto raw = sherlock::Stream<EID>("raw");
  HTTP(FLAGS_port).Register(FLAGS_route + "ok", [](Request r) { r("OK\n"); });

  // "db" is a structured Yoda storage of processed events, sessions, and so on.
  // "db" is exposed via HTTP.
  DB db("db");

  // Expose events, without timestamps, under "/log" for subscriptions, and under "/e" for browsing.
  db.ExposeViaHTTP(FLAGS_port, FLAGS_route + "log");
  HTTP(FLAGS_port).Register(FLAGS_route + "e", [&db](Request r) {
    db.GetWithNext(static_cast<EID>(bricks::strings::FromString<uint64_t>(r.url.query["q"])), std::move(r));
  });

  Listener listener(db);
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
