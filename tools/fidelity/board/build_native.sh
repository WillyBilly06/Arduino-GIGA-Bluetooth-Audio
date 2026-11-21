set -e
cd ~/gbpar; R=~/gbfid3
for pair in sbc:sbc aac:aac aptx:aptx ldac:ldac opus:opus lhdc:lhdc ssc:ssc; do
  c=${pair%%:*}; d=${pair#*:}; U=$(echo $c | tr a-z A-Z)
  F="-O2 -fwrapv -fno-strict-aliasing -DGBR_HOST_TEST=1 -I$R/src"
  for n in SBC AAC AAC_HE APTX LDAC LHDC OPUS SSC; do [ $n = $U ] && F="$F -DGBR_ENABLE_$n=1" || F="$F -DGBR_ENABLE_$n=0"; done
  mkdir -p obj/$c; i=0
  for s in $(find $R/src/vendor/codecs/$d -name '*.c' | sort); do gcc -std=c11 $F -w -c $s -o obj/$c/v$i.o; i=$((i+1)); done
  g++ -std=c++17 $F -w -o gbdec_$c gbdec.cpp $R/src/codecs/Codecs.cpp $R/src/detail/Decoder.cpp $R/src/detail/Memory.cpp $R/src/detail/Media.cpp obj/$c/*.o -lm
done
g++ -O2 -o cmp ~/gbfid2/build/fidelity/cmp.cpp -lm
python3 gen.py
