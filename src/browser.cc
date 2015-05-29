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

#include "helpers.h"

#include "insights.h"
CEREAL_REGISTER_TYPE(insight::MutualInformation);

#include "html.h"

#include "../../Current/Bricks/dflags/dflags.h"
#include "../../Current/Bricks/file/file.h"
#include "../../Current/Bricks/net/api/api.h"
#include "../../Current/Bricks/strings/util.h"
#include "../../Current/Bricks/waitable_atomic/waitable_atomic.h"

DEFINE_int32(port, 3000, "Port to spawn the browser on.");
DEFINE_string(route, "/", "The route to serve the browser on.");
DEFINE_string(output_url_prefix, "http://localhost:3000", "The prefix for the URL-s output by the server.");

DEFINE_string(input, "data/insights.json", "Path to the file containing the insights to browse.");
DEFINE_string(id_key, "you_are_awesome", "The URL parameter name containing smart session token ID.");

using bricks::strings::Printf;
using bricks::strings::FromString;
using bricks::strings::ToString;
using bricks::FileSystem;
using bricks::WaitableAtomic;

std::string RandomString(const size_t length = 8) {
  std::string s;
  for (size_t i = 0; i < length; ++i) {
    s += 'a' + rand() % 26;
  }
  return s;
}

struct TopLevelResponse {
  std::string smart_html_browse_url_EXPERIMENTAL;
  std::string html_browse_url_EXPERIMENTAL;
  std::string smart_browse_url_INTERNAL;
  size_t total;
  std::string browse_url;
  std::string browse_all_url;
  TopLevelResponse(size_t i) : total(i) {
    if (total) {
      const std::string url_prefix = FLAGS_output_url_prefix + FLAGS_route;
      smart_browse_url_INTERNAL = url_prefix + "smart";
      html_browse_url_EXPERIMENTAL = url_prefix + "?id=1&html=yes";
      smart_html_browse_url_EXPERIMENTAL = url_prefix + "smart?html=yes";
      browse_url = url_prefix + "?id=1";
      browse_all_url = url_prefix + "?id=all";
    }
  }
  template <typename A>
  void serialize(A& ar) {
    ar(CEREAL_NVP(smart_html_browse_url_EXPERIMENTAL),
       CEREAL_NVP(html_browse_url_EXPERIMENTAL),
       CEREAL_NVP(smart_browse_url_INTERNAL),
       CEREAL_NVP(total),
       CEREAL_NVP(browse_url),
       CEREAL_NVP(browse_all_url));
  }
};

struct InsightResponse {
  std::string current_url;  // Permalink.
  std::string previous_url;
  std::string next_url;
  std::set<std::string> tags;
  double score;
  std::string description;
  std::unique_ptr<insight::AbstractBase> insight;
  InsightResponse() = default;
  InsightResponse(const InsightsOutput& input, size_t index) { Prepare(input, index); }
  void Prepare(const InsightsOutput& input, size_t index) {
    const std::string url_prefix = FLAGS_output_url_prefix + FLAGS_route;
    current_url = url_prefix + "?id=" + ToString(index + 1);  // It's 1-based in the URL.
    if (index) {
      previous_url = url_prefix + "?id=" + ToString(index);  // It's 1-based in the URL.
    }
    if (index + 1 < input.insight.size()) {
      next_url = url_prefix + "?id=" + ToString(index + 2);  // It's 1-based in the URL.
    }
    score = input.insight[index]->score;
    input.insight[index]->EnumerateFeatures([this, &input](const std::string& feature) {
      const auto cit = input.feature.find(feature);
      assert(cit != input.feature.end());
      tags.insert(cit->second.tag);
      assert(input.tag.find(cit->second.tag) != input.tag.end());
    });
    description = input.insight[index]->Description();
    insight = CloneSerializable(input.insight[index]);
  }
  template <typename A>
  void serialize(A& ar) {
    ar(CEREAL_NVP(current_url),
       CEREAL_NVP(previous_url),
       CEREAL_NVP(next_url),
       CEREAL_NVP(tags),
       CEREAL_NVP(score),
       CEREAL_NVP(description),
       CEREAL_NVP(insight));
  }
};

typedef std::map<std::string, struct SmartSessionInfo> SmartSessionInfoMap;

struct Navigation {
  std::string text;
  std::string url;
  template <typename A>
  void serialize(A& ar) {
    ar(CEREAL_NVP(text), CEREAL_NVP(url));
  }
};

