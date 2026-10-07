// Small CLI demo: loads the real generated dictionary and decodes a
// hard-coded set of example syllable sequences, so the context-aware
// disambiguation can be sanity-checked against the full corpus (not just
// the small fixture used by engine_tests.cpp).
//
// Usage: zhuyin_demo_cli <path-to-dictionary.tsv>
#include "zhuyin/engine.h"

#include <cstdio>
#include <string>
#include <vector>

using namespace zhuyin;

namespace {

struct Example {
  const char* label;
  std::vector<std::string> syllables;
};

void PrintSentence(const char* label, const std::vector<Candidate>& sentence) {
  std::printf("%-28s -> ", label);
  for (const auto& c : sentence) std::printf("%s", c.text.c_str());
  std::printf("   (");
  for (size_t i = 0; i < sentence.size(); ++i) {
    if (i) std::printf(" | ");
    std::printf("%s:%.2f", sentence[i].text.c_str(), sentence[i].score);
  }
  std::printf(")\n");
}

}  // namespace

int main(int argc, char** argv) {
  if (argc < 2) {
    std::fprintf(stderr, "Usage: %s <path-to-dictionary.tsv>\n", argv[0]);
    return 2;
  }

  Dictionary dict;
  if (!dict.LoadFromFile(argv[1])) {
    std::fprintf(stderr, "Failed to load dictionary from %s\n", argv[1]);
    return 1;
  }
  std::printf("Loaded %zu dictionary entries (max phrase length %zu).\n\n",
              dict.EntryCount(), dict.MaxPhraseLength());

  Decoder decoder(dict);

  std::vector<Example> examples = {
      {"再一次 (zai4 yi2 ci4)", {"ㄗㄞˋ", "ㄧˊ", "ㄘˋ"}},
      {"我在這裡 (wo3 zai4 zhe4 li3)",
       {"ㄨㄛˇ", "ㄗㄞˋ", "ㄓㄜˋ", "ㄌㄧˇ"}},
      {"再見 (zai4 jian4)", {"ㄗㄞˋ", "ㄐㄧㄢˋ"}},
      {"現在 (xian4 zai4)", {"ㄒㄧㄢˋ", "ㄗㄞˋ"}},
      {"做作業 (zuo4 zuo4 ye4)", {"ㄗㄨㄛˋ", "ㄗㄨㄛˋ", "ㄧㄝˋ"}},
      {"知道 (zhi1 dao4)", {"ㄓ", "ㄉㄠˋ"}},
  };

  for (const auto& ex : examples) {
    PrintSentence(ex.label, decoder.ComposeBestSentence(ex.syllables));
  }

  return 0;
}
