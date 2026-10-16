#!/usr/bin/env python3
"""G-A3 stable nominal identities (TYPE_IR_CONTRACT §3.1) in real Moss programs.

Cases A-E of the L7 G-A3 decision, using Margo-built packages. Compiled-only
cases delete provider source before the consumer is checked or built, and every
negative must fail with a Moss diagnostic before rustc runs.
"""

import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[2]
MOSS = Path(sys.argv[1]).resolve() if len(sys.argv) > 1 else ROOT / "moss"
MARGO = ROOT / "margo"

# Module identity binding and impersonation checks are ABI-v10 contracts;
# production stays on ABI v9 until the joint cutover.
os.environ["MOSS_STAGED_ABI"] = "v10"


def run(*args, cwd=ROOT, env=None):
    return subprocess.run([str(arg) for arg in args], cwd=cwd, env=env,
                          text=True, capture_output=True, timeout=300)


def payload(result):
    try:
        return json.loads(result.stdout)
    except json.JSONDecodeError as exc:
        raise AssertionError(f"no structured result: {result.stdout}\n{result.stderr}") from exc


def module_env(*directories):
    env = os.environ.copy()
    env["MOSS_MODULE_PATH"] = os.pathsep.join(str(d) for d in directories)
    return env


def package(root, name, version, source_name, source):
    (root / "src").mkdir(parents=True)
    (root / "Moss.toml").write_text(
        f'[project]\nname = "{name}"\nversion = "{version}"\n\n[build]\nsource = "src"\n')
    (root / "src" / source_name).write_text(source)
    return root / "src" / source_name


def build(root, env=None):
    built = run(MARGO, "build", "--json", cwd=root, env=env)
    data = payload(built)
    assert built.returncode == 0 and data.get("ok"), (root, built.stdout[-1500:], built.stderr[-800:])
    return data


def publish(root, destination):
    """Copy each interface with its selected rlib, then remove the source."""
    destination.mkdir(parents=True)
    interfaces = list((root / "build/debug").glob("*.mossi"))
    assert interfaces, root
    for interface in interfaces:
        assert interface.read_bytes().startswith(b"moss-module-interface-v3\n"), interface
        shutil.copy2(interface, destination / interface.name)
        rlib = interface.with_name(f"lib{interface.stem}.rlib")
        shutil.copy2(rlib, destination / rlib.name)
    shutil.rmtree(root / "src")
    return destination


def execute(root, env, expected):
    data = build(root, env)
    executable = data["artifacts"]["artifacts"]["executable"]
    result = run(executable, cwd=root, env=env)
    assert result.returncode == 0 and result.stdout == expected, (root, result.stdout, result.stderr)


def rejected(source, codes, env=None):
    checked = run(MOSS, "check", source, "--json", env=env)
    data = payload(checked)
    assert checked.returncode != 0 and data.get("ok") is False, (source, data)
    error = data["error"]
    assert error["code"] in codes, (source, error)
    assert "rustc" not in json.dumps(error), error
    return error


def nominal(selector, source, env=None, kind=None, enclosing=None):
    args = [MOSS, "type", selector, "--source", source, "--json"]
    if kind:
        args += ["--kind", kind]
    if enclosing:
        args += ["--enclosing", enclosing]
    result = run(*args, env=env)
    data = payload(result)
    assert data.get("ok"), data
    facts = data["result"]["type_facts"].get("nominal")
    assert facts, data["result"]["type_facts"]
    return facts


def template_id(package_name, version, module, name):
    return f"nominal-template-v1:module-v1:{package_name}@{version}::{module}:type:{name}"


def case_a(scratch):
    """Same layout, different declarations."""
    source = scratch / "same_layout.moss"
    source.write_text('''type Box:
  value

type Cell:
  value

fn main():
  var b = Box(value: 1)
  c = Cell(value: 1)
  b = c
''')
    rejected(source, {"TYPE_MISMATCH"})
    distinct = scratch / "same_layout_ok.moss"
    distinct.write_text('''type Box:
  value

type Cell:
  value

fn main():
  b = Box(value: 1)
  c = Cell(value: 1)
  echo b.value
  echo c.value
''')
    box = nominal("b", distinct, kind="binding", enclosing="main")
    cell = nominal("c", distinct, kind="binding", enclosing="main")
    assert box["declaration_id"] != cell["declaration_id"], (box, cell)
    assert box["instance_key"] != cell["instance_key"], (box, cell)


