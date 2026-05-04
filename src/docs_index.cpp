#include "docs_index.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <fstream>
#include <iostream>
#include <sstream>
#include <unordered_set>

namespace sdcpp {

namespace fs = std::filesystem;

namespace {

// Tiny English stopword set. Keeping this small on purpose — for technical
// docs the IDF term in BM25 already discounts very common words. Removing
// them just shrinks the index a bit and avoids "the" matching everywhere.
const std::unordered_set<std::string>& stopwords() {
    static const std::unordered_set<std::string> s = {
        "a", "an", "the", "is", "are", "was", "were", "be", "been", "being",
        "of", "to", "in", "on", "at", "for", "with", "by", "from", "as",
        "and", "or", "but", "if", "then", "else", "so", "than",
        "this", "that", "these", "those", "it", "its", "itself",
        "you", "your", "yours", "we", "our", "us", "i",
        "do", "does", "did", "done",
        "have", "has", "had", "having",
        "will", "would", "should", "can", "could", "may", "might",
        "not", "no",
    };
    return s;
}

// Soft chunk size cap (in tokens). Sections longer than this get split on
// blank lines / paragraph boundaries.
constexpr std::size_t kSoftChunkLimit = 350;

// Hard truncation when returning content to the LLM (in chars), so the
// response stays small even if a chunk happens to be huge.
constexpr std::size_t kReturnedContentBytes = 1200;

bool starts_with(const std::string& s, const std::string& prefix) {
    return s.size() >= prefix.size() &&
           std::equal(prefix.begin(), prefix.end(), s.begin());
}

// Trim leading/trailing whitespace (in place).
void trim(std::string& s) {
    auto issp = [](unsigned char c) { return std::isspace(c) != 0; };
    while (!s.empty() && issp(s.front())) s.erase(s.begin());
    while (!s.empty() && issp(s.back()))  s.pop_back();
}

// Strip basic markdown noise so the chunk reads naturally. Keeps URLs,
// inline code, etc. — just removes leading list/quote markers, link
// brackets around text. Cheap and good enough for retrieval prep.
std::string clean_markdown_line(std::string line) {
    // Strip leading list/quote markers
    std::size_t i = 0;
    while (i < line.size() && (line[i] == ' ' || line[i] == '\t')) ++i;
    if (i < line.size() && (line[i] == '*' || line[i] == '-' || line[i] == '+') &&
        i + 1 < line.size() && line[i + 1] == ' ') {
        line.erase(0, i + 2);
    } else if (i < line.size() && line[i] == '>') {
        line.erase(0, i + 1);
    }
    return line;
}

} // namespace

DocsIndex::DocsIndex(const std::string& docs_dir)
    : docs_dir_(docs_dir) {
    if (docs_dir_.empty() || !fs::exists(docs_dir_) || !fs::is_directory(docs_dir_)) {
        std::cout << "[DocsIndex] No docs directory configured; assistant won't have docs context." << std::endl;
        return;
    }

    // Walk *.md files in the directory (non-recursive — keeps URLs in
    // search results predictable; if you want sub-dirs later, enable
    // recursive_directory_iterator and reflect the relative path in
    // doc_filename).
    std::error_code ec;
    for (auto& entry : fs::directory_iterator(docs_dir_, ec)) {
        if (ec) break;
        if (!entry.is_regular_file()) continue;
        auto p = entry.path();
        if (p.extension() != ".md" && p.extension() != ".MD") continue;
        index_file(p);
    }

    // Compute avg chunk length for BM25's length-normalization term.
    if (!chunk_lengths_.empty()) {
        std::size_t total = 0;
        for (auto n : chunk_lengths_) total += n;
        avg_chunk_length_ = static_cast<double>(total) / chunk_lengths_.size();
    }

    std::cout << "[DocsIndex] Indexed " << chunks_.size()
              << " chunks across " << "*.md in " << docs_dir_
              << " (avg " << static_cast<int>(avg_chunk_length_)
              << " tokens/chunk)." << std::endl;
}

void DocsIndex::index_file(const fs::path& path) {
    std::ifstream f(path);
    if (!f) return;
    std::string filename = path.filename().string();

    // Heading hierarchy as we walk. Index 0 = H1, 1 = H2, 2 = H3, etc.
    std::vector<std::string> heading_stack;

    auto flush_chunk = [&](const std::string& content) {
        std::string trimmed = content;
        trim(trimmed);
        if (trimmed.empty()) return;

        Chunk c;
        c.doc_filename = filename;
        c.section = heading_stack.empty() ? std::string{} : heading_stack.back();
        // Build path string from the stack ("A > B > C")
        for (std::size_t i = 0; i < heading_stack.size(); ++i) {
            if (i) c.section_path += " > ";
            c.section_path += heading_stack[i];
        }
        c.content = trimmed;
        c.tokens  = tokenize(trimmed + " " + c.section_path);

        // Empty-token chunks (e.g. just a code block of unique symbols) are
        // useless for BM25. Skip them.
        if (c.tokens.empty()) return;

        const std::size_t chunk_idx = chunks_.size();
        chunk_lengths_.push_back(c.tokens.size());

        // Term-frequency map for this chunk
        std::unordered_map<std::string, int> tf;
        for (auto& t : c.tokens) ++tf[t];
        for (auto& [term, count] : tf) {
            postings_[term].emplace_back(chunk_idx, count);
        }
        chunks_.push_back(std::move(c));
    };

    std::string line;
    std::ostringstream buf;
    bool in_code_fence = false;
    std::size_t buf_token_estimate = 0;

    while (std::getline(f, line)) {
        // Code fences: copy verbatim and don't try to parse markdown inside.
        if (starts_with(line, "```")) {
            in_code_fence = !in_code_fence;
            buf << line << "\n";
            continue;
        }
        if (in_code_fence) {
            buf << line << "\n";
            buf_token_estimate += 6;  // rough — code is dense
            continue;
        }

        // Headings — H1/H2/H3 close the previous chunk and update stack.
        if (starts_with(line, "# ") || starts_with(line, "## ") ||
            starts_with(line, "### ")) {
            int level = 1;
            if (starts_with(line, "### ")) level = 3;
            else if (starts_with(line, "## ")) level = 2;

            // Flush whatever's been accumulating in the previous section.
            flush_chunk(buf.str());
            buf.str(""); buf.clear();
            buf_token_estimate = 0;

            // Truncate the heading stack to the new level - 1, then push.
            heading_stack.resize(std::min<std::size_t>(heading_stack.size(),
                                                       static_cast<std::size_t>(level - 1)));
            std::string heading = line.substr(static_cast<std::size_t>(level + 1));
            trim(heading);
            heading_stack.push_back(heading);
            continue;
        }

        // Soft-split very long sections on blank lines so the LLM gets
        // tighter chunks. We don't go below paragraph granularity.
        if (line.empty() && buf_token_estimate >= kSoftChunkLimit) {
            flush_chunk(buf.str());
            buf.str(""); buf.clear();
            buf_token_estimate = 0;
            continue;
        }

        std::string cleaned = clean_markdown_line(line);
        buf << cleaned << "\n";
        // Cheap token-count estimate without actually tokenizing each line.
        buf_token_estimate += static_cast<std::size_t>(cleaned.size() / 5 + 1);
    }
    flush_chunk(buf.str());
}

std::vector<std::string> DocsIndex::tokenize(const std::string& text) {
    std::vector<std::string> out;
    out.reserve(text.size() / 5);
    std::string cur;
    cur.reserve(32);
    auto flush = [&]() {
        if (cur.size() < 2) { cur.clear(); return; }
        if (stopwords().count(cur)) { cur.clear(); return; }
        out.push_back(cur);
        cur.clear();
    };
    for (unsigned char c : text) {
        if (std::isalnum(c)) {
            cur.push_back(static_cast<char>(std::tolower(c)));
        } else {
            flush();
        }
    }
    flush();
    return out;
}

std::vector<DocSearchResult> DocsIndex::search(const std::string& query,
                                                std::size_t max_results) const {
    if (chunks_.empty()) return {};

    auto q_tokens = tokenize(query);
    if (q_tokens.empty()) return {};

    // BM25 hyperparameters (Okapi defaults — work well for short docs).
    constexpr double k1 = 1.2;
    constexpr double b  = 0.75;
    const double N      = static_cast<double>(chunks_.size());

    std::unordered_map<std::size_t, double> scores;
    for (const auto& term : q_tokens) {
        auto it = postings_.find(term);
        if (it == postings_.end()) continue;
        const auto& posting_list = it->second;
        const double df  = static_cast<double>(posting_list.size());
        // +1 inside log to avoid negative IDF for terms in >50% of docs.
        const double idf = std::log((N - df + 0.5) / (df + 0.5) + 1.0);
        for (const auto& [chunk_idx, tf_int] : posting_list) {
            const double tf = static_cast<double>(tf_int);
            const double dl = static_cast<double>(chunk_lengths_[chunk_idx]);
            const double norm = 1.0 - b + b * (dl / std::max(1.0, avg_chunk_length_));
            scores[chunk_idx] += idf * (tf * (k1 + 1.0)) / (tf + k1 * norm);
        }
    }
    if (scores.empty()) return {};

    // Top-K selection.
    std::vector<std::pair<std::size_t, double>> ranked(scores.begin(), scores.end());
    const std::size_t take = std::min(max_results, ranked.size());
    std::partial_sort(
        ranked.begin(), ranked.begin() + take, ranked.end(),
        [](const auto& a, const auto& b) { return a.second > b.second; });
    ranked.resize(take);

    std::vector<DocSearchResult> out;
    out.reserve(take);
    for (auto& [idx, score] : ranked) {
        const auto& c = chunks_[idx];
        DocSearchResult r;
        r.doc_filename = c.doc_filename;
        r.section      = c.section;
        r.section_path = c.section_path;
        r.score        = score;
        if (c.content.size() > kReturnedContentBytes) {
            r.content = c.content.substr(0, kReturnedContentBytes) + "\n...[truncated]";
        } else {
            r.content = c.content;
        }
        out.push_back(std::move(r));
    }
    return out;
}

} // namespace sdcpp
