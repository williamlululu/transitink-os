import copy
import importlib.util
import json
from pathlib import Path
import unittest
spec=importlib.util.spec_from_file_location('publication_guard',Path(__file__).resolve().parents[1]/'scripts/validate_context_publication.py')
m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m)

class PublicationGuardTests(unittest.TestCase):
    def fixture(self):return {'schema':1,'generated_at':1900000000,'sources':[{'source':s,'coverage':'partial','fetched_at':1900000000} for s in ('citybus','kmb','lcsd')],'events':[]}
    def test_fresh_partial_coverage_is_honest(self):
        self.assertEqual(0,m.validate(json.dumps(self.fixture()).encode(),1900000000)['events'])
    def test_stale_invalid_and_unavailable_do_not_deploy(self):
        bad=[]
        f=self.fixture();f['generated_at']-=7200;bad.append(f)
        f=self.fixture();f['sources']=[];bad.append(f)
        f=self.fixture()
        for s in f['sources']:s.update(coverage='unavailable',fetched_at=0)
        bad.append(f)
        for f in bad:
            with self.assertRaises(ValueError):m.validate(json.dumps(f).encode(),1900000000)
        with self.assertRaises(ValueError):m.validate(b' '*24577,1900000000)
    def test_provenance_and_operator_guard(self):
        f=self.fixture();f['events']=[{'source':'citybus','source_url':'https://mobile.citybus.com.hk/pdf/SYNTHETIC.pdf','operator_mask':1,'route':'106','direction':'toward_yue_wan','verified':True,'fetched_at':1900000000,'start':1899999900,'end':1900000500,'effect':'stop_omitted','severity':'hard_block'}]
        self.assertEqual(1,m.validate(json.dumps(f).encode(),1900000000)['events'])
        for key,value in [('source_url','https://evil.invalid/x'),('operator_mask',3),('end',1899999800)]:
            bad=copy.deepcopy(f);bad['events'][0][key]=value
            with self.assertRaises(ValueError):m.validate(json.dumps(bad).encode(),1900000000)
