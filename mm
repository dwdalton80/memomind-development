#!/usr/bin/env python3
"""mm - MemoMind glasses plugin workspace driver.

One entry point for the whole workspace: fetch the SDK, scaffold a plugin,
build it, and check the environment. Run ./mm with no arguments for help.
"""

from __future__ import annotations

import argparse
import json
import os
import shutil
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent
SDK = ROOT / ".sdk"
LOCK = ROOT / "sdk.lock"
BUILD = ROOT / "build"
GLASS_SRC = ROOT / "plugins" / "glass"
WEB_SRC = ROOT / "plugins" / "web"
TEMPLATES = ROOT / "templates"

SKIP_DIRS = {".git", ".build", "build", "dist", "node_modules", "__pycache__"}


# --------------------------------------------------------------------------
# small helpers
# --------------------------------------------------------------------------

def die(message: str) -> "NoReturn":  # type: ignore[valid-type]
    print(f"mm: {message}", file=sys.stderr)
    raise SystemExit(1)


def run(command, cwd=None, check=True, quiet=False):
    """Run a command, echoing it unless quiet."""
    if not quiet:
        print("+ " + " ".join(str(part) for part in command))
    return subprocess.run(command, cwd=cwd, check=check)


def need_sdk() -> None:
    if not (SDK / "GlassSDK" / "build.py").is_file():
        die("SDK missing. Run ./mm setup first.")


def read_lock() -> dict:
    if not LOCK.is_file():
        die("sdk.lock is missing from the repository.")
    return json.loads(LOCK.read_text())


def manifest_id(path: Path) -> str:
    try:
        return json.loads(path.read_text()).get("id", "?")
    except (OSError, json.JSONDecodeError):
        return "?"


def discover(base: Path) -> "list[Path]":
    """Return every plugin directory (one holding manifest.json) below base."""
    found = []
    if not base.is_dir():
        return found
    for manifest in sorted(base.rglob("manifest.json")):
        relative = manifest.relative_to(base).parts
        if any(part in SKIP_DIRS for part in relative):
            continue
        found.append(manifest.parent)
    return found


def select(plugins: "list[Path]", base: Path, names: "list[str]") -> "list[Path]":
    """Filter discovered plugins by name, or return all when no name is given."""
    if not names:
        return plugins
    by_name = {str(p.relative_to(base)): p for p in plugins}
    by_name.update({p.name: p for p in plugins})
    chosen = []
    for name in names:
        if name not in by_name:
            continue
        if by_name[name] not in chosen:
            chosen.append(by_name[name])
    return chosen


# --------------------------------------------------------------------------
# commands
# --------------------------------------------------------------------------

def cmd_setup(args) -> int:
    lock = read_lock()
    repo, commit = lock["repo"], lock["commit"]

    # The SDK carries prebuilt Studio binaries, so keep the checkout shallow.
    # GIT_LFS_SKIP_SMUDGE lets the clone finish where LFS objects are not served.
    environment = dict(os.environ, GIT_LFS_SKIP_SMUDGE="1")
    if not (SDK / ".git").is_dir():
        if SDK.exists():
            shutil.rmtree(SDK)
        print(f"Cloning {repo} (shallow)")
        clone = ["git", "clone", "--depth", "1", repo, str(SDK)]
        print("+ " + " ".join(clone))
        subprocess.run(clone, check=True, env=environment)

    def head_sha() -> str:
        return subprocess.run(["git", "rev-parse", "HEAD"], cwd=SDK,
                              capture_output=True, text=True).stdout.strip()

    if args.update:
        run(["git", "fetch", "--depth", "1", "origin", "HEAD"], cwd=SDK)
        run(["git", "checkout", "--detach", "FETCH_HEAD"], cwd=SDK)
        lock["commit"] = head_sha()
        LOCK.write_text(json.dumps(lock, indent=2) + "\n")
        print(f"sdk.lock updated to {lock['commit']}")
    elif head_sha() != commit:
        print(f"Checking out pinned commit {commit[:12]}")
        run(["git", "fetch", "--depth", "1", "origin", commit], cwd=SDK)
        run(["git", "checkout", "--detach", commit], cwd=SDK)

    print("Installing GlassSDK review dependencies")
    run([sys.executable, "-m", "pip", "install", "-q", "-r",
         str(SDK / "GlassSDK" / "build-host" / "tools" / "requirements-review.txt")],
        check=False)

    print("Resolving the RISC-V toolchain (first run downloads it)")
    run([sys.executable, str(SDK / "GlassSDK" / "build.py"), "toolchain"])

    print("\nSetup complete. Try:  ./mm build")
    return 0


