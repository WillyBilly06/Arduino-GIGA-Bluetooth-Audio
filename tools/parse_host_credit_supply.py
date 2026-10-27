"""Pure saved-text H0/H1/H2 parser. No file/device IO or work on import.

Root must independently bind the firmware, source projection and saved capture.
This parser does not qualify a firmware, alter the original44/CUM gates, or
establish causation, RF delivery, audio fidelity, acoustics or total latency.
"""
import hashlib
import re

SCHEMA='HOST_CREDIT_SUPPLY_V2'
ROW=re.compile(r'^GBR_EVENT ms=(\d+) H([012]) ([0-9a-fA-F]{8})((?: [0-9a-fA-F]{8}){8})$')
HOST_MASK=0x3f800000
PRIMARY_FAULT_MASK=((1<<19)-1)|(1<<21)|(1<<22)
UNAVAILABLE=0x3f3f0000
POINT=('Unknown','QueuedWorkPresent','InFlightWork','NoQueuedWork')


def decode_record(w):
    flags=w[5];host=w[14];transport=w[17]
    h=dict(event=host&63,subevent=host>>6&15,deferred=host>>10&15,
           classic_held=host>>14&7,rx_outstanding=host>>17&7,pending_hnc=host>>20&7,
           command_queued=host>>23&15,command_sent=host>>27&15,incomplete=bool(host>>31),
           raw_unassigned=transport&7,classic_outstanding=transport>>3&7,
           classic_pending=transport>>6&7,command_credits=transport>>9&15,
           boundary=transport>>13&7,generation=w[10])
    primary_issues=[];host_issues=[]
    if not 1<=w[0]<=3:primary_issues.append('ordinal outside first3')
    if not w[1] or not w[2]:primary_issues.append('invalid stream/output epoch')
    if not w[6] or w[7]>=w[6] or w[7]>w[8]:primary_issues.append('invalid first short refill')
    if w[11]<w[12] or w[13]>64 or w[11]-w[12]!=w[13]:primary_issues.append('invalid queue publication')
    phase=w[15];claim=flags>>30
    expected=1 if w[13] else 2 if phase in (4,5,6,7) else 3 if phase in (1,8) else 0
    if claim!=expected and not flags&PRIMARY_FAULT_MASK:primary_issues.append('inconsistent original point')
    roles=w[20:23]
    if len(set(roles))!=3 or any(not x or x%4 for x in roles):primary_issues.append('invalid cached roles')
    delta=(w[4]-w[16])&0xffffffff
    primary_known=not primary_issues and not flags&PRIMARY_FAULT_MASK and flags&(1<<19) and flags&(1<<20) and delta<0x80000000 and w[23]>0
    if flags&HOST_MASK:host_issues.append('firmware rejected Host publication')
    if flags&((1<<1)|(1<<13)|(1<<14)|(1<<15)|(1<<16)):
        host_issues.append('capture context or epoch invalid')
    if not 0<w[10]<0xffffffff:host_issues.append('Host generation unavailable')
    if h['incomplete']:host_issues.append('in-flight/transitional boundary')
    for key,limit in (('deferred',8),('classic_held',4),('rx_outstanding',4),('pending_hnc',4),
                      ('command_queued',8),('command_sent',8),('raw_unassigned',4),
                      ('classic_outstanding',4),('classic_pending',4)):
        if h[key]>limit:host_issues.append('unknown '+key)
    if h['pending_hnc']>h['rx_outstanding'] or h['classic_held']+h['classic_pending']!=h['classic_outstanding'] or h['raw_unassigned']+h['rx_outstanding']>4:
        host_issues.append('inconsistent shared receive window')
    if (h['classic_outstanding']>h['rx_outstanding'] or h['classic_pending']>h['pending_hnc'] or
        h['classic_held']>h['rx_outstanding']-h['pending_hnc']):
        host_issues.append('Classic/LE subsets exceed shared receive window')
    if transport&0x3fff0000!=UNAVAILABLE or transport&0xc0000000:
        host_issues.append('unknown Stage1 transport/identity/history schema')
    if h['deferred'] and (not h['event'] or h['event']==63 or h['boundary']!=1):
        host_issues.append('retained head identity/boundary unavailable')
    if not h['deferred'] and (h['event'] or h['subevent']):host_issues.append('head without deferred event')
    known=not host_issues and not primary_issues
    retention='UNKNOWN'
    if known:
        if not h['deferred'] and not h['classic_held'] and not h['raw_unassigned']:
            retention='NO_COPIED_CLASSIC_OR_UNASSIGNED_INPUT_HELD_AT_POINT'
        elif h['deferred'] and (h['classic_held'] or h['raw_unassigned']):
            retention='COPIED_INPUT_WITH_DEFERRED_EVENT_NOT_CAUSE_PROOF'
        else:retention='COHERENT_COUNTS_NOT_CAUSE_PROOF'
    return dict(words=w,primary_point=POINT[claim] if primary_known else 'Unknown',
                primary_issues=primary_issues,host=h,host_issues=host_issues,
                host_status='COHERENT_POINT' if known else 'UNKNOWN',retention=retention,
                transport_status='UNKNOWN',history_status='UNKNOWN_DISABLED',
                host_age_us=None,phase_age_configured_us=delta*1000000/w[23] if primary_known else None,
                timing_scope='CONDITIONAL_PUBLISHED_CONTRACT_NOT_EXCLUSIVE_RESIDENCE')


