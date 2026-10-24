#!/usr/bin/env python3
"""
Persistent JDT Incremental Build Client for Moss Java Compiler (Phase 25.3 J1)
"""

import os
import sys
import time
import socket
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
    WORKSPACE_DIR.mkdir(parents=True, exist_ok=True)
    BIN_DIR.mkdir(parents=True, exist_ok=True)
    cmd = [
        "/opt/eclipse-jdt/eclipse",
        "-nosplash",
        "-data", str(WORKSPACE_DIR),
        "-configuration", str(CONFIG_DIR),
        "-application", "org.mosslang.jdt.builder.headlessBuilder",
        "-port", str(port)
    ]
    log_fp = open(LOG_FILE, "a", encoding="utf-8")
    log_fp.write(f"\n--- Starting JDT Daemon at {time.ctime()} ---\n")
    log_fp.flush()
    proc = subprocess.Popen(cmd, stdout=log_fp, stderr=subprocess.STDOUT)
    
    ready = False
    for _ in range(50):
        time.sleep(0.2)
        if is_daemon_running(port):
            ready = True
            break
    if not ready:
        sys.stderr.write(f"Error: JDT daemon failed to start on port {port}. Check {LOG_FILE}\n")
        sys.exit(1)
    
    # Initialize workspace project
    resp, _ = send_command(f"INIT {SRC_DIR} {LIB_DIR} {BIN_DIR}", port)
    if "OK" not in resp:
        sys.stderr.write(f"Error initializing JDT project: {resp}\n")
        sys.exit(1)

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

def main():
    action = "BUILD_INCREMENTAL"
    if "--clean" in sys.argv:
        action = "BUILD_CLEAN"
    elif "--full" in sys.argv:
        action = "BUILD_FULL"
    elif "--stop" in sys.argv:
        if is_daemon_running():
            send_command("STOP")
            print("JDT daemon stopped.")
        else:
            print("JDT daemon is not running.")
        return

    if not is_daemon_running():
        start_daemon()

    # Always refresh to ensure file modifications on disk are observed
    send_command("REFRESH")
    resp, client_elapsed_ms = send_command(action)
    print(f"JDT {action}: {resp} (client_latency={client_elapsed_ms:.1f}ms)")
    
    if "FAILED" in resp or "ERROR" in resp:
        sys.exit(1)

if __name__ == "__main__":
    main()
