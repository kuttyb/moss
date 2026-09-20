#!/usr/bin/env python3
"""Phase 10.6B.1: the checked routing universe cannot escape into values."""

import json
import subprocess
import sys
import tempfile
from pathlib import Path


compiler = Path(sys.argv[1]).resolve()
repository = Path(__file__).resolve().parents[2]


def invoke(*arguments, cwd=None, success=True):
    result = subprocess.run([str(compiler), *map(str, arguments)], cwd=cwd,
                            capture_output=True, text=True)
    assert (result.returncode == 0) == success, result.stdout + result.stderr
    return result


worker = "domain Worker:\n  fn Run() -> Int:\n    reply 7\n\n"
cases = {
    "handler": ("domain Relay:\n  fn Run(worker: Worker):\n    message worker.Run()\n", "handler payloads"),
    "function": ("fn invoke(worker: Worker):\n  message worker.Run()\n", "ordinary function parameters"),
    "method": ("type Holder:\n  fn invoke(worker: Worker):\n    message worker.Run()\n", "ordinary method parameters"),
    "state": ("domain Bad:\n  worker: Worker\n", "ordinary state"),
    "aggregate": ("type Bad:\n  worker: Worker\n", "aggregates"),
    "nested_type": ("fn bad(workers: Vector[Worker]):\n  echo 1\n", "ordinary function parameters"),
    "payload": ("domain Relay:\n  fn Run(value):\n    echo 1\n\nfn main():\n  worker = Worker()\n  relay = Relay()\n  message relay.Run(worker)\n", "domain handle"),
    "reply": ("domain Relay:\n  domainroutes(worker: Worker)\n  fn Get() -> Worker:\n    reply worker\n", "returned through reply"),
    "reply_inferred": ("domain Relay:\n  domainroutes(worker: Worker)\n  fn Get():\n    reply worker\n\nfn main():\n  worker = Worker()\n  relay = Relay(worker: worker)\n  result = message relay.Get()\n", "domain handle"),
    "alias": ("fn main():\n  worker = Worker()\n  other = worker\n", "domain handle"),
    "reassign": ("fn main():\n  worker = Worker()\n  other = Worker()\n  worker = other\n", "immutable"),
    "reassign_result": ("fn main():\n  worker = Worker()\n  worker = message worker.Run()\n", "immutable"),
    "loop_shadow": ("fn main():\n  worker = Worker()\n  for worker in [1]:\n    echo worker\n", "immutable"),
    "array": ("fn main():\n  worker = Worker()\n  workers = [worker]\n", "domain handle"),
    "mixed_array": ("fn main():\n  worker = Worker()\n  workers = [1, worker]\n", "domain handle"),
    "vector": ("fn main():\n  worker = Worker()\n  workers = [1]\n  workers.push(worker)\n", "domain handle"),
    "optional": ("fn main():\n  worker = Worker()\n  wrapped = Some(worker)\n", "domain handle"),
    "generic": ("fn identity(value):\n  return value\n\nfn main():\n  worker = Worker()\n  result = identity(worker)\n", "domain handle"),
    "state_value": ("domain Bad:\n  value: Int\n\nfn main():\n  worker = Worker()\n  bad = Bad(value: worker)\n", "state data"),
    "read_state": ("fn main():\n  worker = Worker()\n  echo worker.value\n", "domain handle"),
    "write_state": ("fn main():\n  worker = Worker()\n  worker.value = 1\n", "domain handle"),
    "route_shadow": ("domain Relay:\n  domainroutes(worker: Worker)\n  fn Run(worker: Int):\n    echo worker\n", "shadows immutable domain route"),
    "helper_construct": ("fn bad():\n  worker = Worker()\n", "main"),
}

