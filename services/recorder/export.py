import argparse
from record_store import Store
import optical_store
from pathlib import Path
p=argparse.ArgumentParser(description="Export recorded sensor samples to CSV")
p.add_argument("--db",default="/records/incubator.sqlite3")
p.add_argument("--output",default="/records/temperature_humidity.csv")
a=p.parse_args()
s=Store(a.db)
try:
 s.export_samples(a.output)
 optical_store.export(s.db,Path(a.output).parent)
 print("Exported",a.output,"and light/calibration/notification CSVs")
finally:
 s.close()
