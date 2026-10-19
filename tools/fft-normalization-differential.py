#!/usr/bin/env python3
"""TEMP same-revision borrowed inverse-normalization diagnostic; not qualification."""
import argparse, datetime, difflib, hashlib, io, json, os, platform, resource, subprocess, tarfile, time
from pathlib import Path
BASE="4f8f2703802baf03b9f931d5856e19b70350aa1a"
SOURCE_SHA="1740668a7f04dfa20c1f73c35709cb5d9696513b9dc047b31409072c134feebc"

def sha(path): return hashlib.sha256(path.read_bytes()).hexdigest()
def save(path, obj): path.write_text(json.dumps(obj, indent=2)+"\n")
def main():
    ap=argparse.ArgumentParser();ap.add_argument("--root",type=Path,required=True);ap.add_argument("--output",type=Path,required=True);ap.add_argument("--compiler",default="c++");ap.add_argument("--iterations",type=int,default=1);a=ap.parse_args()
    root=a.root.resolve();out=a.output.resolve()
    if not 1<=a.iterations<=16384: raise ValueError("iterations outside bounded range")
    if out.exists(): raise ValueError("output directory must be fresh")
    out.mkdir(parents=True)
    def git(*args): return subprocess.check_output(["git",*args],cwd=root)
    head=git("rev-parse","HEAD").decode().strip()
    meminfo=Path("/proc/meminfo").read_text()
    available_kb=int(next(x.split()[1] for x in meminfo.splitlines() if x.startswith("MemAvailable:")))
    if available_kb<4*1024*1024: raise RuntimeError("less than 4 GiB MemAvailable before compile")
    cpuinfo=Path("/proc/cpuinfo").read_text()
    if not any(" avx2 " in line for line in cpuinfo.splitlines() if line.startswith("flags")):
        raise RuntimeError("host does not advertise AVX2")
    try: os.nice(10)
    except OSError as e: raise RuntimeError("cannot apply authorized nice(10)") from e
    py=Path(__file__);cpp=py.with_suffix(".cpp")
    manifest={"qualification":False,"purpose":"same-revision actual-TU inverse-normalization diagnostic","launcher_head":head,"source_head":BASE,"source_sha256_expected":SOURCE_SHA,"source_binding":"Both translation units and all included project headers are archived from source_head. Launcher HEAD may include unrelated runtime or CI changes.","iterations":a.iterations,"started_utc":datetime.datetime.now(datetime.timezone.utc).isoformat(),"platform":platform.platform(),"mem_available_kib_before_compile":available_kb,"cpu_avx2":True,"compiler":subprocess.check_output([a.compiler,"--version"],cwd=root,text=True),"tool_sha256":sha(py),"harness_sha256":sha(cpp),"commands":[],"state":"preparing","timing":"same-host same-thread ABBA inverse 256/512; exact outputs/flags required; diagnostic only"}
    save(out/"manifest.json",manifest)
    def command(argv, cwd, stdout, stderr, timeout, compile=False, env=None):
        def limits():
            resource.setrlimit(resource.RLIMIT_CPU,(120,120))
            if compile: resource.setrlimit(resource.RLIMIT_AS,(2*1024**3,2*1024**3))
        start=time.monotonic()
        with stdout.open("wb") as so,stderr.open("wb") as se:
            try:
                code=subprocess.run(argv,cwd=cwd,stdout=so,stderr=se,timeout=timeout,preexec_fn=limits,env=env).returncode
                timed_out=False
            except subprocess.TimeoutExpired:
                code=124;timed_out=True
        entry={"argv":list(map(str,argv)),"exit":code,"timed_out":timed_out,"timeout_seconds":timeout,"seconds":time.monotonic()-start,"stdout":str(stdout.relative_to(out)),"stderr":str(stderr.relative_to(out))}
        manifest["commands"].append(entry);save(out/"manifest.json",manifest)
        if code: raise RuntimeError("command failed; retain logs: "+str(argv[0]))
    try:
        srcdir=out/"snapshots"/"baseline";srcdir.mkdir(parents=True)
        raw=git("archive",BASE,"src/search_fft.cpp","src/search_fft.hpp","include")
        with tarfile.open(fileobj=io.BytesIO(raw),mode="r:") as t:
            for x in t.getmembers():
                if not (x.isdir() or x.isfile()) or x.name.startswith("/") or ".." in Path(x.name).parts: raise ValueError("unsafe archive entry")
            t.extractall(srcdir)
        source=srcdir/"src/search_fft.cpp"
        if sha(source)!=SOURCE_SHA: raise ValueError("baseline borrowed FFT source hash mismatch")
        base_text=source.read_text()
        helper='''\n#if defined(DATAPUMP_SEARCH_FFT_X86)\n__attribute__((target("avx2"))) void borrowed_inverse_normalize_avx2(std::complex<double>* values,std::size_t n,std::stop_token stop){\n    const auto scale=_mm256_set1_pd(1.0/static_cast<double>(n));\n    for(std::size_t i=0;i<n;i+=2){if((i&4095U)==0)cancelled(stop);auto v=_mm256_loadu_pd(reinterpret_cast<const double*>(values+i));_mm256_storeu_pd(reinterpret_cast<double*>(values+i),_mm256_mul_pd(v,scale));}\n}\n#endif\nvoid normalize_borrowed_inverse(std::complex<double>* values,std::size_t n,bool vectorized,std::stop_token stop){\n#if defined(DATAPUMP_SEARCH_FFT_X86)\n    if(vectorized){borrowed_inverse_normalize_avx2(values,n,stop);return;}\n#else\n    (void)vectorized;\n#endif\n    for(std::size_t i=0;i<n;++i){if((i&4095U)==0)cancelled(stop);values[i]/=static_cast<double>(n);}\n}\n'''
        marker="}\nFloatPlan::FloatPlan"
        if base_text.count(marker)!=1: raise ValueError("normalizer insertion anchor mismatch")
        patched=base_text.replace(marker,"}\n"+helper+"FloatPlan::FloatPlan",1)
        old='''    if(inverse)for(std::size_t i=0;i<n;++i){\n        if((i&4095U)==0)cancelled(stop);\n        values[i]/=static_cast<double>(n);\n    }'''
        new='''    if(inverse) {
#if defined(DATAPUMP_SEARCH_FFT_X86)
        normalize_borrowed_inverse(values,n,vectorized,stop);
#else
        normalize_borrowed_inverse(values,n,false,stop);
#endif
    }'''
        if patched.count(old)!=1: raise ValueError("inverse normalization replacement mismatch")
        patched=patched.replace(old,new,1)
        cand=out/"snapshots"/"candidate";cand.mkdir(parents=True)
        for item in srcdir.iterdir():
            if item.is_dir():
                import shutil;shutil.copytree(item,cand/item.name)
            else: (cand/item.name).write_bytes(item.read_bytes())
        (cand/"src/search_fft.cpp").write_text(patched)
        (out/"normalization.patch").write_text("".join(difflib.unified_diff(base_text.splitlines(True),patched.splitlines(True),fromfile="baseline/src/search_fft.cpp",tofile="candidate/src/search_fft.cpp")))
        (srcdir/"bridge.cpp").write_text('''#define search_fft baseline_borrowed_fft\n#include "src/search_fft.cpp"\n#undef search_fft\nextern "C" void baseline_prepare(std::complex<double>* f,std::complex<double>* b,std::size_t n){datapump::modem::detail::baseline_borrowed_fft::prepare_double_tables({f,n-1},{b,n-1});}\nextern "C" void baseline_run(std::complex<double>* a,std::size_t n,bool inv,const std::complex<double>* f,const std::complex<double>* b,std::size_t max,std::stop_token s){const datapump::modem::detail::baseline_borrowed_fft::DoubleTableView v{max,{f,max-1},{b,max-1}};datapump::modem::detail::baseline_borrowed_fft::fft({a,n},inv,v,s);}\nextern "C" void baseline_normalize(std::complex<double>* a,std::size_t n,std::stop_token s){for(std::size_t i=0;i<n;++i){if((i&4095U)==0&&s.stop_requested())throw std::runtime_error("pattern search cancelled");a[i]/=static_cast<double>(n);}}\n''')
        (cand/"bridge.cpp").write_text('''#define search_fft candidate_borrowed_fft\n#include "src/search_fft.cpp"\n#undef search_fft\nextern "C" void candidate_prepare(std::complex<double>* f,std::complex<double>* b,std::size_t n){datapump::modem::detail::candidate_borrowed_fft::prepare_double_tables({f,n-1},{b,n-1});}\nextern "C" void candidate_run(std::complex<double>* a,std::size_t n,bool inv,const std::complex<double>* f,const std::complex<double>* b,std::size_t max,std::stop_token s){const datapump::modem::detail::candidate_borrowed_fft::DoubleTableView v{max,{f,max-1},{b,max-1}};datapump::modem::detail::candidate_borrowed_fft::fft({a,n},inv,v,s);}\nextern "C" void candidate_normalize(std::complex<double>* a,std::size_t n,bool vectorized,std::stop_token s){datapump::modem::detail::candidate_borrowed_fft::normalize_borrowed_inverse(a,n,vectorized,s);}\n''')
        manifest.update({"baseline_search_fft_sha256":sha(source),"candidate_search_fft_sha256":sha(cand/"src/search_fft.cpp"),"private_candidate_diff":"single guarded inverse-normalization helper plus call-site replacement","state":"compiling"});save(out/"manifest.json",manifest)
        for san in ("on","off"):
            d=out/("san-"+san);d.mkdir();flags=["-std=c++20","-O1","-g","-fno-fast-math","-ffp-contract=off","-fno-omit-frame-pointer"]+( ["-fsanitize=address,undefined"] if san=="on" else [] );objs=[]
            for label,snapshot in (("baseline",srcdir),("candidate",cand)):
                obj=d/(label+".o");objs.append(str(obj));command([a.compiler,*flags,"-I"+str(snapshot/"src"),"-I"+str(snapshot/"include"),"-c",str(snapshot/"bridge.cpp"),"-o",str(obj)],root,d/(label+"-compile.stdout"),d/(label+"-compile.stderr"),180,True)
            binary=d/"harness";command([a.compiler,*flags,str(cpp),*objs,"-o",str(binary)],root,d/"harness-compile.stdout",d/("harness-compile.stderr"),180,True)
            env=dict(os.environ,ASAN_OPTIONS="detect_leaks=0:halt_on_error=1",UBSAN_OPTIONS="halt_on_error=1:print_stacktrace=1")
            command([str(binary),"--iterations",str(a.iterations)],root,d/"results.jsonl",d/"run.stderr",15,False,env=env)
            rows=[json.loads(line) for line in (d/"results.jsonl").read_text().splitlines()]
            if len(rows)!=18 or rows[0].get("type")!="environment" or rows[-1]!={"type":"complete","blocks":16,"normalization_controls":6,"complete":True}:raise ValueError("invalid row inventory")
            if rows[0].get("normalization_controls")!=6 or rows[0].get("iterations")!=a.iterations or rows[0].get("avx2") is not True:raise ValueError("environment inventory mismatch")
            expected=[(n,r,o,v) for n in (256,512) for r in ("ordinary","tiny_normal") for o,v in enumerate(("baseline","candidate","candidate","baseline"))]
            for row,key in zip(rows[1:-1],expected):
                if (row.get("n"),row.get("recipe"),row.get("order"),row.get("variant"))!=key or row.get("output_exact") is not True:raise ValueError("ABBA row identity/output mismatch")
                if any(type(row.get(k)) is not int or row[k]<=0 for k in ("wall_ns","thread_cpu_ns")):raise ValueError("invalid timing")
            for i in range(1,17,4):
                if len({(x["hash_fnv64"],x["fe_flags"],x["mxcsr_flags"]) for x in rows[i:i+4]})!=1:raise ValueError("ABBA bit/flag mismatch")
        manifest["state"]="completed_diagnostic_not_qualification"
    except BaseException as e:
        manifest["state"]="failed_not_accepted";manifest["error"]=repr(e);raise
    finally:
        manifest["finished_utc"]=datetime.datetime.now(datetime.timezone.utc).isoformat()
        manifest["artifacts"]={str(p.relative_to(out)):sha(p) for p in out.rglob("*") if p.is_file() and p.name!="manifest.json"}
        save(out/"manifest.json",manifest)
    print(json.dumps({"state":manifest["state"],"qualification":False,"manifest_sha256":sha(out/"manifest.json")}))
if __name__=="__main__":main()
