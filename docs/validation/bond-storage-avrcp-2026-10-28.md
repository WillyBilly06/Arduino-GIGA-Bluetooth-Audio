> **Workspace-only historical evidence:** Links in this page that point outside
> the library refer to local `build/dev...` artifacts. They are not part of a
> copied source package. The historical observations below are unchanged.

# Local Classic bond storage and AVRCP integration

The evidence below records the earlier98-run integration. A subsequent normal
setup-parser change preserves these features and adds one direct validation
test; see [the latest99-run/build result](sco-response-validation-2026-10-28.md).

The local development source now includes two reviewed changes. Accepted AVRCP
key pairs schedule a finite series of real GetPlayStatus reads; Next/Previous
also schedule a metadata refresh. Received peer replies update state. The code
does not assign the expected playback value, replay keys or retry indefinitely.
This addresses the reproduced stale received-state lifecycle. It does not prove
the cause of the missing phone notification or physical interoperability.

`ReceiverConfig::classic_bonds` optionally binds application-owned Classic
link-key storage. Its `load`, `save` and `remove` callbacks use typed
`ClassicBondIdentity`/`ClassicBondRecord` values and `BondStorageResult`.
Leave the configuration empty for the existing four-slot RAM cache. That cache
is erased at begin/end and cannot preserve a key across MCU reset.

A configured provider is authoritative, with no silent RAM fallback. `NotFound`
means an actual missing record. Error, invalid schema/identity/key metadata,
exception or callback re-entry stops the host before a key reply is transmitted.
Actual-host tests distinguish that from an ordinary negative-key reply.
Successful cached-key loading still requires the unchanged authentication,
encryption and 16-byte key-size checks before profiles may use the link.

Callback addresses use HCI byte order, including the local identity; they are
not printed MAC order. The context must remain alive until a legitimate
non-callback end() completes. Callbacks run on the serialized host actor and
must be bounded, nonblocking and nonthrowing. Do not call receiver instance
APIs, delete the context/receiver, retain borrowed record references or perform
flash erase/program in a callback. Temporary records are completely initialized
and erased. Serialize fields explicitly, rather than the C++ object layout.

`Ok` acknowledges a complete provider-owned copy. It does **not** prove durable
storage. This interface reserves no storage region and performs no automatic
NV writes. A durable application backend must preload its bounded cache,
protect and journal records, flush only at an explicitly idle boundary, verify
commit/torn-write behavior, and expose its own durable-generation/error state.
It must not treat a failed read as a missing key. Unexpected reset before an
asynchronous flush remains a loss window. No real key bytes are included in
these tests or documentation.

The combined source passed98 native runtime invocations and eight child suites,
248 recorded top-level commands with zero exits. New tests are part of
`tools/run_owned_tests.py`, covering real actor/facade callbacks,11 AVRCP
reconciliation cases, standalone public headers and3000 storage operations over
1000 lifetimes with zero adapter allocations. The host's40665 checks include a
synthetic-key absence scan; this is an assertion count, not a feature count.

The exact426 production files/191 C/C++ units also passed the stock Arduino
Mbed GIGA4.6.0 M7 build and static audit at the default UART2M. The image is545288
bytes; Arduino reports539312 program bytes and54288 static RAM bytes. These
section values do not measure runtime heap/stack peaks or interrupt latency.
The first full native attempt passed98 primaries then exposed a missing
ThisThread::get_id API in older Mbed test stubs. The successor test stubs use
real host thread identities; the full98/eight run then passed. Production and
sketch bytes remained identical to the M7 build, which was not repeated.

Local evidence:

- [Combined integration and exact source receipt](../../../dev298-local-source-integration-20261028/530d2d17b9aa429d8095eae9534729db/receipt.json).
- [Complete passing native commands](../../../dev295-combined-harness-repair-20261028/native-full/receipt.json).
- [Official M7 audit](../../../dev293-combined-bond-avrcp-m7-20261028/Receiver/build-facts.json).
- [Independent provider review](../../../dev292-bond-provider-independent-review-20261028/REPORT.md).
- [Retained original native failure](../../../dev293-combined-bond-avrcp-m7-20261028/native-receipt.json).

No board/phone write accompanied this source integration. The latest physical
AVRCP attempt stopped before music or any of its15 requested cases. The original
full R4 firmware is restored; the phone is unbonded and its original bond/codec
baseline is not restored. Normal pairing awaits the user's choice. No physical
AVRCP fix, durable backend, power-cycle, HFP/SCO, clock, acoustic or latency pass
is claimed. Earlier field and codec evidence is scoped in the
[feature matrix](all-feature-status-2026-10-28.md).
