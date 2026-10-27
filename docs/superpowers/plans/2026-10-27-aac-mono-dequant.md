# AAC mono envelope-dequant storage implementation plan

Goal: allocate the existing960-byte maximum envelope-dequant plane per selected AAC channel, preserving every legal band/envelope value and stereo allocation. This is an allocation-layout optimization, not an audio-gap fix.

Architecture: place only envDataDequant at the end of private PSInfoSBR as a true C flexible array; allocate/reset sizeof(base)+channels*plane bytes in the same existing owned block. Keep noise/scale metadata, shared XBuf, arithmetic, transport parsing and supported configurations unchanged. Public base950d8d is preserved in a separate baseline snapshot.

- [x] Add compile-time allocation geometry RED before the storage change. Prepare actual paired baseline/candidate allocation, flush/bounds/OOM and PCM/status fixtures. Root alone executes target artifacts later; runtime gates remain pending.
- [x] Implement selected-channel flexible storage and full-size reset; compile geometry GREEN. Retain mono-SCE/CPE admission checks and stereo capacity.
- [x] Build official NDK paired executables with identical existing media, numerical input bindings and failure cleanup assertions. No Windows project executable was run.
- [x] Detect and reject first-candidate reset stack regression: official compiler changed the inherited whole-state save from baseline64B to22680B. Add explicit ownership-only snapshot, compile GREEN under256B frame guard (56B actual). Preserve failed candidate evidence.
- [ ] Finish final guarded official CM7 build and source/provenance/patch review. The first full link passed but candidate was rejected by the stack gate. The corrected full-build wrapper also has an explicit noncompiler comparator-edit mismatch; a settled fresh snapshot is required for the final receipt.
- [ ] Root executes paired Android fixtures and binds successful exit/stdout/ELF/media hashes. No target allocation/PCM, complete all-config fidelity, M7 execution, publication or audio-gap repair claim before that evidence.

Expected request geometry is960 bytes lower for mono HE and unchanged for stereo HE/LC; this is a test expectation, not a measured SRAM saving. The remaining45-byte SBR metadata and360-byte LC planes are explicitly deferred rather than broadening the first change.
