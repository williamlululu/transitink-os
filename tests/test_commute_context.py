import importlib.util
import io
import json
import os
from pathlib import Path
import re
import subprocess
import unittest
import zipfile

ROOT=Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location('normalizer',ROOT/'scripts/build_commute_context.py')
n=importlib.util.module_from_spec(spec);spec.loader.exec_module(n)

class ContextTests(unittest.TestCase):
    def test_context_json_contract(self):
        explicit=os.environ.get('TRANSITINK_TEST_JSON_INCLUDE')
        candidates=[Path(explicit)] if explicit else list((ROOT/'.pio/libdeps').glob('*/ArduinoJson/src'))
        self.assertTrue(candidates, 'Install pinned firmware/native ArduinoJson dependency before JSON contract test')
        build=ROOT/'.test-build';build.mkdir(exist_ok=True);binary=build/'test_commute_context_json'
        subprocess.run(['c++','-std=c++17','-Wall','-Wextra','-Werror','-Iinclude','-I'+str(candidates[0]),
            'test_host/test_commute_context_json.cpp','-o',str(binary)],cwd=ROOT,check=True)
        subprocess.run([str(binary)],cwd=ROOT,check=True)

    def test_planner_semantics(self):
        build=ROOT/'.test-build';build.mkdir(exist_ok=True);binary=build/'test_commute_context'
        subprocess.run(['c++','-std=c++17','-Wall','-Wextra','-Werror','-Iinclude',
            'src/core/CommuteDashboardCore.cpp','test_host/test_commute_context.cpp','-o',str(binary)],cwd=ROOT,check=True)
        subprocess.run([str(binary)],cwd=ROOT,check=True)

    def notice(self, direction='小西灣 Siu Sai Wan', replacement='', dates='由2030年1月1日至2030年1月3日'):
        cells=['106',direction,'',dates,'維多利亞公園，高士威道 Victoria Park, Causeway Road',replacement]
        return 'Last Update Suspended Bus Stop<table><tr>'+''.join('<td>'+c+'</td>' for c in cells)+'</tr></table>'

    def test_citybus_omission_and_relocation(self):
        events,unparsed=n.citybus_events(self.notice(),100)
        self.assertEqual(0,unparsed);self.assertEqual('hard_block',events[0]['severity'])
        self.assertEqual('transfer',events[0]['scope']);self.assertEqual(1,events[0]['operator_mask'])
        events,_=n.citybus_events(self.notice(replacement='臨時站'),100)
        self.assertEqual('degraded',events[0]['severity'])

    def test_ambiguous_direction_or_hours_never_block(self):
        for html in (self.notice(direction=''),self.notice(dates='由2030年1月1日至2030年1月3日 每日10:00至12:00'),self.notice(direction='九龍')):
            events,unparsed=n.citybus_events(html,100);self.assertFalse(events);self.assertEqual(1,unparsed)

    def test_lcsd_detail_not_legend(self):
        sheets=[[{'A':'B','C':'Athletic Meet / Athletic Training / Other Activity'}],
                [{'A':'Date & Time of Temporary Closure','C':'Reason'},
                 {'A':'2/1/2030 0800-1600','C':'Athletic Meet 運動會'},
                 {'A':'3/1/2030 0800-1600','C':'Athletic Training'},
                 {'A':'4/1/2030 0800-1600','C':'Football training'},
                 {'A':'5/1/2030 0800-1600','C':'Turf Maintenance'}]]
        dates,unknown=n.athletic_dates(sheets)
        self.assertEqual(['2030-01-02'],[d.isoformat() for d in sorted(dates)]);self.assertEqual(0,unknown)
        with self.assertRaises(ValueError):n.athletic_dates(sheets[:1])

    def test_lcsd_ambiguous_date_not_guessed(self):
        dates,unknown=n.athletic_dates([[{'A':'Date & Time of Temporary Closure'},
            {'A':'2, 4 and 8 January','C':'Athletic Meet'}]])
        self.assertFalse(dates);self.assertEqual(1,unknown)

    def test_xlsx_reader_and_all_day_hkt(self):
        data=io.BytesIO()
        with zipfile.ZipFile(data,'w') as z:
            z.writestr('xl/worksheets/sheet1.xml','''<worksheet xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main"><sheetData>
              <row><c r="A1" t="inlineStr"><is><t>Date &amp; Time of Temporary Closure</t></is></c></row>
              <row><c r="A2" t="inlineStr"><is><t>2/1/2030 1600-1900</t></is></c><c r="C2" t="inlineStr"><is><t>Athletic Meet</t></is></c></row>
              </sheetData></worksheet>''')
        events,_=n.lcsd_events(data.getvalue(),'https://www.lcsd.gov.hk/synthetic.xlsx',100)
        self.assertEqual(86400,events[0]['end']-events[0]['start'])
        self.assertEqual(0,(events[0]['start']+28800)%86400)
        self.assertEqual('advisory',events[0]['severity'])

    def test_kmb_discovery_uses_official_bounds(self):
        seen=[]
        def request(url):
            seen.append(url)
            if 'getroutebound' in url:return json.dumps({'result':True,'data':[{'BOUND':'2'}]}).encode()
            return json.dumps({'result':True,'data':[{'krbpiid_routeno':'106','krbpiid_boundno':'2','kpi_noticeimageurl':'synthetic.pdf','kpi_referenceno':'synthetic'}]}).encode()
        result=n.discover_kmb('106',request)
        self.assertEqual('2',result[0]['provider_bound']);self.assertIn('&bound=2',seen[-1])
        self.assertNotIn('severity',result[0]);self.assertNotIn('direction',result[0])

    def test_kmb_scoped_pdf_table_synthetic(self):
        record={'route':'106','notice_id':'synthetic-1','source_url':'https://search.kmb.hk/KMBWebSite/AnnouncementPicture.ashx?url=synthetic.pdf'}
        text='102, 106 Temporary Bus Stop Suspension From 2 Jan 2030 10:00 To 4 Jan 2030 10:00 Temp. Bus stop(s) suspended To Eastern District: Victoria Park, Causeway Road Temp/Alt. stop(s) No Temporary Stop. Please proceed to: Another stop'
        event=n.kmb_notice_event(text,record,100)
        self.assertEqual(2,event['operator_mask']);self.assertEqual('hard_block',event['severity'])
        self.assertEqual(172800,event['end']-event['start']);self.assertEqual('transfer',event['scope'])
        self.assertIsNone(n.kmb_notice_event(text.replace('Eastern District','Kowloon'),record,100))
        self.assertIsNone(n.kmb_notice_event(text.replace('Causeway Road','Gloucester Road'),record,100))
        self.assertIsNone(n.kmb_notice_event(text.replace('To 4 Jan 2030 10:00','until further notice'),record,100))
        self.assertIsNone(n.kmb_notice_event(text+' From 6 Jan 2030 10:00 To 8 Jan 2030 10:00',record,100))
        self.assertEqual('degraded',n.kmb_notice_event(text.replace('No Temporary Stop.','Replacement stop:'),record,100)['severity'])

    def test_source_failures_are_explicit(self):
        def failed(url):raise OSError('synthetic')
        feed=n.build(100,failed);self.assertFalse(feed['events'])
        self.assertTrue(all(s['coverage']=='unavailable' and s['fetched_at']==0 for s in feed['sources']))

    def test_ci_invokes_shell_without_weakening_gates(self):
        for path in (ROOT/'.github/workflows').glob('*.yml'):
            text=path.read_text()
            self.assertNotRegex(text,r'run:\s+scripts/(?:install_tools|audit_python_tools)\.sh')
        ci=(ROOT/'.github/workflows/ci.yml').read_text()
        for gate in ('bash scripts/install_tools.sh','bash scripts/audit_python_tools.sh','--fail-on-defect high','package_installer.py','Run tests','Scan for committed secrets'):
            self.assertIn(gate,ci)

    def test_normal_copy_no_developer_shorthand(self):
        s=(ROOT/'src/EInkDisplay.cpp').read_text(encoding='utf8')
        self.assertNotIn('"PSR"',s);self.assertNotIn('日PSR',s)
        self.assertNotIn('sources += "九"',s);self.assertIn('部分營辦商資料暫缺',s)
        self.assertIn('全日顯著雨機會',s)

if __name__=='__main__':unittest.main()