def parse_saved(raw,contract,*,firmware_sha256,source_projection_sha256):
    digest=hashlib.sha256(raw).hexdigest()
    for value in (firmware_sha256,source_projection_sha256):
        if not isinstance(value,str) or not re.fullmatch('[0-9a-f]{64}',value):
            raise ValueError('invalid expected artifact digest')
    if (contract.get('schema')!=SCHEMA or contract.get('capture_sha256')!=digest or
        contract.get('firmware_sha256')!=firmware_sha256 or
        contract.get('source_projection_sha256')!=source_projection_sha256):
        raise ValueError('protocol/capture/firmware/source binding mismatch')
    epochs=contract.get('epochs')
    if not isinstance(epochs,list) or not epochs or any(not isinstance(p,list) or len(p)!=2 or
        any(type(v) is not int or not 0<v<0xffffffff for v in p) for p in epochs):
        raise ValueError('independently bound stream/output epochs required')
    pending=[];records=[];unknown=[];seen=set();count=0
    for line_no,data in enumerate(raw.splitlines(keepends=True),1):
        line=data.decode('utf-8','strict').rstrip('\r\n')
        if re.match(r'^GBR_EVENT ms=\d+ S\d',line):
            raise ValueError('old or mixed S protocol cannot be interpreted as H')
        if not re.match(r'^GBR_EVENT ms=\d+ H\d',line):continue
        count+=1;match=ROW.fullmatch(line)
        if not match:
            unknown.append(dict(line=line_no,reason='malformed H row'));pending=[];continue
        part=int(match[2]);ordinal=int(match[3],16)
        row=dict(line=line_no,part=part,ordinal=ordinal,words=[int(x,16) for x in match[4].split()])
        if part==0:
            if pending:unknown.append(dict(reason='new H0 before complete prior record'))
            pending=[row];continue
        if not pending or part!=len(pending) or ordinal!=pending[0]['ordinal']:
            unknown.append(dict(line=line_no,reason='row order/ordinal mismatch'));pending=[];continue
        pending.append(row)
        if part!=2:continue
        w=sum((r['words'] for r in pending),[])
        if w[0]!=ordinal:raise ValueError('payload/header ordinal mismatch')
        if [w[1],w[2]] not in epochs:raise ValueError('foreign stream/output epoch')
        key=(w[1],w[2],ordinal)
        if key in seen:raise ValueError('duplicate stream/output/ordinal')
        seen.add(key);record=decode_record(w);record['rows']=pending;records.append(record);pending=[]
    if pending:unknown.append(dict(reason='partial record at capture end'))
    if raw and not raw.endswith(b'\n'):unknown.append(dict(reason='partial final saved line'))
    status='UNEXERCISED_COVERAGE_UNKNOWN' if not count else 'OBSERVED_WITH_UNKNOWN' if unknown or any(
        r['host_status']=='UNKNOWN' or r['primary_point']=='Unknown' for r in records) else 'COHERENT_POINT_METADATA_ONLY'
    return dict(schema=SCHEMA,status=status,raw_sha256=digest,rows_seen=count,records=records,
                unknown=unknown,no_original44_CUM_gate_rescored=True,no_causal_or_audio_claim=True)
