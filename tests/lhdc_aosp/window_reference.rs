use lhdcv5::lhdc_enc::lhdc_enc_freq_process::lhdc_enc_lossy_frequency_overlap;
use zerocopy::IntoBytes;
fn main() {
    let out=std::env::args().nth(1).unwrap();
    for repeating in [96,192,384] {
        let mut window=vec![0i32;repeating];
        lhdc_enc_lossy_frequency_overlap(repeating as i32,&mut window);
        std::fs::write(format!("{}/window-{}.q31",out,repeating),window.as_bytes()).unwrap();
    }
}
