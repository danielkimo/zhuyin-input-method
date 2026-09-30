#include "zhuyin/engine.h"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <sstream>
#include <unordered_map>

namespace zhuyin {
namespace {

std::string JoinSyllables(const std::vector<std::string>& syllables,
                           size_t start, size_t len) {
  std::string key;
  for (size_t i = 0; i < len; ++i) {
    if (i > 0) key += ' ';
    key += syllables[start + i];
  }
  return key;
}

}  // namespace

bool Dictionary::LoadFromFile(const std::string& path) {
  std::ifstream in(path, std::ios::binary);
  if (!in.is_open()) return false;
  std::ostringstream buffer;
  buffer << in.rdbuf();
  return LoadFromString(buffer.str());
}

bool Dictionary::LoadFromString(const std::string& tsv_text) {
  // Raw (word, reading, weight) tuples, grouped by reading. log_weight is
  // filled in with the *raw* log(weight) first; it gets renormalized into a
  // proper unigram log-probability -- log(weight) - log(total_weight) --
  // once the total corpus weight is known below. This normalization is what
  // makes the Viterbi decoder in decoder.cpp correctly prefer fewer, longer
  // dictionary phrases over many short ones: each extra word in a
  // segmentation subtracts another log(total_weight) term (a large,
  // constant per-token cost), which only a phrase whose own frequency is
  // truly exceptional could ever overcome. Comparing raw log(count) values
  // without this normalization would do the opposite -- since any
  // individual character's raw frequency count usually dwarfs any specific
  // multi-character phrase's count, a decoder would wrongly favor spelling
  // everything out character-by-character.
  std::unordered_map<std::string, std::vector<DictionaryEntry>> grouped;
  size_t max_len = 1;
  double total_weight = 0.0;

  std::istringstream stream(tsv_text);
  std::string line;
  while (std::getline(stream, line)) {
    if (!line.empty() && line.back() == '\r') line.pop_back();
    if (line.empty() || line[0] == '#') continue;

    // Split on tabs into exactly 3 fields: word, reading, weight.
    size_t tab1 = line.find('\t');
    if (tab1 == std::string::npos) continue;
    size_t tab2 = line.find('\t', tab1 + 1);
    if (tab2 == std::string::npos) continue;

    const std::string word = line.substr(0, tab1);
    const std::string reading_key = line.substr(tab1 + 1, tab2 - tab1 - 1);
    const std::string weight_str = line.substr(tab2 + 1);
    if (word.empty() || reading_key.empty()) continue;

    long weight = 1;
    try {
      weight = std::stol(weight_str);
    } catch (...) {
      continue;
    }
    if (weight < 1) weight = 1;

    // Count syllables in the reading (space-separated tokens).
    size_t syllable_count = 1;
    for (char c : reading_key) {
      if (c == ' ') ++syllable_count;
    }
    max_len = std::max(max_len, syllable_count);

    total_weight += static_cast<double>(weight);

    DictionaryEntry entry;
    entry.text = word;
    entry.log_weight = std::log(static_cast<double>(weight));  // raw for now
    grouped[reading_key].push_back(std::move(entry));
  }

  if (grouped.empty()) return false;

  const double log_total = std::log(total_weight);

  entries_.clear();
  entries_.reserve(grouped.size());
  for (auto& kv : grouped) {
    for (auto& e : kv.second) e.log_weight -= log_total;  // -> log P(word)
    // Sort candidates for a given reading by weight descending so callers
    // that just want the top match don't need to re-sort.
    std::sort(kv.second.begin(), kv.second.end(),
              [](const DictionaryEntry& a, const DictionaryEntry& b) {
                return a.log_weight > b.log_weight;
              });
    entries_.emplace_back(kv.first, std::move(kv.second));
  }
  std::sort(entries_.begin(), entries_.end(),
            [](const auto& a, const auto& b) { return a.first < b.first; });

  max_phrase_length_ = max_len;
  sorted_ = true;
  return true;
}

size_t Dictionary::EntryCount() const {
  size_t total = 0;
  for (const auto& kv : entries_) total += kv.second.size();
  return total;
}

const std::vector<DictionaryEntry>* Dictionary::Lookup(
    const std::vector<std::string>& syllables, size_t start,
    size_t len) const {
  if (len == 0 || start + len > syllables.size()) return nullptr;
  const std::string key = JoinSyllables(syllables, start, len);
  auto it = std::lower_bound(
      entries_.begin(), entries_.end(), key,
      [](const auto& kv, const std::string& k) { return kv.first < k; });
  if (it == entries_.end() || it->first != key) return nullptr;
  return &it->second;
}

void Dictionary::EnsureSorted() const {
  // Sorting happens eagerly in LoadFromString; kept as a no-op hook in case
  // future mutation paths are added.
}

}  // namespace zhuyin
