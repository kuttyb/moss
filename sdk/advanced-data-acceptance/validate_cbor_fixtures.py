#!/usr/bin/env python3
"""Validate the dependency-free RFC 8949 fixture/provenance contract."""

import json
from pathlib import Path


FIXTURE = Path(__file__).resolve().parent / "fixtures" / "cbor_rfc8949.json"
EXPECTED = {
    "uint-0": "00",
    "uint-23": "17",
    "uint-24": "1818",
    "uint-1000": "1903e8",
    "negative-1": "20",
    "negative-1000": "3903e7",
    "bytes-01020304": "4401020304",
    "text-ietf": "6449455446",
    "array-1-2-3": "83010203",
    "array-bool-null": "83f5f4f6",
    "nested-map": "a2616101616283f5f6820203",
    "text-umlaut": "62c3bc",
}


def load_and_validate() -> dict:
    contract = json.loads(FIXTURE.read_text(encoding="utf-8"))
    assert contract["contract_version"] == 1
    assert contract["provenance"]["standard"] == "RFC 8949"
    assert contract["provenance"]["section"].startswith("Appendix A.")
    assert contract["oracle"] == {
        "implementation": "cbor2",
        "version": "5.7.1",
        "ordinary_checks_require_oracle": False,
    }
    actual = {entry["id"]: entry["hex"] for entry in contract["vectors"]}
    assert actual == EXPECTED
    for identifier, encoded in actual.items():
        assert encoded == encoded.lower(), identifier
        assert len(encoded) % 2 == 0, identifier
        bytes.fromhex(encoded)
    return contract


if __name__ == "__main__":
    loaded = load_and_validate()
    print(f"RFC 8949 fixture contract passed: {len(loaded['vectors'])} unique vectors")
