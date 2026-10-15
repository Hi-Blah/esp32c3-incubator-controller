from pathlib import Path
import sys,unittest,os
root=Path(__file__).resolve().parents[1]/'services/recorder'
sys.path[:0]=[str(root),str(root/'vendor')];os.chdir(root)
result=unittest.TextTestRunner(verbosity=1).run(unittest.defaultTestLoader.discover(str(root),pattern='test_*.py'))
sys.exit(not result.wasSuccessful())
