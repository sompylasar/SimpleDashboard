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

#include "../../Current/Bricks/cerealize/cerealize.h"
#include "../../Current/Bricks/strings/util.h"

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
  virtual void RenderHTML() = 0;
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
  void RenderHTML() override {
    using namespace html;
    using bricks::strings::ToString;
    if (false) {  // Feature names.
      TABLE table({{"border", "0"}, {"align", "center"}, {"cellpadding", "8"}});
      {
        TR r({{"align", "center"}});
        TD d;
        B("LHS ");
        TEXT(lhs);
      }
      {
        TR r({{"align", "center"}});
        TD d;
        B("RHS ");
        TEXT(rhs);
      }
    }
    {
      // Absolute counters.
      TABLE table({{"border", "1"}, {"align", "center"}, {"cellpadding", "8"}});
      {
        TR r({{"align", "center"}});
        { TD d; }
        {
          TD d;
          B("Number of sessions having this property.");
        }
        {
          TD d;
          B("Number of sessions not having this property.");
        }
      }
      {
        TR r({{"align", "center"}});
        {
          TD d;
          B("LHS");
          PRE((" \" " + lhs + " \" "));
        }
        {
          TD d;
          PRE(ToString(counters.lhs));
        }
        {
          TD d;
          PRE(ToString(counters.N - counters.lhs));
        }
      }
      {
        TR r({{"align", "center"}});
        {
          TD d;
          B("RHS");
          PRE((" \" " + rhs + " \" "));
        }
        {
          TD d;
          PRE(ToString(counters.rhs));
        }
        {
          TD d;
          PRE(ToString(counters.N - counters.rhs));
        }
      }
    }
    {
      // Cross-counters.
      TABLE table({{"border", "1"}, {"align", "center"}, {"cellpadding", "8"}});
      {
        TR r({{"align", "center"}});
        { TD d; }
        {
          TD d;
          B("Has RHS");
        }
        {
          TD d;
          B("Does not have RHS");
        }
      }
      {
        TR r({{"align", "center"}});
        {
          TD d;
          B("Has LHS");
        }
        {
          TD d;
          PRE(ToString(counters.yy));
        }
        {
          TD d;
          PRE(ToString(counters.yn));
        }
      }
      {
        TR r({{"align", "center"}});
        {
          TD d;
          B("Does not have LHS");
        }
        {
          TD d;
          PRE(ToString(counters.ny));
        }
        {
          TD d;
          PRE(ToString(counters.nn));
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
