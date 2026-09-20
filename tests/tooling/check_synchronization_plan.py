#!/usr/bin/env python3
"""Phase 10.6C: inspect the authoritative plan, including compiled providers."""
import json
import subprocess
import sys
import tempfile
from pathlib import Path

compiler = Path(sys.argv[1]).resolve()
repository = Path(__file__).resolve().parents[2]


def invoke(*args, cwd=None, success=True):
    result = subprocess.run([str(compiler), *map(str, args)], cwd=cwd,
                            capture_output=True, text=True)
    assert (result.returncode == 0) == success, result.stdout + result.stderr
    return result


def inspect(source, cwd=None):
    args = ("inspect", "main", "--source", source, "--json")
    raw = invoke(*args, cwd=cwd).stdout
    assert raw == invoke(*args, cwd=cwd).stdout
    result = json.loads(raw)["result"]
    plan = result["synchronization_plan"]
    assert plan["physical_lowering"] == "unchanged"
    assert result["synchronization_dump"].startswith("SynchronizationPlan " + plan["graph_identity"])
    domains = plan["domains"]
    assert len({d["domain_rank"] for d in domains}) == len(domains)
    for d in domains:
        mutable = set(d["protected_mutable_leaves"])
        assert set(d["leaf_to_class"]) == mutable
        classes = {c["class_id"]: c for c in d["sync_classes"]}
        assert [c["class_rank"] for c in classes.values()] == list(range(len(classes)))
        members = [leaf for c in classes.values() for leaf in c["member_leaves"]]
        assert len(members) == len(set(members)) and set(members) == mutable
        assert len({tuple(c["mode_signature"].items()) for c in classes.values()}) == len(classes)
        for h in d["handlers"]:
            x = set(h["write_set"]) | set(h["consume_set"])
            protected = set(h["read_set"]) & mutable
            locks = x | protected
            assert set(h["exclusive_set"]) == x
            assert set(h["protected_read_set"]) == protected
            assert set(h["lock_set"]) == locks <= mutable
            assert set(h["class_set"]) == {d["leaf_to_class"][leaf] for leaf in locks}
            assert set(h["class_modes"]) == set(h["class_set"])
            assert h["self_conflict"] == bool(x)
            for c, mode in h["class_modes"].items():
                assert mode == classes[c]["mode_signature"][h["handler_identity"]]
    return result


def handlers(domain):
    return {h["name"]: h for h in domain["handlers"]}


account = inspect(repository / "tests/phase106c_account.moss")
d = account["synchronization_plan"]["domains"][0]
h = handlers(d)
assert d["protected_mutable_leaves"] == ["balance", "display_name", "risk_limit", "stats"]
assert len(d["sync_classes"]) == 4
assert h["record_fill"]["read_set"] == ["balance", "config_value", "risk_limit", "stats"]
assert h["record_fill"]["write_set"] == ["balance", "stats"]
assert h["record_fill"]["normalized_effects"]["balance"] == "WRITE"
assert h["record_fill"]["lock_set"] == ["balance", "risk_limit", "stats"]
assert set(h["record_fill"]["class_set"]).isdisjoint(h["rename"]["class_set"])
assert h["read_config"]["read_set"] == ["config_value"]
for key in ("protected_read_set", "lock_set", "class_set"):
    assert h["read_config"][key] == []
expected = {"balance": {"record_fill": "EXCLUSIVE"},
            "stats": {"record_fill": "EXCLUSIVE", "read_stats": "SHARED"},
            "risk_limit": {"set_risk_limit": "EXCLUSIVE", "record_fill": "SHARED"},
            "display_name": {"rename": "EXCLUSIVE"}}
for c in d["sync_classes"]:
    signature = {name: c["mode_signature"][handler["handler_identity"]]
                 for name, handler in h.items()
                 if c["mode_signature"][handler["handler_identity"]] != "NONE"}
    assert signature == expected[c["member_leaves"][0]]
assert "Conflict read_stats / record_fill" in account["synchronization_dump"]

