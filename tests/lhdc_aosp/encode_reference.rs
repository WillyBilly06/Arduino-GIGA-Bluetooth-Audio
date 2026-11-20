use lhdcv5::lhdc_api::*;
use std::{env, fs, io::Write};

fn main() {
    let a: Vec<String> = env::args().collect();
    assert_eq!(a.len(), 7, "input output rate bits quality mtu");
    let pcm = fs::read(&a[1]).unwrap();
    let rate: u32 = a[3].parse().unwrap();
    let bits: u32 = a[4].parse().unwrap();
    let quality: u32 = a[5].parse().unwrap();
    let mtu: u32 = a[6].parse().unwrap();
    let mut encoder = Context::new(LHDC_VERSION_1).unwrap();
    encoder.init_encoder(rate, bits, quality, LHDC_FRAME_5MS, mtu, LHDC_ENC_INTERVAL_10MS).unwrap();
    let samples = encoder.get_block_size().unwrap() as usize;
    let stride = samples * 2 * bits as usize / 8;
    assert_eq!(pcm.len() % stride, 0);
    let mut file = fs::File::create(&a[2]).unwrap();
    let mut out = vec![0u8; 65536];
    println!("rate={rate} bits={bits} samples={samples} stride={stride} last_bitrate={}", encoder.last_bitrate());
    for (i, input) in pcm.chunks_exact(stride).enumerate() {
        let (mut written, mut frames) = (0u32, 0u32);
        encoder.enc_process(input, &mut out, &mut written, &mut frames).unwrap();
        assert!(written as usize <= out.len());
        file.write_all(&out[..written as usize]).unwrap();
        println!("call={i} written={written} frames={frames}");
    }
}
