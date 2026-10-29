"""Support object list for native tests that include src/moss.cpp directly."""
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]


def compiler_support_sources() -> list[str]:
    """Return production TUs other than moss.cpp, in stable link order."""
    return sorted(
        path.relative_to(ROOT).as_posix()
        for path in (ROOT / "src").rglob("*.cpp")
        if path.name != "moss.cpp"
    )
