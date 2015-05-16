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

#include <iostream>
#include <algorithm>

#include "html.h"

#include "../Bricks/mq/inmemory/mq.h"
#include "../Bricks/template/metaprogramming.h"
#include "../Bricks/graph/gnuplot.h"
#include "../Bricks/dflags/dflags.h"

// Stored log event structure, to parse the JSON-s.
#include "../SimpleServer/log_collector.h"

// Structured iOS events structure to follow.
#define COMPILE_MIDICHLORIANS_AS_SERVER_SIDE_CODE
#include "../MidichloriansBeta/Current/Midichlorians.h"

DEFINE_int32(port, 8687, "Port to spawn the secret server on.");
DEFINE_string(route, "/secret", "The route to serve the dashboard on.");

DEFINE_int32(initial_tick_wait_ms, 100, "");
DEFINE_int32(tick_interval_ms, 2500, "");

namespace mq {

struct Record {
  std::string did;   // Device ID, copy of the key.
  std::string cid;   // Client ID.
  std::string aid;   // Advertising ID.
  std::string name;  // Device name.
};

struct TimelineEvent {
  uint64_t ms;
  TimelineEvent() = delete;
  explicit TimelineEvent(uint64_t ms) : ms(ms) {}
  virtual ~TimelineEvent() = default;
  virtual std::string EventAsString() { return "<EVENT>"; }
  virtual std::string DetailsAsString() { return "<DETAILS>"; }
};

struct TimelineFocusEvent : TimelineEvent {
  bool gained_focus;
  TimelineFocusEvent(uint64_t ms, bool gained_focus) : TimelineEvent(ms), gained_focus(gained_focus) {}
  virtual std::string EventAsString() { return "Focus"; }
  virtual std::string DetailsAsString() { return gained_focus ? "Activated" : "Backgrounded"; }
};

struct TimelineTitleEvent : TimelineEvent {
  std::string event;
  std::string title;
  TimelineTitleEvent() = delete;
  TimelineTitleEvent(uint64_t ms, const std::string& event, const std::string& title)
      : TimelineEvent(ms), event(event), title(title) {}
  virtual std::string EventAsString() { return event; }
  virtual std::string DetailsAsString() { return title; }
};

struct State {
  const bricks::time::EPOCH_MILLISECONDS start_ms = bricks::time::Now();

  std::map<std::string, size_t> counters_total;
  std::map<std::string, size_t> counters_tick;

  uint64_t abscissa_min = static_cast<uint64_t>(-1);
  uint64_t abscissa_max = static_cast<uint64_t>(0);
  std::map<std::string, std::map<uint64_t, size_t>> events;  // Histogram [event_name][abscissa] = count.

  std::unordered_map<std::string, std::unordered_set<std::string>> reverse_index;  // search term -> [did].
  std::unordered_map<std::string, Record> record;  // did -> info about this device.

  // did -> timestamp -> [event], ordered by timestamp.
  std::unordered_map<std::string, std::map<uint64_t, std::set<std::unique_ptr<TimelineEvent>>>> timeline;

  void IncrementCounter(const std::string& name, size_t delta = 1u) {
    counters_total[name] += delta;
    counters_tick[name] += delta;
  }

  bricks::time::MILLISECONDS_INTERVAL UptimeMs() const { return bricks::time::Now() - start_ms; }

