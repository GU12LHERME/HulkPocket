#!/usr/bin/env python3
"""Run GitHub Actions workflow jobs locally on Linux/WSL2 using their real steps.

The runner reads .github/workflows/*.yml, copies the current working tree
(tracked + untracked, honoring .gitignore) into a private workspace, and runs
each `run:` step with the same bash flags GitHub uses. Supported `uses:`
actions are emulated:

  actions/checkout        workspace copy, or `git clone` for `repository:`
  actions/setup-java      requires a local JDK 17 (tools/setup_wsl.sh)
  android-actions/setup-android   uses $ANDROID_HOME from tools/setup_wsl.sh
  gradle/actions/setup-gradle     requires a matching `gradle` on PATH
  actions/cache           no-op (ccache directories persist locally)
  actions/upload-artifact copies files to <out>/artifacts/<name>

`sudo apt-get install` is checked against installed packages instead of
running, and dl.google.com Android downloads are cached between runs.
Nothing is uploaded anywhere.

Examples:
  python3 tools/run_ci_local.py --list
  python3 tools/run_ci_local.py xbe-startup-layout android-smoke
  python3 tools/run_ci_local.py xbe-recomp-arm64 --out ~/hulk-ci
"""
from __future__ import annotations

import argparse
import os
import pathlib
import re
import shlex
import shutil
import subprocess
import sys
import tempfile
import time

try:
    import yaml
except ImportError:  # pragma: no cover - environment guard
    sys.exit("PyYAML is required: sudo apt-get install python3-yaml (tools/setup_wsl.sh)")

REPO = pathlib.Path(__file__).resolve().parent.parent
WORKFLOWS = REPO / ".github" / "workflows"
EXPR = re.compile(r"\$\{\{\s*(.*?)\s*\}\}")

SUDO_SHIM = r"""#!/usr/bin/env bash
# Local CI sudo shim: verify apt packages instead of installing them.
set -euo pipefail
if [[ "${1:-}" == "apt-get" || "${1:-}" == "apt" ]]; then
    shift
    case "${1:-}" in
        update) exit 0 ;;
        install)
            shift
            missing=()
            for pkg in "$@"; do
                [[ "$pkg" == -* ]] && continue
                dpkg-query -W -f='${Status}' "$pkg" 2>/dev/null | grep -q 'install ok installed' || missing+=("$pkg")
            done
            if (( ${#missing[@]} )); then
                echo "Missing apt packages: ${missing[*]}" >&2
                echo "Install them with: sudo apt-get install -y ${missing[*]}" >&2
                exit 1
            fi
            exit 0 ;;
    esac
fi
exec "$@"
"""

CURL_SHIM = r"""#!/usr/bin/env bash
# Local CI curl shim: cache Android repository downloads between runs.
set -euo pipefail
real=/usr/bin/curl
url="" out=""
args=("$@")
for ((i = 0; i < ${#args[@]}; i++)); do
    case "${args[i]}" in
        -o) out="${args[i+1]}" ;;
        https://dl.google.com/android/repository/*) url="${args[i]}" ;;
    esac
done
if [[ -n "$url" && -n "$out" ]]; then
    cache="${HULK_CI_CACHE:?}/downloads"
    mkdir -p "$cache"
    file="$cache/$(basename "$url")"
    if [[ ! -s "$file" ]]; then
        "$real" "$@"
        cp -- "$out" "$file.partial" && mv -- "$file.partial" "$file"
    else
        cp -- "$file" "$out"
    fi
    exit 0
fi
exec "$real" "$@"
"""


def load(name: str) -> dict:
    path = WORKFLOWS / (name if name.endswith(".yml") else name + ".yml")
    with path.open(encoding="utf-8") as stream:
        return yaml.safe_load(stream)


def current_branch() -> str:
    return subprocess.check_output(["git", "rev-parse", "--abbrev-ref", "HEAD"], cwd=REPO, text=True).strip()


def copy_workspace(dest: pathlib.Path) -> None:
    """Mirror actions/checkout using the working tree (including uncommitted edits)."""
    files = subprocess.check_output(
        ["git", "ls-files", "-z", "--cached", "--others", "--exclude-standard"], cwd=REPO)
    if dest.exists():
        shutil.rmtree(dest)
    dest.mkdir(parents=True)
    for raw in filter(None, files.split(b"\0")):
        rel = raw.decode("utf-8", "surrogateescape")
        src = REPO / rel
        if not src.is_file():
            continue  # deleted but still tracked
        target = dest / rel
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(src, target)
    # Scripts compare `git hash-object` and run `git rev-parse HEAD`. Commit
    # with LF normalization, then rewrite the tree from the index so the
    # workspace matches a Linux actions/checkout even when copied from a
    # Windows (CRLF) working tree.
    git = ["git", "-c", "user.name=ci", "-c", "user.email=ci@localhost",
           "-c", "core.hooksPath=/dev/null", "-c", "core.safecrlf=false"]
    subprocess.run(["git", "init", "-q"], cwd=dest, check=True)
    subprocess.run([*git, "-c", "core.autocrlf=input", "add", "-A"], cwd=dest, check=True)
    subprocess.run([*git, "commit", "-q", "-m", "local ci snapshot"], cwd=dest, check=True)
    subprocess.run([*git, "-c", "core.autocrlf=false", "rm", "-q", "-r", "--cached", "."], cwd=dest, check=True)
    subprocess.run([*git, "-c", "core.autocrlf=false", "reset", "-q", "--hard"], cwd=dest, check=True)


