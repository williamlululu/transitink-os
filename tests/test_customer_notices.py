"""Synthetic Customer Notice formats; no copied real notices or private dates."""
import importlib.util
import io
import json
import os
from pathlib import Path
import subprocess
import unittest
from unittest.mock import patch

ROOT=Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location('citybus_normalizer',ROOT/'scripts/build_commute_context.py')
n=importlib.util.module_from_spec(spec);spec.loader.exec_module(n)
NOW=1894018800
NOTICE='https://mobile.citybus.com.hk/pdf/SYNTHETIC2030.pdf'
PERIOD='''2030年1月6日(星期日)上午10時至
日期及時間 2030年1月9日(星期三)上午10時止
Date & Period From 10:00 a.m. on 6 January 203 0 (Sunday)
to 10:00 a.m. on 9 January 2030 (Wednesday)'''
OMISSION='''Customer Notice 乘客通告 城巴有限公司 Citybus Limited
發佈日: 2029年12月1日 張貼至: 2030年2月1日
路線 Routes 102, 106
Special Arrangement for Bus Stops
%s
方向 筲箕灣 / 小西灣 Direction Shau Kei Wan / Siu Sai Wan
巴士站安排 上述期間不停高士威道維多利亞公園外巴士站
Bus Stop Omit Causeway Road outside Victoria Park bus stop
Arrangement during the said period.
敬請留意。Thank you for your attention.
''' % PERIOD

def record(route='106'):
    return {'route':route,'source_url':NOTICE,'notice_id':'SYNTHETIC2030'}

def listing(url=NOTICE):
    return ('<table id="noticeitemlist"><tr><td>Notice</td></tr><tr><td '
            'onclick="window.open(\''+url+'\');" onkeypress="if(event.keyCode==13){window.open(\''+
            url.replace('https:','http:')+'\');}">Synthetic</td></tr></table>')

