# Zhuyin AI 注音輸入法 — Windows Zhuyin (Bopomofo) IME with context-aware selection

A Windows input method (Text Services Framework text service) for typing
Traditional Chinese using the standard Zhuyin (注音/Bopomofo) keyboard, with
statistical, context-aware disambiguation of homophones — e.g. it correctly
picks **再一次** (not 在一次) and **我在這裡** (not 我再這裡) based on which
multi-character phrase is actually common in real usage, not just the most
frequent single character.

It installs **alongside** your other already-installed IMEs (English, other
Chinese IMEs, etc.) as one more selectable keyboard/TIP in the Windows
language bar — it does not replace or disable anything.

## How the "AI" context-awareness works

Rather than converting one syllable to one character in isolation, the
engine treats the whole run of syllables you've typed as a sentence to
segment: it builds a lattice of every dictionary phrase that matches any
span of the typed syllables, then runs a Viterbi shortest-path search to
find the highest-probability segmentation, using a unigram language model
whose word probabilities come from real Mandarin corpus frequency data (see
[`data/dictionary.tsv`](tools/build_dictionary.py)). A common compound like
"再一次" or "在這裡" scores far higher as a single recognized unit than any
character-by-character alternative, which is exactly what resolves the
再/在 and similar homophone ambiguities from context. See
[`engine/src/decoder.cpp`](engine/src/decoder.cpp) for the implementation
and reasoning, and run `zhuyin_demo_cli` (built below) for a live
demonstration.

This is the same general approach used by other well-regarded open-source
Zhuyin IMEs (e.g. McBopomofo's "Gramambular" language model); it is not a
deep neural network, but a statistical language model — a practical,
explainable, and fast approach that is what actually ships in production
phonetic IMEs today.

## Repository layout

```
engine/         Cross-platform C++17 core: keyboard mapping, syllable
                composition, dictionary, and the language-model decoder.
                No Windows dependencies; has its own unit tests.
windows-ime/    Windows-only Text Services Framework (TSF) text service
                (COM DLL) that plugs the engine into Windows as an IME.
                See windows-ime/README.md for Windows build/install steps.
installer/      register.bat / unregister.bat for installing the TSF DLL.
data/           Generated dictionary.tsv (word, Bopomofo reading, corpus
                frequency) consumed by the engine at runtime.
tools/          build_dictionary.py: regenerates data/dictionary.tsv from
                the vendored third_party/mcbopomofo-data sources.
third_party/    Vendored, attributed third-party dictionary source data.
```

## Building and testing the core engine (any OS)

The engine itself is portable and can be built/tested without Windows:

```sh
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
./build/engine/zhuyin_demo_cli data/dictionary.tsv   # live demo
```

## Building and installing the Windows IME

See [`windows-ime/README.md`](windows-ime/README.md) for full instructions
(requires Visual Studio + the Windows SDK; Windows-only). In short:

```powershell
cmake -B build -A x64
cmake --build build --config Release
# then, as Administrator:
installer\register.bat
```

Afterwards, add "Chinese (Traditional, Taiwan)" in Windows Settings →
Time & Language → Language, then pick "Zhuyin AI 注音輸入法" from that
language's keyboard list / the language bar, exactly like any other IME.

## Regenerating the dictionary

```sh
python3 tools/build_dictionary.py
```

See `tools/build_dictionary.py` and `third_party/mcbopomofo-data/README.md`
for provenance and merge logic.

## Known limitations

- No user dictionary / learning from corrections yet.
- Punctuation input is minimal (stubbed for future work).
- No code signing — Windows SmartScreen may warn on the unregistered DLL;
  see `installer/register.bat` for notes.
- Windows TSF glue is written to the documented COM contract but has not
  been compiled here (no Windows toolchain in this environment) — a first
  Visual Studio build pass may surface small signature/type fixes. The
  engine itself (dictionary + decoder + tests) is fully built and verified
  cross-platform.
- Occasional residual homophone errors remain for words not covered by any
  multi-character dictionary phrase (e.g. an isolated "做" vs "作"), since
  the decoder can only prefer a compound over single characters when that
  compound exists in the dictionary with sufficient corpus frequency.

## License

MIT (see `LICENSE`). Vendored dictionary data is separately attributed in
`third_party/mcbopomofo-data/README.md`.
