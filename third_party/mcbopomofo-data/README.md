# Third-party data: McBopomofo dictionary sources

The files in this directory are vendored, unmodified, from the
[openvanilla/McBopomofo](https://github.com/openvanilla/McBopomofo) project
(`Source/Data/`), which is MIT licensed (see `LICENSE-MIT-McBopomofo.txt` in
this directory).

| File | Purpose |
|---|---|
| `BPMFBase.txt` | Single-Han-character → Bopomofo reading(s), including heteronyms. |
| `BPMFMappings.txt` | Multi-character phrase → Bopomofo reading(s). Per McBopomofo's own documentation, this file was "originally simplified from `tsi.src` of libtabe (BSD licensed) with modifications." |
| `phrase.occ` | Corpus occurrence counts for both single characters and phrases. |
| `heterophony1.list` / `heterophony2.list` / `heterophony3.list` | Reading-priority lists for heteronym characters (not currently consumed by `tools/build_dictionary.py`, kept for future use). |

`tools/build_dictionary.py` combines `BPMFBase.txt` + `BPMFMappings.txt` +
`phrase.occ` into the flattened, frequency-weighted `data/dictionary.tsv`
that the engine actually loads at runtime. See that script for the exact
merge logic, and the top-level `README.md` for the overall project license.

Regenerate `data/dictionary.tsv` after updating any of these files with:

```sh
python3 tools/build_dictionary.py
```
