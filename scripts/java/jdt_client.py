#!/usr/bin/env python3
"""
Persistent JDT Incremental Build Client for Moss Java Compiler (Phase 25.3 J1)
"""

import os
import sys
import time
import socket
import shutil
import subprocess
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[2]
COMPILER_JAVA = REPO_ROOT / "compiler-java"
SRC_DIR = COMPILER_JAVA / "src" / "main" / "java"
LIB_DIR = COMPILER_JAVA / "lib"
BIN_DIR = COMPILER_JAVA / "bin"
WORKSPACE_DIR = COMPILER_JAVA / "jdt" / "workspace"
CONFIG_DIR = COMPILER_JAVA / "jdt" / "configuration"
LOG_FILE = COMPILER_JAVA / "jdt" / "daemon.log"

JDT_PORT = 45290
BUILD_SCRIPT = REPO_ROOT / "scripts" / "java" / "build.sh"

def find_eclipse():
    configured = os.environ.get("MOSS_JDT_EXECUTABLE")
    candidates = [configured, shutil.which("eclipse")]
    for candidate in candidates:
        if candidate and os.path.isfile(candidate) and os.access(candidate, os.X_OK):
            return candidate
    return None

def is_daemon_running(port=JDT_PORT):
    try:
        s = socket.socket()
        s.settimeout(0.5)
        s.connect(("127.0.0.1", port))
        s.close()
        return True
    except Exception:
        return False

def start_daemon(port=JDT_PORT):
    eclipse = find_eclipse()
    if eclipse is None:
        raise RuntimeError("Eclipse JDT executable not found")
    WORKSPACE_DIR.mkdir(parents=True, exist_ok=True)
    BIN_DIR.mkdir(parents=True, exist_ok=True)
    cmd = [
        eclipse,
        "-nosplash",
        "-data", str(WORKSPACE_DIR),
        "-configuration", str(CONFIG_DIR),
        "-application", "org.mosslang.jdt.builder.headlessBuilder",
        "-port", str(port)
    ]
    log_fp = open(LOG_FILE, "a", encoding="utf-8")
    log_fp.write(f"\n--- Starting JDT Daemon at {time.ctime()} ---\n")
    log_fp.flush()
    try:
        proc = subprocess.Popen(cmd, stdout=log_fp, stderr=subprocess.STDOUT)
    except OSError as error:
        sys.stderr.write(f"Could not launch Eclipse JDT: {error}\n")
        return False
    
    ready = False
    for _ in range(50):
        time.sleep(0.2)
        if is_daemon_running(port):
            ready = True
            break
    if not ready:
        stop_process(proc)
        sys.stderr.write(f"JDT daemon did not start on port {port}. Check {LOG_FILE}\n")
        return False
    
    # Initialize workspace project
    try:
        resp, _ = send_command(f"INIT {SRC_DIR} {LIB_DIR} {BIN_DIR}", port)
    except OSError as error:
        stop_process(proc)
        sys.stderr.write(f"JDT daemon initialization failed: {error}\n")
        return False
    if "OK" not in resp:
        stop_process(proc)
        sys.stderr.write(f"JDT daemon initialization failed: {resp}\n")
        return False
    return True

def stop_process(proc):
    if proc.poll() is not None:
        return
    proc.terminate()
    try:
        proc.wait(timeout=3)
    except subprocess.TimeoutExpired:
        proc.kill()
        proc.wait(timeout=3)

def send_command(cmd_str, port=JDT_PORT):
    s = socket.socket()
    s.settimeout(30.0)
    s.connect(("127.0.0.1", port))
    f = s.makefile("rw", encoding="utf-8")
    t0 = time.perf_counter()
    f.write(cmd_str.strip() + "\n")
    f.flush()
    resp = f.readline().strip()
    t1 = time.perf_counter()
    s.close()
    elapsed_ms = (t1 - t0) * 1000.0
    return resp, elapsed_ms

def main(argv=None):
    argv = list(sys.argv[1:] if argv is None else argv)
    action = "BUILD_INCREMENTAL"
    subsystem = "all"
    for arg in argv:
        if arg == "--clean":
            action = "BUILD_CLEAN"
        elif arg == "--full":
            action = "BUILD_FULL"
        elif arg.startswith("--subsystem="):
            subsystem = arg.split("=", 1)[1]
        elif arg == "--status":
            print("JDT daemon is running." if is_daemon_running() else "JDT daemon is not running.")
            return 0
        elif arg == "--stop":
            if is_daemon_running():
                send_command("STOP")
                print("JDT daemon stopped.")
            else:
                print("JDT daemon is not running.")
            return 0
        elif arg == "--start":
            if is_daemon_running():
                print("JDT daemon is already running.")
                return 0
            if find_eclipse() is None:
                print("Eclipse JDT is unavailable; daemon was not started.")
                return 0
            if start_daemon():
                print(f"JDT daemon started on port {JDT_PORT}.")
                return 0
            return 1

    if not is_daemon_running():
        if find_eclipse() is None:
            print("Eclipse JDT is unavailable; using the standalone javac build.")
            fallback_args = ["--clean"] if action == "BUILD_CLEAN" else []
            return subprocess.call([str(BUILD_SCRIPT), *fallback_args])
        if not start_daemon():
            print("JDT daemon startup failed; using the standalone javac build.")
            fallback_args = ["--clean"] if action == "BUILD_CLEAN" else []
            return subprocess.call([str(BUILD_SCRIPT), *fallback_args])

    # Always refresh to ensure file modifications on disk are observed
    _, refresh_ms = send_command("REFRESH")
    resp, client_elapsed_ms = send_command(action)
    total_ms = refresh_ms + client_elapsed_ms
    if (COMPILER_JAVA / "src" / "main" / "resources").is_dir():
        import shutil
        for item in (COMPILER_JAVA / "src" / "main" / "resources").iterdir():
            dest = BIN_DIR / item.name
            if item.is_file():
                shutil.copy2(item, dest)
    log_dir = REPO_ROOT / "tmp"
    log_dir.mkdir(parents=True, exist_ok=True)
    import json, datetime
    record = {
        "timestamp": datetime.datetime.now(datetime.timezone.utc).isoformat(),
        "builder": "eclipse-jdt",
        "mode": action.lower(),
        "subsystem": subsystem,
        "refresh_ms": round(refresh_ms, 2),
        "compile_ms": round(client_elapsed_ms, 2),
        "elapsed_ms": round(total_ms, 2),
        "response": resp
    }
    with open(log_dir / "build_times.jsonl", "a", encoding="utf-8") as fp:
        fp.write(json.dumps(record) + "\n")
    print(f"JDT {action} (subsystem={subsystem}): {resp} (compile={client_elapsed_ms:.1f}ms, total={total_ms:.1f}ms)")
    
    if "FAILED" in resp or "ERROR" in resp:
        sys.exit(1)

if __name__ == "__main__":
    sys.exit(main())
