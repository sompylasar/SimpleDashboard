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
DEFINE_string(output_uri_prefix, "http://localhost", "The prefix for the URI-s output by the server.");

DEFINE_string(input, "data/insights.json", "Path to the file containing the insights to browse.");

using bricks::strings::Printf;
using bricks::strings::FromString;
using bricks::strings::ToString;
using bricks::FileSystem;
using bricks::WaitableAtomic;

struct TopLevelResponse {
  std::string SMART_BROWSE_URI;
  size_t total;
  std::string HTML_BROWSE_URI;
  std::string browse_uri;
  std::string browse_all_uri;
  TopLevelResponse(size_t i) : total(i) {
    if (total) {
      SMART_BROWSE_URI = FLAGS_output_uri_prefix + "/smart";
      HTML_BROWSE_URI = FLAGS_output_uri_prefix + "/?id=1&html=yes";
      browse_uri = FLAGS_output_uri_prefix + "/?id=1";
      browse_all_uri = FLAGS_output_uri_prefix + "/?id=all";
    }
  }
  template <typename A>
  void serialize(A& ar) {
    ar(CEREAL_NVP(SMART_BROWSE_URI),
       CEREAL_NVP(HTML_BROWSE_URI),
       CEREAL_NVP(total),
       CEREAL_NVP(browse_uri),
       CEREAL_NVP(browse_all_uri));
  }
};

struct InsightResponse {
  std::string prev_uri;
  std::string next_uri;
  std::set<std::string> tags;
  double score;
  std::string description;
  std::unique_ptr<insight::AbstractBase> insight;
  InsightResponse(const InsightsOutput& input, size_t index) {
    if (index) {
      prev_uri = FLAGS_output_uri_prefix + "/?id=" + ToString(index);  // It's 1-based in the URI.
    }
    if (index + 1 < input.insight.size()) {
      next_uri = FLAGS_output_uri_prefix + "/?id=" + ToString(index + 2);  // It's 1-based in the URI.
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
    ar(CEREAL_NVP(prev_uri),
       CEREAL_NVP(next_uri),
       CEREAL_NVP(tags),
       CEREAL_NVP(score),
       CEREAL_NVP(description),
       CEREAL_NVP(insight));
  }
};

struct SmartSessionInfo {};

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
        input.insight[one_based_index - 1]->RenderHTML();
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

  WaitableAtomic<SmartSessionInfo> sessions;

  HTTP(FLAGS_port).Register(FLAGS_route + "smart", [&input, &sessions](Request r) {
    const std::string id_key = "you_are_awesome";
    const std::string& id = r.url.query[id_key];
    if (id.empty()) {
      // Create new user session ID and redirect to it.
      std::string fresh_id = "";
      for (size_t i = 0; i < 4; ++i) {
        fresh_id += 'a' + rand() % 26;
      }
      r("",
        HTTPResponseCode.Found,
        "text/html",
        HTTPHeaders(
            {{"Location", Printf("%ssmart?%s=%s", FLAGS_route.c_str(), id_key.c_str(), fresh_id.c_str())}}));
    } else {
      // Smart session browsing.
      r("Yeah!");
    }
  });

  HTTP(FLAGS_port).Join();
}
