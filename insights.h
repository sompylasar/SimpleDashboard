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

#ifndef INSIGHTS_H
#define INSIGHTS_H

#include "helpers.h"
#include "html.h"

#include "../Current/Bricks/cerealize/cerealize.h"
#include "../Current/Bricks/strings/util.h"

// Tags group features into similar ones, for bulk filtering.
struct TagInfo {
  std::string name;  // A human-readable name.
  template <typename A>
  void serialize(A& ar) {
    ar(CEREAL_NVP(name));
  }
};

// Features are descriptive of the product. They are what makes user users say "Wow!"
struct FeatureInfo {
  std::string tag;  // Tag for bulk filtering. `TagInfo` is present for each of them.
  std::string yes;  // A human readable name of the feature if it is present.
  std::string no;   // Optional: A human-readable name of the feature if it is not present.
  const std::string& YesText() const { return yes; }
  std::string NoText() const { return !no.empty() ? no : "Not '" + yes + "'"; }
  template <typename A>
  void serialize(A& ar) {
    ar(CEREAL_NVP(tag), CEREAL_NVP(yes), CEREAL_NVP(no));
  }
};

// === INPUT ===

// The data structure to gather aggregated info across the sessions within the same realm,
// for insights generation, ranking, visualization and further filtering.
struct InsightsInput {
  // Session is, well, a feature-vector. As for this exercise, we use binary features only.
  struct Session {
    // The key for the session. To be able to trace it back.
    std::string key;
    // Binary features set for this session. `FeatureInfo` is present for each of them.
    std::vector<std::string> feature;
    template <typename A>
    void serialize(A& ar) {
      ar(CEREAL_NVP(key), CEREAL_NVP(feature));
    }
  };
  // Realm defines the universe within which sessions can be analyzed as the whole,
  // such that comparing them to each and detecting anomalies is what yields insights.
  struct Realm {
    std::string description;
    std::map<std::string, TagInfo> tag;
    std::map<std::string, FeatureInfo> feature;
    std::vector<Session> session;
    template <typename A>
    void serialize(A& ar) {
      ar(CEREAL_NVP(description), CEREAL_NVP(tag), CEREAL_NVP(feature), CEREAL_NVP(session));
    }
  };
  // InsightsInput is a collection of realms.
  std::vector<Realm> realm;
  template <typename A>
  void serialize(A& ar) {
    ar(CEREAL_NVP(realm));
  }
};

// === OUTPUT ===

// Need to add to a `.cc` file:
// CEREAL_REGISTER_TYPE(insight::MutualInformation);

