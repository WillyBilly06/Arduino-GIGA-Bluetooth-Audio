#!/bin/sh
# Decoder fidelity check (Linux; needs g++, gcc and the dev packages for libsbc,
# libfreeaptx, libldac (encoder), libopus and libfdk-aac).
# Usage: sh tools/fidelity/run.sh [workdir]   (run from the repository root)
set -e
R=$(pwd); D=${1:-build/fidelity}; mkdir -p "$D"; cp tools/fidelity/*.cpp "$D"/; cd "$D"
g++ -O2 -o enc enc.cpp -lsbc -lfreeaptx -lldacBT_enc -lopus -lfdk-aac -lm
g++ -O2 -o cmp cmp.cpp -lm
for pair in sbc:sbc aac:aac aptx:aptx ldac:ldac opus:opus; do
  c=${pair%%:*}; d=${pair#*:}; U=$(echo $c | tr a-z A-Z)
  F="-O2 -fwrapv -fno-strict-aliasing -DGBR_HOST_TEST=1 -I$R/src"
  for n in SBC AAC AAC_HE APTX LDAC LHDC OPUS SSC; do [ $n = $U ] && F="$F -DGBR_ENABLE_$n=1" || F="$F -DGBR_ENABLE_$n=0"; done
  mkdir -p obj/$c; i=0
  for s in $(find "$R/src/vendor/codecs/$d" -name '*.c' | sort); do gcc -std=c11 $F -w -c "$s" -o obj/$c/v$i.o; i=$((i+1)); done
  g++ -std=c++14 $F -o gbdec_$c "$R/tools/fidelity/gbdec.cpp" "$R/src/codecs/Codecs.cpp" "$R/src/detail/Decoder.cpp" "$R/src/detail/Memory.cpp" "$R/src/detail/Media.cpp" obj/$c/*.o -lm
done
# tag codec rate bits harness lag(for tones-only; measured on the noise signal)
run() {
  ./enc $1 >/dev/null; ./gbdec_$5 $2 $3 $4 $1.pkt $1.gb
  [ -f $1.ref ] && { printf '%s vs reference decoder: ' $1; ./cmp $1.ref $1.gb 2048; }
  printf '%s vs source: ' $1; ./cmp $1.src $1.gb 4096 $LAG
}
for t in "sbc sbc 48000 16 sbc 73" "sbcxq sbc 48000 16 sbc 73" "aac aac 48000 16 aac 2048" "aptx aptx 48000 16 aptx 0" \
         "aptxhd aptxhd 48000 24 aptx 0" "ldac990 ldac 96000 24 ldac 256" "ldac660 ldac 96000 24 ldac 256" "opus opus 48000 16 opus 312"; do
  set -- $t; LAG= run $1 $2 $3 $4 $5
(export TONES=1; LAG=$6; ./enc $1 >/dev/null; ./gbdec_$5 $2 $3 $4 $1.pkt $1.gb >/dev/null; printf '%s tones-only vs source: ' $1; ./cmp $1.src $1.gb 4096 $6)
done
