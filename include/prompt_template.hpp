#pragma once

#include <string>
#include <vector>

namespace sdcpp {

// Combinatorial expansion of dynamic-prompt syntax (a1111-style "DynamicPrompts"
// inline syntax — no wildcard files).
//
// Supported:
//   {a|b|c}            — alternation, expands to 3 prompts (one per choice).
//   {N$$a|b|c|d}       — pick-N, expands to C(k, N) prompts (one per N-element
//                        subset of the choices, joined by ", "). N must be a
//                        positive integer not greater than the number of choices.
//
// Nesting is supported: {dog|cat|{red|blue} fox} → 4 expansions.
// Empty alternatives are kept (so `{|red}` legitimately means
// "either nothing or 'red'").
// Mismatched braces or invalid pick-N counts throw std::runtime_error.
//
// The result is deduplicated in stable order, so identical expansion paths only
// produce one prompt each. The order across the input is left-to-right: each
// {…} is expanded in its appearance order.
//
// On a non-templated prompt (no `{…}`), returns a single-element vector
// containing the input verbatim — callers can use this unconditionally.
std::vector<std::string> expand_prompt_template(const std::string& templated);

// Same syntax, count-only — returns the number of variations the template
// would expand to, without materializing the strings. Used by /txt2img to
// validate batch size before allocation. Equivalent to:
//   expand_prompt_template(t).size()
// but cheaper for large fan-outs.
size_t count_prompt_variations(const std::string& templated);

} // namespace sdcpp
