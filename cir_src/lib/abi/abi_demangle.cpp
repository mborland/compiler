/*
Part of the EDG Compiler Project, under the Apache License v2.0 with LLVM
Exceptions.
See https://edgcpp.org/LICENSE.txt for license information.
SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
*/

/*
abi_demangle.cpp -- see abi_demangle.h.  No recursion: a type is a run of
qualifier letters and one base, composed from the inside out.
*/

#include "abi_demangle.h"

#include <cctype>
#include <cstring>
#include <vector>

namespace edg_abi {

namespace {

struct a_parser {
  const std::string  &s;
  size_t              pos = 0;
  bool                failed = false;

  explicit a_parser(const std::string &text) : s(text) {}

  bool at_end() const
    { return pos >= s.size(); }
  char peek() const
    { return pos < s.size() ? s[pos] : '\0'; }
  void fail()
    { failed = true; }
};  /* a_parser */

std::string source_name(a_parser &p)
/*
<source-name>: a decimal length and that many identifier characters.
*/
{
  std::string result;
  size_t length = 0;

  if (!std::isdigit(static_cast<unsigned char>(p.peek()))) {
    p.fail();
  } else {
    while (std::isdigit(static_cast<unsigned char>(p.peek())) &&
           length < 100000) {
      length = length * 10 + static_cast<size_t>(p.peek() - '0');
      ++p.pos;
    }
    if (length == 0 || p.pos + length > p.s.size()) {
      p.fail();
    } else {
      result = p.s.substr(p.pos, length);
      p.pos += length;
      if (result.compare(0, 12, "_GLOBAL__N_1") == 0)
        result = "(anonymous namespace)";
    }
  }
  return result;
}  /* source_name */

const char *operator_name(const char *code)
/*
The spelling of a two-letter operator code, or nullptr.
*/
{
  static const struct { const char *code; const char *text; } table[] = {
    {"nw", "new"}, {"na", "new[]"}, {"dl", "delete"}, {"da", "delete[]"},
    {"ps", "+"}, {"ng", "-"}, {"ad", "&"}, {"de", "*"}, {"co", "~"},
    {"pl", "+"}, {"mi", "-"}, {"ml", "*"}, {"dv", "/"}, {"rm", "%"},
    {"an", "&"}, {"or", "|"}, {"eo", "^"}, {"aS", "="}, {"pL", "+="},
    {"mI", "-="}, {"mL", "*="}, {"dV", "/="}, {"rM", "%="}, {"aN", "&="},
    {"oR", "|="}, {"eO", "^="}, {"ls", "<<"}, {"rs", ">>"}, {"lS", "<<="},
    {"rS", ">>="}, {"eq", "=="}, {"ne", "!="}, {"lt", "<"}, {"gt", ">"},
    {"le", "<="}, {"ge", ">="}, {"ss", "<=>"}, {"nt", "!"}, {"aa", "&&"},
    {"oo", "||"}, {"pp", "++"}, {"mm", "--"}, {"cm", ","}, {"pm", "->*"},
    {"pt", "->"}, {"cl", "()"}, {"ix", "[]"}
  };
  const char *result = nullptr;

  for (const auto &entry : table) {
    if (entry.code[0] == code[0] && entry.code[1] == code[1]) {
      result = entry.text;
      break;
    }
  }
  return result;
}  /* operator_name */

/* A name: the components, the constructor/destructor kind of the last one
   and the cv-qualification of a member function. */
struct a_name {
  std::vector<std::string>  parts;
  char                      structor = 0;   /* 'C' or 'D'. */
  std::string               quals;          /* " const", " volatile". */
};  /* a_name */

bool parse_name(a_parser &p, a_name &out)
/*
<name>: a source-name, St<source-name>, or N [CV-quals] components E.  A
component is a source-name, a constructor or destructor code (after the
class), or an operator code.
*/
{
  bool nested = false;

  if (p.peek() == 'N') {
    nested = true;
    ++p.pos;
    while (p.peek() == 'K' || p.peek() == 'V') {
      out.quals += p.peek() == 'K' ? " const" : " volatile";
      ++p.pos;
    }
    /* A reference qualifier of the member function: not in the subset. */
    if (p.peek() == 'R' || p.peek() == 'O')
      p.fail();
  }
  for (;;) {
    char c = p.peek();

    if (p.failed || p.at_end()) {
      p.fail();
      break;
    }
    if (c == 'S' && p.pos + 1 < p.s.size() && p.s[p.pos + 1] == 't') {
      p.pos += 2;
      out.parts.push_back("std");
    } else if (std::isdigit(static_cast<unsigned char>(c))) {
      out.parts.push_back(source_name(p));
    } else if (c == 'C' && p.pos + 1 < p.s.size() &&
               p.s[p.pos + 1] >= '1' && p.s[p.pos + 1] <= '5' && nested) {
      p.pos += 2;
      out.structor = 'C';
    } else if (c == 'D' && p.pos + 1 < p.s.size() &&
               p.s[p.pos + 1] >= '0' && p.s[p.pos + 1] <= '5' && nested) {
      p.pos += 2;
      out.structor = 'D';
    } else if (std::islower(static_cast<unsigned char>(c)) &&
               p.pos + 1 < p.s.size() &&
               operator_name(p.s.c_str() + p.pos) != nullptr) {
      out.parts.push_back(std::string("operator") +
                          operator_name(p.s.c_str() + p.pos));
      p.pos += 2;
    } else {
      p.fail();   /* Template arguments, substitutions, ... */
      break;
    }
    if (!nested)
      break;
    if (p.peek() == 'E') {
      ++p.pos;
      break;
    }
    if (out.structor != 0) {
      p.fail();   /* Nothing follows a structor code. */
      break;
    }
  }
  return !p.failed && !out.parts.empty();
}  /* parse_name */

std::string name_text(const a_name &name)
/*
The qualified spelling of a parsed name ("A::f", "A::~A", "A::A").
*/
{
  std::string text;
  size_t count = name.parts.size();

  for (size_t k = 0; k < count; ++k) {
    if (k != 0)
      text += "::";
    text += name.parts[k];
  }
  if (name.structor != 0 && count != 0) {
    text += "::";
    if (name.structor == 'D')
      text += "~";
    text += name.parts[count - 1];
  }
  return text;
}  /* name_text */

bool builtin_type(char c, std::string &out)
/*
The spelling of a one-letter builtin type code.
*/
{
  bool ok = true;

  switch (c) {
  case 'v': out = "void"; break;
  case 'w': out = "wchar_t"; break;
  case 'b': out = "bool"; break;
  case 'c': out = "char"; break;
  case 'a': out = "signed char"; break;
  case 'h': out = "unsigned char"; break;
  case 's': out = "short"; break;
  case 't': out = "unsigned short"; break;
  case 'i': out = "int"; break;
  case 'j': out = "unsigned int"; break;
  case 'l': out = "long"; break;
  case 'm': out = "unsigned long"; break;
  case 'x': out = "long long"; break;
  case 'y': out = "unsigned long long"; break;
  case 'n': out = "__int128"; break;
  case 'o': out = "unsigned __int128"; break;
  case 'f': out = "float"; break;
  case 'd': out = "double"; break;
  case 'e': out = "long double"; break;
  case 'z': out = "..."; break;
  default: ok = false; break;
  }
  return ok;
}  /* builtin_type */

std::string parse_type(a_parser &p)
/*
<type>: qualifiers then a builtin, a class name or Di/Ds.  Composed from the
inside out (clang prints "const char *" and "char *const").
*/
{
  std::string base;
  std::string text;
  std::vector<char> modifiers;

  while (!p.failed && (p.peek() == 'P' || p.peek() == 'R' ||
                       p.peek() == 'O' || p.peek() == 'K' ||
                       p.peek() == 'V')) {
    modifiers.push_back(p.peek());
    ++p.pos;
  }
  if (p.at_end()) {
    p.fail();
  } else if (p.peek() == 'D' && p.pos + 1 < p.s.size() &&
             (p.s[p.pos + 1] == 'i' || p.s[p.pos + 1] == 's' ||
              p.s[p.pos + 1] == 'n')) {
    base = p.s[p.pos + 1] == 'i' ? "char32_t"
           : p.s[p.pos + 1] == 's' ? "char16_t" : "decltype(nullptr)";
    p.pos += 2;
  } else if (builtin_type(p.peek(), base)) {
    ++p.pos;
  } else {
    a_name name;

    if (!parse_name(p, name) || name.structor != 0)
      p.fail();
    else
      base = name_text(name);
  }
  text = base;
  for (size_t k = modifiers.size(); !p.failed && k > 0; --k) {
    char m = modifiers[k - 1];
    bool is_pointer = !text.empty() && (text.back() == '*' ||
                                        text.back() == '&');

    if (m == 'P')
      text += is_pointer ? "*" : " *";
    else if (m == 'R')
      text += is_pointer ? "&" : " &";
    else if (m == 'O')
      text += is_pointer ? "&&" : " &&";
    else if (is_pointer)
      text += m == 'K' ? " const" : " volatile";
    else
      text = std::string(m == 'K' ? "const " : "volatile ") + text;
  }
  return p.failed ? std::string() : text;
}  /* parse_type */

}  /* anonymous namespace */

std::string demangle_class(const std::string &type_mangled)
/*
See abi_demangle.h.
*/
{
  a_parser p(type_mangled);
  a_name name;
  std::string result;

  if (parse_name(p, name) && p.at_end() && name.structor == 0)
    result = name_text(name);
  return result;
}  /* demangle_class */

std::string demangle_function(const std::string &symbol)
/*
See abi_demangle.h.
*/
{
  std::string result;

  if (symbol.compare(0, 2, "_Z") == 0) {
    a_parser p(symbol);
    a_name name;
    std::string params;

    p.pos = 2;
    if (parse_name(p, name)) {
      while (!p.failed && !p.at_end()) {
        std::string type = parse_type(p);

        if (p.failed)
          break;
        if (type == "void" && params.empty() && p.at_end()) {
          /* f(void) is "f()". */
        } else {
          params += params.empty() ? type : ", " + type;
        }
      }
      if (!p.failed)
        result = name_text(name) + "(" + params + ")" + name.quals;
    }
  }
  return result;
}  /* demangle_function */

}  /* namespace edg_abi */
