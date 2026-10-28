"""Actual portable actor/frontend link closures; no capability substitutes."""
from pathlib import Path
ACTOR_SOURCES=(
 'src/owned/OwnedHost.cpp','src/owned/OwnedVoiceAudio.cpp','src/owned/VoiceSdp.cpp',
 'src/owned/ClassicProfiles.cpp','src/owned/LeProfiles.cpp','src/platform/RadioProtocol.cpp',
 'src/owned/VoiceProfiles.cpp','src/owned/HfpControl.cpp','src/owned/HspControl.cpp',
)
VOICE_FRONTEND_SOURCES=(
 'src/owned/VoiceMsbc.cpp','src/owned/VoiceAudioPipeline.cpp','src/owned/VoiceAudioWorker.cpp',
 'src/owned/VoiceDacOutput.cpp','src/owned/OwnedVoiceAudio.cpp',
)
FRONTEND_BASE_SOURCES=(
 'tests/empty_registry.cpp','src/GigaBredr.cpp','src/Types.cpp','src/detail/Memory.cpp',
 'src/detail/Decoder.cpp','src/detail/Media.cpp','src/detail/PacketQueue.cpp',
 'src/detail/AudioTiming.cpp','tests/support/clock_backend_unavailable.cpp',
)
UNAVAILABLE_VOICE_RADIO='tests/support/voice_radio_unavailable.cpp'
VOICE_CORE_C_SOURCES=('src/owned/core/bth_packet.c','src/owned/core/bth_sco.c','src/owned/core/bth_sco_setup.c')
def unique(sources):return list(dict.fromkeys(sources))
def frontend_sources(*,include_giga=True,unavailable_radio=True):
 base=[p for p in FRONTEND_BASE_SOURCES if include_giga or p!='src/GigaBredr.cpp']
 return unique([*base,*VOICE_FRONTEND_SOURCES,*([UNAVAILABLE_VOICE_RADIO]if unavailable_radio else[])])
def voice_sbc_sources(root):
 root=Path(root)
 return [*sorted((root/'src/vendor/codecs/sbc/srce').glob('*.c')),*sorted((root/'src/vendor/voice/sbc_encoder/srce').glob('*.c'))]
def voice_extra_cpp(source_root):
 root=Path(source_root)
 return [root/p for p in VOICE_FRONTEND_SOURCES]if(root/'src/owned/VoiceAudioWorker.cpp').is_file()else[]
def voice_extra_c(source_root,*,include_decoder=True):
 root=Path(source_root)
 if not(root/'src/owned/VoiceAudioWorker.cpp').is_file():return[]
 sbc=voice_sbc_sources(root)
 if not include_decoder:sbc=[p for p in sbc if 'sbc_encoder'in p.parts]
 return [*[root/p for p in VOICE_CORE_C_SOURCES],*sbc]
def unavailable_voice_boundary(test_root,source_root=None):
 root=Path(test_root);source=Path(source_root)if source_root is not None else root
 return [root/UNAVAILABLE_VOICE_RADIO]if(source/'src/owned/VoiceAudioWorker.cpp').is_file()else[]
