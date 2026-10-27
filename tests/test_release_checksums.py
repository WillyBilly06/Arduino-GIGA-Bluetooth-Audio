from pathlib import Path
import hashlib,importlib.util,tempfile,unittest

ROOT=Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location('release_checksums',ROOT/'tools/release_checksums.py')
release=importlib.util.module_from_spec(spec);spec.loader.exec_module(release)

class ChecksumFormat(unittest.TestCase):
    def test_exact_gnu_bytes_are_lf_only(self):
        with tempfile.TemporaryDirectory()as folder:
            a=Path(folder)/'GigaBredr.zip';b=Path(folder)/'release-manifest.json'
            a.write_bytes(b'zip fixture');b.write_bytes(b'{}\n')
            expected=(hashlib.sha256(a.read_bytes()).hexdigest()+'  GigaBredr.zip\n'+hashlib.sha256(b.read_bytes()).hexdigest()+'  release-manifest.json\n').encode('ascii')
            data=release.checksum_bytes([a,b]);self.assertEqual(data,expected);self.assertNotIn(b'\r',data)
    def test_ambiguous_filename_and_duplicate_rejected(self):
        with tempfile.TemporaryDirectory()as folder:
            a=Path(folder)/'ambiguous name.zip';a.write_bytes(b'x')
            with self.assertRaises(ValueError):release.checksum_bytes([a])
            b=Path(folder)/'safe.zip';b.write_bytes(b'x')
            with self.assertRaises(ValueError):release.checksum_bytes([b,b])
    def test_empty_input_rejected(self):
        with self.assertRaises(ValueError):release.checksum_bytes([])

if __name__=='__main__':unittest.main()
