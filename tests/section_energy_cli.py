"""Exercise section energy requests in a deck and reopen the on-disk history."""
import csv
import io
from pathlib import Path
import subprocess
import sys
import tempfile

section, reader, fixture = map(Path, sys.argv[1:])
with tempfile.TemporaryDirectory() as scratch:
    directory = Path(scratch)
    deck, result = directory/"torque.inp", directory/"torque.sdbres"
    text = fixture.read_text().replace("2,2,6", "2,1,3\n2,5,6").replace("2,1,.002", "2,4,.0002")
    text = text.replace("*End Step", "*Output,field,frequency=50\n*Energy Output\nALLKE,ALLWK,ETOTAL\n*End Step")
    deck.write_text(text)
    subprocess.run([section,deck,result],check=True)
    def history(field):
        output = subprocess.check_output([reader,"--read",result,"--field",field,"--entity","0"],text=True)
        return [float(row["value"]) for row in csv.DictReader(io.StringIO(output))]
    wk, total = history("ALLWK"), history("ETOTAL")
    if len(wk)!=21 or len(total)!=21 or max(wk)<=0 or max(map(abs,total))>1e-9:
        raise SystemExit("unexpected energy history or schedule")
    print("section CLI torque energy histories PASS")
