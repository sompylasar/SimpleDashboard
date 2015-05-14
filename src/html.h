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

#ifndef WHATEVER_HTML_H
#define WHATEVER_HTML_H

#include <cassert>
#include <vector>
#include <string>
#include <utility>
#include <initializer_list>

#include "../Bricks/util/singleton.h"

namespace html {

using bricks::ThreadLocalSingleton;

struct State {
  std::string html;
  enum { NONE, IN_PROGRESS, COMMITTED } state = NONE;
  void Begin() {
    assert(state == NONE);
    html = "<!doctype html>\n";
    state = IN_PROGRESS;
  }
  std::string Commit() {
    assert(state == IN_PROGRESS);
    state = COMMITTED;
    return html;
  }
  template <typename T>
  void Append(T&& content) {
    assert(state == IN_PROGRESS);
    html.append(content);
  }
  void End() {
    assert(state == COMMITTED);
    state = NONE;
  }
};

// TODO(dkorolev): Eliminate some copy-pasting.
// TODO(dkorolev): Escaping?

struct HTML final {
  HTML() { ThreadLocalSingleton<State>().Begin(); }
  ~HTML() { ThreadLocalSingleton<State>().End(); }
  std::string AsString() { return ThreadLocalSingleton<State>().Commit(); }
};

#if defined(SCOPED_TAG) || defined(TEXT_TAG) || defined(SHORT_TAG) || defined(STANDALONE_TAG)
#error "`SCOPED_TAG`, `TEXT_TAG`, ``SHORT_TAG` and `STANDALONE_TAG` should not be defined by here."
#endif

#define SCOPED_TAG(tag)                                                                   \
  struct tag {                                                                            \
    tag() { ThreadLocalSingleton<State>().Append("<" #tag ">"); }                         \
    template <typename T>                                                                 \
    void construct(T&& params) {                                                          \
      auto& html = ThreadLocalSingleton<State>();                                         \
      html.Append("<" #tag);                                                              \
      for (const auto cit : params) {                                                     \
        html.Append(" ");                                                                 \
        html.Append(cit.first);                                                           \
        html.Append("='");                                                                \
        html.Append(cit.second);                                                          \
        html.Append("'");                                                                 \
      }                                                                                   \
      html.Append(">");                                                                   \
    }                                                                                     \
    tag(const std::vector<std::pair<std::string, std::string>>& v) { construct(v); }      \
    tag(std::initializer_list<std::pair<std::string, std::string>> il) { construct(il); } \
    ~tag() { ThreadLocalSingleton<State>().Append("</" #tag ">"); }                       \
  }

#define TEXT_TAG(tag)                                               \
  struct tag {                                                      \
    tag() { ThreadLocalSingleton<State>().Append("<" #tag ">"); }   \
    tag(const std::string& content) {                               \
      ThreadLocalSingleton<State>().Append("<" #tag ">");           \
      ThreadLocalSingleton<State>().Append(content);                \
    }                                                               \
    ~tag() { ThreadLocalSingleton<State>().Append("</" #tag ">"); } \
  }

#define SHORT_TAG(tag)                                                                    \
  struct tag {                                                                            \
    tag() = delete;                                                                       \
    template <typename T>                                                                 \
    void construct(T&& params) {                                                          \
      auto& html = ThreadLocalSingleton<State>();                                         \
      html.Append("<" #tag);                                                              \
      for (const auto cit : params) {                                                     \
        html.Append(" ");                                                                 \
        html.Append(cit.first);                                                           \
        html.Append("='");                                                                \
        html.Append(cit.second);                                                          \
        html.Append("'");                                                                 \
      }                                                                                   \
      html.Append(" />");                                                                 \
    }                                                                                     \
    tag(const std::vector<std::pair<std::string, std::string>>& v) { construct(v); }      \
    tag(std::initializer_list<std::pair<std::string, std::string>> il) { construct(il); } \
  }

#define STANDALONE_TAG(tag)                                                               \
  struct tag {                                                                            \
    tag() = delete;                                                                       \
    template <typename T>                                                                 \
    void construct(T&& params) {                                                          \
      auto& html = ThreadLocalSingleton<State>();                                         \
      html.Append("<" #tag);                                                              \
      for (const auto cit : params) {                                                     \
        html.Append(" ");                                                                 \
        html.Append(cit.first);                                                           \
        html.Append("='");                                                                \
        html.Append(cit.second);                                                          \
        html.Append("'");                                                                 \
      }                                                                                   \
      html.Append(">");                                                                   \
    }                                                                                     \
    tag(const std::vector<std::pair<std::string, std::string>>& v) { construct(v); }      \
    tag(std::initializer_list<std::pair<std::string, std::string>> il) { construct(il); } \
  }

SCOPED_TAG(HEAD);
SCOPED_TAG(BODY);

TEXT_TAG(TITLE);
TEXT_TAG(P);
TEXT_TAG(PRE);

SCOPED_TAG(TABLE);
SCOPED_TAG(TR);
SCOPED_TAG(TD);

SCOPED_TAG(A);

TEXT_TAG(B);
TEXT_TAG(I);
TEXT_TAG(U);

SHORT_TAG(IMG);

SCOPED_TAG(FORM);
STANDALONE_TAG(INPUT);

template <typename T>
void TEXT(T&& x) {
  ThreadLocalSingleton<State>().Append(std::forward<T>(x));
}

#undef SCOPED_TAG
#undef TEXT_TAG
#undef SHORT_TAG
#undef STANDALONE_TAG

}  // namespace html

#endif
