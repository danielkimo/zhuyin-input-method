#include "zhuyin/engine.h"

#include <algorithm>
#include <limits>

namespace zhuyin {
namespace {

constexpr double kUnknownSyllablePenalty = -1e9;  // last-resort fallback only

}  // namespace

std::vector<Candidate> Decoder::ComposeBestSentence(
    const std::vector<std::string>& syllables) const {
  const size_t n = syllables.size();
  std::vector<Candidate> result;
  if (n == 0) return result;

  const size_t max_len =
      std::max<size_t>(1, dictionary_.MaxPhraseLength());

  // Standard Viterbi shortest-path (here: highest-log-probability path)
  // over the word lattice implied by the dictionary. best_score[i] is the
  // score of the best segmentation of syllables[0, i). back[i] records how
  // many syllables the last word of that best segmentation covers, and
  // which dictionary entry (by pointer + index) was chosen, so we can
  // reconstruct the winning path afterwards.
  std::vector<double> best_score(n + 1,
                                  -std::numeric_limits<double>::infinity());
  std::vector<size_t> back_len(n + 1, 0);
  std::vector<const DictionaryEntry*> back_entry(n + 1, nullptr);
  best_score[0] = 0.0;

  for (size_t i = 1; i <= n; ++i) {
    const size_t max_word_len = std::min(max_len, i);
    for (size_t len = 1; len <= max_word_len; ++len) {
      const size_t j = i - len;
      if (best_score[j] == -std::numeric_limits<double>::infinity()) continue;

      const DictionaryEntry* best_entry = nullptr;
      double edge_score;
      const auto* matches = dictionary_.Lookup(syllables, j, len);
      if (matches != nullptr && !matches->empty()) {
        best_entry = &(*matches)[0];  // already sorted, highest weight first
        edge_score = best_entry->log_weight;
      } else if (len == 1) {
        // No dictionary entry at all for this syllable: fall back to
        // showing the raw Bopomofo reading so composition never gets
        // stuck, but score it so low it is only ever used when there is
        // truly no alternative.
        edge_score = kUnknownSyllablePenalty;
      } else {
        continue;  // unknown multi-syllable span: only single-syllable
                    // spans get the raw-reading fallback.
      }

      const double candidate_score = best_score[j] + edge_score;
      if (candidate_score > best_score[i]) {
        best_score[i] = candidate_score;
        back_len[i] = len;
        back_entry[i] = best_entry;  // may be nullptr for the fallback case
      }
    }
  }

  // Reconstruct the path from n back to 0, then reverse.
  size_t i = n;
  while (i > 0) {
    const size_t len = back_len[i];
    const size_t j = i - len;
    Candidate c;
    c.syllable_count = len;
    c.reading = [&] {
      std::string r;
      for (size_t k = j; k < i; ++k) {
        if (k > j) r += ' ';
        r += syllables[k];
      }
      return r;
    }();
    if (back_entry[i] != nullptr) {
      c.text = back_entry[i]->text;
      c.score = back_entry[i]->log_weight;
    } else {
      c.text = syllables[j];  // show the raw reading, e.g. "ㄗㄞˋ"
      c.score = kUnknownSyllablePenalty;
    }
    result.push_back(std::move(c));
    i = j;
  }
  std::reverse(result.begin(), result.end());
  return result;
}

std::vector<Candidate> Decoder::GetCandidatesAt(
    const std::vector<std::string>& syllables, size_t start) const {
  std::vector<Candidate> result;
  if (start >= syllables.size()) return result;

  const size_t max_len = std::max<size_t>(1, dictionary_.MaxPhraseLength());
  const size_t remaining = syllables.size() - start;

  for (size_t len = std::min(max_len, remaining); len >= 1; --len) {
    const auto* matches = dictionary_.Lookup(syllables, start, len);
    if (matches == nullptr) continue;
    std::string reading;
    for (size_t k = 0; k < len; ++k) {
      if (k > 0) reading += ' ';
      reading += syllables[start + k];
    }
    for (const auto& entry : *matches) {
      Candidate c;
      c.text = entry.text;
      c.reading = reading;
      c.score = entry.log_weight;
      c.syllable_count = len;
      result.push_back(std::move(c));
    }
  }

  std::sort(result.begin(), result.end(),
            [](const Candidate& a, const Candidate& b) {
              // Prefer longer matches first (they resolve more input at
              // once), then higher score within the same length.
              if (a.syllable_count != b.syllable_count) {
                return a.syllable_count > b.syllable_count;
              }
              return a.score > b.score;
            });
  return result;
}

}  // namespace zhuyin
