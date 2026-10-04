import importlib.util
from pathlib import Path
import subprocess
import unittest
import json
import datetime
ROOT=Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location('context_a2',ROOT/'scripts/build_commute_context.py')
n=importlib.util.module_from_spec(spec);spec.loader.exec_module(n)
class AlternateTests(unittest.TestCase):
    def test_native_a2(self):
        build=ROOT/'.test-build';build.mkdir(exist_ok=True);binary=build/'test_commute_a2'
        subprocess.run(['c++','-std=c++17','-Wall','-Wextra','-Werror','-Iinclude','src/core/CommuteDashboardCore.cpp','src/core/CommuteBusCore.cpp','test_host/test_commute_a2.cpp','-o',str(binary)],cwd=ROOT,check=True)
        subprocess.run([str(binary)],cwd=ROOT,check=True)
    def test_exact_alternate_aliases(self):
        self.assertEqual('alternate_alighting',n.critical_scope('106',"Ngan Mok Street, King's Road"))
        self.assertIsNone(n.critical_scope('106','Ngan Mok Street, Other Road'))
        self.assertEqual('alternate_boarding',n.critical_scope('8P','興發街 Hing Fat Street'))
        self.assertIsNone(n.critical_scope('118','Hing Fat Street'))
        self.assertIsNone(n.critical_scope('8P','Hing Fat Street flyover'))
    def test_structured_multi_stop_and_relocation(self):
        def html(stops,replacement=''):
            cells=['106','小西灣 Siu Sai Wan','','由2030年1月1日至2030年1月3日',stops,replacement]
            return 'Last Update Suspended Bus Stop<table><tr>'+''.join('<td>'+x+'</td>' for x in cells)+'</tr></table>'
        events,unknown=n.citybus_events(html('Victoria Park, Causeway Road; Ngan Mok Street, King\'s Road'),100)
        self.assertEqual({'transfer','alternate_alighting'},{e['scope'] for e in events});self.assertEqual(0,unknown)
        events,_=n.citybus_events(html("Ngan Mok Street, King's Road",'Replacement unknown'),100)
        self.assertEqual('degraded',events[0]['severity'])
    def test_legacy_feed_and_failed_coverage_cannot_enable_alternate(self):
        def fail(url):raise OSError('synthetic')
        self.assertEqual(0,n.build(100,fail)['alternate_checked_mask'])
    def test_profile_and_partition_preservation(self):
        header=(ROOT/'include/core/CommuteDashboardCore.h').read_text()
        for value in ('route106EarlyRideMinutes = 12','route8pEarlyRideMinutes = 17','route118RideMinutes = 35','transferBufferMinutes = 2'):
            self.assertIn(value,header)
    def test_official_graph_requires_exact_provider_direction_sequence_and_stops(self):
        at=1894018800
        def feed(route,direction,pairs,kmb=False):
            return json.dumps({'generated_timestamp':datetime.datetime.fromtimestamp(at,datetime.timezone.utc).isoformat(),
                'data':[dict(route=route,seq=seq,stop=stop,**({'bound':direction,'service_type':'1'} if kmb else {'dir':direction})) for seq,stop in pairs]}).encode()
        city=feed('106','I',[(14,'001533'),(18,'001213'),(19,'001259'),(48,'001382')])
        eight=feed('8P','I',[(5,'001213'),(6,'001251'),(9,'001224'),(14,'001382')])
        kmb=feed('106','O',[(14,'997CCAB996935BD7'),(18,'EAFC59DD23C39D5C'),(19,'834CC2324AAFB567'),(48,'EB077E683E6C8DDA')],True)
        def request(url):return kmb if 'etabus' in url else eight if '8P/' in url else city
        self.assertEqual(3,n.alternate_route_mask(at,request))
        city=city.replace(b'"I"',b'"O"')
        self.assertEqual(2,n.alternate_route_mask(at,request))
        eight=eight.replace(b'001251',b'001213')
        self.assertEqual(0,n.alternate_route_mask(at,request))
if __name__=='__main__':unittest.main()
