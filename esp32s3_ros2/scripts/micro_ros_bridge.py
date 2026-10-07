"""Build a WSL Xtensa archive against the existing Windows ESP-IDF SDK.

Windows: idf.py -DMICROROS_PREPARE=ON reconfigure
WSL:     bash scripts/build_micro_ros.sh
Windows: idf.py -DMICROROS_PREPARE=OFF build
No Linux ESP-IDF checkout or IDF Python environment is installed.
"""
from pathlib import Path
import argparse
import hashlib
import json
import os
import re
import shlex
import shutil
import subprocess
import sys


def run(args, **kwargs):
    shown = [str(arg) for arg in args if not str(arg).startswith("IDF_INCLUDES=")]
    print("+ " + shlex.join(shown), flush=True)
    return subprocess.run(list(map(str, args)), check=True, **kwargs)


def local_path(value):
    if not value:
        return None
    if os.name != "nt" and re.match(r"^[A-Za-z]:[/\\]", value):
        value = subprocess.check_output(["wslpath", "-u", value], text=True).strip()
    return Path(value)


def load_request(path):
    request = json.loads(path.read_text(encoding="utf-8"))
    if request["target"] != "esp32s3" or request["middleware"] != "microxrcedds":
        raise RuntimeError("This project bridge supports ESP32-S3 / Micro XRCE-DDS only.")
    return request


def fingerprint(request):
    digest = hashlib.sha256(json.dumps(request, sort_keys=True).encode())
    component = local_path(request["component"])
    files = [local_path(request["build_dir"]) / "config/sdkconfig.h",
             local_path(request["build_dir"]) / "config/sdkconfig.cmake",
             local_path(request["idf_path"]) / "tools/tools.json",
             component / "libmicroros.mk", component / "esp32_toolchain.cmake.in",
             component / "colcon.meta", Path(__file__)]
    if request["app_meta"]:
        files.append(local_path(request["app_meta"]))
    files.extend(sorted((component / "include_override").rglob("*"), key=lambda p: p.as_posix()))
    files.extend(sorted(local_path(request["extra_packages"]).rglob("*"), key=lambda p: p.as_posix()))
    for file in files:
        if file.is_file():
            digest.update(file.read_bytes())
    sdk = local_path(request["idf_path"])
    sdk_files = set()
    for value in request["includes"]:
        directory = local_path(value)
        if directory.is_relative_to(sdk) and directory.is_dir():
            sdk_files.update(p for p in directory.rglob("*") if p.is_file())
    for file in sorted(sdk_files, key=lambda p: p.relative_to(sdk).as_posix()):
        digest.update(file.relative_to(sdk).as_posix().encode())
        digest.update(file.read_bytes())
    return digest.hexdigest()


def snapshot_headers(request, cache):
    sdk = local_path(request["idf_path"])
    snapshot = cache / "sdk_headers"
    copied = []
    includes = []
    print("Copying the Windows SDK include directories to the WSL cache...", flush=True)
    for value in request["includes"]:
        source = local_path(value)
        if source.is_relative_to(sdk):
            destination = snapshot / source.relative_to(sdk)
            if source.is_dir() and not any(source == parent or parent in source.parents for parent in copied):
                shutil.copytree(source, destination, dirs_exist_ok=True)
                copied.append(source)
            includes.append(destination)
        elif source == local_path(request["build_dir"]) / "config":
            destination = cache / "sdk_build/config"
            shutil.copytree(source, destination, dirs_exist_ok=True)
            includes.append(destination)
        elif source == local_path(request["component"]) / "include_override":
            destination = cache / "include_override"
            shutil.copytree(source, destination, dirs_exist_ok=True)
            includes.append(destination)
        else:
            includes.append(source)
    # Include overrides must precede the SDK's platform headers.
    override = cache / "include_override"
    includes.remove(override)
    includes.insert(0, override)
    return snapshot, cache / "sdk_build", includes


