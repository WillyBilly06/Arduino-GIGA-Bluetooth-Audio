# Selected mono mSBC encoder port

The13 Broadcom encoder files were selected from the owner's supplied SBC encoder directory. Each original Apache-2.0 header remains. Modified files carry a dated change notice. `manifest.json` binds each supplied file to its current bytes; the sibling `VoiceSbcCompat.h` is a project adapter. These files do not license unrelated codecs or third-party patents.

This port specializes the encoder to mono,8subbands,15blocks and mSBC. It uses exact-width16/32/64-bit arithmetic and external PCM input. Analysis history, DCT scratch and shift state belong to each encoder instance. Unused stereo/joint processing and platform allocator coupling are disabled. The encoder is not the A2DP SBC decoder and does not change its configuration support.

`VoiceMsbc` reserves one selected context on the audio worker and releases it after voice ends. No encoder globals or all-codec table copy is introduced. Its constant analysis tables remain read-only. The existing OI decoder has a conditional ARM LP64 typedef correction; the32-bit M7 branch is preserved. Compile-time `GBR_ENABLE_SBC=0` leaves CVSD available and the mSBC methods unsupported.

The test reference is independently generated synthetic mono PCM encoded/decoded by the pinned FFmpeg executable. Codec numeric/context tests are CPU evidence. They do not establish synchronous HCI routing, M7 voice timing, real microphone operation or acoustic output.
