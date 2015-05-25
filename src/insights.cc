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

double dE(double p) {
  assert(p >= 0.0 && p <= 1.0);
  if (p > 1e-6) {
    return p * log(p);
  } else {
    return 0.0;
  }
}

double E(double p1, double p2) {
  return (dE(p1) + dE(p2));  // * log(2.0);
}

int main() {
  std::set<std::string> fields;
  std::vector<std::set<std::string>> input;
  std::map<std::string, double> total;
  for (auto line : Split<ByLines>(FileSystem::ReadFileAsString(FLAGS_input))) {
    std::vector<std::string> cols = Split(line, '\t');
    if (!cols.empty()) {
      input.emplace_back(cols.begin() + 1, cols.end());
      for (const auto& field : input.back()) {
        fields.insert(field);
        ++total[field];
      }
    }
  }
  std::cerr << "Rows: " << input.size() << ", cols: " << fields.size() << std::endl;
  if (!input.empty()) {
    const double N = input.size();
    std::map<std::string, double> e1;
    for (const auto& f : fields) {
      double p1 = total[f] / N;
      double p2 = (N - total[f]) / N;
      std::cout << E(p1, p2) << "\t" << f << "\n";
    }
  }
}