def verify(request):
    output = local_path(request["project"]) / "lib/micro_ros/jazzy"
    manifest = output / "build_manifest.json"
    if not manifest.is_file() or not (output / "libmicroros.a").is_file():
        raise RuntimeError("Missing WSL micro-ROS archive. Run idf.py -DMICROROS_PREPARE=ON reconfigure, then the WSL builder.")
    if not (output / "include/rcl/rcl.h").is_file() and not (output / "include/rcl/rcl/rcl.h").is_file():
        raise RuntimeError("Missing micro-ROS headers. Rebuild the library in WSL.")
    if json.loads(manifest.read_text())["fingerprint"] != fingerprint(request):
        raise RuntimeError("SDK/configuration/library settings changed. Run the WSL builder again before building the application.")
    print("micro-ROS library matches the Windows SDK and configuration.")


def toolchain(request):
    sdk = local_path(request["idf_path"])
    tools = json.loads((sdk / "tools/tools.json").read_text())
    spec = next(t for t in tools["tools"] if t["name"] == "xtensa-esp-elf")
    match = re.search(r"crosstool-NG (esp-[^) ]+)", request["compiler_banner"])
    if not match:
        raise RuntimeError("Cannot identify the Windows Xtensa toolchain release.")
    version = match[1]
    if version not in {v["name"] for v in spec["versions"]}:
        raise RuntimeError(f"Windows compiler {version} is absent from this SDK's tools.json.")
    tools_root = Path(os.environ.get("MICROROS_WSL_TOOLS_PATH", Path.home() / ".cache/mcu_ros2/espressif"))
    compiler = tools_root / "tools/xtensa-esp-elf" / version / "xtensa-esp-elf/bin/xtensa-esp32s3-elf-gcc"
    if not compiler.is_file():
        env = os.environ.copy()
        env["IDF_TOOLS_PATH"] = str(tools_root)
        run([sys.executable, sdk / "tools/idf_tools.py", "--idf-path", sdk,
             "--non-interactive", "install", f"xtensa-esp-elf@{version}"], env=env)
    banner = subprocess.check_output([str(compiler), "--version"], text=True)
    if version not in banner:
        raise RuntimeError("Windows and WSL Xtensa compiler releases differ.")
    return compiler


