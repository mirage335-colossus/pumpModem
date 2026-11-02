import json, platform, torch, numpy, scipy, pathlib
x={"python":platform.python_version(),"machine":platform.machine(),"torch":torch.__version__,"numpy":numpy.__version__,"scipy":scipy.__version__,"cuda":torch.version.cuda,"gpu":torch.cuda.get_device_name(0),"gpu_total_bytes":torch.cuda.get_device_properties(0).total_memory,"scope":"scientific runtime identity; no provider account metadata"}
pathlib.Path('/workspace/minus60-evidence/environment.json').write_text(json.dumps(x,indent=2)+'\n')
