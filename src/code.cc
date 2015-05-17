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

// TODO(sompylasar): Remove when `Base64Decode` is moved to Bricks `cerealize/cerealize.h`.
#include "../Bricks/3party/cereal/include/external/base64.hpp"

// Stored log event structure, to parse the JSON-s.
#include "../SimpleServer/log_collector.h"

// Structured iOS events structure to follow.
#define COMPILE_MIDICHLORIANS_AS_SERVER_SIDE_CODE
#include "../MidichloriansBeta/Current/Midichlorians.h"

DEFINE_int32(port, 8687, "Port to spawn the secret server on.");
DEFINE_string(route, "/secret", "The route to serve the dashboard on.");

DEFINE_int32(initial_tick_wait_ms, 100, "");
DEFINE_int32(tick_interval_ms, 2500, "");

// WARNING: The `static_json_path` is sensitive to the current working directory.
DEFINE_string(static_json_path, "./static/static.json", "The path to the static files bundle.");


// TODO(sompylasar): Move to Bricks `cerealize/cerealize.h`.
namespace bricks {
namespace cerealize {
inline std::string Base64Decode(const std::string& s) {
  return base64::decode(s);
}
}  // namespace cerealize
}  // namespace bricks


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


struct HtmlMaterializeTheme {
  const std::string primary_color = "#64b5f6";
  const std::string primary_color_class = "blue lighten-2";
  
  const std::string primary_color_lighten_10 = "#90caf9";
  const std::string primary_color_lighten_20 = "#bbdefb";
  
  const std::string input_valid_color = "#00e676";
  const std::string input_invalid_color = "#ff1744";
};

struct HtmlMaterializeBody {
  HtmlMaterializeBody() {
    using namespace html;
    {
      NAV nav({{"class", ThreadLocalSingleton<HtmlMaterializeTheme>().primary_color_class}});
      {
        DIV nav_wrapper({{"class", "nav-wrapper container"}});
        {
          A logo({{"class", "brand-logo"}, {"href", FLAGS_route + "/"}}, "MixBoard");
        }
      }
    }
    TEXT("<main>");
  }
  ~HtmlMaterializeBody() {
    using namespace html;
    TEXT("</main>");
    {
      FOOTER footer({{"class", "page-footer blue-grey darken-4"}});
      {
        DIV container({{"class", "container"}});
        {
          DIV copyright({{"class", "footer-copyright"}}, "&copy; 2015 MixBoard");
        }
      }
    }
  }
};

struct HtmlMaterializeSection {
  html::SECTION section;
  html::DIV container;
  html::DIV row;
  html::DIV col;
  
  HtmlMaterializeSection()
    : section({{"class", "section no-pad-bot"}}),
      container({{"class", "container"}}),
      row({{"class", "row center"}, {"style", "margin-bottom: 0;"}}),
      col({{"class", "col s12"}})
  {}
};

void RenderHtmlHead(const std::string& title_text) {
  using namespace html;
  {
    HEAD head;
    META charset({{"charset", "utf-8"}});
    META viewport({{"name", "viewport"}, {"content", "width=device-width, initial-scale=1.0, maximum-scale=1.0, user-scalable=no"}});
    TITLE title(title_text);
    LINK materializecss_style({{"rel", "stylesheet"}, {"href", FLAGS_route + "/static/" + "materialize-0.96.1/css/materialize.min.css"}});
    {
      SCRIPT jquery({{"src", FLAGS_route + "/static/" + "jquery-2.1.4.min.js"}});
    }
    {
      SCRIPT materializecss_script({{"src", FLAGS_route + "/static/" + "materialize-0.96.1/js/materialize.min.js"}});
    }
    {
      using namespace html;
      auto& theme = ThreadLocalSingleton<HtmlMaterializeTheme>();
      STYLE materializecss_overrides(
        // Sticky footer.
        "body {"
        "  display: flex;"
        "  min-height: 100vh;"
        "  flex-direction: column;"
        "}"
        "main {"
        "  flex: 1 0 auto;"
        "}"
        
        // Input placeholder styles.
        "::-webkit-input-placeholder {"
        "  color: " + theme.primary_color_lighten_20 + ";"
        "}"
        ":-moz-placeholder { /* Firefox 18- */"
        "  color: " + theme.primary_color_lighten_20 + ";"
        "}"
        "::-moz-placeholder {  /* Firefox 19+ */"
        "  color: " + theme.primary_color_lighten_20 + ";"
        "}"
        ":-ms-input-placeholder {"
        "  color: " + theme.primary_color_lighten_20 + ";"
        "}"
        
        // Input styles.
        "/* label color */"
        ".input-field label {"
        "  color: " + theme.primary_color + ";"
        "}"
        "/* label focus color */"
        ".input-field input[type=text]:focus + label {"
        "  color: " + theme.primary_color + ";"
        "}"
        "/* label underline focus color */"
        ".input-field input[type=text]:focus {"
        "  border-bottom: 1px solid " + theme.primary_color + ";"
        "  box-shadow: 0 1px 0 0 " + theme.primary_color + ";"
        "}"
        "/* valid color */"
          ".input-field input[type=text].valid {"
        "  border-bottom: 1px solid " + theme.input_valid_color + ";"
        "  box-shadow: 0 1px 0 0 " + theme.input_valid_color + ";"
        "}"
        "/* invalid color */"
        ".input-field input[type=text].invalid {"
        "  border-bottom: 1px solid " + theme.input_invalid_color + ";"
        "  box-shadow: 0 1px 0 0 " + theme.input_invalid_color + ";"
        "}"
        "/* icon prefix focus color */"
        ".input-field .prefix.active {"
        "  color: " + theme.primary_color + ";"
        "}"
        
        // Button styles.
        ".btn, .btn-large {"
        "  background-color: " + theme.primary_color + ";"
        "}"
        ".btn:hover, .btn-large:hover {"
        "  background-color: " + theme.primary_color + ";"
        "}"
        "button:focus {"
        "  background-color: " + theme.primary_color_lighten_10 + ";"
        "}"
        "button.btn-flat:focus {"
        "  background-color: transparent;"
        "  color: " + theme.primary_color + ";"
        "}"
        "button.btn-flat.waves-red:focus {"
        "  background-color: #ffcdd2;"
        "  color: #343434;"
        "}"
        
        // Blockquote styles.
        "blockquote {"
        "  text-align: left;"
        "}"
      );
    }
  }
}

