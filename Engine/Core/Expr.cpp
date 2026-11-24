#include "Core/Expr.h"
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <string>

namespace bdg {
namespace {
struct Parser {
  std::string_view s;
  size_t i = 0;
  bool ok = true;
  void Skip() { while (i < s.size() && std::isspace((unsigned char)s[i])) ++i; }
  bool Eat(char c) {
    Skip();
    if (i < s.size() && s[i] == c) { ++i; return true; }
    return false;
  }
  double Number() {
    Skip();
    const std::string rest(s.substr(i));
    char* end = nullptr;
    const double v = std::strtod(rest.c_str(), &end);
    if (end == rest.c_str() || (rest[0] != '.' && !std::isdigit((unsigned char)rest[0]))) { ok = false; return 0; }
    i += (size_t)(end - rest.c_str());
    return v;
  }
  double Primary(int depth) {
    if (depth > 64) { ok = false; return 0; }
    if (Eat('-')) return -Primary(depth + 1);
    if (Eat('+')) return Primary(depth + 1);
    if (Eat('(')) {
      const double v = Sum(depth + 1);
      if (!Eat(')')) ok = false;
      return v;
    }
    return Number();
  }
  double Product(int depth) {
    double v = Primary(depth);
    while (ok) {
      if (Eat('*')) v *= Primary(depth);
      else if (Eat('/')) {
        const double d = Primary(depth);
        if (d == 0) { ok = false; return 0; }
        v /= d;
      } else if (Eat('%')) {
        const double d = Primary(depth);
        if (d == 0) { ok = false; return 0; }
        v = std::fmod(v, d);
      } else break;
    }
    return v;
  }
  double Sum(int depth) {
    double v = Product(depth);
    while (ok) {
      if (Eat('+')) v += Product(depth);
      else if (Eat('-')) v -= Product(depth);
      else break;
    }
    return v;
  }
};
} // namespace

bool EvalExpression(std::string_view text, double& out) {
  Parser p{text};
  const double v = p.Sum(0);
  p.Skip();
  if (!p.ok || p.i != text.size() || !std::isfinite(v)) return false;
  out = v;
  return true;
}
} // namespace bdg
