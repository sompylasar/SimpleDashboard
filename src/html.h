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

// TODO(dkorolev): (or sompylasar or anybody) Move this `ReplaceAllByMap` into Bricks.
std::string ReplaceAllByMap(const std::string& str, const std::vector<std::pair<std::string, std::string>>& replacements) {
  std::string output = str;
  for (const auto& kv : replacements) {
    std::size_t pos = 0;
    while (std::string::npos != (pos = output.find(kv.first, pos))) {
      output.replace(pos, kv.first.length(), kv.second);
      pos += kv.second.length();
    }
  }
  return output;
}

std::string EscapeHtmlEntities(const std::string& str) {
  // The following map does not contain all the entities but it's enough.
  // http://stackoverflow.com/a/9189067
  static const std::vector<std::pair<std::string, std::string>> replacements = {
    {"<", "&lt;"},
    {"&", "&amp;"},
    {"\"", "&quot;"},
    {"'", "&#39;"}};
  return ReplaceAllByMap(str, replacements);
}

#if defined(SCOPED_TAG) || defined(TEXT_TAG) || defined(SHORT_TAG)
#error "`SCOPED_TAG`, `TEXT_TAG`, ``SHORT_TAG` should not be defined by here."
#endif

// A macro to define HTML tags that start a block when constructed.
// These tags are closed during destruction, e.g. leaving the scope.
#define SCOPED_TAG(tag)                                                                   \
  struct tag {                                                                            \
    template <typename T>                                                                 \
    void construct(T&& params, const std::string& content) {                              \
      auto& html = ThreadLocalSingleton<State>();                                         \
      html.Append("<" #tag);                                                              \
      for (const auto cit : params) {                                                     \
        html.Append(" ");                                                                 \
        html.Append(cit.first);                                                           \
        html.Append("=\"");                                                               \
        html.Append(EscapeHtmlEntities(cit.second));                                      \
        html.Append("\"");                                                                \
      }                                                                                   \
      html.Append(">");                                                                   \
      html.Append(content);                                                               \
    }                                                                                     \
    tag(const std::vector<std::pair<std::string, std::string>>& v, const std::string& content = "") { construct(v, content); }      \
    tag(std::initializer_list<std::pair<std::string, std::string>> il, const std::string& content = "") { construct(il, content); } \
    tag(const std::string& content = "") : tag({}, content) {}                            \
    ~tag() { ThreadLocalSingleton<State>().Append("</" #tag ">"); }                       \
  };

// A macro to define HTML tags that contain only a text string passed to the constructor.
// These tags are closed immediately during construction.
#define TEXT_TAG(tag)                                                                     \
  struct tag {                                                                            \
    template <typename T>                                                                 \
    void construct(T&& params, const std::string& content) {                              \
      auto& html = ThreadLocalSingleton<State>();                                         \
      html.Append("<" #tag);                                                              \
      for (const auto cit : params) {                                                     \
        html.Append(" ");                                                                 \
        html.Append(cit.first);                                                           \
        html.Append("=\"");                                                               \
        html.Append(EscapeHtmlEntities(cit.second));                                      \
        html.Append("\"");                                                                \
      }                                                                                   \
      html.Append(">");                                                                   \
      html.Append(content);                                                               \
      html.Append("</" #tag ">");                                                         \
    }                                                                                     \
    template <typename T>                                                                 \
    tag(const std::vector<std::pair<std::string, std::string>>& v, const std::string& content = "") { construct(v, content); }      \
    tag(std::initializer_list<std::pair<std::string, std::string>> il, const std::string& content = "") { construct(il, content); } \
    tag(const std::string& content = "") : tag({}, content) {}                            \
    ~tag() {}                                                                             \
  };

// A macro to define HTML tags that have no child elements and close immediately, --
// so called void elements: area, base, br, col, command, embed, hr, img, input, keygen, link, meta, param, source, track, wbr.
// In HTML5, a trailing slash is optional for a void element, but an end tag would be invalid.
// In XHTML, a trailing slash is mandatory, so we keep it for strictness.
// These tags are closed immediately during construction.
#define SHORT_TAG(tag)                                                                    \
  struct tag {                                                                            \
    template <typename T>                                                                 \
    void construct(T&& params) {                                                          \
      auto& html = ThreadLocalSingleton<State>();                                         \
      html.Append("<" #tag);                                                              \
      for (const auto cit : params) {                                                     \
        html.Append(" ");                                                                 \
        html.Append(cit.first);                                                           \
        html.Append("=\"");                                                               \
        html.Append(EscapeHtmlEntities(cit.second));                                      \
        html.Append("\"");                                                                \
      }                                                                                   \
      html.Append(" />");                                                                 \
    }                                                                                     \
    tag(const std::vector<std::pair<std::string, std::string>>& v) { construct(v); }      \
    tag(std::initializer_list<std::pair<std::string, std::string>> il) { construct(il); } \
    tag() : tag({}) {}                                                                    \
    ~tag() {}                                                                             \
  };

// Document structure tags.
SCOPED_TAG(HEAD);
SCOPED_TAG(BODY);

// HTML5 body structure tags.
SCOPED_TAG(HEADER);
SCOPED_TAG(MAIN);
SCOPED_TAG(FOOTER);
SCOPED_TAG(NAV);
SCOPED_TAG(SECTION);
SCOPED_TAG(ARTICLE);
SCOPED_TAG(ASIDE);

// Page title and metadata.
TEXT_TAG(TITLE);
SHORT_TAG(META);

// Script and style tags.
TEXT_TAG(SCRIPT);
TEXT_TAG(STYLE);
SHORT_TAG(LINK);

// Commonly used tags.
SCOPED_TAG(DIV);
SCOPED_TAG(SPAN);
SCOPED_TAG(P);
SCOPED_TAG(BLOCKQUOTE);

SCOPED_TAG(A);

TEXT_TAG(B);
TEXT_TAG(I);
TEXT_TAG(U);

SHORT_TAG(BR);

SHORT_TAG(IMG);

// Headings.
SCOPED_TAG(H1);
SCOPED_TAG(H2);
SCOPED_TAG(H3);
SCOPED_TAG(H4);
SCOPED_TAG(H5);
SCOPED_TAG(H6);

// Lists.
SCOPED_TAG(UL);
SCOPED_TAG(OL);
SCOPED_TAG(LI);

// Preformatted blocks.
SCOPED_TAG(PRE);

// Tables.
SCOPED_TAG(TABLE);
SCOPED_TAG(THEAD);
SCOPED_TAG(TBODY);
SCOPED_TAG(TR);
SCOPED_TAG(TD);
SCOPED_TAG(TH);

// Form tags.
SCOPED_TAG(FORM);
SHORT_TAG(INPUT);
SCOPED_TAG(LABEL);
SCOPED_TAG(BUTTON);

// A block of text or HTML.
template <typename T>
void TEXT(T&& x) {
  ThreadLocalSingleton<State>().Append(std::forward<T>(x));
}

#undef SCOPED_TAG
#undef TEXT_TAG
#undef SHORT_TAG

}  // namespace html

#endif
