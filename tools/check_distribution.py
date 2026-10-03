"""Audit generated TGZ contents without extracting untrusted archive paths."""
import argparse
from pathlib import PurePosixPath
import tarfile


def audit(path, source):
    with tarfile.open(path, "r:gz") as archive:
        members = archive.getmembers()
    if not members:
        raise ValueError("empty archive")
    roots = {PurePosixPath(member.name).parts[0] for member in members}
    if len(roots) != 1:
        raise ValueError("archive must have exactly one package root")
    paths = set()
    for member in members:
        path = PurePosixPath(member.name)
        if path.is_absolute() or ".." in path.parts:
            raise ValueError("unsafe archive path")
        if any(part in {".git", "build", "__pycache__", "ROADMAP.md"} for part in path.parts):
            raise ValueError(f"private/generated content: {path}")
        if path.suffix == ".pyc" or member.isdev() or member.issym() or member.islnk():
            raise ValueError(f"unexpected archive entry: {path}")
        paths.add("/".join(path.parts[1:]))
    if source:
        required = {"CMakeLists.txt", "README.md", "LICENSE", "src/read_mostly_map.cpp",
                    "tests/fuzz_model.cpp", "tools/compare_benchmarks.py",
                    "include/read_mostly/read_mostly_map.hpp"}
        if not required.issubset(paths):
            raise ValueError("incomplete source archive")
    else:
        if "include/read_mostly/read_mostly_map.hpp" not in paths:
            raise ValueError("public headers absent")
        for ending in ("/libread_mostly_map.a", "/ReadMostlyMapConfig.cmake",
                       "/docs/compatibility.md", "/LICENSE"):
            if not any(name.endswith(ending) for name in paths):
                raise ValueError(f"binary package missing {ending}")
        if any(name.startswith(("tests/", "examples/", "src/")) for name in paths):
            raise ValueError("test/example implementation accidentally installed")
    print(f"Distribution audit passed: {len(paths)} entries")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", required=True)
    parser.add_argument("--binary", required=True)
    args = parser.parse_args()
    audit(args.source, True)
    audit(args.binary, False)


if __name__ == "__main__":
    main()
