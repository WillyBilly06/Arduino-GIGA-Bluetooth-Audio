"""Synthetic saved-log checks, never a firmware, concurrency or audio test."""
from pathlib import Path
import hashlib, importlib.util, unittest

spec=importlib.util.spec_from_file_location('host_credit_saved_parser',Path(__file__).resolve().parents[1]/'tools/parse_host_credit_supply.py')
parser=importlib.util.module_from_spec(spec);spec.loader.exec_module(parser)
IMAGE='1'*64;SOURCE='2'*64

def words(host=0,transport=0x3f3f0000,extra_flags=0):
 return [1,4,5,1000,200,(3<<30)|(1<<19)|(1<<20)|extra_flags,
         48,0,0,0,7,0,0,0,host,1,100,transport,0x2000,0x2000,0x1000,0x2000,0x3000,480000000]

def rows(w,tag='H'):
 return ''.join('GBR_EVENT ms=10 '+tag+str(part)+' '+f'{w[0]:08x}'+''.join(f' {v:08x}' for v in w[part*8:part*8+8])+'\n' for part in range(3)).encode()

def contract(raw):
 return dict(schema='HOST_CREDIT_SUPPLY_V2',capture_sha256=hashlib.sha256(raw).hexdigest(),
             firmware_sha256=IMAGE,source_projection_sha256=SOURCE,epochs=[[4,5]])

def parse(raw,binding=None):
 return parser.parse_saved(raw,binding or contract(raw),firmware_sha256=IMAGE,source_projection_sha256=SOURCE)

class SavedParserTests(unittest.TestCase):
 def test_valid_point_transport_and_history_unknown(self):
  r=parse(rows(words()))['records'][0]
  self.assertEqual(r['primary_point'],'NoQueuedWork')
  self.assertEqual(r['host_status'],'COHERENT_POINT')
  self.assertEqual(r['retention'],'NO_COPIED_CLASSIC_OR_UNASSIGNED_INPUT_HELD_AT_POINT')
  self.assertEqual(r['transport_status'],'UNKNOWN')
  self.assertEqual(r['history_status'],'UNKNOWN_DISABLED')

 def test_host_unknown_does_not_erase_original_point(self):
  r=parse(rows(words(host=0x80000000,extra_flags=(1<<23)|(1<<26))))['records'][0]
  self.assertEqual(r['host_status'],'UNKNOWN')
  self.assertEqual(r['primary_point'],'NoQueuedWork')

 def test_retained_head_and_classic_lease_is_candidate_only(self):
  r=parse(rows(words(host=5|(2<<10)|(1<<14)|(1<<17),transport=0x3f3f0000|8|(1<<13))))['records'][0]
  self.assertEqual(r['retention'],'COPIED_INPUT_WITH_DEFERRED_EVENT_NOT_CAUSE_PROOF')
  self.assertEqual(r['host']['classic_held'],1)

 def test_raw_packet_never_infers_classic_owner(self):
  r=parse(rows(words(host=5|(1<<10),transport=0x3f3f0000|1|(1<<13))))['records'][0]
  self.assertEqual(r['host']['classic_held'],0)
  self.assertEqual(r['host']['raw_unassigned'],1)

 def test_invalid_count_cannot_be_coherent(self):
  r=parse(rows(words(host=7<<14)))['records'][0]
  self.assertEqual(r['host_status'],'UNKNOWN')
  self.assertEqual(r['primary_point'],'NoQueuedWork')

 def test_unavailable_transport_never_becomes_zero_backlog(self):
  r=parse(rows(words(transport=0)))['records'][0]
  self.assertEqual(r['host_status'],'UNKNOWN')
  self.assertEqual(r['transport_status'],'UNKNOWN')

 def test_old_or_mixed_protocol_rejected(self):
  for raw in (rows(words(),'S'),rows(words())+rows(words(),'S')):
   with self.assertRaises(ValueError):parse(raw)

 def test_partial_and_out_of_order_are_unknown(self):
  data=rows(words()).splitlines(keepends=True)
  for raw in (b''.join(data[:2]),data[1]+data[0]+data[2]):
   self.assertEqual(parse(raw)['status'],'OBSERVED_WITH_UNKNOWN')

 def test_duplicate_record_rejected(self):
  with self.assertRaises(ValueError):parse(rows(words())*2)

 def test_artifact_and_epoch_bindings(self):
  raw=rows(words());binding=contract(raw)
  for field,value in (('capture_sha256','0'*64),('firmware_sha256','3'*64),
                      ('source_projection_sha256','4'*64),('schema','SUPPLY_SAVED_DRAFT_V1'),('epochs',[[1,2]])):
   bad=dict(binding);bad[field]=value
   with self.assertRaises(ValueError):parse(raw,bad)

 def test_no_event_is_unexercised(self):
  self.assertEqual(parse(b'GBR_STATUS idle=1\n')['status'],'UNEXERCISED_COVERAGE_UNKNOWN')

 def test_unknown_H_tag_is_not_silently_unexercised(self):
  raw=b'GBR_EVENT ms=10 H3 00000001\n'
  self.assertEqual(parse(raw)['status'],'OBSERVED_WITH_UNKNOWN')

 def test_invalid_capture_epoch_cannot_validate_host_point(self):
  r=parse(rows(words(extra_flags=1<<15)))['records'][0]
  self.assertEqual(r['host_status'],'UNKNOWN')

 def test_classic_must_be_subset_of_shared_window(self):
  r=parse(rows(words(transport=0x3f3f0000|(1<<3)|(1<<6))))['records'][0]
  self.assertEqual(r['host_status'],'UNKNOWN')

 def test_remaining_LE_pending_must_fit_remaining_LE_outstanding(self):
  h=(2<<14)|(4<<17)|(3<<20)
  t=0x3f3f0000|(3<<3)|(1<<6)
  self.assertEqual(parse(rows(words(host=h,transport=t)))['records'][0]['host_status'],'UNKNOWN')

 def test_invalid_role_publication_cannot_validate_host_point(self):
  self.assertEqual(parse(rows(words(extra_flags=1<<14)))['records'][0]['host_status'],'UNKNOWN')

if __name__=='__main__':unittest.main()
