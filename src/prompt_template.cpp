#include "prompt_template.hpp"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <set>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace sdcpp {

namespace {

// AST: a Sequence is a list of Atoms; an Atom is either a literal string or
// a Group. A Group is a list of options, each itself a Sequence; the group
// optionally has a pick_n meaning "produce all C(options.size(), pick_n)
// subsets, each joined with ', '".
struct Atom;
using Sequence = std::vector<Atom>;

struct Group {
    std::vector<Sequence> options;
    int pick_n = 1; // 1 = simple alternation; >1 = pick-N
};

struct Atom {
    bool is_literal = true;
    std::string literal;
    Group group;
};

// ---------- parser ----------

class Parser {
public:
    explicit Parser(const std::string& src) : src_(src) {}

    Sequence parse_top() {
        Sequence seq = parse_sequence(/*stop_on_brace*/ false);
        if (pos_ != src_.size()) {
            throw std::runtime_error("Prompt template: unexpected '}' at position "
                                     + std::to_string(pos_));
        }
        return seq;
    }

private:
    // Parse a run of literal text + groups, stopping at end-of-input. If
    // stop_on_brace is true, also stop at a top-level '|' or '}' so the caller
    // (parse_group) can pick up the alternation.
    Sequence parse_sequence(bool stop_on_brace) {
        Sequence seq;
        std::string buf;

        auto flush_literal = [&]() {
            if (!buf.empty()) {
                Atom a;
                a.is_literal = true;
                a.literal = std::move(buf);
                buf.clear();
                seq.push_back(std::move(a));
            }
        };

        while (pos_ < src_.size()) {
            char c = src_[pos_];

            if (stop_on_brace && (c == '|' || c == '}')) {
                break;
            }

            if (c == '\\' && pos_ + 1 < src_.size()) {
                // Escape: \{ \} \| treated as literal characters.
                char next = src_[pos_ + 1];
                if (next == '{' || next == '}' || next == '|' || next == '\\') {
                    buf += next;
                    pos_ += 2;
                    continue;
                }
            }

            if (c == '{') {
                flush_literal();
                Atom a;
                a.is_literal = false;
                a.group = parse_group();
                seq.push_back(std::move(a));
                continue;
            }

            // '|' or '}' at top-level (not inside a group) — treat as literal,
            // since they have no syntactic meaning outside a group.
            buf += c;
            ++pos_;
        }

        flush_literal();
        return seq;
    }

    Group parse_group() {
        // src_[pos_] == '{'
        ++pos_; // consume '{'
        Group g;

        // Optional N$$ prefix. Only valid if the first thing inside is digits
        // followed by "$$".
        size_t save = pos_;
        bool has_pick = false;
        int pick = 0;
        while (pos_ < src_.size() && std::isdigit(static_cast<unsigned char>(src_[pos_]))) {
            pick = pick * 10 + (src_[pos_] - '0');
            ++pos_;
            has_pick = true;
        }
        if (has_pick && pos_ + 1 < src_.size() && src_[pos_] == '$' && src_[pos_ + 1] == '$') {
            pos_ += 2;
            if (pick <= 0) {
                throw std::runtime_error("Prompt template: pick-N count must be >= 1");
            }
            g.pick_n = pick;
        } else {
            // Not a pick-N prefix — rewind so the digits are part of the first
            // option's literal text.
            pos_ = save;
            g.pick_n = 1;
        }

        // Parse one or more options separated by '|'.
        while (true) {
            g.options.push_back(parse_sequence(/*stop_on_brace*/ true));
            if (pos_ >= src_.size()) {
                throw std::runtime_error("Prompt template: unterminated '{' (missing '}')");
            }
            char c = src_[pos_];
            if (c == '|') {
                ++pos_;
                continue;
            }
            if (c == '}') {
                ++pos_; // consume '}'
                break;
            }
            // Should not reach (parse_sequence would have stopped on these).
            throw std::runtime_error("Prompt template: parser desync at "
                                     + std::to_string(pos_));
        }

        if (g.pick_n > static_cast<int>(g.options.size())) {
            throw std::runtime_error(
                "Prompt template: pick-N count " + std::to_string(g.pick_n)
                + " exceeds number of options " + std::to_string(g.options.size()));
        }
        return g;
    }

