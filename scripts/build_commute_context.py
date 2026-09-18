#!/usr/bin/env python3
"""Bounded OFFICIAL PUBLIC source normalizer; no device/private input.

Strict formats have route effects; unparsed notices are counted as coverage gaps.
LCSD generic B/legend cells are never evidence of a dated Athletic Meet.
"""
from __future__ import annotations
import argparse
import datetime as dt
import hashlib
from html.parser import HTMLParser
import io
import json
from pathlib import Path
import re
import urllib.parse
import urllib.request
import xml.etree.ElementTree as ET
import zipfile

HKT = dt.timezone(dt.timedelta(hours=8))
CITYBUS = 'https://mobile.citybus.com.hk/NWST/special-arrangement-listing.php'
CITYBUS_NOTICES = 'https://mobile.citybus.com.hk/nwp3/getnotice.php'
LCSD = 'https://www.lcsd.gov.hk/clpss/en/webApp/Facility/Details.do?ftid=38&did=2'
KMB = 'https://search.kmb.hk/KMBWebSite/Function/FunctionRequest.ashx'
ALLOWED = {'mobile.citybus.com.hk', 'search.kmb.hk', 'www.lcsd.gov.hk'}
ROUTES = {'106': 0, '8P': 1, '118': 2}
MAX_BYTES = 2_000_000
MAX_EVENTS = 24

def fetch(url):
    parsed = urllib.parse.urlsplit(url)
    if parsed.scheme != 'https' or parsed.hostname not in ALLOWED or parsed.username:
        raise ValueError('Non-official source URL')
    request = urllib.request.Request(url, headers={'User-Agent': 'TransitInk-context-normalizer/1.3.1'})
    class OfficialRedirect(urllib.request.HTTPRedirectHandler):
        def redirect_request(self, req, fp, code, msg, headers, newurl):
            dest = urllib.parse.urlsplit(newurl)
            if dest.scheme != 'https' or dest.hostname not in ALLOWED:
                raise ValueError('Non-official redirect')
            return super().redirect_request(req, fp, code, msg, headers, newurl)
    with urllib.request.build_opener(OfficialRedirect).open(request, timeout=20) as response:
        data = response.read(MAX_BYTES + 1)
        if len(data) > MAX_BYTES: raise ValueError('Source size limit')
        return data

class Tables(HTMLParser):
    def __init__(self):
        super().__init__(); self.rows=[]; self.row=[]; self.cell=None; self.links=[]
    def handle_starttag(self, tag, attrs):
        if tag == 'tr': self.row=[]
        if tag in ('td','th'): self.cell=[]
        if tag == 'br' and self.cell is not None: self.cell.append('\n')
        if tag == 'a':
            href=dict(attrs).get('href','')
            if href: self.links.append(href)
    def handle_data(self, data):
        if self.cell is not None: self.cell.append(data)
    def handle_endtag(self, tag):
        if tag in ('td','th') and self.cell is not None:
            self.row.append(''.join(self.cell).strip()); self.cell=None
        if tag == 'tr' and self.row: self.rows.append(self.row); self.row=[]

def interval(text):
    # Only a single explicit full-date pair with no time-of-day/recurrence clause
    # is recognized. Other arrangements are retained as unparsed diagnostics.
    dates=re.findall(r'(20\d\d)年\s*(\d{1,2})月\s*(\d{1,2})日',text)
    if len(dates) != 2 or re.search(r'\d{1,2}[:：]\d{2}|\d+\s*(?:時|點)|每日|每天|逢|頭班|尾班',text):
        return None
    if not re.search(r'20\d\d年\s*\d{1,2}月\s*\d{1,2}日\s*(?:[（(][^）)]*[）)]\s*)?(?:至|到|[-–])\s*20\d\d年\s*\d{1,2}月\s*\d{1,2}日',text):
        return None
    start,end=[dt.datetime(*map(int,value),tzinfo=HKT) for value in dates]
    end+=dt.timedelta(days=1)
    if not 0 < (end-start).days <= 366: return None
    return int(start.timestamp()),int(end.timestamp())