def cmd_list(args) -> int:
    glass = discover(GLASS_SRC)
    web = discover(WEB_SRC)
    if not glass and not web:
        print("No plugins yet. Create one with:  ./mm new glass my_plugin")
        return 0
    for label, base, plugins in (("glass (.gmp)", GLASS_SRC, glass),
                                 ("web (.mmpkg)", WEB_SRC, web)):
        print(f"\n{label}")
        if not plugins:
            print("  (none)")
            continue
        for plugin in plugins:
            relative = plugin.relative_to(base)
            print(f"  {relative}  ->  {manifest_id(plugin / 'manifest.json')}")
    print()
    return 0


def cmd_new(args) -> int:
    base = GLASS_SRC if args.kind == "glass" else WEB_SRC
    target = base / args.name
    if target.exists():
        die(f"{target.relative_to(ROOT)} already exists.")
    source = TEMPLATES / args.kind / "__name__"
    if not source.is_dir():
        die(f"missing template at {source.relative_to(ROOT)}")

    identifier = args.id or f"com.memomind.{args.name.replace('_', '-')}"
    shutil.copytree(source, target)
    for path in sorted(target.rglob("*")):
        if not path.is_file():
            continue
        text = path.read_text()
        text = text.replace("__NAME__", args.name).replace("__ID__", identifier)
        path.write_text(text)
        if "__name__" in path.name:
            path.rename(path.with_name(path.name.replace("__name__", args.name)))

    print(f"Created {target.relative_to(ROOT)}  (id: {identifier})")
    print(f"Build it with:  ./mm build {args.name}")
    return 0


def build_glass(plugin: Path) -> bool:
    output = BUILD / "glass"
    output.mkdir(parents=True, exist_ok=True)
    print(f"\n=== glass: {plugin.relative_to(GLASS_SRC)} ===")
    result = run([sys.executable, str(SDK / "GlassSDK" / "build.py"), "build",
                  "--project", str(plugin), "--build-dir", str(output)],
                 check=False)
    return result.returncode == 0


def build_web(plugin: Path) -> bool:
    output = BUILD / "web"
    output.mkdir(parents=True, exist_ok=True)
    name = plugin.name
    print(f"\n=== web: {plugin.relative_to(WEB_SRC)} ===")

    # .mmpkg packages are self-contained, so the Web SDK is vendored into the
    # plugin rather than resolved from node_modules at runtime.
    vendored = run(["node", str(ROOT / "tools" / "vendor-web-sdk.mjs"),
                    str(SDK), str(plugin)], check=False)
    if vendored.returncode != 0:
        return False

    source = plugin
    package = plugin / "package.json"
    if package.is_file():
        scripts = json.loads(package.read_text()).get("scripts", {})
        if "build" in scripts:
            if not (plugin / "node_modules").is_dir():
                installer = "ci" if (plugin / "package-lock.json").is_file() else "install"
                if run(["npm", installer], cwd=plugin, check=False).returncode != 0:
                    return False
            if run(["npm", "run", "build"], cwd=plugin, check=False).returncode != 0:
                return False
            source = plugin / "dist"

    if not (source / "manifest.json").is_file():
        print(f"mm: no manifest.json in {source}", file=sys.stderr)
        return False

    packer = SDK / "PhoneSDK" / "tools" / "build-mmpkg.mjs"
    result = run(["node", str(packer), str(source), str(output / f"{name}.mmpkg")],
                 check=False)
    return result.returncode == 0


def cmd_build(args) -> int:
    need_sdk()
    glass = select(discover(GLASS_SRC), GLASS_SRC, args.names)
    web = select(discover(WEB_SRC), WEB_SRC, args.names)

    if args.names and not glass and not web:
        die(f"no plugin matched: {', '.join(args.names)}")
    if not glass and not web:
        print("No plugins to build. Create one with:  ./mm new glass my_plugin")
        return 0

    failed = []
    for plugin in glass:
        if not build_glass(plugin):
            failed.append(str(plugin.relative_to(ROOT)))
    for plugin in web:
        if not build_web(plugin):
            failed.append(str(plugin.relative_to(ROOT)))

    print()
    if failed:
        for name in failed:
            print(f"FAILED  {name}")
        return 1
    print(f"Built {len(glass) + len(web)} plugin(s) into {BUILD.relative_to(ROOT)}/")
    return 0