  template <typename A>
  void save(A& ar) const {
    ar(cereal::make_nvp("uptime_ms", static_cast<uint64_t>(UptimeMs())), CEREAL_NVP(counters_total));
  }
};

struct Message {
  virtual void Process(State&) = 0;
};

struct Tick : Message {
  virtual void Process(State& state) {
    // Dump intermediate counters and reset them between ticks.
    std::cout << "uptime=" << static_cast<uint64_t>(state.UptimeMs()) / 1000 << "s";
    auto& counters = state.counters_tick;
    for (const auto cit : counters) {
      std::cout << ' ' << cit.first << '=' << cit.second;
    }
    std::cout << std::endl;
    counters.clear();
  }
  // Do something else.
};

struct Entry : Message {
  const uint64_t ms;
  const std::unique_ptr<MidichloriansEvent> entry;
  typedef std::tuple<iOSIdentifyEvent,
                     iOSDeviceInfo,
                     iOSAppLaunchEvent,
                     iOSFirstLaunchEvent,
                     iOSFocusEvent,
                     iOSGenericEvent,
                     iOSBaseEvent> T_TYPES;
  Entry() = delete;
  Entry(uint64_t ms, std::unique_ptr<MidichloriansEvent>&& entry) : ms(ms), entry(std::move(entry)) {}
  struct Processor {
    const uint64_t ms;
    State& state;
    Processor() = delete;
    Processor(uint64_t ms, State& state) : ms(ms), state(state) {}
    void operator()(const MidichloriansEvent& e) {
      state.IncrementCounter("MidichloriansEvent['" + e.device_id + "','" + e.client_id + "']");
    }
    void operator()(const iOSFocusEvent& e) {
      const std::string did = bricks::strings::ToLower(e.device_id);
      if (did.empty()) {
        std::cerr << "Warning: empty did for `iOSFocusEvent`." << std::endl;
      } else {
        state.timeline[did][ms].insert(
            std::unique_ptr<TimelineEvent>(new TimelineFocusEvent(ms, e.gained_focus)));
      }
    }
    void operator()(const iOSDeviceInfo& e) {
      const std::string did = bricks::strings::ToLower(e.device_id);
      if (did.empty()) {
        std::cerr << "Warning: empty did for `iOSDeviceInfo`." << std::endl;
      } else {
        state.reverse_index[did].insert(did);
        auto& record = state.record[did];
        record.did = e.device_id;
        if (!e.client_id.empty()) {
          state.reverse_index[bricks::strings::ToLower(e.client_id)].insert(did);
          record.cid = e.client_id;
        }
        const auto map = e.info;
        const auto cit = map.find("deviceName");
        if (cit != map.end()) {
          const std::string name = cit->second;
          record.name = name;
          // std::cerr << "Device name: " << name << ' ' << e.device_id << ' ' << e.client_id << std::endl;
          // Build the reverse index: The components should map to `e.device_id`.
          for (const auto cit : bricks::strings::Split(name, ::isalnum)) {
            // std::cerr << bricks::strings::ToLower(cit) << std::endl;
            state.reverse_index[bricks::strings::ToLower(cit)].insert(did);
          }
        }
        const auto cit2 = map.find("advertisingIdentifier");
        if (cit2 != map.end()) {
          state.reverse_index[bricks::strings::ToLower(cit->second)].insert(did);
          record.aid = cit2->second;
        }
      }
    }
    void operator()(const iOSBaseEvent& e) { state.IncrementCounter("iosBaseEvent['" + e.description + "']"); }
    void operator()(const iOSGenericEvent& e) {
      state.IncrementCounter("iosGenericEvent['" + e.event + "','" + e.source + "']");
      const uint64_t abscissa = ms / 1000 / 60 / 60 / 24;
      ++state.events[e.event][abscissa];
      state.abscissa_min = std::min(state.abscissa_min, abscissa);
      state.abscissa_max = std::max(state.abscissa_min, abscissa);

      if (e.fields.count("title")) {  // && e.unparsable_fields.count("url")) {
        std::cerr << "`" << e.device_id << "`, " << ms << " -> " << e.fields.find("title")->second << std::endl;
        state.timeline[bricks::strings::ToLower(e.device_id)][ms].insert(std::unique_ptr<TimelineEvent>(
            new TimelineTitleEvent(ms, e.event, e.fields.find("title")->second)));
      }
    }
  };
  virtual void Process(State& state) {
    bricks::metaprogramming::RTTIDynamicCall<T_TYPES>(*entry.get(), Processor(ms, state));
    state.IncrementCounter("entries_total");
  }
};

struct ParseErrorLogMessage : Message {
  virtual void Process(State& state) { state.IncrementCounter("entries_parse_json_error"); }
};

struct ParseErrorLogRecord : Message {
  virtual void Process(State& state) { state.IncrementCounter("entries_parse_record_error"); }
};

namespace api {

struct Status : Message {
  Request r;
  Status() = delete;
  explicit Status(Request&& r) : r(std::move(r)) {}
  virtual void Process(State& state) { r(state); }
};

struct Chart : Message {
  Request r;
  Chart() = delete;
  explicit Chart(Request&& r) : r(std::move(r)) {}
  virtual void Process(State& state) {
    using namespace bricks::gnuplot;
    if (state.abscissa_min <= state.abscissa_max) {
      const uint64_t current_abscissa = static_cast<uint64_t>(bricks::time::Now()) / 1000 / 60 / 60 / 24;
      // NOTE: `auto& plot = GNUPlot().Dot().Notation()` compiles, but fails miserably.
      GNUPlot plot;
      plot.Grid("back").XLabel("Time, days ago").YLabel("Number of events").ImageSize(1500, 750).OutputFormat(
          "pngcairo");
      // .Title("MixBoard") -- Commented out for a cleaner page. -- D.K.
      for (const auto cit : state.events) {
        // NOTE: `&cit` compiles but won't work since the method is only evaluated at render time.
        plot.Plot(WithMeta([&state, cit, current_abscissa](Plotter& p) {
                             for (uint64_t t = state.abscissa_min; t <= state.abscissa_max; ++t) {
                               const auto cit2 = cit.second.find(t);
                               p(-1.0 * (current_abscissa - t), cit2 != cit.second.end() ? cit2->second : 0);
                             }
                           })
                      .Name(cit.first)
                      .LineWidth(2.5));
      }
      r(plot);
    } else {
      r("No datapoints.");
    }
  }
};

}  // namespace mq::api

struct Consumer {
  void OnMessage(std::unique_ptr<Message>&& message) { message->Process(state); }
  State state;
};

}  // namespace mq

