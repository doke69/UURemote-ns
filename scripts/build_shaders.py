"""Compile project-owned GLSL with an existing devkitPro uam; no downloads."""
from pathlib import Path
import argparse,subprocess
p=argparse.ArgumentParser();p.add_argument('--uam',default='uam');a=p.parse_args()
r=Path(__file__).resolve().parents[1];out=r/'romfs/shaders';out.mkdir(parents=True,exist_ok=True)
for name in ('nv12.vert','nv12.frag','ui.vert','ui.frag'):
    subprocess.run([a.uam,'-s',name.rsplit('.',1)[1],'-o',str(out/(name+'.dksh')),str(r/'shaders'/name)],check=True)
