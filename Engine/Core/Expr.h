#pragma once
// Arithmetic in number fields (queue E-4), like Unity and Unreal: "2*3", "1.5 + 0.25", "-(4 - 1) / 2", "90 * 0.5".
// + - * / %, parentheses, unary minus, decimals and exponents ("1e-3"). No variables or functions.
#include <string_view>

namespace bdg {
// false when the text is not a whole expression (or divides by zero); `out` is left untouched then.
bool EvalExpression(std::string_view text, double& out);
}
