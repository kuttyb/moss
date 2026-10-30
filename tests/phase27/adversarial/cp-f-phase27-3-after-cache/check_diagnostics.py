import concurrent.futures, json, pathlib, subprocess, sys

candidate, baseline, output = map(pathlib.Path, sys.argv[1:4])
files = subprocess.check_output(["git", "ls-files", "-z", "--", "*.moss"], cwd=candidate).decode().split("\0")
files = [f for f in files if f and "tests/phase27/adversarial/" not in f and (candidate/f).is_file() and (baseline/f).is_file()]

def run(root, rel):
    try:
        p = subprocess.run([str(root / "moss"), "check", rel, "--json"], cwd=root, text=True, capture_output=True, timeout=120)
    except subprocess.TimeoutExpired:
        return {"timeout":120}
    try:
        obj = json.loads(p.stdout)
        result = obj.get("result", {})
        diagnostics = result.get("diagnostics", [])
        for item in diagnostics:
            item.pop("source_file", None)
        return {"rc":p.returncode,"ok":obj.get("ok"),"diagnostics":diagnostics}
    except Exception:
        return {"rc":p.returncode,"stdout":p.stdout[-1000:],"stderr":p.stderr[-1000:]}

def one(rel):
    b, c = run(baseline, rel), run(candidate, rel)
    return {"file":rel,"baseline":b,"candidate":c,"same":b==c}

with output.open("w") as f:
    with concurrent.futures.ThreadPoolExecutor(max_workers=8) as pool:
        for row in pool.map(one, files):
            f.write(json.dumps(row, sort_keys=True)+"\n")
            f.flush()
print(json.dumps({"files":len(files),"different":sum(not json.loads(x)["same"] for x in output.read_text().splitlines()),"output":str(output)}))