def critical_scope(route, text):
    # Physical stop aliases, not interchangeable provider stop/direction codes.
    if route == '106' and ('維多利亞公園' in text or 'Victoria Park' in text) and ('高士威道' in text or 'Causeway Road' in text):
        return 'transfer'
    if route == '106' and ('紅磡街市' in text or 'Hung Hom Market' in text): return 'boarding'
    if route == '8P' and ('維多利亞公園' in text or 'Victoria Park' in text) and ('高士威道' in text or 'Causeway Road' in text): return 'boarding'
    if route == '118' and ('紅磡海底隧道收費廣場' in text or 'Cross Harbour Tunnel Toll Plaza' in text): return 'boarding'
    if route in ('8P','118') and ('漁灣邨' in text or 'Yue Wan Estate' in text): return 'destination'
    return None

def stop_name(route, scope):
    if scope=='transfer' or (route=='8P' and scope=='boarding'):return '維多利亞公園，高士威道'
    if scope=='destination':return '漁灣邨'
    if scope=='boarding':return '紅磡街市' if route=='106' else '紅磡海底隧道收費廣場'
    return ''

def kmb_notice_event(text, record, now):
    """Recognize one dated suspension table, never infer semantics from a title.

    Multi-table, image-only, open-ended and unsupported formats remain unparsed.
    This parser runs on the host, not the ESP32. Tests use synthetic notice text.
    """
    text=' '.join(text.split())
    route=record['route']
    if not re.search(r'(?:^|[,\s])'+route+r'(?=[,\s])',text):return None
    if 'Temporary Bus Stop Suspension' not in text:return None
    date=r'(\d{1,2})\s+([A-Za-z]+)\s+(20\d{2})\s+(\d{1,2}):(\d{2})'
    periods=re.findall(r'\bFrom\s+'+date+r'\s+To\s+'+date,text)
    sections=re.findall(r'Temp\.\s*Bus\s*stop\(s\)\s*suspended\s+To\s+([^:]+):\s*(.*?)\s*Temp/Alt\.\s*stop\(s\)\s*(.*)',text)
    if len(periods)!=1 or len(sections)!=1:return None
    direction,stops,replacement=sections[0]
    if direction.strip().lower() not in ('eastern district','siu sai wan','siu sai wan (island resort)'):return None
    scope=critical_scope(route,stops)
    if scope is None:return None
    parts=periods[0];months={name:i+1 for i,name in enumerate(('jan','feb','mar','apr','may','jun','jul','aug','sep','oct','nov','dec'))}
    def epoch(p):return int(dt.datetime(int(p[2]),months[p[1][:3].lower()],int(p[0]),int(p[3]),int(p[4]),tzinfo=HKT).timestamp())
    start,end=epoch(parts[:5]),epoch(parts[5:])
    if not 0 < end-start <= 366*86400:return None
    no_replacement=re.match(r'No Temporary Stop\.',replacement,re.I) is not None
    severity='hard_block' if no_replacement else 'degraded'
    return {'id':'kmb-'+hashlib.sha256((record['source_url']+route+scope).encode()).hexdigest()[:16],
        'source':'kmb','source_url':record['source_url'],'notice_id':record['notice_id'],
        'fetched_at':now,'route':route,'direction':'toward_yue_wan','operator_mask':2,
        'scope':scope,'stop_name_tc':stop_name(route,scope),'effect':'stop_omitted' if no_replacement else 'stop_relocated',
        'start':start,'end':end,'severity':severity,'verified':True,
        'message_tc':'九巴'+route+('維園轉車站' if scope=='transfer' else '所需車站')+('暫停' if no_replacement else '改動，步程待核實')}

def kmb_pdf_text(data):
    import pypdf
    if pypdf.__version__ != '6.10.0':raise ValueError('Use pinned context PDF dependency')
    try:
        reader=pypdf.PdfReader(io.BytesIO(data),strict=True)
        if len(reader.pages)>4:raise ValueError('Notice page limit')
        text='\n'.join(page.extract_text() or '' for page in reader.pages)
    except (pypdf.errors.PyPdfError,RecursionError,IndexError) as error:
        raise ValueError('Unsupported official notice PDF') from error
    if len(text)>64000:raise ValueError('Notice text limit')
    return text