void RenderSearchBoxSection(const std::string& q = "") {
  using namespace html;
  HtmlMaterializeSection section;
  const std::string input_id = "search-box";
  FORM form({{"method", "get"}, {"onsubmit", "return !!this.elements['q'].value;"}});
  {
    DIV row({{"class", "row"}, {"style", "margin-bottom: 0;"}});
    {
      DIV col({{"class", "col s9"}});
      {
        DIV input_wrapper({{"class", "input-field"}});
        INPUT input({{"type", "text"},
                     {"id", input_id},
                     {"style", "text-align: center; font-size: 3rem; height: 4rem;"},
                     {"name", "q"},
                     {"value", q},
                     {"required", "required"},
                     {"autocomplete", "off"}});
        {
          LABEL label({{"for", input_id}, {"style", "top: 1.2rem;"}}, "Search Sessions");
        }
      }
    }
    {
      DIV col({{"class", "col s3"}});
      {
        BUTTON button({{"type", "submit"},
                       {"class", "btn btn-large waves-effect waves-light"},
                       {"style", "width: 100%; margin-top: 1rem;"}});
        {
          I icon({{"class", "mdi-action-search right"}});
        }
        {
          SPAN text("Search");
        }
      }
    }
  }
}

void RenderErrorSection(const std::string& error_text = "") {
  using namespace html;
  HtmlMaterializeSection section;
  BLOCKQUOTE error({{"class", "flow-text"}}, error_text);
}