def cmd_studio(args) -> int:
    need_sdk()
    web = discover(WEB_SRC)
    chosen = select(web, WEB_SRC, [args.name]) if args.name else web
    if not chosen:
        die("no web plugin found. Create one with:  ./mm new web my-plugin")
    plugin = chosen[0]
    source = plugin / "dist" if (plugin / "dist" / "manifest.json").is_file() else plugin
    runner = SDK / "PhoneSDK" / "tools" / "run-browser-studio.mjs"
    print(f"Browser Studio: {plugin.relative_to(ROOT)} (port {args.port})")
    return run(["node", str(runner), "--plugin", str(source), "--port", str(args.port)],
               cwd=SDK / "PhoneSDK", check=False).returncode


def cmd_clean(args) -> int:
    if BUILD.exists():
        shutil.rmtree(BUILD)
        print(f"Removed {BUILD.relative_to(ROOT)}/")
    for plugin in discover(GLASS_SRC) + discover(WEB_SRC):
        stale = plugin / ".build"
        if stale.exists():
            shutil.rmtree(stale)
            print(f"Removed {stale.relative_to(ROOT)}/")
    print("Clean. The SDK checkout and toolchain cache are untouched.")
    return 0


def cmd_doctor(args) -> int:
    ok = True

    def check(label: str, good: bool, detail: str = "") -> None:
        nonlocal ok
        ok = ok and good
        print(f"  [{'ok' if good else '--'}] {label}{(': ' + detail) if detail else ''}")

    print("Environment")
    check("python >= 3.8", sys.version_info >= (3, 8),
          ".".join(str(n) for n in sys.version_info[:3]))
    for tool in ("git", "node", "npm"):
        path = shutil.which(tool)
        check(tool, path is not None, path or "not found")

    print("\nWorkspace")
    check("sdk.lock", LOCK.is_file())
    sdk_present = (SDK / "GlassSDK" / "build.py").is_file()
    check("SDK checkout (.sdk)", sdk_present, "run ./mm setup" if not sdk_present else "")
    if sdk_present:
        head = subprocess.run(["git", "rev-parse", "--short", "HEAD"], cwd=SDK,
                              capture_output=True, text=True).stdout.strip()
        pinned = read_lock()["commit"][:len(head)] if head else ""
        check("SDK matches sdk.lock", head == pinned, head or "unknown")
        # Desktop Studio is informational: it is a per-platform binary and its
        # absence never blocks building. Import built packages instead.
        print("\nDesktop Studio (simulator, optional)")
        studio = SDK / "Studio"
        for platform in ("linux", "macos", "windows"):
            files = [p for p in (studio / platform).glob("*") if p.name != ".gitkeep"]
            print(f"  [{'ok' if files else '  '}] Studio/{platform}: "
                  f"{files[0].name if files else 'not published in this release'}")

    print("\nPlugins")
    check("glass plugins", True, str(len(discover(GLASS_SRC))))
    check("web plugins", True, str(len(discover(WEB_SRC))))

    print()
    return 0 if ok else 1


# --------------------------------------------------------------------------

def main(argv) -> int:
    parser = argparse.ArgumentParser(
        prog="./mm", description="MemoMind glasses plugin workspace driver.")
    sub = parser.add_subparsers(dest="command")

    p = sub.add_parser("setup", help="fetch the pinned SDK and toolchain")
    p.add_argument("--update", action="store_true",
                   help="move the pin to the SDK's latest commit")
    p.set_defaults(func=cmd_setup)

    p = sub.add_parser("list", help="list plugins in this workspace")
    p.set_defaults(func=cmd_list)

    p = sub.add_parser("new", help="scaffold a new plugin from a template")
    p.add_argument("kind", choices=["glass", "web"])
    p.add_argument("name")
    p.add_argument("--id", help="plugin id (default: com.memomind.<name>)")
    p.set_defaults(func=cmd_new)

    p = sub.add_parser("build", help="build every plugin, or the named ones")
    p.add_argument("names", nargs="*")
    p.set_defaults(func=cmd_build)

    p = sub.add_parser("studio", help="run PhoneSDK Browser Studio for a web plugin")
    p.add_argument("name", nargs="?")
    p.add_argument("--port", type=int, default=4173)
    p.set_defaults(func=cmd_studio)

    p = sub.add_parser("clean", help="remove build outputs")
    p.set_defaults(func=cmd_clean)

    p = sub.add_parser("doctor", help="check the toolchain and workspace")
    p.set_defaults(func=cmd_doctor)

    args = parser.parse_args(argv)
    if not args.command:
        parser.print_help()
        return 0
    return args.func(args)


if __name__ == "__main__":
    try:
        raise SystemExit(main(sys.argv[1:]))
    except KeyboardInterrupt:
        raise SystemExit(130)