class CitybusNoticeLinks(HTMLParser):
    """Read links in the official route Notice table, never execute its JS."""
    def __init__(self):
        super().__init__(); self.depth=0; self.found=False; self.urls=set(); self.invalid=0
    def handle_starttag(self, tag, attrs):
        attrs=dict(attrs)
        if tag=='table':
            if self.depth: self.depth+=1
            elif attrs.get('id')=='noticeitemlist': self.depth=1; self.found=True
        if not self.depth:return
        links=[]
        if tag=='a' and attrs.get('href'):links.append(attrs['href'])
        for key in ('onclick','onkeypress'):
            # This is the literal window.open used by Citybus, not arbitrary JS.
            links+=re.findall(r"window\.open\(\s*['\"]([^'\"]+)['\"]\s*\)",attrs.get(key,''))
        for link in links:
            url=urllib.parse.urlsplit(urllib.parse.urljoin(CITYBUS_NOTICES,link))
            if (url.scheme not in ('http','https') or url.netloc!='mobile.citybus.com.hk'
                    or not re.fullmatch(r'/pdf/[A-Za-z0-9_-]{1,64}\.pdf',url.path)
                    or url.query or url.fragment):
                self.invalid+=1;continue
            # The legacy keyboard link says http; fetch only HTTPS on this exact host.
            self.urls.add('https://mobile.citybus.com.hk'+url.path)
    def handle_endtag(self,tag):
        if tag=='table' and self.depth:self.depth-=1

def discover_citybus(route, request=fetch):
    if route not in ROUTES:raise ValueError('Unsupported route')
    page=request(CITYBUS_NOTICES+'?id='+route+'&l=1').decode('utf-8-sig')
    parser=CitybusNoticeLinks();parser.feed(page)
    if not parser.found:raise ValueError('Citybus Notice table unavailable')
    urls=sorted(parser.urls)
    return ([{'route':route,'source_url':url,'notice_id':url.rsplit('/',1)[1][:-4]}
             for url in urls[:30]], parser.invalid+max(0,len(urls)-30))

def citybus_pdf_text(data):
    """A single-page text table only. Layout extraction keeps split digits together.

    Image/OCR, multi-page and arbitrarily positioned multi-table notices fail closed.
    Keep the existing KMB extraction path unchanged.
    """
    import pypdf
    if pypdf.__version__!='6.10.0':raise ValueError('Use pinned context PDF dependency')
    try:
        reader=pypdf.PdfReader(io.BytesIO(data),strict=True)
        if len(reader.pages)!=1:raise ValueError('Unsupported Citybus page count')
        text=reader.pages[0].extract_text(extraction_mode='layout') or ''
    except (pypdf.errors.PyPdfError,RecursionError,IndexError,KeyError) as error:
        raise ValueError('Unsupported Citybus PDF') from error
    if not text or len(text)>64000:raise ValueError('Citybus text limit')
    return text

