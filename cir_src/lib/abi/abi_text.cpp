/*
Part of the EDG Compiler Project, under the Apache License v2.0 with LLVM
Exceptions.
See https://edgcpp.org/LICENSE.txt for license information.
SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
*/

/*
abi_text.cpp -- reading and writing the text form of the class facts.
*/

#include "abi_text.h"

#include <cstdlib>
#include <sstream>

namespace edg_abi {

namespace {

bool next_token(const std::string &line, size_t &pos, std::string &token,
                bool &quoted)
/*
The next blank-separated token; a quoted one ("...", with \" and \\) keeps
its blanks.
*/
{
  bool found = false;

  quoted = false;
  token.clear();
  while (pos < line.size() && (line[pos] == ' ' || line[pos] == '\t'))
    ++pos;
  if (pos < line.size()) {
    found = true;
    if (line[pos] == '"') {
      quoted = true;
      ++pos;
      while (pos < line.size() && line[pos] != '"') {
        if (line[pos] == '\\' && pos + 1 < line.size())
          ++pos;
        token += line[pos++];
      }
      if (pos < line.size())
        ++pos;
    } else {
      while (pos < line.size() && line[pos] != ' ' && line[pos] != '\t')
        token += line[pos++];
    }
  }
  return found;
}  /* next_token */

bool to_int(const std::string &token, int64_t &value)
/*
Parse a signed decimal integer.
*/
{
  char *end = nullptr;
  long long v = std::strtoll(token.c_str(), &end, 10);

  value = v;
  return !token.empty() && end != nullptr && *end == '\0';
}  /* to_int */

}  /* anonymous namespace */

std::string parse_facts(const std::string &text, a_class_db &db)
/*
Parse the text form.
*/
{
  std::istringstream in(text);
  std::string line;
  std::string problem;
  size_t line_no = 0;
  a_class_id current_class = no_id;
  a_fn_id current_fn = no_id;

  db.classes.clear();
  db.fns.clear();
  while (problem.empty() && std::getline(in, line)) {
    size_t pos = 0;
    std::string key;
    bool quoted;
    std::vector<std::string> tokens;
    std::string token;
    bool indented = !line.empty() && (line[0] == ' ' || line[0] == '\t');

    ++line_no;
    if (!next_token(line, pos, key, quoted) || key[0] == '#')
      continue;
    while (next_token(line, pos, token, quoted)) {
      if (!quoted && token[0] == '#')
        break;
      tokens.push_back(token);
    }
    auto num = [&](size_t i, int64_t &v) {
      return i < tokens.size() && to_int(tokens[i], v);
    };
    int64_t a = 0, b = 0, c = 0, d = 0;
    std::string where = "line " + std::to_string(line_no) + ": ";

    if (key == "pointer_size" && num(0, a)) {
      db.pointer_size = static_cast<uint32_t>(a);
    } else if (key == "class" && !indented && tokens.size() >= 14 &&
               num(0, a)) {
      /* class ID NAME size N nvsize N dynamic B primary ID pvirtual B
         nslots N */
      a_class_fact f;
      int64_t size, nvsize, dyn, prim, pv, ns, e = 0;

      if (a != static_cast<int64_t>(db.classes.size()) ||
          !num(3, size) || !num(5, nvsize) || !num(7, dyn) ||
          !num(9, prim) || !num(11, pv) || !num(13, ns)) {
        problem = where + "bad class line (ids must be dense, in order)";
      } else {
        f.name = tokens[1];
        f.size = size;
        f.nvsize = nvsize;
        f.is_dynamic = dyn != 0;
        f.primary = prim < 0 ? no_id : static_cast<a_class_id>(prim);
        f.primary_virtual = pv != 0;
        f.nslots = static_cast<uint32_t>(ns);
        for (size_t i = 14; i + 1 < tokens.size(); i += 2) {
          if (tokens[i] == "mangled") {
            f.mangled = tokens[i + 1];
          } else if (tokens[i] == "empty" && num(i + 1, e)) {
            f.is_empty = e != 0;
          } else {
            problem = where + "unknown class attribute \"" + tokens[i] +
                      "\"";
          }
        }
        db.classes.push_back(f);
        current_class = static_cast<a_class_id>(a);
        current_fn = no_id;
      }
    } else if (key == "base" && indented && current_class != no_id &&
               num(0, a) && num(1, b) && num(2, c)) {
      int64_t pub = 1;

      if (tokens.size() >= 5 && tokens[3] == "public")
        num(4, pub);
      db.classes[current_class].bases.push_back(
          {static_cast<a_class_id>(a), b != 0, c, pub != 0});
    } else if (key == "vbase" && indented && current_class != no_id &&
               num(0, a) && num(1, b)) {
      int64_t pub = 1;

      if (tokens.size() >= 4 && tokens[2] == "public")
        num(3, pub);
      db.classes[current_class].vbases.push_back(
          {static_cast<a_class_id>(a), true, b, pub != 0});
    } else if (key == "fns" && indented && current_class != no_id) {
      for (size_t i = 0; i < tokens.size(); ++i) {
        if (!num(i, a))
          problem = where + "bad function id";
        else
          db.classes[current_class].fns.push_back(static_cast<a_fn_id>(a));
      }
    } else if (key == "ovr" && indented && current_class != no_id &&
               num(0, a) && num(1, b) && num(2, c) && num(3, d)) {
      db.classes[current_class].overrides.push_back(
          {static_cast<a_fn_id>(a), b, static_cast<a_fn_id>(c), d});
    } else if (key == "fn" && !indented && tokens.size() >= 18 && num(0, a)) {
      /* fn ID NAME owner ID slot N pure B deleted B special N implicit B
         sig N ret ID */
      a_fn_fact f;
      int64_t owner, slot, pure, del, special, impl, sig, ret;

      if (a != static_cast<int64_t>(db.fns.size()) || !num(3, owner) ||
          !num(5, slot) || !num(7, pure) || !num(9, del) ||
          !num(11, special) || !num(13, impl) || !num(15, sig) ||
          !num(17, ret)) {
        problem = where + "bad fn line (ids must be dense, in order)";
      } else {
        f.name = tokens[1];
        f.owner = static_cast<a_class_id>(owner);
        f.slot = static_cast<int32_t>(slot);
        f.is_pure = pure != 0;
        f.is_deleted = del != 0;
        f.special = static_cast<a_fn_special>(special);
        f.is_implicit = impl != 0;
        f.sig = static_cast<uint32_t>(sig);
        f.ret_class = ret < 0 ? no_id : static_cast<a_class_id>(ret);
        for (size_t i = 18; i + 1 < tokens.size(); i += 2) {
          if (tokens[i] == "mangled")
            f.mangled = tokens[i + 1];
          else
            problem = where + "unknown fn attribute \"" + tokens[i] + "\"";
        }
        db.fns.push_back(f);
        current_fn = static_cast<a_fn_id>(a);
        current_class = no_id;
      }
    } else if (key == "overridden" && indented && current_fn != no_id) {
      for (size_t i = 0; i < tokens.size(); ++i) {
        if (!num(i, a))
          problem = where + "bad function id";
        else
          db.fns[current_fn].overridden.push_back(static_cast<a_fn_id>(a));
      }
    } else {
      problem = where + "cannot parse \"" + key + "\"";
    }
  }
  if (problem.empty())
    problem = db.validate();
  return problem;
}  /* parse_facts */

std::string write_facts(const a_class_db &db)
/*
The text form.
*/
{
  std::ostringstream out;

  out << "pointer_size " << db.pointer_size << "\n";
  for (size_t i = 0; i < db.classes.size(); ++i) {
    const a_class_fact &c = db.classes[i];

    out << "class " << i << " \"" << c.name << "\" size " << c.size
        << " nvsize " << c.nvsize << " dynamic " << (c.is_dynamic ? 1 : 0)
        << " primary " << (c.primary == no_id ? -1 : int64_t(c.primary))
        << " pvirtual " << (c.primary_virtual ? 1 : 0) << " nslots "
        << c.nslots;
    if (!c.mangled.empty())
      out << " mangled \"" << c.mangled << "\"";
    if (c.is_empty)
      out << " empty 1";
    out << "\n";
    for (const a_base_fact &b : c.bases)
      out << "  base " << b.cls << " " << (b.is_virtual ? 1 : 0) << " "
          << b.offset << (b.is_public ? "" : " public 0") << "\n";
    for (const a_base_fact &b : c.vbases)
      out << "  vbase " << b.cls << " " << b.offset
          << (b.is_public ? "" : " public 0") << "\n";
    if (!c.fns.empty()) {
      out << "  fns";
      for (a_fn_id f : c.fns)
        out << " " << f;
      out << "\n";
    }
    for (const a_override_fact &o : c.overrides)
      out << "  ovr " << o.fn << " " << o.sub_off << " " << o.ovr << " "
          << o.ovr_off << "\n";
  }
  for (size_t i = 0; i < db.fns.size(); ++i) {
    const a_fn_fact &f = db.fns[i];

    out << "fn " << i << " \"" << f.name << "\" owner " << f.owner
        << " slot " << f.slot << " pure " << (f.is_pure ? 1 : 0)
        << " deleted " << (f.is_deleted ? 1 : 0) << " special "
        << int(f.special) << " implicit " << (f.is_implicit ? 1 : 0)
        << " sig " << f.sig << " ret "
        << (f.ret_class == no_id ? -1 : int64_t(f.ret_class));
    if (!f.mangled.empty())
      out << " mangled \"" << f.mangled << "\"";
    out << "\n";
    if (!f.overridden.empty()) {
      out << "  overridden";
      for (a_fn_id g : f.overridden)
        out << " " << g;
      out << "\n";
    }
  }
  return out.str();
}  /* write_facts */

}  /* namespace edg_abi */