struct SmartInsightResponse {
  bool done;
  std::vector<Navigation> navigation;
  InsightResponse insight;
  SmartSessionInfoMap sessions;
  template <typename A>
  void serialize(A& ar) {
    ar(CEREAL_NVP(done), CEREAL_NVP(navigation), CEREAL_NVP(insight), CEREAL_NVP(sessions));
  }
};

struct SmartSessionInfo {
  std::vector<size_t> history;
  std::set<std::set<std::string>> filters;

  std::map<std::string, std::set<std::set<std::string>>> actions;

  size_t current_insight_index = static_cast<size_t>(-1);

  operator bool() const { return current_insight_index != static_cast<size_t>(-1); }

  bool PassesFilter(size_t index, const InsightsOutput& input) const {
    std::set<std::string> tags;
    input.insight[index]->EnumerateFeatures([&input, &tags](const std::string& feature) {
      const auto cit = input.feature.find(feature);
      assert(cit != input.feature.end());
      tags.insert(cit->second.tag);
      assert(input.tag.find(cit->second.tag) != input.tag.end());
    });
    assert(tags.size() == 2u);  // Because we can.
    for (const auto& filter : filters) {
      size_t matches = 0;
      for (const auto& tag : filter) {
        if (tags.count(tag)) {
          ++matches;
        }
      }
      if (matches == filter.size()) {
        return false;
      }
    }
    return true;
  }

  void TakeAction(const InsightsOutput& input,
                  const std::string& action,
                  SmartInsightResponse& response,
                  const std::string& current_id_key,
                  bool as_html) {
    // Apply action by augmenting the set of filters.
    for (const auto filter : actions[action]) {
      filters.insert(filter);
    }

    // Current browsing.
    current_insight_index = static_cast<size_t>(-1);
    std::set<size_t> history_as_set(history.begin(), history.end());
    size_t i = 0;
    while (current_insight_index == static_cast<size_t>(-1) && i < input.insight.size()) {
      if (!history_as_set.count(i) && PassesFilter(i, input)) {
        current_insight_index = i;
      }
      ++i;
    }
    if (current_insight_index != static_cast<size_t>(-1)) {
      history.push_back(current_insight_index);
      // Grab the tags of this particular insight.
      std::vector<std::string> tags;
      input.insight[current_insight_index]->EnumerateFeatures([&input, &tags](const std::string& feature) {
        const auto cit = input.feature.find(feature);
        assert(cit != input.feature.end());
        tags.push_back(cit->second.tag);
        assert(input.tag.find(cit->second.tag) != input.tag.end());
      });
      assert(tags.size() == 2u);  // Because we can.

      // Generate navigation actions.
      const std::string action_a = RandomString();
      const std::string action_b = RandomString();
      const std::string action_a_b = RandomString();
      const std::string action_ab = RandomString();
      actions[action_a].insert(std::set<std::string>(tags.begin(), tags.begin() + 1));
      actions[action_b].insert(std::set<std::string>(tags.begin() + 1, tags.end()));
      actions[action_ab].insert(std::set<std::string>(tags.begin(), tags.end()));
      actions[action_a_b].insert(std::set<std::string>(tags.begin(), tags.begin() + 1));
      actions[action_a_b].insert(std::set<std::string>(tags.begin() + 1, tags.end()));

      // Populate the navigation links.
      const std::string url_prefix =
          FLAGS_output_url_prefix + FLAGS_route + "smart?html=" + (as_html ? "yes" : "");

      response.navigation.emplace_back(
          Navigation{"Next", url_prefix + Printf("&%s=%s", FLAGS_id_key.c_str(), current_id_key.c_str())});

      response.navigation.emplace_back(Navigation{
          "Filter out insights on the same pair (" + tags[0] + ", " + tags[1] + ").",
          url_prefix + Printf("&%s=%s&action=", FLAGS_id_key.c_str(), current_id_key.c_str()) + action_ab});

      response.navigation.emplace_back(Navigation{
          "Filter out insights on A (" + tags[0] + ").",
          url_prefix + Printf("&%s=%s&action=", FLAGS_id_key.c_str(), current_id_key.c_str()) + action_a});

      response.navigation.emplace_back(Navigation{
          "Filter out insights on B (" + tags[1] + ").",
          url_prefix + Printf("&%s=%s&action=", FLAGS_id_key.c_str(), current_id_key.c_str()) + action_b});

      response.navigation.emplace_back(Navigation{
          "Filter out insights on both A and B (" + tags[0] + " + " + tags[1] + ").",
          url_prefix + Printf("&%s=%s&action=", FLAGS_id_key.c_str(), current_id_key.c_str()) + action_a_b});

      // TODO(dkorolev): Add navigation over `info.history` here.
    }
  }

