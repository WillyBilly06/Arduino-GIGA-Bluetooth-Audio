# Packaged-source decoder execution on GIGA M7, 2026-10-28

The actual GIGA executed a decoder-only diagnostic built from all 424 source
files of local source ZIP `ca7d621d48b90b5ba9d08204cca3dbd501a4fb7053591b57ec0939ec1b3b231a`.
The 733460-byte diagnostic image passed complete upload/readback verification.
Its original fourteen input/reference cases and numerical thresholds remained
unchanged. All eight codec identities were compiled and exercised. The harness
retains its historical dev.14 boot-label text; the current-source association is
established by the compiler/source inventory, linked fixture bytes, exact image
readback and identity-checked USB capture, not that old label.

The run completed 28 vector safety checks, 26 numerical passes and two
`QUALITY_UNQUALIFIED` LHDC nonzero comparisons. Eight LHDC zero/lifecycle cases
passed. All 328 provider allocations were released; residual tracked memory,
decode-time allocation/free calls, memory drift, concealment flags and dropped
diagnostic rows were zero. The 2716 timed decode calls recorded zero deadline
misses at the reported 480 MHz. Radio and I2S were absent from this diagnostic;
these are finite isolated CPU observations, not combined playback qualification.

| AAC configuration exercised | Current requested open bytes | Change from earlier M7 run |
|---|---:|---:|
| LC stereo ADTS | 25300 | +38 |
| LC stereo LATM | 25300 | +38 |
| HE-v1 mono | 56278 | -866 |
| HE-v1 stereo ADTS | 88416 | +38 |
| HE-v1 stereo LATM | 88416 | +38 |

Allocation-block counts and finite decoded PCM digests were unchanged. This
confirms the expected mono saving and small stereo pointer/alignment overhead
on M7 for these configurations. It does not establish optimal total RAM or
target execution of every allocation-failure/configuration combination.

The LHDC mismatch was retained as a failure of numerical qualification. Both
passes produced S32 CRC `dbdf4256` against ARM64 differential `fe87f2ef`; all
sixteen frame CRCs differed. The sparse comparison observed 158/156 differing
points per channel, with sampled maxima 192/160 S32 counts. These are sparse
maxima, not full-stream bounds. The result matches the earlier M7 observation;
no reference, lower bits or tolerance was changed. Independent LHDC fidelity
remains unknown. LDAC's cases retain their source-tone oracle limitation.

After the test, the complete 786432-byte application backup was restored and
readback-matched byte for byte, including its tail. Full backup/readback SHA-256:
`079c1df4c7276c425397a4725a2265dde78523dde0a136ab93b7107c6295a09f`.
Its 510728-byte active-image prefix remains
`ad1d2f5e9ff2743fcab45582027f31aa5180cee74abb9c13d5d09b7fd8c1242f`.
All nine saved phone codec fields were independently restored, the source
resumed and fresh decoded/DMA progress was observed. The original reader
resumed at serial epoch 5 with its ten control receipts preserved.

The overall workflow deliberately records
`DIAGNOSTIC_FAILED_EXACT_STATE_RESTORED` because LHDC quality remains
unqualified. The packaged Receiver application and AVRCP/HFP examples have not
been field-deployed by this experiment. Controller/profile interoperability,
radio/I2S timing, clock overruns, acoustics and total latency remain separate.
