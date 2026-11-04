import sys,time,json,subprocess,pathlib,resource,datetime
out=pathlib.Path(sys.argv[1]);cmd=sys.argv[2:]
start=time.perf_counter();when=datetime.datetime.now(datetime.timezone.utc).isoformat()
r=subprocess.run(cmd);elapsed=time.perf_counter()-start
usage=resource.getrusage(resource.RUSAGE_CHILDREN)
out.write_text(json.dumps({'command':cmd,'started_utc':when,'wall_seconds':elapsed,'returncode':r.returncode,'child_user_seconds':usage.ru_utime,'child_system_seconds':usage.ru_stime,'max_rss_kib':usage.ru_maxrss},indent=2)+'\n')
sys.exit(r.returncode)
