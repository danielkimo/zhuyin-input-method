#include "zhuyin/engine.h"

#include <unordered_map>
#include <unordered_set>

namespace zhuyin {
namespace {

// Standard ("大千式" / DaChen, aka the layout Windows' built-in Zhuyin IME
// and most printed Taiwanese keyboards use) key -> Bopomofo symbol mapping.
// Verified against the widely used, MIT-licensed rime-bopomofo project's
// `keymap_bopomofo` transliteration table (a factual keyboard-layout
// standard, not copyrighted expression), not copied from any proprietary
// source.
const std::unordered_map<char, std::string>& KeyMap() {
  static const std::unordered_map<char, std::string> kMap = {
      {'1', "ㄅ"}, {'q', "ㄆ"}, {'a', "ㄇ"}, {'z', "ㄈ"},
      {'2', "ㄉ"}, {'w', "ㄊ"}, {'s', "ㄋ"}, {'x', "ㄌ"},
      {'e', "ㄍ"}, {'d', "ㄎ"}, {'c', "ㄏ"},
      {'r', "ㄐ"}, {'f', "ㄑ"}, {'v', "ㄒ"},
      {'5', "ㄓ"}, {'t', "ㄔ"}, {'g', "ㄕ"}, {'b', "ㄖ"},
      {'y', "ㄗ"}, {'h', "ㄘ"}, {'n', "ㄙ"},
      {'u', "ㄧ"}, {'j', "ㄨ"}, {'m', "ㄩ"},
      {'8', "ㄚ"}, {'i', "ㄛ"}, {'k', "ㄜ"}, {',', "ㄝ"},
      {'9', "ㄞ"}, {'o', "ㄟ"}, {'l', "ㄠ"}, {'.', "ㄡ"},
      {'0', "ㄢ"}, {'p', "ㄣ"}, {';', "ㄤ"}, {'/', "ㄥ"},
      {'-', "ㄦ"},
      {' ', "ˉ"},  // explicit first tone marker (space bar)
      {'6', "ˊ"}, {'3', "ˇ"}, {'4', "ˋ"}, {'7', "˙"},
  };
  return kMap;
}

const std::unordered_set<std::string>& InitialSymbols() {
  static const std::unordered_set<std::string> kSet = {
      "ㄅ", "ㄆ", "ㄇ", "ㄈ", "ㄉ", "ㄊ", "ㄋ", "ㄌ", "ㄍ", "ㄎ", "ㄏ",
      "ㄐ", "ㄑ", "ㄒ", "ㄓ", "ㄔ", "ㄕ", "ㄖ", "ㄗ", "ㄘ", "ㄙ",
  };
  return kSet;
}

const std::unordered_set<std::string>& MedialSymbols() {
  static const std::unordered_set<std::string> kSet = {"ㄧ", "ㄨ", "ㄩ"};
  return kSet;
}

const std::unordered_set<std::string>& FinalSymbols() {
  static const std::unordered_set<std::string> kSet = {
      "ㄚ", "ㄛ", "ㄜ", "ㄝ", "ㄞ", "ㄟ", "ㄠ", "ㄡ",
      "ㄢ", "ㄣ", "ㄤ", "ㄥ", "ㄦ", "ㄧ", "ㄨ", "ㄩ",
  };
  return kSet;
}

const std::unordered_set<std::string>& ToneSymbols() {
  static const std::unordered_set<std::string> kSet = {"ˊ", "ˇ", "ˋ", "˙",
                                                          "ˉ"};
  return kSet;
}

}  // namespace

std::string BopomofoKeyboard::KeyToSymbol(char key) {
  const auto& map = KeyMap();
  auto it = map.find(key);
  return it == map.end() ? std::string() : it->second;
}

bool BopomofoKeyboard::IsToneSymbol(const std::string& symbol) {
  return ToneSymbols().count(symbol) != 0;
}

bool BopomofoKeyboard::IsInitialSymbol(const std::string& symbol) {
  return InitialSymbols().count(symbol) != 0;
}

bool BopomofoKeyboard::IsMedialSymbol(const std::string& symbol) {
  return MedialSymbols().count(symbol) != 0;
}

bool BopomofoKeyboard::IsFinalSymbol(const std::string& symbol) {
  return FinalSymbols().count(symbol) != 0;
}

}  // namespace zhuyin
