#!/usr/bin/env python3
"""TEMP fixed-work FFT comparison. Never GUI/receiver qualification."""
import argparse,datetime,gzip,hashlib,io,json,os,platform,resource,subprocess,tarfile,time
from pathlib import Path
BASELINE="bbb47e020d9c6ec3e85fca8cf58d7062b776d9ff"
CANDIDATE="789fb91e351f9c2ecd3ef72b9881f74d7a4796fd"
PINS={"baseline":"190c0697b64209ddc05204e3ec690f936d30fec94e4273f36e2da8f9496888f0","candidate":"1740668a7f04dfa20c1f73c35709cb5d9696513b9dc047b31409072c134feebc"}
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def save(p,v):p.write_text(json.dumps(v,indent=2)+"\n")
def main():
 p=argparse.ArgumentParser();p.add_argument("--root",type=Path,required=True);p.add_argument("--output",type=Path,required=True);p.add_argument("--compiler",default="g++-11");p.add_argument("--iterations",type=int,default=4096);a=p.parse_args();r=a.root.resolve();out=a.output.resolve()
 if not 1<=a.iterations<=16384:raise ValueError("iterations outside bounded range")
 if out.exists():raise ValueError("output must be fresh")
 out.mkdir(parents=True);manifest={"qualification":False,"kind":"synthetic fixed-work paired inverse256/512, not full EU fixture","source_head":subprocess.check_output(["git","rev-parse","HEAD"],cwd=r,text=True).strip(),"baseline":BASELINE,"candidate":CANDIDATE,"pins":PINS,"iterations":a.iterations,"harness_sha256":sha(Path(__file__).with_suffix(".cpp")),"tool_sha256":sha(Path(__file__)),"started_utc":datetime.datetime.now(datetime.timezone.utc).isoformat(),"runner":{k:os.environ.get(k) for k in ("RUNNER_NAME","RUNNER_OS","RUNNER_ARCH","GITHUB_RUN_ID","GITHUB_RUN_ATTEMPT","GITHUB_SHA")},"platform":platform.platform(),"cpuinfo":Path("/proc/cpuinfo").read_text(),"memory":Path("/proc/meminfo").read_text(),"compiler":subprocess.check_output([a.compiler,"--version"],cwd=r,text=True),"state":"preparing","commands":[],"timing_boundary":"Same-thread wall and thread CPU per ABBA block, includes identical input copy/ABI wrapper. No performance acceptance gate; diagnostic not real-time/stop qualification."}
 save(out/"manifest.json",manifest)
 def command(cmd,stdout,stderr,limit=180,compile=False,env=None):
  def limits():
   resource.setrlimit(resource.RLIMIT_CPU,(120,120))
   if compile:resource.setrlimit(resource.RLIMIT_AS,(2*1024**3,2*1024**3))
  start=time.monotonic()
  with stdout.open("wb") as o,stderr.open("wb") as e:c=subprocess.run(cmd,cwd=r,stdout=o,stderr=e,timeout=limit,preexec_fn=limits,env=env)
  manifest["commands"].append({"argv":cmd,"exit":c.returncode,"seconds":time.monotonic()-start,"stdout":str(stdout.relative_to(out)),"stderr":str(stderr.relative_to(out))});save(out/"manifest.json",manifest)
  if c.returncode:raise RuntimeError("command failed; retain original logs: "+str(cmd[0]))
 try:
  headers={}
  for name,rev in (("baseline",BASELINE),("candidate",CANDIDATE)):
   d=out/"snapshots"/name;d.mkdir(parents=True)
   raw=subprocess.check_output(["git","archive",rev,"src/search_fft.cpp","src/search_fft.hpp","include"],cwd=r)
   with tarfile.open(fileobj=io.BytesIO(raw),mode="r:") as t:
    for item in t.getmembers():
     if not (item.isdir() or item.isfile()) or item.name.startswith("/") or ".." in Path(item.name).parts:raise ValueError("unsafe Git archive entry")
    t.extractall(d)
   if sha(d/"src/search_fft.cpp")!=PINS[name]:raise ValueError("FFT source pin mismatch")
   headers[name]={str(q.relative_to(d)):sha(q) for q in d.rglob("*") if q.is_file() and q.name!="search_fft.cpp"}
   (d/"bridge.cpp").write_text(f'''#define search_fft {name}_borrowed_fft
#include "src/search_fft.cpp"
#undef search_fft
extern "C" void {name}_prepare(std::complex<double>* f,std::complex<double>* b,std::size_t n){{datapump::modem::detail::{name}_borrowed_fft::prepare_double_tables({{f,n-1}},{{b,n-1}});}}
extern "C" void {name}_run(std::complex<double>* a,std::size_t n,bool inv,const std::complex<double>* f,const std::complex<double>* b,std::size_t max,std::stop_token stop){{const datapump::modem::detail::{name}_borrowed_fft::DoubleTableView view{{max,{{f,max-1}},{{b,max-1}}}};datapump::modem::detail::{name}_borrowed_fft::fft({{a,n}},inv,view,stop);}}
''')
  if headers["baseline"]!=headers["candidate"]:raise ValueError("header snapshots differ")
  manifest["headers"]=headers["baseline"];manifest["state"]="compiling";save(out/"manifest.json",manifest)
  for san in ("on","off"):
   d=out/("san-"+san);d.mkdir();flags=["-std=c++20","-O1","-g","-fno-fast-math","-ffp-contract=off","-fno-omit-frame-pointer"]+(["-fsanitize=address,undefined"] if san=="on" else []);objects=[]
   for name in ("baseline","candidate"):
    snap=out/"snapshots"/name;obj=d/(name+".o");objects.append(str(obj))
    command([a.compiler,*flags,"-I"+str(snap/"src"),"-I"+str(snap/"include"),"-c",str(snap/"bridge.cpp"),"-o",str(obj)],d/(name+"-compile.stdout"),d/(name+"-compile.stderr"),compile=True)
    command(["objdump","-drwC",str(obj)],d/(name+"-assembly.txt"),d/(name+"-assembly.stderr"))
    command(["nm","-S","-C",str(obj)],d/(name+"-symbols.txt"),d/(name+"-symbols.stderr"))
    assembly=d/(name+"-assembly.txt");assembly.with_suffix(".txt.gz").write_bytes(gzip.compress(assembly.read_bytes(),mtime=0));assembly.unlink()
   binary=d/"harness"
   command([a.compiler,*flags,str(Path(__file__).with_suffix(".cpp")),*objects,"-o",str(binary)],d/"harness-compile.stdout",d/"harness-compile.stderr",compile=True)
   env=dict(os.environ,ASAN_OPTIONS="detect_leaks=0:halt_on_error=1",UBSAN_OPTIONS="halt_on_error=1:print_stacktrace=1")
   command([str(binary),"--iterations",str(a.iterations)],d/"results.jsonl",d/"run.stderr",limit=90,env=env)
   rows=[json.loads(line) for line in (d/"results.jsonl").read_text().splitlines()]
   if len(rows)!=34 or rows[0]["type"]!="environment" or rows[-1]!={"type":"complete","blocks":32,"environment_rows":1,"complete":True}:raise ValueError("incomplete row inventory")
   expected=[(n,recipe,order,variant) for n in (256,512) for recipe in ("ordinary","signed_zero","tiny_normal","subnormal") for order,variant in enumerate(("baseline","candidate","candidate","baseline"))]
   for row,(n,recipe,order,variant) in zip(rows[1:-1],expected):
    if (row["n"],row["recipe"],row["order"],row["variant"])!=(n,recipe,order,variant) or row["output_exact"] is not True or row["iterations"]!=(min(a.iterations,256) if recipe=="subnormal" else a.iterations):raise ValueError("row identity mismatch")
    for field in ("wall_ns","thread_cpu_ns"):
     if type(row[field]) is not int or row[field]<=0:raise ValueError("invalid timing")
    if row["rounding"]!=0 or row["traps"]!=0 or row["mxcsr_control"]!=8064:raise ValueError("floating-point controls mismatch")
   for i in range(1,33,4):
    if len({(x["output_hash_fnv64"],x["fe_flags"],x["mxcsr_flags"]) for x in rows[i:i+4]})!=1:raise ValueError("paired outputs or flags differ")
  manifest["state"]="completed_pair_not_qualification"
 except BaseException as e:manifest["state"]="failed_not_accepted";manifest["error"]=repr(e);raise
 finally:
  manifest["finished_utc"]=datetime.datetime.now(datetime.timezone.utc).isoformat();manifest["artifacts"]={str(q.relative_to(out)):sha(q) for q in out.rglob("*") if q.is_file() and q.name!="manifest.json"};save(out/"manifest.json",manifest)
 print(json.dumps({"state":manifest["state"],"qualification":False,"source":manifest["source_head"],"manifest_sha256":sha(out/"manifest.json")}))
if __name__=="__main__":main()
