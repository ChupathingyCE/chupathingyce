#!/usr/bin/env python3
"""bot_drivetest.py <run folder glob>...: the legs of HALO_BOT_DRIVEBENCH (port/linux/game/bots.c) added up,
per folder (each with the out.txt of one game) and per vehicle: legs driven, given up, left (out), with no
way; the mean speed over the legs driven; stops, standing and backing per leg; how they were driven."""
import collections, glob, re, sys
pat=re.compile(r'drivetest bot(\d+) (\S+) leg (\d+) (\d+) (in|given up|out|no way) in ([\d.]+) s \(([-\d.]+) u/s, (\d+) left\) stops (\d+) still ([\d.]+) s reverse ([\d.]+) s mode (\d)')
for g in sys.argv[1:]:
  for d in sorted(glob.glob(g)):
    rows=collections.defaultdict(list)
    for f in glob.glob(d+'/out.txt'):
      for line in open(f,errors='replace'):
        m=pat.search(line)
        if m: rows[m.group(2).split('\\')[-1]].append(m.groups())
    for v,r in sorted(rows.items()):
      done=[x for x in r if x[4]=='in']; gu=[x for x in r if x[4]=='given up']; out=[x for x in r if x[4]=='out']; nw=[x for x in r if x[4]=='no way']
      dist=sum(float(x[3]) for x in done); tm=sum(float(x[5]) for x in done)
      n=max(len(r),1)
      modes=collections.Counter(x[11] for x in r)
      print(f"{d.split('/')[-1]:28s} {v:22s} legs {len(done):3d} in {len(gu):2d} gave up {len(out):2d} out {len(nw):2d} noway | {dist/max(tm,1e-9):5.2f} u/s | stops/leg {sum(int(x[8]) for x in r)/n:4.2f} still {sum(float(x[9]) for x in r)/n:4.1f}s rev {sum(float(x[10]) for x in r)/n:4.1f}s | modes {dict(modes)}")
