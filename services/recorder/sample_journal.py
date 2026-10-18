"""Durable offline samples. Historical imports never drive alerts or the live cache."""
import json, math, re
MIN_EPOCH=1704067200000
MAX_EPOCH=4102444799999
REPAIR_BATCH=128

def integer(v,lo=0,hi=2**63-1):
    if type(v) is not int or not lo<=v<=hi:raise ValueError('sample integer bounds')
    return v

def initialize(db):
    db.executescript('''
    CREATE TABLE IF NOT EXISTS offline_samples(
      device TEXT,boot TEXT,boot_order INTEGER,seq TEXT,uptime_ms INTEGER,
      sensor_uptime_ms INTEGER,epoch_ms INTEGER,time_quality TEXT,
      received_ms INTEGER,payload TEXT,PRIMARY KEY(device,boot,seq));
    CREATE INDEX IF NOT EXISTS offline_timeline ON offline_samples(device,boot_order,uptime_ms);
    CREATE INDEX IF NOT EXISTS offline_unknown_clock ON offline_samples(device,boot,uptime_ms)
      WHERE time_quality='unknown';
    CREATE INDEX IF NOT EXISTS offline_nas_clock ON offline_samples(device,boot,uptime_ms)
      WHERE time_quality IN ('nas','backfilled-nas');
    CREATE TABLE IF NOT EXISTS sample_clock_anchors(
      device TEXT,boot TEXT,offset_ms INTEGER,uptime_ms INTEGER,epoch_ms INTEGER,
      received_ms INTEGER,quality TEXT,PRIMARY KEY(device,boot));
    CREATE TABLE IF NOT EXISTS sample_journal_status(
      device TEXT PRIMARY KEY,received_ms INTEGER,payload TEXT);
    ''')

def _repair_clock(db,device,boot,anchor,limit=REPAIR_BATCH):
    """Repair only unresolved rows, using indexed point updates and a bounded batch.

    Rewriting all sensor/light rows for every packet blocked the MQTT network loop
    as history grew. Already trusted NTP rows must never be revisited here.
    The caller owns the transaction; a receipt still follows a durable commit.
    """
    offset,quality=anchor
    changed=0
    predicates=["time_quality='unknown'"]
    if quality=='ntp':predicates.append("time_quality IN ('nas','backfilled-nas')")
    for predicate in predicates:
        if changed>=limit:break
        rows=db.execute(f'''SELECT seq,uptime_ms FROM offline_samples
            WHERE device=? AND boot=? AND {predicate} AND uptime_ms BETWEEN ? AND ?
            ORDER BY uptime_ms LIMIT ?''',
            (device,boot,MIN_EPOCH-offset,MAX_EPOCH-offset,limit-changed)).fetchall()
        for seq,uptime in rows:
            epoch=offset+uptime
            result=db.execute(f'UPDATE offline_samples SET epoch_ms=?,time_quality=? WHERE device=? AND boot=? AND seq=? AND {predicate}',
                (epoch,'backfilled-'+quality,device,boot,seq))
            if not result.rowcount:continue
            # These primary-key lookups also preserve rows which are not journal imports.
            for table in ('sensor_samples','light_samples'):
                db.execute(f'UPDATE {table} SET epoch_ms=?,trusted=1 WHERE boot=? AND sample_uptime=?',
                    (epoch,boot,uptime))
            changed+=1
    return changed

def repair_pending(db,device,limit=REPAIR_BATCH):
    """Continue bounded clock repairs even if no further replay packets arrive."""
    if not 1<=limit<=REPAIR_BATCH:raise ValueError('clock repair batch bounds')
    for predicate,anchor_rule in [("o.time_quality='unknown'",''),
            ("o.time_quality IN ('nas','backfilled-nas')","AND a.quality='ntp'")]:
        row=db.execute(f'''SELECT o.boot,a.offset_ms,a.quality FROM offline_samples o
            JOIN sample_clock_anchors a ON a.device=o.device AND a.boot=o.boot
            WHERE o.device=? AND {predicate} {anchor_rule}
            AND a.offset_ms+o.uptime_ms BETWEEN ? AND ? LIMIT 1''',
            (device,MIN_EPOCH,MAX_EPOCH)).fetchone()
        if row:
            with db:
                # The monitor and receive loop have separate connections. Acquire the
                # short write transaction, then re-read the anchor so a new NTP anchor
                # cannot be downgraded by a monitor which selected an earlier NAS one.
                db.execute('BEGIN IMMEDIATE')
                anchor=db.execute('SELECT offset_ms,quality FROM sample_clock_anchors WHERE device=? AND boot=?',
                    (device,row[0])).fetchone()
                return _repair_clock(db,device,row[0],anchor,limit) if anchor else 0
    return 0