def citybus_notice_period(text):
    """Require one explicit continuous HKT period, with matching bilingual times.

    Publication/posting dates are not validity. No recurrence, inferred year,
    first/last bus, until-further-notice, or OCR repairs are accepted.
    """
    compact=re.sub(r'\s+','',text)
    for label in ('日期及時間','Date&Period','DateandTime','生效時間','Effectivetime'):
        compact=compact.replace(label,'')
    if re.search(r'每日|每天|逢|頭班|尾班|另行|除外|只限|僅限|恢復|已取消|取消安排|daily|except|each|until further|cancelled|canceled|withdrawn|resumed',text,re.I):return None
    date=r'(20\d{2})年(\d{1,2})月(\d{1,2})日(?:[（(]星期[一二三四五六日天][）)])?(上午|下午|中午)(\d{1,2})時(?:(\d{1,2})分|正)?'
    matches=re.findall(date+r'(?:起)?至'+date+r'(?:止)?',compact)
    if len(matches)!=1:return None
    def epoch(p):
        year,month,day,ampm,hour,minute=p;hour=int(hour)
        if not 1<=hour<=12 or (ampm=='中午' and hour!=12):raise ValueError('Unsupported hour')
        return int(dt.datetime(int(year),int(month),int(day),hour%12+(12 if ampm!='上午' else 0),int(minute or 0),tzinfo=HKT).timestamp())
    try:
        start,end=epoch(matches[0][:6]),epoch(matches[0][6:])
        # Independently cross-check the English period when supplied. Whitespace
        # inside a four-digit year is a PDF glyph spacing artifact, not a new year.
        edate=r'(\d{1,2}):(\d{2})\s*([ap])\.?m\.?\s+on\s+(\d{1,2})\s+([A-Za-z]+)\s+(20\s*\d\s*\d)\s*\([A-Za-z]+\s*\)'
        periods=re.findall(r'\bFrom\s+'+edate+r'\s+to\s+'+edate,' '.join(text.split()),re.I)
        if len(periods)!=1:return None
        months={name:i+1 for i,name in enumerate(('january','february','march','april','may','june','july','august','september','october','november','december'))}
        def english(p):
            hour,minute,ampm,day,month,year=p;hour=int(hour)
            if not 1<=hour<=12:raise ValueError('Unsupported hour')
            return int(dt.datetime(int(re.sub(r'\s','',year)),months[month.lower()],int(day),hour%12+(12 if ampm.lower()=='p' else 0),int(minute),tzinfo=HKT).timestamp())
        if (start,end)!=(english(periods[0][:6]),english(periods[0][6:])):return None
    except (ValueError,KeyError):return None
    return (start,end) if 0<end-start<=366*86400 else None