TOKEN = '''module {module}
export type Token:
  id: Int

export fn make() -> Token:
  return Token(id: {id})

export fn read(token: Token) -> Int:
  return token.id
'''


def case_b(scratch):
    """Two packages export the same declaration name and layout."""
    alpha = scratch / "alpha"
    beta = scratch / "beta"
    package(alpha, "alpha", "1.0.0", "alpha.moss", TOKEN.format(module="Alpha", id=1))
    package(beta, "beta", "2.0.0", "beta.moss", TOKEN.format(module="Beta", id=2))
    build(alpha)
    build(beta)
    env = module_env(publish(alpha, scratch / "alpha-artifacts"),
                     publish(beta, scratch / "beta-artifacts"))
    ok = scratch / "token-ok"
    ok_source = package(ok, "tokenok", "0.1.0", "main.moss", '''module Consumer
import Alpha
import Beta
fn main():
  a = Alpha.make()
  b = Beta.make()
  echo Alpha.read(a)
  echo Beta.read(b)
''')
    execute(ok, env, "1\n2\n")
    a = nominal("a", ok_source, env, "binding", "main")
    b = nominal("b", ok_source, env, "binding", "main")
    assert a["declaration_id"] == template_id("alpha", "1.0.0", "Alpha", "Token"), a
    assert b["declaration_id"] == template_id("beta", "2.0.0", "Beta", "Token"), b
    bad = scratch / "token-bad"
    bad_source = package(bad, "tokenbad", "0.1.0", "main.moss", '''module Consumer
import Alpha
import Beta
fn main():
  echo Alpha.read(Beta.make())
''')
    rejected(bad_source, {"TYPE_MISMATCH"}, env)


PROVIDER = '''module Provider
export type Box:
  value

export fn unbox(item: Box):
  return item.value

export fn answer() -> Int:
  return {answer}
'''


def case_c_e(scratch):
    """One provider, two separately compiled consumers; provider source absent."""
    provider = scratch / "provider"
    provider_source = package(provider, "g2provider", "0.1.0", "provider.moss",
                              PROVIDER.format(answer=42))
    expected = template_id("g2provider", "0.1.0", "Provider", "Box")
    # The provider's own source-backed identity, before its source is removed.
    assert nominal("entity-v1:type:Provider__Box", provider_source)["declaration_id"] == expected
    build(provider)
    provider_artifacts = publish(provider, scratch / "provider-artifacts")
    assert not provider_source.exists()

    consumer_a = scratch / "consumera"
    package(consumer_a, "consumera", "0.1.0", "consumera.moss", '''module ConsumerA
import Provider
export fn make():
  return Provider.Box(value: 7)
''')
    build(consumer_a, module_env(provider_artifacts))
    consumer_a_artifacts = publish(consumer_a, scratch / "consumera-artifacts")

    consumer_b = scratch / "consumerb"
    b_source = package(consumer_b, "consumerb", "0.1.0", "main.moss", '''module ConsumerB
import Provider
import ConsumerA
fn main():
  item = ConsumerA.make()
  echo Provider.unbox(item)
''')
    env = module_env(provider_artifacts, consumer_a_artifacts)
    # Case C: a value produced by compiled Consumer A crosses into Consumer B
    # through the provider-owned type, and both crates link one provider rlib.
    execute(consumer_b, env, "7\n")
    # Case E: with provider source absent, B binds the exact provider identity.
    facts = nominal("item", b_source, env, "binding", "main")
    assert facts["declaration_id"] == expected, facts
    assert facts["module_identity"] == "module-v1:g2provider@0.1.0::Provider", facts
    assert facts["instance_key"].startswith("nominal-instance-v1:sha256:"), facts
    return provider_artifacts, consumer_a_artifacts, b_source