void RenderSearchBox(const std::string& q = "") {
  using namespace html;
  {
    TABLE table({{"border", "0"}, {"align", "center"}});
    TR r;
    TD d({{"align", "center"}});
    FORM form({{"align", "center"}});
    INPUT input({{"type", "text"},
                 {"style", "font-size:50px;text-align:center"},
                 {"name", "q"},
                 {"value", q},
                 {"autocomplete", "off"}});
  }
  TEXT("<br><br>");
}

void RenderImage() {
  using namespace html;
  IMG({{"src", "./mixboard.png"}});  //?x=" + r.url.query["x"] + "&y=" + r.url.query["y"]}});
}

std::string TimeIntervalAsString(uint64_t ms) {
  int s = static_cast<int>(1e-3 * ms + 0.5);
  using bricks::strings::Printf;
  if (s < 60) {
    return Printf("%ds", s);
  } else {
    int m = s / 60;
    s %= 60;
    if (m < 60) {
      return Printf("%dm %ds", m, s);
    } else {
      int h = m / 60;
      m %= 60;
      if (h < 24) {
        return Printf("%dh %dm %ds", h, m, s);
      } else {
        const int d = h / 24;
        h %= 24;
        return Printf("%dd %dh %dm %ds", d, h, m, s);
      }
    }
  }
}

