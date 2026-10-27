"""Helpers for synthetic ABI-v9 interface mutations in tooling tests."""

from __future__ import annotations

import hashlib
import re


def section1_digest_material(text: str) -> str:
    """Return provider seal bytes, excluding its self fields and Section 2."""
    material = re.sub(
        r"(?m)^provider_contract_digest sha256:[0-9a-f]{64}\n", "", text)
    material = re.sub(r"(?m)^package_source_hash [^\n]*\n", "", material)
    marker = "\nexecution_body_version "
    section2 = material.find(marker)
    if section2 >= 0:
        material = material[:section2 + 1]
    return material


def section1_semantic_exports(text: str) -> str:
    semantic = text.split("\nsemantic_exports\n", 1)[1]
    marker = "\nexecution_body_version "
    section2 = semantic.find(marker)
    if section2 >= 0:
        semantic = semantic[:section2] + "\n"
    return semantic


def refresh_v9_execution_section(text: str) -> str:
    """Rebind a synthetic Section 2 after a valid Section 1 test mutation."""
    marker = "\nexecution_body_version "
    section2 = text.find(marker)
    if section2 < 0:
        return text
    digest_match = re.search(
        r"(?m)^provider_contract_digest sha256:([0-9a-f]{64})$", text)
    if not digest_match:
        return text
    prefix = text[:section2 + 1]
    section = text[section2 + 1:]
    framing = re.match(
        r"execution_body_version 1\nexecution_body_length [0-9]+\n"
        r"execution_body_hash sha256:[0-9a-f]{64}\n"
        r"execution_body_payload\n", section)
    if not framing or not section.endswith("execution_body_end\n"):
        raise AssertionError("malformed execution-body fixture framing")
    payload_begin = framing.end()
    payload_end = len(section) - len("execution_body_end\n")
    payload = section[payload_begin:payload_end]
    payload, count = re.subn(
        r"(?m)^interface_digest [0-9a-f]{64}$",
        "interface_digest " + digest_match.group(1), payload)
    if count != 1:
        raise AssertionError(f"expected one Section 2 interface binding, got {count}")
    header = (
        "execution_body_version 1\n"
        f"execution_body_length {len(payload.encode('utf-8'))}\n"
        "execution_body_hash sha256:" + hashlib.sha256(
            payload.encode("utf-8")).hexdigest() +
        "\nexecution_body_payload\n")
    return prefix + header + payload + "execution_body_end\n"