def citybus_notice_event(text,record,now):
    """Supported bilingual single-table omission/suspension/relocation formats.

    Match the operative row, never a title, route substring, map or another
    route's footnote. Routes/directions must map one-to-one, or have one shared
    physical direction. An indexed document is not itself a route effect.
    """
    route=record['route'];compact=re.sub(r'\s+','',text)
    if route not in ROUTES or 'CustomerNotice' not in compact or 'CitybusLimited' not in compact:return None
    period=citybus_notice_period(text)
    if period is None:return None
    headers=re.findall(r'^[ \t]*路線[ \t]+Routes?[ \t]*([^\r\n]+)$',text,re.M)
    if len(headers)!=1:return None
    headers=[re.sub(r'\s+','',headers[0])]
    if not re.fullmatch(r'\d{1,3}[A-Z]?(?:[,，]\d{1,3}[A-Z]?)*',headers[0]):return None
    routes=re.split('[,，]',headers[0])
    if len(set(routes))!=len(routes) or route not in routes:return None
    direction_rows=re.findall(r'^[ \t]*方向[ \t]*(?:Direction[ \t]*)?([^\r\n]+)$',text,re.M)
    directions=[]
    for row in direction_rows:
        match=re.match(r'([\u4e00-\u9fff（）()/／]+)',re.sub(r'\s+','',row))
        if match:directions.append(match[1])
    if len(directions)!=1:return None
    bounds=re.split('[/／]',directions[0])
    if len(bounds)==1:bound=bounds[0]
    elif len(bounds)==len(routes):bound=bounds[routes.index(route)]
    else:return None
    if bound not in ('小西灣','小西灣(藍灣半島)','小西灣（藍灣半島）'):return None
    aliases_direction={'小西灣':'SiuSaiWan','筲箕灣':'ShauKeiWan',
        '小西灣(藍灣半島)':'SiuSaiWan(IslandResort)','小西灣（藍灣半島）':'SiuSaiWan(IslandResort)'}
    if any(b not in aliases_direction for b in bounds):return None
    block=re.split('巴士站安排|停用巴士站',compact.split('方向',1)[1],maxsplit=1)[0]
    # Verify the second language too, including the order of paired directions.
    # PDF glyph spacing is removed, but no words/directions are invented.
    if block.count('Direction')!=1:return None
    english_bounds=block.replace(directions[0],'',1).replace('Direction','',1).replace('／','/')
    if english_bounds!='/'.join(aliases_direction[b] for b in bounds):return None
    # Require a single operative section; multi-table and per-route exceptions
    # are deliberately not flattened into an all-routes stop suspension.
    if compact.count('巴士站安排')==1 and '停用巴士站' not in compact:
        matches=re.findall(r'上述期間不停([\u4e00-\u9fff，,（）()]+)巴士站',compact)
        if len(matches)!=1 or compact.count('不停')!=1:return None
        stop=matches[0]
        sentence=text.replace('Arrangement','')
        english=re.findall(r'^\s*(?:Bus Stop\s+)?Omit\s+(.+?)\s+bus stop\s+during the said period\.',sentence,re.I|re.M|re.S)
        if len(english)!=1:return None
        effect='stop_omitted'
    elif compact.count('停用巴士站')==1 and compact.count('臨時巴士站')==1:
        section=compact.split('停用巴士站',1)[1].split('臨時巴士站',1)[0]
        section=section.replace('SuspendedBusStop','')
        match=re.fullmatch(r'(?:SuspendedBusStop)?([\u4e00-\u9fff，,（）()]+)([A-Za-z ,’\'-]+)',section)
        if not match:return None
        stop=match[1];english=[match[2]]
        replacement=compact.split('臨時巴士站',1)[1].split('不便之處',1)[0]
        if re.fullmatch(r'(?:TemporaryBusStop)?不設臨時站NoTemporaryBusStop\.?',replacement,re.I):effect='stop_omitted'
        elif re.fullmatch(r'(?:TemporaryBusStop)?[前後]移約\d{1,3}米Relocated(?:forward|backward)forabout\d{1,3}metres\.?',replacement,re.I):effect='stop_relocated'
        else:return None
    else:return None
    aliases={
        '高士威道維多利亞公園外':('Causeway Road outside Victoria Park','Victoria Park, Causeway Road'),
        '維多利亞公園，高士威道':('Causeway Road outside Victoria Park','Victoria Park, Causeway Road'),
        '紅磡街市':('Hung Hom Market',),
        '紅磡海底隧道收費廣場':('Cross Harbour Tunnel Toll Plaza',),
        '漁灣邨':('Yue Wan Estate',),
    }
    if stop not in aliases:return None
    en=re.sub(r'\s+','',english[0]).casefold()
    if en not in {re.sub(r'\s+','',a).casefold() for a in aliases[stop]}:return None
    scope=critical_scope(route,stop)
    if scope is None:return None
    start,end=period
    return {'id':'ctb-pdf-'+hashlib.sha256((record['source_url']+route+scope).encode()).hexdigest()[:16],
        'source':'citybus','source_url':record['source_url'],'notice_id':record['notice_id'],
        'fetched_at':now,'route':route,'direction':'toward_yue_wan','operator_mask':1,
        'scope':scope,'stop_name_tc':stop_name(route,scope),'effect':effect,'start':start,'end':end,
        'severity':'hard_block' if effect=='stop_omitted' else 'degraded','verified':True,
        'message_tc':'城巴'+route+('維園轉車站' if scope=='transfer' else '所需車站')+('暫停' if effect=='stop_omitted' else '改動，步程待核實')}