    const std::string& src_;
    size_t pos_ = 0;
};

// ---------- expansion ----------

std::vector<std::string> expand_sequence(const Sequence& seq);

// Expand a single Atom into all possible string fragments.
std::vector<std::string> expand_atom(const Atom& atom) {
    if (atom.is_literal) {
        return {atom.literal};
    }
    const Group& g = atom.group;

    if (g.pick_n == 1) {
        // Simple alternation: union of all options' expansions.
        std::vector<std::string> out;
        for (const auto& opt : g.options) {
            auto sub = expand_sequence(opt);
            for (auto& s : sub) out.push_back(std::move(s));
        }
        return out;
    }

    // Pick-N: enumerate all C(k, N) subsets of options (in their original
    // order — so {2$$a|b|c} → "a, b", "a, c", "b, c"). For each subset, the
    // chosen options are themselves expanded; we cross-product across them
    // and join the resulting fragments with ", ".
    const int N = g.pick_n;
    const int K = static_cast<int>(g.options.size());

    // Pre-compute each option's expansion list.
    std::vector<std::vector<std::string>> per_option;
    per_option.reserve(g.options.size());
    for (const auto& opt : g.options) {
        per_option.push_back(expand_sequence(opt));
    }

    std::vector<std::string> out;

    // Combinations of K-choose-N indices (lexicographic order).
    std::vector<int> indices(N);
    for (int i = 0; i < N; ++i) indices[i] = i;

    while (true) {
        // Cross product of expansions for each chosen index, joined by ", ".
        std::vector<std::string> acc = {""};
        for (int slot = 0; slot < N; ++slot) {
            const auto& choices = per_option[indices[slot]];
            std::vector<std::string> next;
            next.reserve(acc.size() * choices.size());
            for (const auto& a : acc) {
                for (const auto& c : choices) {
                    if (a.empty()) next.push_back(c);
                    else next.push_back(a + ", " + c);
                }
            }
            acc = std::move(next);
        }
        for (auto& s : acc) out.push_back(std::move(s));

        // Advance the combination indices (next K-choose-N).
        int i = N - 1;
        while (i >= 0 && indices[i] == K - N + i) --i;
        if (i < 0) break;
        ++indices[i];
        for (int j = i + 1; j < N; ++j) indices[j] = indices[j - 1] + 1;
    }

    return out;
}

// Expand a Sequence: cross-product across its atoms.
std::vector<std::string> expand_sequence(const Sequence& seq) {
    std::vector<std::string> acc = {""};
    for (const auto& atom : seq) {
        auto frags = expand_atom(atom);
        if (frags.empty()) {
            // Atom with no choices — collapses the whole product to nothing.
            // (Shouldn't happen with valid input but guard anyway.)
            return {};
        }
        std::vector<std::string> next;
        next.reserve(acc.size() * frags.size());
        for (const auto& a : acc) {
            for (const auto& f : frags) {
                next.push_back(a + f);
            }
        }
        acc = std::move(next);
    }
    return acc;
}

// Counting variant — same algorithm but returns sizes instead of materializing
// the strings.
size_t count_atom(const Atom& atom);

size_t count_sequence(const Sequence& seq) {
    size_t product = 1;
    for (const auto& atom : seq) {
        size_t n = count_atom(atom);
        if (n == 0) return 0;
        // Saturating multiply — caller should reject absurd counts before
        // calling expand. We cap at SIZE_MAX/2 to avoid overflow during
        // further multiplications.
        if (product > (SIZE_MAX / 2) / std::max<size_t>(n, 1)) {
            return SIZE_MAX;
        }
        product *= n;
    }
    return product;
}

size_t count_atom(const Atom& atom) {
    if (atom.is_literal) return 1;
    const Group& g = atom.group;

    if (g.pick_n == 1) {
        size_t total = 0;
        for (const auto& opt : g.options) {
            total += count_sequence(opt);
        }
        return total;
    }

    // Pick-N: sum over all C(K, N) subsets of (product of option counts).
    const int N = g.pick_n;
    const int K = static_cast<int>(g.options.size());

    std::vector<size_t> per_option;
    per_option.reserve(g.options.size());
    for (const auto& opt : g.options) {
        per_option.push_back(count_sequence(opt));
    }

    size_t total = 0;
    std::vector<int> indices(N);
    for (int i = 0; i < N; ++i) indices[i] = i;
    while (true) {
        size_t product = 1;
        for (int slot = 0; slot < N; ++slot) {
            size_t n = per_option[indices[slot]];
            if (n == 0) { product = 0; break; }
            if (product > SIZE_MAX / std::max<size_t>(n, 1)) { return SIZE_MAX; }
            product *= n;
        }
        total += product;

        int i = N - 1;
        while (i >= 0 && indices[i] == K - N + i) --i;
        if (i < 0) break;
        ++indices[i];
        for (int j = i + 1; j < N; ++j) indices[j] = indices[j - 1] + 1;
    }
    return total;
}

// Stable dedup that preserves first-seen order.
void stable_dedup(std::vector<std::string>& v) {
    std::set<std::string> seen;
    std::vector<std::string> out;
    out.reserve(v.size());
    for (auto& s : v) {
        if (seen.insert(s).second) {
            out.push_back(std::move(s));
        }
    }
    v = std::move(out);
}

} // anonymous namespace

std::vector<std::string> expand_prompt_template(const std::string& templated) {
    if (templated.find('{') == std::string::npos) {
        // Fast path: no template syntax at all.
        return {templated};
    }
    Parser p(templated);
    Sequence top = p.parse_top();
    auto out = expand_sequence(top);
    stable_dedup(out);
    return out;
}

size_t count_prompt_variations(const std::string& templated) {
    if (templated.find('{') == std::string::npos) return 1;
    Parser p(templated);
    Sequence top = p.parse_top();
    return count_sequence(top);
    // Note: counting overcounts when expansion has duplicates (e.g.
    // {a|a|b} reports 3 but expand returns 2). Acceptable: the warning
    // modal is a ceiling, the actual job count uses expand().size().
}

} // namespace sdcpp
