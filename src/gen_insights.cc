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

#include <cmath>
#include <map>
#include <set>

#include "insights.h"

#include "../../Current/Bricks/dflags/dflags.h"
#include "../../Current/Bricks/file/file.h"
#include "../../Current/Bricks/strings/util.h"

using bricks::FileSystem;
using namespace bricks::strings;

DEFINE_string(input, "data/insights_input.json", "");
DEFINE_double(prior, 0.5, "");
DEFINE_double(gain_threshold, 0.1, "Threshold on delta entropy in mutual information vs. individual information.");

typedef long double DOUBLE;
const DOUBLE EPS = 1e-8;
const DOUBLE BITS = std::log(DOUBLE(0.5));  // Represent entropy in bits.

DOUBLE entropy(DOUBLE p) {
  assert(p >= 0.0 && p <= 1.0 + EPS);
  if (p > EPS && p < 1.0) {
    return p * std::log(p);
  } else {
    return 0;
  }
}

DOUBLE bits(size_t n, size_t c1, size_t c2) {
  assert(n > 0);
  assert(c1 <= n);
  assert(c2 <= n);
  assert(c1 + c2 == n);
  const DOUBLE p = FLAGS_prior;
  const DOUBLE k = DOUBLE(1) / (p * 2 + n);
  return (entropy(k * (p + c1)) + entropy(k * (p + c2))) * BITS * n;
}

DOUBLE bits(size_t n, size_t c1, size_t c2, size_t c3, size_t c4) {
  assert(n > 0);
  assert(c1 <= n);
  assert(c2 <= n);
  assert(c3 <= n);
  assert(c4 <= n);
  assert(c1 + c2 + c3 + c4 == n);
  const DOUBLE p = FLAGS_prior;
  const DOUBLE k = DOUBLE(1) / (p * 4 + n);
  return (entropy(k * (p + c1)) + entropy(k * (p + c2)) + entropy(k * (p + c3)) + entropy(k * (p + c4))) * BITS * n;
}

int main() {
  fprintf(stderr, "Reading '%s' ...", FLAGS_input.c_str());
  fflush(stderr);
  const auto input = ParseJSON<InsightsInput>(FileSystem::ReadFileAsString(FLAGS_input));
  fprintf(stderr, "\b\b\b: Done, %d realm(s).\n", static_cast<int>(input.realm.size()));

  for (const auto& realm : input.realm) {
    const auto& S = realm.session;
    const size_t N = S.size();
    fprintf(stderr, "Realm '%s', %d sessions ...", realm.description.c_str(), static_cast<int>(N));
    fflush(stderr);
    // Build indexes and reverse indexes for features and tags.
    std::vector<std::string> tag;
    std::unordered_map<std::string, size_t> tag_index;
    std::vector<std::string> feature;
    std::unordered_map<std::string, size_t> feature_index;
    for (const auto& cit : realm.tag) {
      tag_index[cit.first];
    }
    for (auto& it : tag_index) {
      it.second = tag.size();
      tag.push_back(it.first);
    }
    for (const auto& cit : realm.feature) {
      feature_index[cit.first];
    }
    for (auto& it : feature_index) {
      it.second = feature.size();
      feature.push_back(it.first);
    }
    const size_t T = tag.size();
    const size_t F = feature.size();
    fprintf(stderr, "\b\b\b\b, %d tags, %d features ...", static_cast<int>(T), static_cast<int>(F));
    fflush(stderr);

    // Compute counters and entropies.
    std::vector<size_t> C(F, 0u);
    // Efficiently pack in memory { F * F * 4 } size_t's.
    std::vector<size_t> CC_storage(F * F * 4, 0u);
    std::vector<std::vector<size_t*>> CC(F, std::vector<size_t*>(F));  // [i][j] -> { --, -+, +-, ++ }.
    {
      size_t* ptr = &CC_storage[0];
      for (size_t i = 0; i < F; ++i) {
        for (size_t j = 0; j < F; ++j) {
          CC[i][j] = ptr;
          ptr += 4;
        }
      }
      assert(ptr == &CC_storage[0] + (F * F * 4));
    }
    {
      // Last session index that had this feature set. Start from infinity.
      std::vector<size_t> q(F);
      memset(&q[0], 0xff, F * sizeof(size_t));
      for (size_t sid = 0; sid < N; ++sid) {
        for (const auto& f : S[sid].feature) {
          const auto cit = feature_index.find(f);
          assert(cit != feature_index.end());
          assert(cit->second < F);
          q[cit->second] = sid;
        }
        // has(f) -> size_t(1) if feature f is present, and size_t(0) otherwise.
        const auto has = [&q, &sid](const size_t f) { return q[f] == sid ? 1u : 0u; };
        for (size_t fi = 0; fi < F; ++fi) {
          const size_t bi = has(fi);
          if (bi) {
            // Keep the `+` counter per one feature. The `-` counter is obviously `N - C[f]`.
            ++C[fi];
          }
          for (size_t fj = fi + 1; fj < F; ++fj) {
            // Keep the { `--`, `-+`, `+-`, `++` } counters for pairs of features.
            const size_t bj = has(fj);
            const size_t offset = bi * 2 + bj;
            ++CC[fi][fj][offset];
            ++CC[fj][fi][offset];
          }
        }
      }
    }

    // Compute entropies.
    std::vector<DOUBLE> E(F);

    for (size_t f = 0; f < F; ++f) {
      assert(C[f] <= N);
      E[f] = bits(N, C[f], N - C[f]);
    }

    std::vector<std::vector<DOUBLE>> EE(F, std::vector<DOUBLE>(F));

    for (size_t fi = 0; fi + 1 < F; ++fi) {
      for (size_t fj = fi + 1; fj < F; ++fj) {
        const auto& cc = CC[fi][fj];
        assert(cc[0] <= N);
        assert(cc[1] <= N);
        assert(cc[2] <= N);
        assert(cc[3] <= N);
        assert(cc[0] + cc[1] + cc[2] + cc[3] == N);
        EE[fi][fj] = EE[fj][fi] = bits(N, cc[0], cc[1], cc[2], cc[3]);
      }
    }

    fprintf(stderr, "\b\b\b\b, bits done ...");

    for (size_t fi = 0; fi + 1 < F; ++fi) {
      for (size_t fj = fi + 1; fj < F; ++fj) {
        if (!FLAGS_prior) {
          if (!((EE[fi][fj] < E[fi] + E[fj] + EPS))) {
            std::cerr << fi << ' ' << fj << ": " << C[fi] << ' ' << C[fj] << ", " << CC[fi][fj][0] << ' '
                      << CC[fi][fj][1] << ' ' << CC[fi][fj][2] << ' ' << CC[fi][fj][3] << ": " << E[fi] << ' '
                      << E[fj] << ' ' << EE[fi][fj] << std::endl;
          }
          assert(EE[fi][fj] < E[fi] + E[fj] + EPS);
        }
        const DOUBLE gain = (E[fi] + E[fj] - EE[fi][fj]);
        if (gain > FLAGS_gain_threshold) {
          std::cout << (E[fi] + E[fj] - EE[fi][fj]) << '\t' << feature[fi] << '\t' << feature[fj] << std::endl;
        }
      }
    }

    fprintf(stderr, "\b\b\b\b, done.\n");
    fflush(stderr);
  }

  fprintf(stderr, "Done generating insights.\n");
}