int main(int argc, char** argv) {
  ParseDFlags(&argc, &argv);

  // Thread-safe sequential processing of events of multiple types, namely:
  // 1) External log entries,
  // 2) HTTP requests,
  // 3) Timer events to update console line
  mq::Consumer consumer;
  const mq::State& immutable_state = consumer.state;
  bricks::mq::MMQ<std::unique_ptr<mq::Message>, mq::Consumer> mmq(consumer);

  // HTTP(FLAGS_port).Register(FLAGS_route + "/", [](Request r) { r("OK"); });
  HTTP(FLAGS_port).Register(FLAGS_route + "/status/",
                            [&mmq](Request r) { mmq.EmplaceMessage(new mq::api::Status(std::move(r))); });
  HTTP(FLAGS_port).Register(FLAGS_route + "/mixboard.png",
                            [&mmq](Request r) { mmq.EmplaceMessage(new mq::api::Chart(std::move(r))); });
  HTTP(FLAGS_port).Register(FLAGS_route + "/", [&immutable_state](Request r) {
    using namespace html;
    HTML html_scope;
    {
      HEAD head;
      TITLE("MixBoard Status Page");
    }
    {
      BODY body;

      const std::string user_query = bricks::strings::ToLower(r.url.query["q"]);

      const std::vector<std::string> query = bricks::strings::Split<bricks::strings::ByWhitespace>(user_query);

      std::set<std::string> search_results;

      if (!query.empty()) {
        const auto& rix = immutable_state.reverse_index;
        const auto cit = rix.find(query[0]);
        if (cit != rix.end()) {
          std::set<std::string>(cit->second.begin(), cit->second.end()).swap(search_results);
        }
        for (size_t i = 1u; i < query.size(); ++i) {
          const auto cit = rix.find(query[0]);
          if (cit != rix.end()) {
            std::set<std::string> new_search_results;
            for (const auto r : search_results) {
              if (cit->second.count(r)) {
                new_search_results.insert(r);
              }
            }
            new_search_results.swap(search_results);
          } else {
            search_results.clear();
          }
        }
      }

      for (const auto r : search_results) {
        std::cerr << "[" << user_query << "] = `" << r << "`." << std::endl;
      }

      RenderSearchBox(r.url.query["q"]);

      if (search_results.empty()) {
        RenderImage();
      } else {
        // TODO(dkorolev): UL/LI ?
        TABLE table({{"border", "1"}, {"align", "center"}, {"cellpadding", "8"}});
        {
          TR r({{"align", "center"}});
          {
            TD d;
            B("Name");
          }
          {
            TD d;
            B("Device ID");
          }
          {
            TD d;
            B("Client ID");
          }
          {
            TD d;
            B("Advertising ID");
          }
        }
        for (const auto did : search_results) {
          const auto record_cit = immutable_state.record.find(did);
          if (record_cit != immutable_state.record.end()) {
            const mq::Record& record = record_cit->second;
            TR r({{"align", "center"}});
            {
              TD d1;
              A a({{"href", "browse?did=" + did}});
              TEXT(record.name);
            }
            {
              TD d2;
              TEXT(record.did);
            }  // == lookup key
            {
              TD d3;
              TEXT(record.cid);
            }
            {
              TD d4;
              TEXT(record.aid);
            }
          } else {
            std::cerr << "Warning: No record for `" << did << "`." << std::endl;
          }
        }
      }
    }
    // TODO(dkorolev): (Or John or Max) -- enable Bricks' HTTP server to send custom types via user code.
    r(html_scope.AsString(), HTTPResponseCode.OK, "text/html");
  });

  HTTP(FLAGS_port).Register(FLAGS_route + "/browse", [&immutable_state](Request r) {
    const std::string user_query = bricks::strings::ToLower(r.url.query["q"]);
    if (!user_query.empty()) {
      r("",
        HTTPResponseCode.Found,
        "text/html",
        HTTPHeaders({{"Location", FLAGS_route + "/?q=" + user_query}}));
    } else {
      const std::string did = bricks::strings::ToLower(r.url.query["did"]);

      using namespace html;
      HTML html_scope;
      {
        HEAD head;
        TITLE("Browse by device");
      }
      {
        BODY body;

        RenderSearchBox();

        const auto cit = immutable_state.timeline.find(did);
        if (cit != immutable_state.timeline.end()) {
          typedef std::map<uint64_t, std::set<std::unique_ptr<mq::TimelineEvent>>> TimelineEntry;
          const TimelineEntry& t = cit->second;

          const uint64_t now = static_cast<uint64_t>(bricks::time::Now());

          TABLE table({{"border", "1"}, {"align", "center"}, {"cellpadding", "8"}});
          {
            TR r({{"align", "center"}});
            {
              TD d;
              B("Timestamp");
            }
            {
              TD d;
              B("Event");
            }
            {
              TD d;
              B("Details");
            }
          }
          std::string previous = "";
          for (auto time_cit = t.rbegin(); time_cit != t.rend(); ++time_cit) {
            for (const auto& single_event : time_cit->second) {
              std::string current = single_event->EventAsString() + ' ' + single_event->DetailsAsString();
              if (current != previous) {
                previous = current;
                TR r({{"align", "center"}});
                {
                  TD d;
                  TEXT(TimeIntervalAsString(now - time_cit->first) + " ago");
                }
                {
                  TD d;
                  TEXT(single_event->EventAsString());
                }
                {
                  TD d;
                  TEXT(single_event->DetailsAsString());
                }
              }
            }
          }
        } else {
          B("Device ID not found.");
          TEXT("<br><br>");
          RenderImage();
        }
      }

      r(html_scope.AsString(), HTTPResponseCode.OK, "text/html");
    }
  });

  bool stop_timer = false;
  std::thread timer([&mmq, &stop_timer]() {
    std::this_thread::sleep_for(std::chrono::milliseconds(FLAGS_initial_tick_wait_ms));
    while (!stop_timer) {
      mmq.EmplaceMessage(new mq::Tick());
      std::this_thread::sleep_for(std::chrono::milliseconds(FLAGS_tick_interval_ms));
    }
  });

  std::string log_entry_as_string;
  LogEntry log_entry;
  std::unique_ptr<MidichloriansEvent> log_event;
  while (std::getline(std::cin, log_entry_as_string)) {
    try {
      ParseJSON(log_entry_as_string, log_entry);
      try {
        ParseJSON(log_entry.b, log_event);
        mmq.EmplaceMessage(new mq::Entry(log_entry.t, std::move(log_event)));
      } catch (const bricks::ParseJSONException&) {
        mmq.EmplaceMessage(new mq::ParseErrorLogMessage());
      }
    } catch (const bricks::ParseJSONException&) {
      mmq.EmplaceMessage(new mq::ParseErrorLogRecord());
    }
  }

  stop_timer = true;
  timer.join();
}
