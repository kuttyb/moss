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
    # Forged Section 1 tests may add a failure arm to probe ABI validation.
    # Keep the separately framed execution bodies structurally aligned so the
    # test reaches the intended Section 1 validation instead of failing at the
    # earlier cross-section consistency check.
    interface_counts = {
        match.group(1): int(match.group(2))
        for match in re.finditer(
            r'(?m)^  handler_failure_arms "([^"]+)" ([0-9]+)$', prefix)
    }
    handler_pattern = re.compile(
        r'(?m)^handler ("[^"]+") ("[^"]+") ([0-9]+) '
        r'("[^"]+") ([0-9]+) ([0-9]+)$')
    rebuilt = []
    cursor = 0
    for match in handler_pattern.finditer(payload):
        rebuilt.append(payload[cursor:match.start()])
        owner, quoted_name, line, source_path, statement_count, old_count = \
            match.groups()
        name = bytes(quoted_name[1:-1], "utf-8").decode("unicode_escape")
        desired = interface_counts.get(name, int(old_count))
        handler_end = payload.find("handler_end\n", match.end())
        if handler_end < 0:
            raise AssertionError("unterminated handler in execution section")
        handler_end += len("handler_end\n")
        body = payload[match.end():handler_end - len("handler_end\n")]
        if body.startswith("\n"):
            body = body[1:]
        arm_pattern = re.compile(
            r'(?ms)^failure_arm ([0-9]+) ([0-9]+) ([0-9]+)\n'
            r'.*?^failure_arm_end\n')
        arms = list(arm_pattern.finditer(body))
        if len(arms) != desired:
            if len(arms) > desired:
                body = body[:arms[desired].start()]
            else:
                body += "".join(
                    f"failure_arm {index} {line} 0\nfailure_arm_end\n"
                    for index in range(len(arms), desired))
        rebuilt.append(
            f"handler {owner} {quoted_name} {line} {source_path} "
            f"{statement_count} {desired}\n{body}handler_end\n")
        cursor = handler_end
    rebuilt.append(payload[cursor:])
    payload = "".join(rebuilt)
    header = (
        "execution_body_version 1\n"
        f"execution_body_length {len(payload.encode('utf-8'))}\n"
        "execution_body_hash sha256:" + hashlib.sha256(
            payload.encode("utf-8")).hexdigest() +
        "\nexecution_body_payload\n")
    return prefix + header + payload + "execution_body_end\n"