def citybus_context(now,request):
    events=[];details=[];unparsed=0;success=0;cache={}
    try:
        batch,unmatched=citybus_events(request(CITYBUS).decode('utf-8'),now)
        events+=batch;unparsed+=unmatched;success+=1
        details.append({'channel':'structured','status':'fetched','unparsed':unmatched})
    except (OSError,ValueError,KeyError,TypeError):
        details.append({'channel':'structured','status':'unavailable'});unparsed+=1
    for route in ROUTES:
        try:
            records,unmatched=discover_citybus(route,request);success+=1;unparsed+=unmatched
            details.append({'channel':'notices','route':route,'status':'fetched','discovered':len(records),'unparsed_links':unmatched})
        except (OSError,ValueError,KeyError,TypeError):
            details.append({'channel':'notices','route':route,'status':'unavailable'});unparsed+=1;continue
        for record in records:
            url=record['source_url'];reason='unsupported_format';event=None
            try:
                if url not in cache:
                    if len(cache)>=12:raise ValueError('Citybus PDF fetch limit')
                    cache[url]=None  # failed/unsupported URLs also consume the bound
                    raw=request(url);cache[url]=(citybus_pdf_text(raw),hashlib.sha256(raw).hexdigest())
                if cache[url] is not None:event=citybus_notice_event(cache[url][0],record,now)
                if event:
                    event['source_sha256']=cache[url][1];events.append(event)
                    reason='recognized_active' if event['start']<=now<event['end'] else 'recognized_inactive'
            except (OSError,ValueError,KeyError,TypeError,ImportError):reason='fetch_or_parse_unavailable'
            if event is None:unparsed+=1
            if len(details)<18:details.append({'route':route,'notice_id':record['notice_id'],'status':reason})
    return events,{'source':'citybus','coverage':'partial' if success else 'unavailable',
        'fetched_at':now if success else 0,'unparsed':unparsed,
        'limitation':'Official structured listing plus dynamically discovered Customer Notice PDFs. Strict bilingual dated single-table omission/relocation only; maps, image route badges, recurrence and other formats remain unparsed. Citybus only.'},details

def citybus_events(html, now):
    parser=Tables(); parser.feed(html)
    if 'Suspended Bus Stop' not in html or 'Last Update' not in html: raise ValueError('Citybus schema changed')
    events=[]; unresolved=0
    for row in parser.rows:
        if len(row) != 6: continue
        route,direction,origin,arrangement,suspended,replacement=row
        route=route.strip()
        if route not in ROUTES: continue
        # Direction absent/ambiguous must not become a physically scoped block.
        toward=bool(re.search(r'小西灣|Siu Sai Wan',direction,re.I))
        scope=critical_scope(route,suspended)
        validity=interval(arrangement)
        severity='hard_block'; effect='stop_omitted'
        if replacement.strip(): severity='degraded';effect='stop_relocated'
        if not scope:
            scope=critical_scope(route,arrangement)
            if scope and re.search(r'遷移|後移|前移|relocat',arrangement,re.I): severity='degraded';effect='stop_relocated'
            else: scope=None
        if not scope and re.search(r'全線暫停服務|service suspended on the entire route',arrangement,re.I):
            scope='route';effect='service_suspended'
        if not (toward and scope and validity): unresolved+=1;continue
        label={'boarding':'上車站','transfer':'維園轉車站','destination':'漁灣邨站','route':'服務'}[scope]
        message=f'城巴{route}{label}'+('暫停' if severity=='hard_block' else '遷移，步程未核實')
        events.append({'id':'ctb-'+hashlib.sha256('|'.join(row).encode()).hexdigest()[:16],
            'source':'citybus','source_url':CITYBUS,'fetched_at':now,'route':route,
            'direction':'toward_yue_wan','operator_mask':1,'scope':scope,'stop_name_tc':stop_name(route,scope),'effect':effect,
            'start':validity[0],'end':validity[1],'severity':severity,'verified':True,'message_tc':message})
    return events,unresolved

def workbook_rows(data):
    ns={'m':'http://schemas.openxmlformats.org/spreadsheetml/2006/main'}
    with zipfile.ZipFile(io.BytesIO(data)) as archive:
        if sum(i.file_size for i in archive.infolist()) > 12_000_000: raise ValueError('Workbook expansion limit')
        strings=[]
        if 'xl/sharedStrings.xml' in archive.namelist():
            strings=[''.join(n.text or '' for n in item.findall('.//m:t',ns)) for item in ET.fromstring(archive.read('xl/sharedStrings.xml')).findall('m:si',ns)]
        sheets=[]
        for name in archive.namelist():
            if not re.fullmatch(r'xl/worksheets/sheet\d+\.xml',name): continue
            xml=ET.fromstring(archive.read(name)); rows=[]
            for row in xml.findall('.//m:row',ns):
                cells={}
                for c in row.findall('m:c',ns):
                    v=c.findtext('m:v','',ns)
                    text=strings[int(v)] if c.get('t')=='s' and v else ''.join(n.text or '' for n in c.findall('.//m:t',ns)) if c.get('t')=='inlineStr' else v
                    cells[re.sub(r'\d','',c.get('r',''))]=text
                rows.append(cells)
            sheets.append(rows)
        return sheets

