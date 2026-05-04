#pragma once

#include <filesystem>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace sdcpp {

struct DocSearchResult {
    std::string doc_filename;     // e.g. "RUNPOD.md"
    std::string section;          // last heading on the path, e.g. "Bootstrap profiles"
    std::string section_path;     // full hierarchy, e.g. "Choosing a GPU > Bootstrap profiles"
    std::string content;          // chunk text (already truncated to a reasonable size)
    double      score;            // BM25 score
};

/**
 * In-memory BM25 index over the project's markdown documentation.
 *
 * Built once at server startup from the configured docs directory. The
 * assistant LLM exposes a `search_docs(query)` tool that ranks chunks
 * against the user's question, so the assistant can answer "how do I X?"
 * questions about features (auth, mount, RunPod deploy, etc.) without
 * needing an external embedding service.
 *
 * Why BM25 instead of vector embeddings:
 *   - Works on RunPod / any deploy with no embedding model dependency.
 *   - Pre-computable at server start; no per-query model call.
 *   - For technical-keyword-heavy docs and "how do I X" questions, quality
 *     is on par with embeddings while costing 0 dependencies.
 */
class DocsIndex {
public:
    /**
     * Read every *.md under docs_dir, chunk by H2/H3 sections, build the
     * BM25 index. Empty / missing dir → empty index (search returns []).
     */
    explicit DocsIndex(const std::string& docs_dir);

    /**
     * Rank chunks against `query` by BM25. Returns at most `max_results`
     * results, ordered descending by score. Empty list if the corpus is
     * empty or no terms match.
     */
    std::vector<DocSearchResult> search(const std::string& query,
                                         std::size_t max_results = 3) const;

    std::size_t chunk_count() const { return chunks_.size(); }
    const std::string& docs_dir() const { return docs_dir_; }

private:
    struct Chunk {
        std::string doc_filename;
        std::string section;
        std::string section_path;
        std::string content;
        std::vector<std::string> tokens;
    };

    void index_file(const std::filesystem::path& path);

    // Tokenize: lowercase, split on non-alphanumeric, drop tokens < 2 chars
    // and a tiny English stopword set. Static so the search path can re-use it.
    static std::vector<std::string> tokenize(const std::string& text);

    std::string                 docs_dir_;
    std::vector<Chunk>          chunks_;
    // term → list of (chunk index, term frequency in that chunk)
    std::unordered_map<std::string,
                       std::vector<std::pair<std::size_t, int>>> postings_;
    std::vector<std::size_t>    chunk_lengths_;       // token count per chunk
    double                      avg_chunk_length_ = 0;
};

} // namespace sdcpp
