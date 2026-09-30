// Cross-platform core engine for the Zhuyin (Bopomofo) IME.
//
// This library contains no Windows-specific code. It implements:
//   - the standard ("large-key"/DaChen 大千式) Bopomofo keyboard mapping,
//   - a syllable builder that assembles key presses into a Zhuyin syllable,
//   - a frequency-weighted phrase dictionary, and
//   - a context-aware decoder that picks the most likely sequence of words
//     for a run of typed syllables by finding the highest-scoring path
//     through a lattice of dictionary matches (Viterbi shortest path over
//     a statistical language model built from real corpus frequencies).
//
// The Windows Text Services Framework (TSF) text service in windows-ime/
// links against this library and only deals with COM/UI concerns.
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace zhuyin {

// ---------------------------------------------------------------------------
// Keyboard mapping
// ---------------------------------------------------------------------------

// Maps physical keys (typed using a US QWERTY keyboard) to Bopomofo symbols
// following the standard 大千式 / "Microsoft/Standard Bopomofo Keyboard"
// layout used by Windows' built-in Zhuyin IME and most printed Taiwanese
// keyboards. This is a factual keyboard-layout standard, not copied source
// code.
class BopomofoKeyboard {
 public:
  // Returns the UTF-8 encoded Bopomofo symbol (or tone mark) produced by
  // `key` (a lowercase ASCII letter, digit, ',', '.', ';', '/', '-', or ' '
  // for the space bar / first tone). Returns an empty string if `key` is not
  // mapped on the Bopomofo layer (the caller should treat the key as a
  // literal ASCII character in that case, e.g. for punctuation shortcuts).
  static std::string KeyToSymbol(char key);

  // True if `symbol` (as returned by KeyToSymbol) is a tone mark
  // (ˊ ˇ ˋ ˙) or the space bar acting as an explicit first-tone marker.
  static bool IsToneSymbol(const std::string& symbol);

  // True if `symbol` is an initial consonant (ㄅㄆㄇ...).
  static bool IsInitialSymbol(const std::string& symbol);

  // True if `symbol` is a medial/glide (ㄧㄨㄩ).
  static bool IsMedialSymbol(const std::string& symbol);

  // True if `symbol` is a final/rime (ㄚㄛㄜ...ㄦ), including medials that
  // can also stand alone as a final (ㄧㄨㄩ).
  static bool IsFinalSymbol(const std::string& symbol);
};

// ---------------------------------------------------------------------------
// Syllable composition (per-syllable input buffer state machine)
// ---------------------------------------------------------------------------

// Accumulates Bopomofo symbols (initial, medial, final, tone) typed for a
// single syllable that has not yet been committed. A syllable is considered
// "ready" as soon as it has a final (or medial-as-final) component; a tone
// key press or the start of the next initial commits it.
class SyllableComposer {
 public:
  SyllableComposer() = default;

  enum class AddResult {
    kAdded,       // Symbol was accepted into the current syllable slot.
    kRejected,    // Symbol is invalid in the current slot (buffer unchanged).
    kReplaced,    // Symbol replaced an existing symbol in the same slot.
  };

  // Feeds one Bopomofo symbol (as returned by BopomofoKeyboard::KeyToSymbol)
  // into the composer.
  AddResult AddSymbol(const std::string& symbol);

  // Removes the most-recently-added symbol (used for the Backspace key).
  // Returns false if the buffer was already empty.
  bool BackspaceSymbol();

  // Clears the whole buffer (used after a syllable is committed, or Esc).
  void Clear();

  bool Empty() const;

  // Has enough components (a final) to be committed as a syllable.
  bool HasFinal() const;

  // Human-readable Bopomofo string for the composition/preedit UI,
  // e.g. "ㄗㄞˋ".
  std::string Display() const;

  // Canonical syllable key used for dictionary lookups, e.g. "ㄗㄞˋ". Only
  // meaningful once HasFinal() is true. Syllables with no explicit tone key
  // pressed are treated as first tone (no mark), matching standard usage.
  std::string Reading() const;

 private:
  std::string initial_;
  std::string medial_;
  std::string final_;
  std::string tone_;  // empty means first tone (no mark)
};

// ---------------------------------------------------------------------------
// Dictionary
// ---------------------------------------------------------------------------

struct DictionaryEntry {
  std::string text;     // UTF-8 Han characters, e.g. "在這裡"
  double log_weight;     // natural-log corpus-frequency weight (higher = more common)
};

class Dictionary {
 public:
  // Loads a UTF-8 TSV file with lines of the form:
  //   word<TAB>space-separated bopomofo syllables<TAB>integer frequency
  // Blank lines and lines starting with '#' are ignored.
  // Returns false (and leaves the dictionary unchanged) if the file cannot
  // be opened.
  bool LoadFromFile(const std::string& path);

  // Same as LoadFromFile but reads already-loaded TSV text (used by tests).
  bool LoadFromString(const std::string& tsv_text);

  size_t EntryCount() const;

  // Longest phrase (in syllables) present in the dictionary. Used by the
  // decoder to bound its lattice search window.
  size_t MaxPhraseLength() const { return max_phrase_length_; }

  // Returns all entries whose reading is exactly the syllable sequence
  // syllables[start, start+len). Empty if there is no match.
  const std::vector<DictionaryEntry>* Lookup(
      const std::vector<std::string>& syllables, size_t start,
      size_t len) const;

 private:
  // Key: syllables joined with a single space, e.g. "ㄗㄞˋ ㄓㄜˋ ㄌㄧˇ".
  std::vector<std::pair<std::string, std::vector<DictionaryEntry>>> entries_;
  // Sorted for binary search; built lazily after loading.
  bool sorted_ = false;
  size_t max_phrase_length_ = 1;

  void EnsureSorted() const;
};

// ---------------------------------------------------------------------------
// Candidates & context-aware decoding
// ---------------------------------------------------------------------------

struct Candidate {
  std::string text;      // UTF-8 Han characters for this word/phrase
  std::string reading;   // space-joined Bopomofo syllables covered
  double score;           // language-model score (higher = more likely)
  size_t syllable_count;  // number of input syllables this candidate covers
};

// Context-aware decoder: given a run of typed syllables (one per Han
// character the user intends to type), finds the highest-scoring
// segmentation into dictionary words using a Viterbi shortest-path search
// over the phrase lattice, i.e. a statistical language-model approach. This
// is what lets the engine pick "再一次" over "在一次" and "我在這裡" over
// "我再這裡": multi-character phrases that are common in the corpus score
// higher as a single unit than the alternative built from less-compatible
// pieces.
class Decoder {
 public:
  explicit Decoder(const Dictionary& dictionary) : dictionary_(dictionary) {}

  // Best full-sentence segmentation for `syllables`. Every input syllable is
  // covered by exactly one returned Candidate (falls back to a single
  // "unknown syllable" placeholder candidate, whose text equals its reading
  // in Bopomofo, for syllables with no dictionary match at all).
  std::vector<Candidate> ComposeBestSentence(
      const std::vector<std::string>& syllables) const;

  // All dictionary matches starting exactly at syllables[start] (length 1..
  // dictionary MaxPhraseLength, capped by the remaining buffer), sorted by
  // score descending. Used to populate a manual candidate-selection window.
  std::vector<Candidate> GetCandidatesAt(
      const std::vector<std::string>& syllables, size_t start) const;

 private:
  const Dictionary& dictionary_;
};

}  // namespace zhuyin
