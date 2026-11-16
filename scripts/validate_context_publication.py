#!/usr/bin/env python3
"""Fail a Pages deployment on invalid/stale/all-unavailable normalized context.

Input comes only from the reviewed normalizer; no device/private input is read.
Partial coverage is retained explicitly. This is not field accuracy validation.
"""
import json
from pathlib import Path
import sys
import time
from urllib.parse import urlsplit

def validate(raw,now,require_alternate=False):
    if len(raw)>24576:raise ValueError('Context exceeds firmware body limit')
    obj=json.loads(raw)
    if obj.get('schema')!=1 or type(obj.get('generated_at')) is not int:raise ValueError('Context schema')
    generated=obj['generated_at']
    if not now-3600<=generated<=now+300:raise ValueError('Context not freshly generated')
    expected={'citybus':'mobile.citybus.com.hk','kmb':'search.kmb.hk','lcsd':'www.lcsd.gov.hk'}
    sources=obj.get('sources',[])
    if len(sources)!=3 or {s.get('source') for s in sources}!=set(expected):raise ValueError('Source inventory')
    for s in sources:
        if s.get('coverage') not in ('supported','partial','unavailable'):raise ValueError('Source coverage')
        if s.get('fetched_at') not in (0,generated):raise ValueError('Source fetch time')
    if not any(s['fetched_at']==generated and s['coverage']!='unavailable' for s in sources):raise ValueError('All sources unavailable')
    if require_alternate:
        mask=obj.get('alternate_checked_mask')
        if type(mask) is not int or mask not in (1,2,3):raise ValueError('A2 official route membership unverified')
        if type(obj.get('truncated_events')) is not int or obj['truncated_events']!=0:raise ValueError('A2 context truncated')
        required={'citybus'} | ({'kmb'} if mask & 2 else set())
        for s in sources:
            if s['source'] in required and (s['fetched_at']!=generated or s['coverage']=='unavailable'):
                raise ValueError('A2 notice source is unavailable or stale')
    events=obj.get('events',[])
    if not isinstance(events,list) or len(events)>24:raise ValueError('Event count')
    for e in events:
        source=e.get('source');u=urlsplit(e.get('source_url',''))
        if source not in expected or u.scheme!='https' or u.netloc!=expected[source]:raise ValueError('Event provenance')
        if e.get('operator_mask')!=(2 if source=='kmb' else 1):raise ValueError('Operator scope')
        if e.get('route') not in ('106','8P','118') or e.get('direction')!='toward_yue_wan':raise ValueError('Route scope')
        if e.get('verified') is not True or e.get('fetched_at')!=generated:raise ValueError('Event verification')
        if type(e.get('start')) is not int or type(e.get('end')) is not int:raise ValueError('Event interval')
        if not 0<e['end']-e['start']<=366*86400 or e['end']<=generated:raise ValueError('Event validity')
        if e.get('effect')=='athletic_meet':
            if source!='lcsd' or e.get('severity')!='advisory' or e['route']!='8P' or e['end']-e['start']!=86400 or (e['start']+28800)%86400:raise ValueError('Meet must stay all-day advisory')
    return {'events':len(events),'coverage':{s['source']:s['coverage'] for s in sources},'generated_at':generated}

if __name__=='__main__':
    import argparse
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('context',type=Path)
    parser.add_argument('--require-alternate',action='store_true')
    args=parser.parse_args()
    print(json.dumps(validate(args.context.read_bytes(),int(time.time()),args.require_alternate)))