class CustomerNoticeTests(unittest.TestCase):
    def test_official_dynamic_discovery_and_dedup(self):
        seen=[]
        def request(url):seen.append(url);return listing().encode()
        rows,unknown=n.discover_citybus('106',request)
        self.assertEqual([record()],rows);self.assertEqual(0,unknown)
        self.assertEqual([n.CITYBUS_NOTICES+'?id=106&l=1'],seen)
        changed=NOTICE.replace('2030','2031')
        self.assertEqual(changed,n.discover_citybus('8P',lambda _:listing(changed).encode())[0][0]['source_url'])

    def test_anchor_and_relative_links_inside_table_only(self):
        html='<a href="/pdf/OUTSIDE.pdf">outside</a><table id="noticeitemlist"><tr><td><a href="/pdf/NEW.pdf">new</a></td></tr></table>'
        rows,_=n.discover_citybus('118',lambda _:html.encode())
        self.assertEqual(['https://mobile.citybus.com.hk/pdf/NEW.pdf'],[r['source_url'] for r in rows])

    def test_untrusted_links_never_fetched(self):
        for url in ('https://evil.invalid/x.pdf','https://mobile.citybus.com.hk.evil.invalid/pdf/x.pdf',
                    'https://user@mobile.citybus.com.hk/pdf/x.pdf','https://mobile.citybus.com.hk:443/pdf/x.pdf',
                    'https://mobile.citybus.com.hk/pdf/x.pdf?redirect=1','/pdf/../x.pdf','javascript:alert(1)'):
            with self.subTest(url=url):
                rows,count=n.discover_citybus('106',lambda _:listing(url).encode())
                self.assertFalse(rows);self.assertGreater(count,0)

    def test_discovery_schema_failure_not_empty_success(self):
        with self.assertRaises(ValueError):n.discover_citybus('106',lambda _:b'<html>Error</html>')
        with self.assertRaises(ValueError):n.discover_citybus('106A',lambda _:b'')

    def test_current_pattern_synthetic_exact_effect(self):
        e=n.citybus_notice_event(OMISSION,record(),NOW)
        self.assertEqual(('106',1,'transfer','hard_block','stop_omitted'),tuple(e[k] for k in ('route','operator_mask','scope','severity','effect')))
        self.assertEqual(1893895200,e['start']);self.assertEqual(1894154400,e['end'])
        self.assertTrue(e['start']<=NOW<e['end'])

    def test_wrong_route_direction_or_critical_stop(self):
        variants=[OMISSION.replace('102, 106','102, 106P'),OMISSION.replace('102, 106','106, 102'),
                  OMISSION.replace('小西灣','黃大仙'),OMISSION.replace('高士威道維多利亞公園外','告士打道維多利亞公園外'),
                  OMISSION.replace('Causeway Road outside Victoria Park','Victoria Park, Gloucester Road'),
                  OMISSION.replace('高士威道維多利亞公園外','高士威道維多利亞公園外附近'),
                  OMISSION.replace('102, 106','102 & 106')]
        for text in variants:
            with self.subTest(text=text):self.assertIsNone(n.citybus_notice_event(text,record(),NOW))
        self.assertIsNone(n.citybus_notice_event(OMISSION,record('8P'),NOW))
        self.assertIsNone(n.citybus_notice_event(OMISSION,record('118'),NOW))

    def test_8p_and_118_exact_scope(self):
        one=OMISSION.replace('102, 106','8P').replace('筲箕灣 / 小西灣','小西灣').replace('Shau Kei Wan / Siu Sai Wan','Siu Sai Wan')
        self.assertEqual('boarding',n.citybus_notice_event(one,record('8P'),NOW)['scope'])
        for route,cn,en,scope in [('118','紅磡海底隧道收費廣場','Cross Harbour Tunnel Toll Plaza','boarding'),
                                  ('118','漁灣邨','Yue Wan Estate','destination'),('8P','漁灣邨','Yue Wan Estate','destination')]:
            text=one.replace('Routes 8P','Routes '+route).replace('高士威道維多利亞公園外',cn).replace('Causeway Road outside Victoria Park',en)
            self.assertEqual(scope,n.citybus_notice_event(text,record(route),NOW)['scope'])

    def test_stop_table_suspension_and_relocation(self):
        text=('Customer Notice Citybus Limited\n路線 Route 118\n'+PERIOD+
              '\n方向 Direction 小西灣 Siu Sai Wan\n停用巴士站 Suspended Bus Stop 紅磡海底隧道收費廣場 Cross Harbour Tunnel Toll Plaza\n'
              '臨時巴士站 Temporary Bus Stop 不設臨時站 No Temporary Bus Stop\n不便之處，敬請原諒。')
        e=n.citybus_notice_event(text,record('118'),NOW);self.assertEqual('hard_block',e['severity'])
        relocated=text.replace('不設臨時站 No Temporary Bus Stop','後移約60米 Relocated backward for about 60 metres')
        e=n.citybus_notice_event(relocated,record('118'),NOW);self.assertEqual('degraded',e['severity'])
        self.assertEqual('stop_relocated',e['effect'])
        self.assertIsNone(n.citybus_notice_event(text.replace('不設臨時站','另行通知'),record('118'),NOW))

    def test_unsupported_map_and_other_route_footnote(self):
        text=OMISSION.replace('路線 Routes 102, 106','Stops Routes Bounds A2 8P, 8X, N8P, N8X').replace('上述期間不停高士威道維多利亞公園外巴士站','路線102(筲箕灣方向)及106(小西灣方向)於上述期間不停此站')
        self.assertIsNone(n.citybus_notice_event(text,record('8P'),NOW))

    def test_dates_not_posting_dates_and_fail_closed(self):
        for text in (OMISSION.replace('2030年1月9日','2030年2月30日'),
                     OMISSION.replace('9 January','10 January'),OMISSION.replace('2030年1月9日','2030年1月5日'),
                     OMISSION.replace('2030年1月9日(星期三)上午10時止','另行通知'),
                     OMISSION+' 每日10:00至12:00',OMISSION+' except Sunday',OMISSION+' Service resumed',
                     OMISSION.replace('From 10:00','From 13:00'),OMISSION+'\n'+PERIOD):
            with self.subTest(text=text):self.assertIsNone(n.citybus_notice_event(text,record(),NOW))

    def test_multi_section_and_ambiguous_directions(self):
        for text in (OMISSION+OMISSION,OMISSION.replace('筲箕灣 / 小西灣','筲箕灣 / 小西灣 / 柴灣'),
                     OMISSION.replace('Omit Causeway','Do not omit Causeway'),OMISSION.replace('不停','可能不停')):
            with self.subTest(text=text):self.assertIsNone(n.citybus_notice_event(text,record(),NOW))

    def test_bilingual_direction_disagreement_is_unparsed(self):
        for bound in ('Shau Kei Wan / Wong Tai Sin','Siu Sai Wan / Shau Kei Wan',''):
            self.assertIsNone(n.citybus_notice_event(OMISSION.replace('Shau Kei Wan / Siu Sai Wan',bound),record(),NOW))

    def test_listing_survives_notice_discovery_failure(self):
        html='Last Update Suspended Bus Stop<table><tr>'+''.join('<td>'+s+'</td>' for s in
            ['106','小西灣 Siu Sai Wan','','由2030年1月6日至2030年1月9日','維多利亞公園，高士威道',''])+'</tr></table>'
        def request(url):
            if url==n.CITYBUS:return html.encode()
            raise OSError('notice channel unavailable')
        events,status,details=n.citybus_context(NOW,request)
        self.assertEqual(1,len(events));self.assertEqual('partial',status['coverage'])
        self.assertEqual(3,len([d for d in details if d['status']=='unavailable']))

    def test_discovery_limits(self):
        html='<table id="noticeitemlist">'+''.join('<a href="/pdf/T%d.pdf">x</a>'%i for i in range(40))+'</table>'
        rows,count=n.discover_citybus('106',lambda _:html.encode());self.assertEqual(30,len(rows));self.assertEqual(10,count)
        calls=[]
        def request(url):
            if '/pdf/' in url:calls.append(url);raise OSError('unavailable')
            if 'getnotice' in url:return html.encode()
            raise OSError('unavailable')
        _,status,_=n.citybus_context(NOW,request)
        self.assertEqual(12,len(calls));self.assertGreater(status['unparsed'],0)

    def request_fixture(self,url):
        if url==n.CITYBUS:raise OSError('structured listing offline')
        if 'getnotice' in url:
            return (listing() if 'id=106&' in url else '<table id="noticeitemlist"></table>').encode()
        if url==NOTICE:return b'synthetic PDF extraction boundary'
        raise OSError('independent source offline')

    def test_pdf_survives_listing_failure_and_expiry(self):
        with patch.object(n,'citybus_pdf_text',return_value=OMISSION):
            feed=n.build(NOW,self.request_fixture)
            self.assertEqual(1,len(feed['events']));self.assertEqual('partial',feed['sources'][0]['coverage'])
            self.assertEqual('recognized_active',feed['diagnostics']['citybus_notices'][2]['status'])
            expired=n.build(1894154400,self.request_fixture);self.assertFalse(expired['events'])
            future=n.build(1893895199,self.request_fixture);self.assertEqual(1,len(future['events']))

    def test_pdf_dependency_and_bounds(self):
        # Exercises the real pinned extractor on a synthetic, textless document.
        import pypdf
        writer=pypdf.PdfWriter();writer.add_blank_page(width=100,height=100);data=io.BytesIO();writer.write(data)
        with self.assertRaises(ValueError):n.citybus_pdf_text(data.getvalue())
        writer.add_blank_page(width=100,height=100);data=io.BytesIO();writer.write(data)
        with self.assertRaises(ValueError):n.citybus_pdf_text(data.getvalue())

    def test_normalizer_json_to_production_planner(self):
        with patch.object(n,'citybus_pdf_text',return_value=OMISSION):feed=n.build(NOW,self.request_fixture)
        build=ROOT/'.test-build';build.mkdir(exist_ok=True)
        body=build/'synthetic-citybus-context.json';body.write_text(json.dumps(feed),encoding='utf-8')
        explicit=os.environ.get('TRANSITINK_TEST_JSON_INCLUDE')
        candidates=[Path(explicit)] if explicit else list((ROOT/'.pio/libdeps').glob('*/ArduinoJson/src'))
        self.assertTrue(candidates,'Run pinned dependency bootstrap via test_commute_context first')
        binary=build/'test_citybus_notice_pipeline'
        subprocess.run(['c++','-std=c++17','-Wall','-Wextra','-Werror','-Iinclude','-I'+str(candidates[0]),
                        'src/core/CommuteDashboardCore.cpp','test_host/test_citybus_notice_pipeline.cpp','-o',str(binary)],cwd=ROOT,check=True)
        subprocess.run([str(binary),str(body)],cwd=ROOT,check=True)

if __name__=='__main__':unittest.main()