with tempfile.TemporaryDirectory(prefix="moss-sync-plan-") as temporary:
    root = Path(temporary)
    source = root / "primitive_effect.moss"
    source.write_text(
        "fn change(value: Int):\n  value = value + 1\n\n"
        "domain Data:\n  counter: Int\n  fn Update():\n    change(counter)\n\n"
        "fn main():\n  data = Data(counter: 0)\n  message data.Update()\n")
    primitive = inspect(source)["synchronization_plan"]["domains"][0]
    assert handlers(primitive)["Update"]["write_set"] == ["counter"]
    # Planning preserves inferred WRITE, without deciding write-through
    # semantics or changing the production calling convention.
    effect = json.loads(invoke("effects", "change", "--source", source, "--json").stdout)
    assert "WRITE" in json.dumps(effect)

    source = root / "sharing.moss"
    base = ("domain Pair:\n  x: Int\n  y: Int\n"
            "  fn Update():\n    x = x + 1\n    y = y + 1\n"
            "  fn Read() -> Int:\n    reply x + y\n")
    main = "\nfn main():\n  pair = Pair()\n  message pair.Update()\n"
    source.write_text(base + main)
    shared = inspect(source)["synchronization_plan"]["domains"][0]
    assert len(shared["sync_classes"]) == 1
    assert shared["sync_classes"][0]["member_leaves"] == ["x", "y"]
    source.write_text(base + "  fn OnlyX() -> Int:\n    reply x\n" + main)
    split = inspect(source)["synchronization_plan"]["domains"][0]
    assert [c["member_leaves"] for c in split["sync_classes"]] == [["x"], ["y"]]

    source = root / "specialized.moss"
    source.write_text(
        "type Left:\n  a: Int\n  b: Int\n  fn touch():\n    a = a + 1\n  fn fresh() -> Left:\n    return Left(a: a, b: b)\n\n"
        "type Right:\n  c: Int\n  fn touch():\n    c = c + 1\n  fn fresh() -> Right:\n    return Right(c: c)\n\n"
        "fn change(value):\n  value.touch()\n\n"
        "domain Box:\n  value\n  fn Set(x):\n    value = x.fresh()\n"
        "  fn Update():\n    change(value)\n\n"
        "fn main():\n  left = Box()\n  right = Box()\n"
        "  message left.Set(Left(a: 0, b: 0))\n"
        "  message right.Set(Right(c: 0))\n"
        "  message left.Update()\n  message right.Update()\n")
    specialized = inspect(source)["synchronization_plan"]["domains"]
    assert specialized[0]["specialization_id"] != specialized[1]["specialization_id"]
    by_id = {d["concrete_instance_id"]: d for d in specialized}
    assert handlers(by_id["main::left"])["Update"]["write_set"] == ["value.a"]
    assert handlers(by_id["main::right"])["Update"]["write_set"] == ["value.c"]

    source = root / "aggregate.moss"
    source.write_text(
        "type Values:\n  mutable: Int\n  immutable: Int\n\n"
        "fn update(value: Values):\n  value.mutable = value.mutable + 1\n\n"
        "fn transform(values, operation):\n  return values |> map(operation)\n\n"
        "domain Data:\n  value: Values\n  values: Vector[Int]\n  offset: Int\n"
        "  fn Update(index: Int):\n    update(value)\n    values[index] = 3\n"
        "  fn Sum() -> Int:\n    mapped = values |> map(_ + offset)\n"
        "    total = mapped |> sum\n    reply total + value.immutable\n\n"
        "fn main():\n  data = Data(value: Values(mutable: 0, immutable: 7), values: [1, 2], offset: 3)\n"
        "  message data.Update(0)\n  result = message data.Sum()\n  echo result\n")
    aggregate = inspect(source)["synchronization_plan"]["domains"][0]
    assert aggregate["protected_mutable_leaves"] == ["value.mutable", "values"]
    assert handlers(aggregate)["Sum"]["read_set"] == ["offset", "value.immutable", "values"]
    assert handlers(aggregate)["Sum"]["lock_set"] == ["values"]

    source = root / "functional.moss"
    source.write_text(
        "type Scale:\n  factor: Int\n  ignored: Int\n"
        "  fn apply(value: Int) -> Int:\n    return value * factor\n\n"
        "fn identity(value):\n  return value\n\n"
        "fn transform(values, operation):\n  return values |> map(operation)\n\n"
        "domain Functional:\n  values: Vector[Int]\n  scale: Scale\n  left: Int\n  right: Int\n"
        "  fn Change(flag: Bool):\n    if flag:\n      left = 1\n    else:\n      right = 2\n"
        "    scale.factor = 3\n"
        "  fn Read() -> Int:\n    first = transform(values, identity)\n"
        "    second = first |> map(scale.apply)\n    total = second |> sum\n    reply total\n\n"
        "fn main():\n  f = Functional(values: [1], scale: Scale(factor: 2, ignored: 0), left: 0, right: 0)\n"
        "  message f.Change(true)\n  total = message f.Read()\n  echo total\n")
    functional = inspect(source)["synchronization_plan"]["domains"][0]
    assert handlers(functional)["Change"]["write_set"] == ["left", "right", "scale.factor"]
    assert handlers(functional)["Read"]["read_set"] == ["scale.factor", "values"]
    assert handlers(functional)["Read"]["lock_set"] == ["scale.factor"]

    topology = inspect(repository / "tests/phase106b_topology.moss")
    for domain in topology["synchronization_plan"]["domains"]:
        if not domain["state_leaves"]:
            assert domain["sync_classes"] == []
            assert all(h["lock_set"] == [] for h in domain["handlers"])

    project = root / "modules"
    (project / "src").mkdir(parents=True)
    (project / "moss.toml").write_text('[project]\nname = "sync"\nversion = "0.1.0"\n')
    helper = project / "src/helper.moss"
    # Concrete helper contracts are the subject here. Provider-native calls to
    # exported generics require a separate existing backend linkage fix.
    helper.write_text("module helper\nexport fn bump(value: Int) -> Int:\n  return value\n\nexport fn append(values: Vector[Int]) -> Int:\n  first = values[0]\n  values[0] = bump(first) + 1\n  return 0\n")
    provider = project / "src/provider.moss"
    provider.write_text(
        "module provider\nimport helper\nexport domain Data:\n  values: Vector[Int]\n  config: Int\n"
        "  fn Update():\n    ignored = helper.append(values)\n"
        "  fn Read() -> Int:\n    total = values |> sum\n    reply total + config\n")
    application = project / "src/main.moss"
    application.write_text(
        "module app\nimport provider\nimport helper\nfn main():\n"
        "  data = provider.Data(values: [1], config: 7)\n"
        "  message data.Update()\n  result = message data.Read()\n  echo result\n")
    before = inspect(application, project)["synchronization_plan"]["domains"][0]
    assert before["protected_mutable_leaves"] == ["values"]
    built = json.loads(invoke("build", "--json", cwd=project).stdout)
    assert subprocess.check_output([built["result"]["artifacts"]["executable"]], text=True) == "9\n"
    interface = project / "build/debug/provider.mossi"
    contents = interface.read_text()
    assert "handler_state_effects" in contents
    assert "class_rank" not in contents and "sync_class" not in contents
    # A source domain calling only a compiled helper must also retain effects.
    helper.rename(helper.with_suffix(".hidden"))
    mixed = inspect(application, project)["synchronization_plan"]["domains"][0]
    for key in ("protected_mutable_leaves", "sync_classes", "handlers"):
        assert before[key] == mixed[key], (key, before[key], mixed[key])
    helper_interface = project / "build/debug/helper.mossi"
    helper_contents = helper_interface.read_text()
    assert "parameter_leaf_effects" in helper_contents
    helper_interface.write_text("\n".join(line for line in helper_contents.splitlines()
                                           if "parameter_leaf_effects" not in line) + "\n")
    missing_helper = invoke("inspect", "main", "--source", application, "--json",
                            cwd=project, success=False)
    assert "rebuild" in missing_helper.stdout + missing_helper.stderr
    helper_interface.write_text(helper_contents)
    provider.rename(provider.with_suffix(".hidden"))
    after = inspect(application, project)["synchronization_plan"]["domains"][0]
    for key in ("protected_mutable_leaves", "sync_classes", "handlers", "leaf_to_class"):
        assert before[key] == after[key], (key, before[key], after[key])
    built = json.loads(invoke("build", "--json", cwd=project).stdout)
    assert subprocess.check_output([built["result"]["artifacts"]["executable"]], text=True) == "9\n"
    # Old/incomplete provider interfaces must not silently invent an empty plan.
    interface.write_text("\n".join(line for line in contents.splitlines()
                                    if "handler_state_effects" not in line) + "\n")
    failure = invoke("inspect", "main", "--source", application, "--json", cwd=project, success=False)
    assert "rebuild" in failure.stdout + failure.stderr

print("Phase 10.6C synchronization-plan checks passed")