  template <typename A>
  void serialize(A& ar) {
    ar(CEREAL_NVP(history), CEREAL_NVP(filters));
  }
};

int main(int argc, char** argv) {
  ParseDFlags(&argc, &argv);

  if (FLAGS_route.empty() || FLAGS_route.back() != '/') {
    std::cerr << "`--route` should end with a slash." << std::endl;
    return -1;
  }

  const auto input = ParseJSON<InsightsOutput>(FileSystem::ReadFileAsString(FLAGS_input));

  HTTP(FLAGS_port).Register(FLAGS_route, [&input](Request r) {
    const auto one_based_index = FromString<size_t>(r.url.query["id"]);
    if (one_based_index && one_based_index <= input.insight.size()) {
      if (!r.url.query["html"].empty()) {
        using namespace html;
        HTML html_scope;
        {  // HEAD.
          HEAD head;
          TITLE("Insights Visualization Alpha");
        }
        {  // TABLE, TR.
          TABLE table({{"border", "0"}, {"align", "center"}, {"cellpadding", "8"}});
          TR r({{"align", "center"}});
          if (one_based_index > 1) {
            TD d;
            A a({{"href", FLAGS_route + "?id=" + ToString(one_based_index - 1) + "&html=yes"}});
            TEXT("Previous insight");
          }
          if (one_based_index < input.insight.size()) {
            TD d;
            A a({{"href", FLAGS_route + "?id=" + ToString(one_based_index + 1) + "&html=yes"}});
            TEXT("Next insight");
          }
        }  // TABLE, TR.
        input.insight[one_based_index - 1]->RenderHTML(input.feature);
        r(html_scope.AsString(), HTTPResponseCode.OK, "text/html");
      } else {
        r(InsightResponse(input, one_based_index - 1));
      }
    } else if (r.url.query["id"] == "all") {
      r(input.insight, "insights");
    } else if (r.url.query["id"] == "everything") {
      r(input, "everything");
    } else {
      r(TopLevelResponse(input.insight.size()));
    }
  });

  WaitableAtomic<SmartSessionInfoMap> sessions;

  HTTP(FLAGS_port).Register(FLAGS_route + "smart", [&input, &sessions](Request r) {
    const bool as_html = !r.url.query["html"].empty();
    const std::string& id = r.url.query[FLAGS_id_key];
    const std::string& action = r.url.query["action"];
    if (id.empty()) {
      // Create new user session ID and redirect to it.
      const std::string fresh_id = RandomString();
      const std::string url_prefix = FLAGS_output_url_prefix + FLAGS_route + "smart?" + FLAGS_id_key + '=' +
                                     fresh_id + "&html=" + (as_html ? "yes" : "");
      r("", HTTPResponseCode.Found, "text/html", HTTPHeaders({{"Location", url_prefix}}));
    } else {
      // Smart session browsing.
      SmartInsightResponse payload;
      sessions.MutableUse([&](SmartSessionInfoMap& map) {
        auto& info = map[id];
        info.TakeAction(input, action, payload, id, as_html);
        if (info) {
          payload.done = false;
          payload.insight.Prepare(input, info.current_insight_index);
        } else {
          payload.done = true;
        }
        payload.sessions = map;
      });
      if (!as_html) {
        r(payload, "smart_insight");
      } else {
        using namespace html;
        HTML html_scope;
        {  // HEAD.
          HEAD head;
          TITLE("Insights Visualization Alpha");
        }
        if (payload.done) {
          TEXT("<h1>You are awesome!</h1>");
          {
            A a({{"href", FLAGS_output_url_prefix + FLAGS_route + "smart?html=yes"}});
            TEXT("Start over!");
          }
        } else {
          for (const auto& nav : payload.navigation) {
            TEXT("<p align=center>");
            A a({{"href", nav.url}});
            TEXT(nav.text);
            TEXT("</p>");
          }
          TEXT("<hr>");
          {
            TEXT("<p align=center>");
            A a({{"href", payload.insight.current_url + "&html=yes"}});
            TEXT("[Not yet a] permalink to this insight.");
            TEXT("</p>");
          }
          payload.insight.insight->RenderHTML(input.feature);
        }
        r(html_scope.AsString(), HTTPResponseCode.OK, "text/html");
      }
    }
  });

  HTTP(FLAGS_port).Join();
}
