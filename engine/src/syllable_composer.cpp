#include "zhuyin/engine.h"

#include <unordered_set>

namespace zhuyin {
namespace {

// Pure finals (rimes) that are never also medials/glides.
const std::unordered_set<std::string>& PureFinalSymbols() {
  static const std::unordered_set<std::string> kSet = {
      "ㄚ", "ㄛ", "ㄜ", "ㄝ", "ㄞ", "ㄟ", "ㄠ", "ㄡ", "ㄢ", "ㄣ", "ㄤ", "ㄥ", "ㄦ",
  };
  return kSet;
}

// The seven sibilant/retroflex initials that can form a complete syllable
// entirely on their own (e.g. ㄕˋ = 是, ㄗˇ = 子), with no medial or final
// glyph typed at all.
const std::unordered_set<std::string>& StandaloneInitialSymbols() {
  static const std::unordered_set<std::string> kSet = {
      "ㄓ", "ㄔ", "ㄕ", "ㄖ", "ㄗ", "ㄘ", "ㄙ",
  };
  return kSet;
}

}  // namespace

SyllableComposer::AddResult SyllableComposer::AddSymbol(
    const std::string& symbol) {
  if (symbol.empty()) return AddResult::kRejected;

  if (BopomofoKeyboard::IsToneSymbol(symbol)) {
    // A tone can only apply once the syllable already has a vowel (or is a
    // standalone-capable initial).
    const bool has_vowel = !medial_.empty() || !final_.empty() ||
                            (StandaloneInitialSymbols().count(initial_) != 0);
    if (!has_vowel) return AddResult::kRejected;
    const bool replaced = !tone_.empty();
    tone_ = symbol;
    return replaced ? AddResult::kReplaced : AddResult::kAdded;
  }

  if (!tone_.empty()) {
    // Tone already set: syllable is finished, no more symbols accepted.
    return AddResult::kRejected;
  }

  if (BopomofoKeyboard::IsMedialSymbol(symbol)) {
    if (!final_.empty()) return AddResult::kRejected;
    const bool replaced = !medial_.empty();
    medial_ = symbol;
    return replaced ? AddResult::kReplaced : AddResult::kAdded;
  }

  if (PureFinalSymbols().count(symbol) != 0) {
    const bool replaced = !final_.empty();
    final_ = symbol;
    return replaced ? AddResult::kReplaced : AddResult::kAdded;
  }

  if (BopomofoKeyboard::IsInitialSymbol(symbol)) {
    if (!medial_.empty() || !final_.empty()) return AddResult::kRejected;
    const bool replaced = !initial_.empty();
    initial_ = symbol;
    return replaced ? AddResult::kReplaced : AddResult::kAdded;
  }

  return AddResult::kRejected;
}

bool SyllableComposer::BackspaceSymbol() {
  if (!tone_.empty()) {
    tone_.clear();
    return true;
  }
  if (!final_.empty()) {
    final_.clear();
    return true;
  }
  if (!medial_.empty()) {
    medial_.clear();
    return true;
  }
  if (!initial_.empty()) {
    initial_.clear();
    return true;
  }
  return false;
}

void SyllableComposer::Clear() {
  initial_.clear();
  medial_.clear();
  final_.clear();
  tone_.clear();
}

bool SyllableComposer::Empty() const {
  return initial_.empty() && medial_.empty() && final_.empty() &&
         tone_.empty();
}

bool SyllableComposer::HasFinal() const {
  return !medial_.empty() || !final_.empty() ||
         (StandaloneInitialSymbols().count(initial_) != 0);
}

std::string SyllableComposer::Display() const {
  // "ˉ" is the explicit first-tone marker the user typed with the space
  // bar; show it so they get feedback, even though the canonical Reading()
  // omits it (dictionary readings never carry a first-tone mark).
  return initial_ + medial_ + final_ + tone_;
}

std::string SyllableComposer::Reading() const {
  const std::string& canonical_tone = (tone_ == "ˉ") ? std::string() : tone_;
  return initial_ + medial_ + final_ + canonical_tone;
}

}  // namespace zhuyin