with tempfile.TemporaryDirectory(prefix="moss-handle-closure-") as temporary:
    root = Path(temporary)
    for name, (body, diagnostic) in cases.items():
        source = root / (name + ".moss")
        source.write_text(worker + body)
        result = invoke("--check", source, success=False)
        assert diagnostic in result.stderr, (name, result.stderr)

    # Route identity is concrete even for siblings of one type, including two
    # different inferred layouts. A repeated route to one instance is legal.
    source = root / "routes.moss"
    source.write_text(
        "domain Box:\n  value\n  fn Set(x):\n    value = x\n"
        "  fn Run() -> Int:\n    reply 7\n\n"
        "domain Router:\n  domainroutes(left: Box, again: Box, right: Box)\n"
        "  fn Run() -> Int:\n    a = message left.Run()\n"
        "    b = message again.Run()\n    c = message right.Run()\n    reply a + b + c\n\n"
        "fn main():\n  left = Box()\n  right = Box()\n"
        "  router = Router(left: left, again: left, right: right)\n"
        "  message left.Set(10)\n  message right.Set(3.5)\n"
        "  result = message router.Run()\n  echo result\n"
    )
    first = invoke("inspect", "main", "--source", source, "--json").stdout
    assert first == invoke("inspect", "main", "--source", source, "--json").stdout
    graph = json.loads(first)["result"]["concrete_domain_graph"]
    assert graph["closed"] is True
    instances = {item["binding"]: item for item in graph["instances"]}
    assert instances["left"]["source_domain_id"] == instances["right"]["source_domain_id"]
    assert instances["left"]["specialization_id"] == "domain-specialization:Box:left"
    assert instances["right"]["specialization_id"] == "domain-specialization:Box:right"
    assert len({item["domain_rank"] for item in instances.values()}) == 3
    for edge in graph["edges"]:
        src = next(i for i in instances.values() if i["identity"] == edge["source_instance"])
        dst = next(i for i in instances.values() if i["identity"] == edge["target_instance"])
        assert src["domain_rank"] < dst["domain_rank"]
        assert Path(edge["source_file"]) == source
    for binding, expected in (("left", "int"), ("right", "float")):
        fact = json.loads(invoke("inspect", instances[binding]["specialization_id"],
                                 "--source", source, "--json").stdout)
        assert {"name": "value", "type": expected} in fact["result"]["target"]["specialization_fields"]
    for profile in ("-O0", "-Oshared-memory"):
        rust = root / ("routes" + profile.replace("-", "_") + ".rs")
        invoke(profile, source, "-o", rust)
        executable = rust.with_suffix("")
        subprocess.run(["rustc", "-D", "warnings", str(rust), "-o", str(executable)], check=True)
        assert subprocess.check_output([str(executable)], text=True) == "21\n"

    # Primary error is the first route witness, not main's declaration.
    source = root / "cycle.moss"
    source.write_text("domain A:\n  domainroutes(next: B)\n\ndomain B:\n"
                      "  domainroutes(next: A)\n\nfn main():\n"
                      "  a = A(next: b)\n  b = B(next: a)\n")
    result = json.loads(invoke("check", source, "--json", success=False).stdout)
    diagnostic = result["error"]
    assert diagnostic["line"] == 8, diagnostic
    assert str(source) + ":8" in diagnostic["message"]
    assert str(source) + ":9" in diagnostic["message"]
    assert "main::a --next--> main::b" in diagnostic["message"]

    project = root / "modules"
    (project / "src").mkdir(parents=True)
    (project / "moss.toml").write_text('[project]\nname = "routes"\nversion = "0.1.0"\n[build]\nsource = "src"\n')
    provider = project / "src" / "provider.moss"
    provider.write_text("module provider\n\nexport domain Worker:\n"
                        "  value: Int\n  fn Run(delta: Int) -> Int:\n    reply value + delta\n\n"
                        "export domain Relay:\n  domainroutes(worker: Worker)\n"
                        "  fn Run(delta: Int) -> Int:\n    reply message worker.Run(delta)\n")
    application = project / "src" / "main.moss"
    application.write_text("module app\nimport provider\n\nfn main():\n"
                           "  worker = provider.Worker(value: 9)\n"
                           "  relay = provider.Relay(worker: worker)\n"
                           "  result = message relay.Run(0)\n  echo result\n")
    graph = json.loads(invoke("inspect", "main", "--source", application,
                              "--json", cwd=project).stdout)["result"]["concrete_domain_graph"]
    assert len(graph["edges"]) == 1
    assert all("provider" in i["source_domain_id"] for i in graph["instances"])
    for instance in graph["instances"]:
        record = json.loads(invoke("inspect", instance["specialization_id"],
                                    "--source", application, "--json", cwd=project).stdout)
        assert record["result"]["target"]["specialization_identity"] == instance["specialization_id"]
    built = json.loads(invoke("build", "--json", cwd=project).stdout)
    executable = built["result"]["artifacts"]["executable"]
    assert subprocess.check_output([executable], text=True) == "9\n"
    interface = project / "build" / "debug" / "provider.mossi"
    assert "route worker:" in interface.read_text()
    assert "domain_rank" not in interface.read_text()
    provider.rename(provider.with_suffix(".hidden"))
    built = json.loads(invoke("build", "--json", cwd=project).stdout)
    assert subprocess.check_output([built["result"]["artifacts"]["executable"]], text=True) == "9\n"

    # Imported implicit domains must retain the same exact specialization
    # records through module projection and native construction.
    project = root / "mixed_modules"
    (project / "src").mkdir(parents=True)
    (project / "moss.toml").write_text('[project]\nname = "mixed"\nversion = "0.1.0"\n')
    (project / "src" / "provider.moss").write_text(
        "module provider\nexport domain Box:\n  value\n"
        "  fn Set(x):\n    value = x\n  fn Run() -> Int:\n    reply 7\n"
    )
    application = project / "src" / "main.moss"
    application.write_text(
        "module app\nimport provider\n\ndomain Relay:\n"
        "  domainroutes(left: provider.Box, right: provider.Box)\n"
        "  fn Run() -> Int:\n    a = message left.Run()\n"
        "    b = message right.Run()\n    reply a + b\n\nfn main():\n"
        "  left = provider.Box()\n  right = provider.Box()\n"
        "  relay = Relay(left: left, right: right)\n"
        "  message left.Set(10)\n  message right.Set(3.5)\n"
        "  result = message relay.Run()\n  echo result\n"
    )
    graph = json.loads(invoke("inspect", "main", "--source", application,
                              "--json", cwd=project).stdout)["result"]["concrete_domain_graph"]
    boxes = [item for item in graph["instances"] if item["domain"] == "provider__Box"]
    assert len(boxes) == 2 and boxes[0]["specialization_id"] != boxes[1]["specialization_id"]
    for profile in ([], ["--release"]):
        built = json.loads(invoke("build", *profile, "--json", cwd=project).stdout)
        assert subprocess.check_output([built["result"]["artifacts"]["executable"]], text=True) == "14\n"

print("Phase 10.6B.1 closed-handle/topology checks passed")