class Job:
    def __init__(self, workflow: str, job_id: str, spec: dict, out: pathlib.Path):
        self.workflow, self.job_id, self.spec = workflow, job_id, spec
        self.root = out / workflow / job_id
        self.workspace = self.root / "workspace"
        self.temp = self.root / "runner-temp"
        self.artifacts = out / "artifacts"
        self.cache = out / "cache"
        self.shims = self.root / "shims"
        self.env: dict[str, str] = {}

    def expand(self, text: str) -> str:
        def resolve(match: re.Match) -> str:
            expr = match.group(1)
            if expr == "runner.temp":
                return str(self.temp)
            if expr in ("runner.os",):
                return "Linux"
            if expr in ("runner.arch",):
                return "X64"
            if expr.startswith("env."):
                return self.env.get(expr[4:], os.environ.get(expr[4:], ""))
            if expr in ("github.ref",):
                return "refs/heads/" + self.env["GITHUB_REF_NAME"]
            if expr in ("github.ref_name",):
                return self.env["GITHUB_REF_NAME"]
            if expr in ("github.run_id", "github.run_attempt", "github.run_number"):
                return "1"
            if expr in ("github.workspace",):
                return str(self.workspace)
            if expr in ("github.sha",):
                return self.env.get("GITHUB_SHA", "")
            raise ValueError(f"Unsupported expression: ${{{{ {expr} }}}}")
        return EXPR.sub(resolve, str(text))

    def prepare(self, branch: str) -> None:
        if self.root.exists():
            shutil.rmtree(self.root)
        self.temp.mkdir(parents=True)
        self.shims.mkdir(parents=True)
        self.artifacts.mkdir(parents=True, exist_ok=True)
        self.cache.mkdir(parents=True, exist_ok=True)
        for name, body in (("sudo", SUDO_SHIM), ("curl", CURL_SHIM)):
            shim = self.shims / name
            shim.write_text(body)
            shim.chmod(0o755)
        android = os.environ.get("ANDROID_HOME") or str(pathlib.Path.home() / "Android/Sdk")
        path = os.pathsep.join([
            str(self.shims),
            f"{android}/cmdline-tools/latest/bin",
            f"{android}/platform-tools",
            os.environ.get("PATH", ""),
        ])
        self.env = {
            **os.environ,
            "CI": "true",
            "GITHUB_ACTIONS": "true",
            "GITHUB_REF_NAME": branch,
            "GITHUB_WORKSPACE": str(self.workspace),
            "GITHUB_ENV": str(self.root / "github-env"),
            "GITHUB_STEP_SUMMARY": str(self.root / "step-summary.md"),
            "GITHUB_OUTPUT": str(self.root / "github-output"),
            "RUNNER_TEMP": str(self.temp),
            "RUNNER_OS": "Linux",
            "ANDROID_HOME": android,
            "ANDROID_SDK_ROOT": android,
            "HULK_CI_CACHE": str(self.cache),
            "PATH": path,
        }
        for key, value in (self.spec.get("env") or {}).items():
            self.env[key] = self.expand(value)
        if "CCACHE_DIR" in self.env:
            # Persist ccache across local runs instead of the CI temp path.
            self.env["CCACHE_DIR"] = str(self.cache / "ccache" / self.workflow)
        for name in ("github-env", "step-summary.md", "github-output"):
            (self.root / name).touch()
        copy_workspace(self.workspace)
        self.env["GITHUB_SHA"] = subprocess.check_output(
            ["git", "rev-parse", "HEAD"], cwd=self.workspace, text=True).strip()

    def absorb_github_env(self) -> None:
        env_file = self.root / "github-env"
        for line in env_file.read_text().splitlines():
            if "=" in line:
                key, value = line.split("=", 1)
                self.env[key] = value
        env_file.write_text("")

    def uses(self, step: dict) -> None:
        action = step["uses"].split("@", 1)[0]
        options = {k: self.expand(v) for k, v in (step.get("with") or {}).items()}
        if action == "actions/checkout":
            if "repository" in options:
                dest = self.workspace / options.get("path", ".")
                subprocess.run(["git", "clone", "--depth", "1",
                                f"https://github.com/{options['repository']}.git", str(dest)], check=True)
            return
        if action == "actions/setup-java":
            version = subprocess.run(["java", "-version"], capture_output=True, text=True).stderr
            if f'"{options.get("java-version", "17")}' not in version:
                raise RuntimeError(f"JDK {options.get('java-version')} required, found: {version.splitlines()[:1]}")
            return
        if action == "android-actions/setup-android":
            if not pathlib.Path(self.env["ANDROID_HOME"], "cmdline-tools/latest/bin/sdkmanager").exists():
                raise RuntimeError("Linux Android SDK missing; run tools/setup_wsl.sh")
            return
        if action == "gradle/actions/setup-gradle":
            wanted = options.get("gradle-version", "")
            gradle = shutil.which("gradle", path=self.env["PATH"])
            found = subprocess.run([gradle, "--version"], capture_output=True, text=True).stdout if gradle else ""
            if f"Gradle {wanted}" not in found:
                raise RuntimeError(f"Gradle {wanted} not on PATH (build the APK on Windows instead)")
            return
        if action == "actions/cache":
            return
        if action == "actions/upload-artifact":
            dest = self.artifacts / options["name"]
            dest.mkdir(parents=True, exist_ok=True)
            found = False
            for pattern in str(options["path"]).split("\n"):
                pattern = pattern.strip()
                if not pattern:
                    continue
                src = pathlib.Path(pattern)
                if not src.is_absolute():
                    src = self.workspace / src
                if src.is_dir():
                    shutil.copytree(src, dest, dirs_exist_ok=True)
                    found = True
                elif src.is_file():
                    shutil.copy2(src, dest / src.name)
                    found = True
            if not found and options.get("if-no-files-found", "warn") == "error":
                raise RuntimeError(f"No files for artifact {options['name']}")
            return
        raise RuntimeError(f"Unsupported action: {step['uses']}")

    def run(self, skip: re.Pattern | None) -> bool:
        ok = True
        log = (self.root / "job.log").open("w", encoding="utf-8")
        for index, step in enumerate(self.spec.get("steps", []), 1):
            name = step.get("name") or step.get("uses") or step.get("run", "").splitlines()[0]
            condition = str(step.get("if", ""))
            always = "always()" in condition
            if not ok and not always:
                print(f"  [skip] {name}")
                continue
            if skip and skip.search(name):
                print(f"  [skip by --skip] {name}")
                continue
            started = time.monotonic()
            print(f"  [{index:02d}] {name} ...", flush=True)
            try:
                if "uses" in step:
                    self.uses(step)
                else:
                    env = dict(self.env)
                    for key, value in (step.get("env") or {}).items():
                        env[key] = self.expand(value)
                    cwd = self.workspace
                    if "working-directory" in step:
                        cwd = pathlib.Path(self.expand(step["working-directory"]))
                        if not cwd.is_absolute():
                            cwd = self.workspace / cwd
                    script = self.temp / f"step-{index:02d}.sh"
                    script.write_text(self.expand(step["run"]))
                    result = subprocess.run(
                        ["bash", "--noprofile", "--norc", "-eo", "pipefail", str(script)],
                        cwd=cwd, env=env, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True,
                        errors="replace")
                    log.write(f"\n===== {name} =====\n{result.stdout}")
                    log.flush()
                    if result.returncode != 0:
                        tail = "\n".join(result.stdout.splitlines()[-25:])
                        raise RuntimeError(f"exit {result.returncode}\n{tail}")
                self.absorb_github_env()
                print(f"       ok ({time.monotonic() - started:.0f}s)")
            except Exception as error:  # report and continue to always() steps
                ok = False
                print(f"       FAILED ({time.monotonic() - started:.0f}s): {error}")
                log.write(f"\n===== {name} FAILED =====\n{error}\n")
        log.close()
        return ok


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("workflows", nargs="*", help="workflow file names without .yml")
    parser.add_argument("--job", help="run only this job id")
    parser.add_argument("--list", action="store_true")
    parser.add_argument("--out", type=pathlib.Path, default=pathlib.Path.home() / "hulk-ci")
    parser.add_argument("--skip", help="regex of step names to skip")
    parser.add_argument("--branch", default=None, help="value for GITHUB_REF_NAME (default: current)")
    args = parser.parse_args()
    if sys.platform != "linux":
        parser.error("run inside Linux or WSL2: wsl -d Ubuntu-24.04 -- python3 tools/run_ci_local.py ...")
    names = args.workflows or sorted(p.stem for p in WORKFLOWS.glob("*.yml"))
    if args.list:
        for name in names:
            spec = load(name)
            print(f"{name}: {', '.join(spec.get('jobs', {}))}")
        return 0
    out = args.out.expanduser().resolve()
    branch = args.branch or current_branch()
    skip = re.compile(args.skip) if args.skip else None
    results = []
    for name in names:
        spec = load(name)
        for job_id, job_spec in spec.get("jobs", {}).items():
            if args.job and job_id != args.job:
                continue
            print(f"== {name} / {job_id}", flush=True)
            job = Job(name, job_id, job_spec, out)
            started = time.monotonic()
            try:
                job.prepare(branch)
                ok = job.run(skip)
            except Exception as error:
                print(f"  setup FAILED: {error}")
                ok = False
            results.append((name, job_id, ok, time.monotonic() - started, job.root / "job.log"))
    print("\nSummary")
    for name, job_id, ok, seconds, log in results:
        print(f"  {'PASS' if ok else 'FAIL'}  {name}/{job_id}  {seconds:.0f}s  log: {log}")
    return 0 if results and all(r[2] for r in results) else 1


if __name__ == "__main__":
    raise SystemExit(main())