void RenderImageSection() {
  using namespace html;
  HtmlMaterializeSection section;
  // The tiniest transparent pixel image: http://stackoverflow.com/a/12483396 http://probablyprogramming.com/2009/03/15/the-tiniest-gif-ever
  const std::string empty_image_url = "data:image/gif;base64,R0lGODlhAQABAAAAACH5BAEKAAEALAAAAAABAAEAAAICTAEAOw==";
  const std::string image_url = "./mixboard.png"; //?x=" + r.url.query["x"] + "&y=" + r.url.query["y"];
  {
    DIV image_container;
    IMG({{"class", "responsive-img"},
         {"src", image_url},
         {"data-src", image_url},
         {"onload",
           // Sorry for the inline scripts. It's simpler for now. -- sompylasar
           "if (this.src !== '" + empty_image_url + "') {"
           "this.parentNode.lastChild.style.display='none';"
           "}"
         },
         {"onerror",
           // Sorry for the inline scripts. It's simpler for now. -- sompylasar
           "this.src = '" + empty_image_url + "';"
           "this.parentNode.lastChild.style.display='';"
         }});
    {
      DIV image_error({{"style", "display: none;"}});
      {
        DIV image_error_message({{"class", "red-text"}}, "Couldn't load the image.");
      }
      {
        BUTTON reload_button({
            {"type", "button"},
            {"class", "btn-flat waves-effect waves-red"},
            {"onclick",
              // Sorry for the inline scripts. It's simpler for now. -- sompylasar
              "this.parentNode.parentNode.firstChild.src = this.parentNode.parentNode.firstChild.getAttribute('data-src');"
              "this.blur();"
              "return false;"
            }}, "Reload");
      }
    }
  }
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

struct StaticFilesBundle {
  struct File {
    std::string path;
    std::string content;
    
    template <typename A>
    void serialize(A& ar) {
      ar(CEREAL_NVP(path), CEREAL_NVP(content));
    }
  };
  
  std::vector<File> files;
  
  template <typename A>
  void serialize(A& ar) {
    ar(CEREAL_NVP(files));
  }
};

void RegisterStaticFiles() {
  using bricks::FileSystem;
  using bricks::cerealize::ParseJSON;
  using bricks::cerealize::Base64Decode;
  using bricks::net::GetFileMimeType;
  using bricks::net::api::StaticFileServer;
  using bricks::net::CannotServeStaticFilesOfUnknownMIMEType;
  
  // For the JSON file format, see `build-static.js`.
  static auto bundle = ParseJSON<StaticFilesBundle>(FileSystem::ReadFileAsString(FLAGS_static_json_path));
  for (const auto file : bundle.files) {
    std::string content_type(GetFileMimeType(file.path, ""));
    if (content_type.empty()) {
      // TODO(sompylasar): Move to `bricks::net::GetFileMimeType` (`net/mime_type.h`).
      // Web Fonts support. http://www.fantomfactory.org/articles/mime-types-for-web-fonts-in-bedsheet#mimeTypes
      static const std::map<std::string, std::string> file_extension_to_mime_type_map = {
        {"woff", "application/font-woff"},
        {"woff2", "application/font-woff2"},
        {"ttf", "application/font-sfnt"},
        {"otf", "application/font-sfnt"},
        {"eot", "application/vnd.ms-fontobject"}
      };
      std::string extension = FileSystem::GetFileExtension(file.path);
      std::transform(extension.begin(), extension.end(), extension.begin(), ::tolower);
      const auto cit = file_extension_to_mime_type_map.find(extension);
      if (cit != file_extension_to_mime_type_map.end()) {
        content_type = cit->second;
      }
    }
    if (!content_type.empty()) {
      // TODO(sompylasar): Properly convert Windows-style paths to URLs.
      HTTP(FLAGS_port).Register(FLAGS_route + "/static/" + file.path,
          new StaticFileServer(Base64Decode(file.content), content_type));
    } else {
      BRICKS_THROW(CannotServeStaticFilesOfUnknownMIMEType(file.path));
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

  RegisterStaticFiles();

  // HTTP(FLAGS_port).Register(FLAGS_route + "/", [](Request r) { r("OK"); });
  HTTP(FLAGS_port).Register(FLAGS_route + "/status/",
                            [&mmq](Request r) { mmq.EmplaceMessage(new mq::api::Status(std::move(r))); });
  HTTP(FLAGS_port).Register(FLAGS_route + "/mixboard.png",
                            [&mmq](Request r) { mmq.EmplaceMessage(new mq::api::Chart(std::move(r))); });
  HTTP(FLAGS_port).Register(FLAGS_route + "/", [&immutable_state](Request r) {
    using namespace html;
    HTML html_scope;
    RenderHtmlHead("MixBoard Status Page");
    {
      BODY body;
      HtmlMaterializeBody body_template;

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

      RenderSearchBoxSection(r.url.query["q"]);

      if (search_results.empty()) {
        if (user_query.empty()) {
          RenderImageSection();
        }
        else {
          RenderErrorSection("No results for your search. Please try other keywords.");
        }
      } else {
        HtmlMaterializeSection section;
        
        // TODO(dkorolev): UL/LI ?
        TABLE table;
        {
          TR tr;
          {
            TH th("Name");
          }
          {
            TH th("Device ID");
          }
          {
            TH th("Client ID");
          }
          {
            TD th("Advertising ID");
          }
        }
        for (const auto did : search_results) {
          const auto record_cit = immutable_state.record.find(did);
          if (record_cit != immutable_state.record.end()) {
            const mq::Record& record = record_cit->second;
            TR tr;
            {
              TD td;
              A a({{"href", "browse?did=" + did}}, EscapeHtmlEntities(record.name));
            }
            {
              TD td(EscapeHtmlEntities(record.did));
            }  // == lookup key
            {
              TD td(EscapeHtmlEntities(record.cid));
            }
            {
              TD td(EscapeHtmlEntities(record.aid));
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
      RenderHtmlHead("Browse by device");
      {
        BODY body;
        HtmlMaterializeBody body_template;

        RenderSearchBoxSection();

        const auto cit = immutable_state.timeline.find(did);
        if (cit != immutable_state.timeline.end()) {
          typedef std::map<uint64_t, std::set<std::unique_ptr<mq::TimelineEvent>>> TimelineEntry;
          const TimelineEntry& t = cit->second;

          const uint64_t now = static_cast<uint64_t>(bricks::time::Now());

          HtmlMaterializeSection section;
          TABLE table;
          {
            TR tr;
            {
              TH td("Timestamp");
            }
            {
              TH td("Event");
            }
            {
              TH td("Details");
            }
          }
          std::string previous = "";
          for (auto time_cit = t.rbegin(); time_cit != t.rend(); ++time_cit) {
            for (const auto& single_event : time_cit->second) {
              std::string current = single_event->EventAsString() + ' ' + single_event->DetailsAsString();
              if (current != previous) {
                previous = current;
                TR tr;
                {
                  TD td({{"data-timestamp", bricks::strings::ToString(time_cit->first)}}, TimeIntervalAsString(now - time_cit->first) + " ago");
                }
                {
                  TD td(EscapeHtmlEntities(single_event->EventAsString()));
                }
                {
                  TD td(EscapeHtmlEntities(single_event->DetailsAsString()));
                }
              }
            }
          }
        } else {
          RenderErrorSection("Device ID not found.");
          RenderImageSection();
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
