# Complete local native regression run, 2026-10-28

The final `tools/run_owned_tests.py --asan` run returned zero and recorded
`PASS_NATIVE_SYNTHETIC_SUITES`. All 91 primary native test executions and all
seven child suites returned zero:

| Child suite | Evidence scope |
|---|---|
| Retained ADTS frontend | 11 actual AAC frontend cases, fake radio/I2S |
| Audio clock | Actual controller/frontend/register code with native clock and register models |
| Owned wake/lifetime | Actual owner/adapter and public receiver with RTOS/UART boundary mocks |
| Codec memory | Real SBC/Opus selected-storage and allocation regressions |
| AAC allocations | 14 actual AAC allocation/retry/element cases |
| Audio scheduling | 20 cases and 31 portable C kernels, ASan/UBSan models |
| Runner plans | 9 offline tests proving planning does not execute compiler/runtime/child recipes |

Primary coverage includes the AVRCP typed facade and copied metadata snapshots,
paired keys, stale generations, cancellation and channel retirement; HF/HS
control and voice worker/pipeline/input ownership; HCI/ATT credits and lifecycle;
BLE request/command isolation, fragmentation, QoS and connection procedures;
PCM buffering/recovery; and the existing Classic progress batches.

The native harness now supports Windows' standard-library keyword checks and
MSVC member-access name decoration. Its ISO C runtime compatibility flags remain
confined to tests. The actual production host was not changed by these harness
repairs. Failed commands now leave a failed receipt instead of a stale running
label.

Older fixtures were updated to the current, separately inspected contracts:
the one-second preference-start age; optional notification startup hold;
strict exact-echo-or-reject peer parameter policy; and zero synchronous command
budget for validated normal LE detach. Deferred FIFO tests now establish a real
older event barrier rather than assuming every detach needs command storage.
They retain owner, security, credit, pending-copy and post-dispatch checks.
The startup regression separately proves that reliable ATT replies progress
while optional notifications wait, then verifies the full 512-byte notification
after a matching actual parameter update. The prefix-order test begins with
already committed ATT SDUs and checks their actual fragmentation/timing.

AddressSanitizer applies to the primary native run. Child clock/scheduling
models also request UBSan; codec suites retain their documented fixed-point
shift/overflow exclusions where used. Do not describe this as complete UBSan
coverage of every codec or as exhaustive input/conformance testing.

These results do not prove M7 scheduling, physical controller/radio behavior,
every codec's fidelity or sender interoperability, real HFP call audio,
electrical/acoustic output, or total latency. See the separate
[M7 builds](profile-examples-2026-10-28.md), [AAC evidence](aac-selected-storage-2026-10-28.md)
and [finite physical BLE/LHDC run](ble-continuous-2026-10-28.md).
