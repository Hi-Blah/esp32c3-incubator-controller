"""Read-only recorder diagnostics; pipe into the running container's Python."""
import json
import os
import sqlite3
import time
from pathlib import Path
from urllib.parse import quote
import sample_journal


def main():
    started=time.monotonic()
    device=os.getenv('DEVICE_ID','a1b2c3')
    path=Path(os.getenv('RECORD_DB','/records/incubator.sqlite3')).resolve()
    db=sqlite3.connect('file:'+quote(path.as_posix(),safe='/:')+'?mode=ro',uri=True,timeout=3)
    try:
        indexes={row[1] for row in db.execute('PRAGMA index_list(offline_samples)')}
        indexed={'offline_unknown_clock','offline_nas_clock'}<=indexes
        counts={table:db.execute('SELECT COUNT(*) FROM '+table).fetchone()[0]
            for table in ('offline_samples','sensor_samples','light_samples','messages')}
        row=db.execute('SELECT payload,last_live_ms,offline FROM nas_live WHERE device=?',(device,)).fetchone()
        data=json.loads(row[0]) if row else {}
        age=round((time.time_ns()//1000000-row[1])/1000,1) if row else None
        batch=getattr(sample_journal,'REPAIR_BATCH',None)
        installed=batch==128 and callable(getattr(sample_journal,'repair_pending',None)) and indexed
        result={'installation_ok':installed,'repair_batch':batch,'repair_indexes':indexed,
            'sqlite_version':sqlite3.sqlite_version,'rows':counts,'device_firmware':data.get('firmware'),
            'boot_id':data.get('boot_id'),'live_age_s':age,
            'live_fresh':bool(row and not row[2] and age is not None and 0<=age<=15),
            'sample_journal':data.get('sample_journal'),
            'database_read_ms':round((time.monotonic()-started)*1000)}
        print(json.dumps(result,ensure_ascii=False,indent=2))
        return 0 if installed else 1
    finally:db.close()


if __name__=='__main__':raise SystemExit(main())