def case_d(scratch, provider_artifacts, consumer_a_artifacts, b_source):
    """Incompatible providers claiming one module identity are rejected."""
    impostor = scratch / "impostor"
    package(impostor, "g2provider", "0.1.0", "provider.moss", PROVIDER.format(answer=43))
    build(impostor)
    impostor_artifacts = publish(impostor, scratch / "impostor-artifacts")
    probe = scratch / "probe"
    probe_source = package(probe, "probe", "0.1.0", "main.moss", '''module Probe
import Provider
fn main():
  echo Provider.answer()
''')
    # Two differently sealed providers for one identity in one graph.
    rejected(probe_source, {"SOURCE_FREE_MODULE_ID_CONFLICT"},
             module_env(provider_artifacts, impostor_artifacts))
    # A byte-identical copy of the same bound provider is the same provider.
    duplicate = scratch / "duplicate-artifacts"
    shutil.copytree(provider_artifacts, duplicate)
    execute(probe, module_env(provider_artifacts, duplicate), "42\n")
    # Consumer A was built against the original provider; substituting the
    # impostor under the same identity must not link.
    rejected(b_source, {"SOURCE_FREE_MODULE_ID_CONFLICT"},
             module_env(impostor_artifacts, consumer_a_artifacts))


def case_f(scratch):
    """Identity comes from the declaration's module, not its spelling."""
    project = scratch / "nmproj"
    source = package(project, "nmproj", "4.5.6", "main.moss", '''type Foo__Bar:
  value: Int

type Plain:
  value: Int

fn main():
  b = Foo__Bar(value: 1)
  p = Plain(value: 2)
  echo b.value
  echo p.value
''')
    mangled = nominal("b", source, kind="binding", enclosing="main")
    plain = nominal("p", source, kind="binding", enclosing="main")
    assert mangled["declaration_id"] == template_id("nmproj", "4.5.6", "nmproj", "Foo__Bar"), mangled
    assert plain["declaration_id"] == template_id("nmproj", "4.5.6", "nmproj", "Plain"), plain
    standalone = scratch / "standalone.moss"
    standalone.write_text('''type Foo__Bar:
  value: Int

fn main():
  b = Foo__Bar(value: 1)
  echo b.value
''')
    other = nominal("b", standalone, kind="binding", enclosing="main")
    assert other["declaration_id"] != mangled["declaration_id"], (other, mangled)
    assert other["instance_key"] != mangled["instance_key"], (other, mangled)
    # One spelling names one declaration across records, enums, domains, traits.
    for name, text in (("type_domain", '''type X:
  value: Int

domain X:
  count: Int = 0

  fn Get() -> Int:
    reply count

fn main():
  echo 1
'''), ("trait_type", '''trait Circle:
  fn area() -> Int

type Circle:
  radius: Int

  fn area():
    return radius * radius

fn main():
  echo Circle(radius: 3).area()
''')):
        clash = scratch / f"{name}.moss"
        clash.write_text(text)
        error = rejected(clash, {"DUPLICATE_SYMBOL"})
        assert error["line"] > 0, error
    # A legacy standard-error declaration is the compiler's own std type.
    legacy = scratch / "legacy_std.moss"
    legacy.write_text('''enum ParseError:
  Invalid
  Overflow

fn main():
  error = ParseError.Invalid
  echo 1
''')
    std_error = nominal("error", legacy, kind="binding", enclosing="main")
    assert std_error["declaration_id"] == \
        "nominal-enum-v1:module-v1:moss-std@0.1.0::std:enum:ParseError", std_error
    # A user type cannot redeclare a standard error; the clash names it.
    user = scratch / "user_parse_error.moss"
    user.write_text('''type ParseError:
  value: Int

fn main():
  echo 1
''')
    error = rejected(user, {"DUPLICATE_SYMBOL"})
    assert error["line"] == 1 and "std.ParseError" in error["message"], error


def main():
    (ROOT / "tmp").mkdir(exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="phase25-nominal-identity-", dir=ROOT / "tmp") as directory:
        scratch = Path(directory)
        for case in ("a", "b", "c", "d", "f"):
            (scratch / case).mkdir()
        case_a(scratch / "a")
        print("G-A3 case A (same layout, different declarations): PASS")
        case_b(scratch / "b")
        print("G-A3 case B (same name in two packages): PASS")
        artifacts = case_c_e(scratch / "c")
        print("G-A3 case C (one provider, two compiled consumers): PASS")
        print("G-A3 case E (provider source removed): PASS")
        case_d(scratch / "d", *artifacts)
        print("G-A3 case D (provider impersonation): PASS")
        case_f(scratch / "f")
        print("G-A3 case F (declaration-module identity, one name per declaration): PASS")
    print("Phase 25 G-A3 nominal identity: PASS")
    return 0


if __name__ == "__main__":
    sys.exit(main())
