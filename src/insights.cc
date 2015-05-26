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
#include <set>

#include "../../Current/Bricks/dflags/dflags.h"
#include "../../Current/Bricks/file/file.h"
#include "../../Current/Bricks/strings/util.h"

using bricks::FileSystem;
using namespace bricks::strings;

DEFINE_string(input, "data.txt", "");

typedef long double DOUBLE;

DOUBLE dE(DOUBLE p) {
  assert(p >= 0.0 && p <= 1.0 + 1e-6);
  if (p > 1e-6 && p < 1.0) {
    return p * logl(p);
  } else {
    return 0.0;
  }
}

DOUBLE E(DOUBLE p1, DOUBLE p2) { return logl(0.5) * (dE(p1) + dE(p2)); }

DOUBLE E(DOUBLE p1, DOUBLE p2, DOUBLE p3, DOUBLE p4) { return logl(0.5) * (dE(p1) + dE(p2) + dE(p3) + dE(p4)); }

int main() {
  std::set<std::string> features_set;
  std::vector<std::set<std::string>> input;
  std::map<std::string, DOUBLE> total;

  std::map<std::string, DOUBLE> entropy1;

  for (auto line : Split<ByLines>(FileSystem::ReadFileAsString(FLAGS_input))) {
    std::vector<std::string> cols = Split(line, '\t');
    if (!cols.empty()) {
      input.emplace_back(cols.begin() + 1, cols.end());
      for (const auto& field : input.back()) {
        features_set.insert(field);
        ++total[field];
      }
    }
  }

  std::vector<std::string> features(features_set.begin(), features_set.end());
  std::cerr << "Rows: " << input.size() << ", cols: " << features.size() << std::endl;

  const DOUBLE prior = 5.0;

  // Single-feature entropy.
  if (!input.empty()) {
    const DOUBLE N = input.size();
    std::map<std::string, DOUBLE> e1;
    for (const auto& f : features) {
      const DOUBLE p1 = (total[f] + prior) / (N + prior * 2);
      const DOUBLE p2 = ((N - total[f]) + prior) / (N + prior * 2);
      // std::cerr << p1 << ' ' << p2 << std::endl;
      const DOUBLE e = E(p1, p2);
      entropy1[f] = e;
      // std::cout << e << "\t" << f << "\n";
    }
  }

  for (size_t i = 0; i + 1 < input.size(); ++i) {
    const std::string& fi = features[i];
    const std::string fi_prefix = fi.substr(0, std::min(fi.find_first_of(">="), fi.find_first_of("<")));
    for (size_t j = i + 1; j < input.size(); ++j) {
      const std::string& fj = features[j];
      const std::string fj_prefix = fj.substr(0, std::min(fj.find_first_of(">="), fj.find_first_of("<")));
      if (fi_prefix != fj_prefix) {
        DOUBLE c[2][2] = {{prior, prior}, {prior, prior}};
        size_t cc[2][2] = {{0u, 0u}, {0u, 0u}};
        for (const auto& cit : input) {
          ++c[cit.count(fi)][cit.count(fj)];
          ++cc[cit.count(fi)][cit.count(fj)];
        }
        assert(fabsl(c[0][0] + c[0][1] + c[1][0] + c[1][1] - (prior * 4 + input.size())) < 1e-6);
        const DOUBLE q = 1.0 / (prior * 4 + input.size());
        const DOUBLE e = E(q * c[0][0], q * c[0][1], q * c[1][0], q * c[1][1]);
        const DOUBLE de = entropy1[fi] + entropy1[fj] - e;
        { // if (de > 1e-9) {
          std::cout << de << '\t' << e << '\t' << features[i] << '\t' << features[j] << '\t' << cc[0][0] << ' '
                    << cc[0][1] << ' ' << cc[1][0] << ' ' << cc[1][1] << std::endl;
        }
      }
    }
  }
}
