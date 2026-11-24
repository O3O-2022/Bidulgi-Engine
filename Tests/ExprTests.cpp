// Number field arithmetic (queue E-4).
#include "Core/Expr.h"
#include "bdg_test.h"
#include <cmath>

using namespace bdg;

TEST_CASE("expr: arithmetic in number fields (queue E-4)") {
  double v = 0;
  auto is = [&](const char* text, double want) { return EvalExpression(text, v) && std::abs(v - want) < 1e-9; };
  CHECK(is("2*3", 6));
  CHECK(is(" 1.5 + 0.25 ", 1.75));
  CHECK(is("-(4 - 1) / 2", -1.5));
  CHECK(is("2 + 3 * 4", 14));
  CHECK(is("(2 + 3) * 4", 20));
  CHECK(is("10 % 4", 2));
  CHECK(is("1e-3 * 1000", 1));
  CHECK(is(".5", 0.5));
  CHECK(is("--2", 2));
  CHECK(is("42", 42));
  v = 7;
  CHECK(!EvalExpression("1/0", v) && v == 7); // untouched on failure
  CHECK(!EvalExpression("2 *", v));
  CHECK(!EvalExpression("(1 + 2", v));
  CHECK(!EvalExpression("abc", v));
  CHECK(!EvalExpression("", v));
  CHECK(!EvalExpression("1 2", v));
}