def _history(db,boot,uptime,epoch,received,d,quality):
    flags=d['flags'];trusted=int(quality!='unknown');sensor=bool(flags&1)
    if flags&512:return
    db.execute('INSERT OR IGNORE INTO sensor_samples VALUES(?,?,?,?,?,?,?,?)',
      (boot,uptime,epoch or 0,received,d['temperature'] if sensor else None,d['humidity'] if sensor else None,int(sensor),trusted))
    for ch in (1,2):
        raw=d['raw'][ch-1] if flags&(2 if ch==1 else 4) else None
        # Keep historical ADC raw. Do not apply today's calibration to yesterday's lamp.
        db.execute('INSERT OR IGNORE INTO light_samples VALUES(?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?)',
          (boot,uptime,ch,epoch or 0,received,raw,None,None,None,0,0,0,None,None,'HISTORICAL_RAW',trusted,json.dumps(d,separators=(',',':'))))

def ingest(db,device,d,received,retained=False):
    if retained:raise ValueError('retained historical sample rejected')
    required={'v','boot_id','boot_order','seq','uptime_ms','epoch_ms','sensor_uptime_ms','flags','cfg','temperature','humidity','raw','frequency','duty','mode'}
    if set(d)-{'journal','outage_id','offline_elapsed_ms','period_s'}!=required:raise ValueError('sample schema')
    integer(d['v'],1,1)
    boot=d['boot_id'];m=re.fullmatch(re.escape(device)+r'-([1-9][0-9]{0,9})-([0-9a-f]{8})',boot) if isinstance(boot,str) else None
    if not m:raise ValueError('sample boot identity')
    order=integer(d['boot_order'],1,2**32-1)
    if order!=int(m[1]):raise ValueError('sample boot order')
    seq=d['seq']
    if not isinstance(seq,str) or not re.fullmatch(r'[1-9][0-9]{0,18}',seq):raise ValueError('sample sequence string')
    integer(int(seq),1)
    uptime=integer(d['uptime_ms'],0,2**53-1);sensor_uptime=integer(d['sensor_uptime_ms'],0,uptime)
    flags=integer(d['flags'],0,8191);integer(d['cfg'],1,2**32-1)
    epoch=integer(d['epoch_ms'],0,MAX_EPOCH)
    if flags&1024 and not flags&8:raise ValueError('NAS clock flag without anchor')
    if flags&8:
        if epoch<MIN_EPOCH or epoch>received+300000:raise ValueError('sample clock anchor bounds')
    elif epoch:raise ValueError('untrusted absolute sample time')
    for key,lo,hi in [('raw',0,4095),('frequency',0,20000),('duty',0,100),('mode',0,2)]:
        if not isinstance(d[key],list) or len(d[key])!=2:raise ValueError('sample pair')
        for v in d[key]:integer(v,lo,hi)
    for key,lo,hi in [('temperature',-100,200),('humidity',0,100)]:
        v=d[key]
        if flags&1:
            if type(v) not in (int,float) or not math.isfinite(v) or not lo<=v<=hi:raise ValueError('sample sensor range')
        elif v is not None:raise ValueError('invalid sensor must be null')
    adaptive={'outage_id','offline_elapsed_ms','period_s'}
    if adaptive & set(d):
        if not adaptive<=set(d):raise ValueError('incomplete adaptive sample metadata')
        if not isinstance(d['outage_id'],str) or not re.fullmatch(r'0|[1-9][0-9]{0,18}',d['outage_id']):raise ValueError('outage identity')
        integer(int(d['outage_id']));integer(d['offline_elapsed_ms'],0,2**53-1);integer(d['period_s'],1,3600)
        if bool(flags&2048)!=bool(int(d['outage_id'])):raise ValueError('outage flag mismatch')
        if not flags&2048 and d['offline_elapsed_ms']:raise ValueError('online sample with outage elapsed')
    meta=d.get('journal')
    if meta is not None:
        if not isinstance(meta,dict) or set(meta)-{'thinned'}!={'pending','capacity','covered','failures'}:raise ValueError('sample journal status')
        integer(meta['capacity'],1,2048);integer(meta['pending'],0,meta['capacity'])
        integer(meta.get('thinned',0),0,2**32-1);integer(meta['covered'],0,2**32-1);integer(meta['failures'],0,2**32-1)
    canonical=json.dumps({k:v for k,v in d.items() if k!='journal'},sort_keys=True,separators=(',',':'),allow_nan=False)
    # All modifications and any timestamp repairs commit before the caller publishes a receipt.
    with db:
        previous=db.execute('SELECT payload FROM offline_samples WHERE device=? AND boot=? AND seq=?',(device,boot,seq)).fetchone()
        if previous and previous[0]!=canonical:raise ValueError('conflicting duplicate sample')
        if flags&8:
            source='nas' if flags&1024 else 'ntp'
            old=db.execute('SELECT uptime_ms,quality FROM sample_clock_anchors WHERE device=? AND boot=?',(device,boot)).fetchone()
            if not old or (source=='ntp' and old[1]!='ntp') or (source==old[1] and uptime>=old[0]):
                db.execute('INSERT OR REPLACE INTO sample_clock_anchors VALUES(?,?,?,?,?,?,?)',(device,boot,epoch-uptime,uptime,epoch,received,source))
        anchor=db.execute('SELECT offset_ms,quality FROM sample_clock_anchors WHERE device=? AND boot=?',(device,boot)).fetchone()
        quality=('ntp' if flags&8 and not flags&1024 else 'backfilled-ntp') if anchor and anchor[1]=='ntp' else ('nas' if flags&8 else 'backfilled-nas') if anchor else 'unknown'
        resolved=epoch if flags&8 and (not flags&1024 or not anchor or anchor[1]!='ntp') else anchor[0]+uptime if anchor else None
        if resolved is not None and not MIN_EPOCH<=resolved<=MAX_EPOCH:raise ValueError('resolved time outside bounds')
        if not previous:
            db.execute('INSERT INTO offline_samples VALUES(?,?,?,?,?,?,?,?,?,?)',(device,boot,order,seq,uptime,sensor_uptime,resolved,quality,received,canonical))
            _history(db,boot,uptime,resolved,received,d,quality)
        if anchor:
            # Same-boot repairs only; never infer time across an unpowered reboot gap.
            _repair_clock(db,device,boot,anchor)
        if meta is not None:db.execute('INSERT OR REPLACE INTO sample_journal_status VALUES(?,?,?)',(device,received,json.dumps(meta,separators=(',',':'))))
    return {'v':1,'boot_id':boot,'seq':seq,'uptime_ms':uptime}