def athletic_dates(sheets):
    dates=set(); recognized=False; unresolved=0
    for rows in sheets:
        # Detailed dated closure table only; never use the generic jogging legend.
        detailed=any('Date & Time of Temporary Closure' in ' '.join(r.values()) or '暫停開放時期' in ' '.join(r.values()) for r in rows)
        if not detailed: continue
        recognized=True
        for row in rows:
            reason=' '.join(v for k,v in row.items() if k != 'A')
            if not re.search(r'\bAthletic\s+Meet\b|運動會',reason,re.I): continue
            text=row.get('A','').strip()
            # Exact one-day or explicit date-range labels; ambiguous multi-date
            # shorthand is flagged, never guessed or applied to every date.
            match=re.fullmatch(r'(\d{1,2})/(\d{1,2})/(20\d{2})\s+(?:\d{4}|\d{1,2}:\d{2})\s*[-–]\s*(?:\d{4}|\d{1,2}:\d{2})',text)
            if match:
                day,month,year=map(int,match.groups());dates.add(dt.date(year,month,day));continue
            match=re.fullmatch(r'(\d{1,2})/(\d{1,2})/(20\d{2})\s*[-–]\s*(\d{1,2})/(\d{1,2})/(20\d{2})(?:\s+\d{4}\s*[-–]\s*\d{4})?',text)
            if match:
                d,m,y,d2,m2,y2=map(int,match.groups());a=dt.date(y,m,d);b=dt.date(y2,m2,d2)
                if 0 <= (b-a).days <= 31:
                    dates.update(a+dt.timedelta(days=i) for i in range((b-a).days+1));continue
            unresolved+=1
    if not recognized: raise ValueError('LCSD dated detail sheet not found')
    return dates,unresolved

def lcsd_events(data, url, now):
    dates,unresolved=athletic_dates(workbook_rows(data)); events=[]
    for date in sorted(dates):
        start=int(dt.datetime.combine(date,dt.time(),HKT).timestamp())
        events.append({'id':'lcsd-'+date.isoformat(),'source':'lcsd','source_url':url,'fetched_at':now,
            'route':'8P','direction':'toward_yue_wan','operator_mask':1,'scope':'route','effect':'athletic_meet',
            'start':start,'end':start+86400,'severity':'advisory','verified':True,
            'message_tc':'小西灣運動場有運動會，8P今日或較擠迫'})
    return events,unresolved

def discover_kmb(route, request=fetch):
    bounds=json.loads(request(KMB+'?action=getroutebound&route='+route))
    if bounds.get('result') is not True or not isinstance(bounds.get('data'),list): raise ValueError('KMB bounds schema changed')
    # Discover available bound identifiers from official route metadata; never
    # assume that local O/I or numeric 1/2 means a physical direction globally.
    records=[]
    for bound in sorted({str(row['BOUND']) for row in bounds['data'] if 'BOUND' in row}):
        if bound not in ('1','2'): continue
        url=KMB+'?action=getAnnounce&route='+route+'&bound='+bound
        obj=json.loads(request(url))
        if obj.get('result') is not True or not isinstance(obj.get('data'),list): raise ValueError('KMB notice schema changed')
        for row in obj['data'][:30]:
            identifier=row.get('kpi_noticeimageurl','')
            if not re.fullmatch(r'[A-Za-z0-9_.-]+\.(?:pdf|html)',identifier): continue
            if row.get('krbpiid_routeno') != route or str(row.get('krbpiid_boundno')) != bound: continue
            records.append({'route':route,'provider_bound':bound,'notice_id':row.get('kpi_referenceno','')[:64],
                'source_url':'https://search.kmb.hk/KMBWebSite/AnnouncementPicture.ashx?url='+identifier})
    if not bounds['data'] or not any('BOUND' in row for row in bounds['data']): raise ValueError('KMB bound identifiers unavailable')
    return records