def build(request):
    if os.name == "nt":
        raise RuntimeError("Run library generation in WSL; Windows IDF compiles the application.")
    for program in ("make", "git", "gcc", "g++", "cmake", "colcon", "vcs"):
        if not shutil.which(program):
            raise RuntimeError(f"Missing {program}. Use the WSL dependencies in README.md.")
    for module in ("catkin_pkg", "colcon_core", "lark", "em", "numpy"):
        __import__(module)
    project = local_path(request["project"])
    component = local_path(request["component"])
    # Linux build caches stay on the Linux filesystem. Only the finished
    # library and headers are published to the shared Windows project.
    project_id = hashlib.sha256(request["project"].encode()).hexdigest()[:12]
    cache = Path.home() / ".cache/mcu_ros2/esp32s3" / project_id
    cache.mkdir(parents=True, exist_ok=True)
    compiler = toolchain(request)
    for name in ("libmicroros.mk", "esp32_toolchain.cmake.in", "colcon.meta"):
        shutil.copyfile(component / name, cache / name)
    # Jazzy has removed some optional packages still named by the upstream
    # ignore list. Guard only those optional markers, and fail on real errors.
    makefile = cache / "libmicroros.mk"
    recipe = makefile.read_text()
    recipe = re.sub(r"touch (src/[^;\s]+/COLCON_IGNORE);",
                    lambda m: f'if [ -d "{Path(m[1]).parent}" ]; then touch {m[1]}; fi;', recipe)
    makefile.write_text(recipe)
    stamp = fingerprint(request)
    stamp_file = cache / "request_fingerprint"
    if not stamp_file.is_file() or stamp_file.read_text() != stamp:
        # These are fixed cache children, never user source directories.
        for name in ("libmicroros.a", "esp32_toolchain.cmake", "include", "micro_ros_src", "sdk_headers", "sdk_build", "include_override"):
            child = cache / name
            if child.is_dir():
                shutil.rmtree(child)
            elif child.exists():
                child.unlink()
    sdk_snapshot, sdk_build, include_paths = snapshot_headers(request, cache)
    bin_dir = compiler.parent
    includes = " ".join("-I" + shlex.quote(str(p)) for p in include_paths)
    env = os.environ.copy()
    for name in ("AMENT_PREFIX_PATH", "CMAKE_PREFIX_PATH", "COLCON_PREFIX_PATH", "ROS_DISTRO", "ROS_VERSION", "ROS_PYTHON_VERSION", "PYTHONPATH"):
        env.pop(name, None)
    env["PATH"] = str(bin_dir) + os.pathsep + env["PATH"]
    # colcon's default host cmake packages use GCC; firmware uses the IDF
    # toolchain template and the sdkconfig generated by Windows.
    run(["make", "-s", "-j4", "-f", "libmicroros.mk", "SHELL=/bin/bash", ".SHELLFLAGS=-ec",
         "X_CC=" + str(compiler), "X_CXX=" + str(bin_dir / "xtensa-esp32s3-elf-g++"),
         "X_AR=" + str(bin_dir / "xtensa-esp32s3-elf-ar"),
         "C_STANDARD=" + str(request["c_standard"]), "MIDDLEWARE=" + request["middleware"],
         "BUILD_DIR=" + str(sdk_build), "IDF_INCLUDES=" + includes,
         "IDF_PATH=" + str(sdk_snapshot), "IDF_TARGET=" + request["target"],
         "APP_COLCON_META=" + str(local_path(request["app_meta"]) or ""),
         "EXTRA_ROS_PACKAGES=" + str(local_path(request["extra_packages"]))], cwd=cache, env=env)
    archive = cache / "libmicroros.a"
    headers = cache / "include"
    if not archive.is_file() or not (headers / "rcl/rcl/rcl.h").is_file() and not (headers / "rcl/rcl.h").is_file():
        raise RuntimeError("The upstream builder did not produce the complete archive/headers. Inspect the colcon log.")
    members = subprocess.check_output([str(bin_dir / "xtensa-esp32s3-elf-ar"), "t", str(archive)], text=True)
    if not members.strip():
        raise RuntimeError("The generated archive contains no objects.")
    output = project / "lib/micro_ros/jazzy"
    output.mkdir(parents=True, exist_ok=True)
    # Replace headers as a whole so removed packages cannot leave stale APIs.
    if (output / "include").is_dir():
        shutil.rmtree(output / "include")
    shutil.copytree(headers, output / "include")
    shutil.copyfile(archive, output / "libmicroros.a")
    (output / "ROS_DISTRO").write_text("jazzy\n")
    (output / "build_manifest.json").write_text(json.dumps({"fingerprint": stamp, "compiler": request["compiler_banner"], "target": request["target"]}, indent=2))
    repos = {}
    for git_dir in cache.glob("micro_ros_*/src/**/.git"):
        repos[str(git_dir.parent.relative_to(cache))] = subprocess.check_output(
            ["git", "-C", str(git_dir.parent), "rev-parse", "HEAD"], text=True).strip()
    (output / "source_commits.json").write_text(json.dumps(repos, indent=2))
    stamp_file.write_text(stamp)
    verify(request)
    print(f"Generated {output}/libmicroros.a ({len(members.splitlines())} objects).")
    print("Windows: idf.py -DMICROROS_PREPARE=OFF build")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("action", choices=("build", "verify"))
    parser.add_argument("request", type=Path)
    args = parser.parse_args()
    try:
        request = load_request(args.request)
        (build if args.action == "build" else verify)(request)
    except subprocess.CalledProcessError as error:
        print(f"micro-ROS: {Path(error.cmd[0]).name} failed with exit code {error.returncode}; see the command output above.", file=sys.stderr)
        return 1
    except (RuntimeError, OSError, ImportError) as error:
        print(f"micro-ROS: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
