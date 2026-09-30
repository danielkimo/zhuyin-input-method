# Windows TSF Zhuyin IME

This directory contains the Windows-only Text Services Framework (TSF) glue for the cross-platform `zhuyin_engine` library.

## Architecture

- `dllmain.cpp`: COM in-proc server exports and DLL lifetime bookkeeping.
- `ClassFactory.*`: `IClassFactory` implementation for the TSF text service COM class.
- `TextService.*`: `ITfTextInputProcessorEx` + `ITfKeyEventSink` implementation. It maps physical keys to Bopomofo symbols, keeps a syllable composer plus pending syllable buffer, updates an inline TSF composition, and commits the best decoded sentence on Space/Enter.
- `CandidateWindow.*`: lightweight owner-drawn popup that displays up to 9 candidates near the caret.
- `Registrar.*`: COM, TSF profile, and category registration helpers used by `DllRegisterServer` / `DllUnregisterServer`.

## Build

1. Open a **Developer Command Prompt for Visual Studio**.
2. From the repository root, configure a 64-bit build:
   ```bat
   cmake -B build -A x64
   ```
3. Build the Release configuration:
   ```bat
   cmake --build build --config Release
   ```

The top-level CMake project needs to expose both the `zhuyin_engine` target and
`add_subdirectory(windows-ime)` so the `zhuyin_ime` DLL is part of the build graph.

The `zhuyin_ime` target builds a normal TSF DLL (`zhuyin_ime.dll`). TSF text services do **not** use a special `.ime` file extension.

> The text service expects a UTF-8 `dictionary.tsv` beside the DLL (or under `data\dictionary.tsv`). Packaging that dictionary with the final installer is still required.

## Install

1. Build the DLL.
2. Run `installer\register.bat` as **Administrator**.
3. Open **Settings > Time & Language > Language & region**.
4. Ensure **Chinese (Traditional, Taiwan)** is installed, then add/select the keyboard entry for **Zhuyin AI 注音輸入法**.
5. Switch to it from the language bar like any other installed IME/TIP.

## Uninstall

1. Switch away from the IME in the language bar.
2. Run `installer\unregister.bat` as **Administrator**.
3. Remove the keyboard entry from Windows Settings if desired.

## Known limitations / TODO

- Candidate popup is keyboard-driven only; mouse selection is not implemented yet.
- Candidate paging beyond the first 9 entries is still TODO.
- Punctuation handling is intentionally minimal and should be expanded with a full symbol table.
- No custom display-attribute provider/underline styling yet; TSF host default composition rendering is used.
- Password-field / secure-context opt-out policy is not specialized yet beyond standard TSF keyboard-TIP behavior.
- Ship a signed installer (MSI/EXE) and signed binaries for real distribution.