def build(now, request=fetch):
    events=[];sources=[]; diagnostics={}
    for source in ('citybus','kmb','lcsd'):
        status={'source':source,'coverage':'unavailable','fetched_at':0,'unparsed':0}
        try:
            if source=='citybus':
                batch,status,details=citybus_context(now,request)
                events+=batch;diagnostics['citybus_notices']=details
            elif source=='kmb':
                notices=discover_kmb('106',request)+discover_kmb('118',request)
                diagnostics['kmb_notices']=notices[:12]
                diagnostics['kmb_discovered_count']=len(notices)
                cache={};parsed=set();unparsed=0
                for record in notices[:12]:
                    try:
                        url=record['source_url']
                        if not url.endswith('.pdf'):unparsed+=1;continue
                        if url not in cache:
                            raw=request(url);cache[url]=(kmb_pdf_text(raw),hashlib.sha256(raw).hexdigest())
                        event=kmb_notice_event(cache[url][0],record,now)
                        if event and event['id'] not in parsed:
                            event['source_sha256']=cache[url][1];events.append(event);parsed.add(event['id'])
                        elif event is None:unparsed+=1
                    except (OSError,ValueError,KeyError,ImportError):unparsed+=1
                status.update(coverage='partial',unparsed=unparsed+max(0,len(notices)-12),
                    limitation='Official discovery plus one explicit dated suspension-table PDF format; other PDF/HTML formats and timetable semantics remain unparsed. Operator-specific only.')
            else:
                page=request(LCSD).decode('utf-8');parser=Tables();parser.feed(page)
                links=sorted({urllib.parse.urljoin(LCSD,link) for link in parser.links if re.search(r'/jogging/488_20\d{4}\.xlsx$',link)})
                if not links or len(links)>3: raise ValueError('LCSD timetable discovery format changed')
                unparsed=0
                for link in links:
                    batch,unmatched=lcsd_events(request(link),link,now);events+=batch;unparsed+=unmatched
                status.update(coverage='partial' if unparsed else 'supported',unparsed=unparsed,
                    limitation='Explicit dated Athletic Meet detail rows only; general booking letters are not specific enough. Bookings can change.')
            if source!='citybus':status['fetched_at']=now
        except (OSError,ValueError,KeyError,TypeError,ET.ParseError,zipfile.BadZipFile) as error:
            status['error']=type(error).__name__  # no raw body or private environment output
        sources.append(status)
    events=[e for e in events if e['end']>now]
    events.sort(key=lambda e:(e['start'],e['id']))
    dropped=max(0,len(events)-MAX_EVENTS)
    # Prioritize active disruptions, then forthcoming crowding days.
    events.sort(key=lambda e:(e['effect']=='athletic_meet',e['start'],e['id']))
    feed={'schema':1,'generated_at':now,'sources':sources,'events':events[:MAX_EVENTS],
          'truncated_events':dropped,'diagnostics':diagnostics}
    encoded=json.dumps(feed,ensure_ascii=False,separators=(',',':')).encode()
    if len(encoded)>24576: raise ValueError('Normalized feed size limit')
    return feed

def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--output',type=Path,required=True);args=p.parse_args()
    result=build(int(dt.datetime.now(dt.timezone.utc).timestamp()))
    args.output.parent.mkdir(parents=True,exist_ok=True)
    args.output.write_text(json.dumps(result,ensure_ascii=False,separators=(',',':'))+'\n',encoding='utf-8')
    print(json.dumps({'events':len(result['events']),'coverage':result['sources']},ensure_ascii=False))
if __name__=='__main__': main()