namespace insight {

struct AbstractBase {
  double score;  // The higher, the better.
  virtual ~AbstractBase() = default;
  virtual std::string Description() = 0;
  virtual void RenderHTML(const std::map<std::string, FeatureInfo>&) = 0;
  virtual void EnumerateFeatures(std::function<void(const std::string&)>) = 0;
  template <typename A>
  void serialize(A& ar) {
    ar(CEREAL_NVP(score));
  }
};

struct MutualInformation : AbstractBase {
  struct Counters {
    size_t N;    // Total sessions.
    size_t lhs;  // C[+][*] ( == N - C[-][*]).
    size_t rhs;  // C[*][+] ( == N - C[*][-]).
    size_t nn;   // C[-][-].
    size_t ny;   // C[-][+].
    size_t yn;   // C[+][-].
    size_t yy;   // C[+][+].
    template <typename A>
    void serialize(A& ar) {
      ar(CEREAL_NVP(N),
         CEREAL_NVP(lhs),
         CEREAL_NVP(rhs),
         CEREAL_NVP(nn),
         CEREAL_NVP(ny),
         CEREAL_NVP(yn),
         CEREAL_NVP(yy));
    }
  };
  std::string lhs;
  std::string rhs;
  Counters counters;
  std::string Description() override { return "WE HAZ INSIGHTS!"; }
  void RenderHTML(const std::map<std::string, FeatureInfo>& feature) override {
    using namespace html;
    using bricks::strings::ToString;
    TEXT(
        bricks::strings::Printf("<div style='text-align: center; width: 100%%'><h2>%s</h2>vs.<h2>%s</h2></div>",
                                lhs.c_str(),
                                rhs.c_str()));
    {
      TABLE table({{"border", "0"}, {"align", "center"}, {"cellspacing", "24"}});
      TR tr;

      {
        TD td;
        // Absolute counters A.
        TABLE table({{"border", "1"}, {"align", "center"}, {"cellpadding", "8"}});
        {
          TR r({{"align", "center"}});
          { TD d; }
          {
            TD d;
            B("YES");
            PRE(feature.find(lhs)->second.YesText());
          }
          {
            TD d;
            B("NO");
            PRE(feature.find(lhs)->second.NoText());
          }
        }
        {
          TR r({{"align", "center"}});
          {
            TD d;
            B("A");
          }
          {
            TD d;
            TEXT("<font size=+2>");
            PRE(ToString(counters.lhs));
            TEXT("</font>");
          }
          {
            TD d;
            TEXT("<font size=+2>");
            PRE(ToString(counters.N - counters.lhs));
            TEXT("</font>");
          }
        }
      }
      // TEXT("<br>");
      {
        TD td;
        // Absolute counters B.
        TABLE table({{"border", "1"}, {"align", "center"}, {"cellpadding", "8"}});
        {
          TR r({{"align", "center"}});
          { TD d; }
          {
            TD d;
            B("YES");
            PRE(feature.find(rhs)->second.YesText());
          }
          {
            TD d;
            B("NO");
            PRE(feature.find(rhs)->second.NoText());
          }
        }
        {
          TR r({{"align", "center"}});
          {
            TD d;
            B("B");
          }
          {
            TD d;
            TEXT("<font size=+2>");
            PRE(ToString(counters.rhs));
            TEXT("</font>");
          }
          {
            TD d;
            TEXT("<font size=+2>");
            PRE(ToString(counters.N - counters.rhs));
            TEXT("</font>");
          }
        }
      }
    }
    TEXT("<br>");
    {
      // Cross-counters.
      TABLE table({{"border", "1"}, {"align", "center"}, {"cellpadding", "8"}});
      {
        TR r({{"align", "center"}});
        { TD d; }
        {
          TD d;
          B("B: YES");
        }
        {
          TD d;
          B("B: NO");
        }
      }
      {
        TR r({{"align", "center"}});
        {
          TD d;
          B("A: YES");
        }
        {
          TD d;
          TEXT("<font size=+4>");
          PRE(ToString(counters.yy));
          TEXT("</font>");
        }
        {
          TD d;
          TEXT("<font size=+4>");
          PRE(ToString(counters.yn));
          TEXT("</font>");
        }
      }
      {
        TR r({{"align", "center"}});
        {
          TD d;
          B("A: NO");
        }
        {
          TD d;
          TEXT("<font size=+4>");
          PRE(ToString(counters.ny));
          TEXT("</font>");
        }
        {
          TD d;
          TEXT("<font size=+4>");
          PRE(ToString(counters.nn));
          TEXT("</font>");
        }
      }
    }
  }
  virtual void EnumerateFeatures(std::function<void(const std::string&)> f) {
    f(lhs);
    f(rhs);
  }
  template <typename A>
  void serialize(A& ar) {
    AbstractBase::serialize(ar);
    ar(CEREAL_NVP(lhs), CEREAL_NVP(rhs), CEREAL_NVP(counters));
  }
};

};  // namespace insight

struct InsightsOutput {
  std::map<std::string, TagInfo> tag;
  std::map<std::string, FeatureInfo> feature;
  std::vector<std::unique_ptr<insight::AbstractBase>> insight;
  template <typename A>
  void serialize(A& ar) {
    ar(CEREAL_NVP(tag), CEREAL_NVP(feature), CEREAL_NVP(insight));
  }
};

#endif  // INSIGHTS_H