def status(db,device):
    row=db.execute('SELECT received_ms,payload FROM sample_journal_status WHERE device=?',(device,)).fetchone()
    counts=db.execute("SELECT COUNT(*),SUM(time_quality='unknown') FROM offline_samples WHERE device=?",(device,)).fetchone()
    live=db.execute('SELECT payload,last_live_ms FROM nas_live WHERE device=?',(device,)).fetchone()
    latest=json.loads(live[0]).get('sample_journal') if live else None
    return {'received_ms':live[1] if latest is not None else row[0] if row else 0,'journal':latest if latest is not None else json.loads(row[1]) if row else None,'stored':counts[0],'unknown_time':counts[1] or 0}

def csv_iter(db,device):
    import csv,io
    from datetime import datetime,timezone
    out=io.StringIO(newline='');w=csv.writer(out)
    def line(values):
        out.seek(0);out.truncate();w.writerow(values);return out.getvalue()
    yield line(['boot_id','boot_order','seq','capture_uptime_ms','sensor_uptime_ms','epoch_ms','time_quality','received_ms','temperature_C','humidity_percent','photo1_raw','photo2_raw','flags','mode1','mode2','frequency1','frequency2','duty1','duty2','outage_id','offline_powered_elapsed_ms','capture_period_s','capture_time_UTC'])
    for row in db.execute('SELECT boot,boot_order,seq,uptime_ms,sensor_uptime_ms,epoch_ms,time_quality,received_ms,payload FROM offline_samples WHERE device=? ORDER BY boot_order,uptime_ms,CAST(seq AS INTEGER)',(device,)):
        d=json.loads(row[-1]);yield line([*row[:-1],d['temperature'],d['humidity'],*d['raw'],d['flags'],*d['mode'],*d['frequency'],*d['duty'],d.get('outage_id',''),d.get('offline_elapsed_ms',''),d.get('period_s',''),datetime.fromtimestamp(row[5]/1000,timezone.utc).isoformat(timespec='milliseconds') if row[5] is not None else ''])

def csv_text(db,device):
    return ''.join(csv_iter(db,device))
