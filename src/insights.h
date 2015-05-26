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

#include "../../Current/Bricks/cerealize/cerealize.h"

// The data structure to gather aggregated info across the sessions within the same realm,
// for insights generation, ranking, visualization and further filtering.
struct PreInsights {
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
  // Realm defines the universe within which sessions can be analyzed as the whole.
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
  // PreInsights is a collection of realms.
  std::vector<Realm> realm;
  template <typename A>
  void serialize(A& ar) {
    ar(CEREAL_NVP(realm));
  }
};

#endif  // INSIGHTS_H
