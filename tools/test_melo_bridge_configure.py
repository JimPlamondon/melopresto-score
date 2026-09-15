#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# MuseScore-Studio-CLA-applies
# Copyright (C) 2026 Jim Plamondon
"""Exercise the MeloPresto bridge checkout-selection CMake contract without Rust builds.

Run with either ``python3`` or the project's ``.venv/bin/python``.  The default
checks the checked-in bridge configuration.  ``--expect-env-reset`` is provided
to demonstrate the historical behaviour with a saved older CMake file.
"""

from __future__ import annotations

import argparse
import os
import re
from pathlib import Path
import shutil
import stat
import subprocess
import sys
import tempfile
from typing import Optional


REPOSITORY_ROOT = Path(__file__).resolve().parents[1]
DEFAULT_BRIDGE_CMAKE = REPOSITORY_ROOT / "src/engraving/melo/SetupMeloBridge.cmake"


def fail(message: str) -> None:
    raise AssertionError(message)


def write_checkout(root: Path) -> None:
    crate = root / "Libraries/melo/crates/melo-musescore-bridge"
    (crate / "include").mkdir(parents=True)
    (crate / "src").mkdir()
    (crate / "src/lib.rs").write_text("initial-source\n", encoding="utf-8")
    (root / "Libraries/melo/.melo-configure-fixture").touch()
    (crate / "Cargo.toml").write_text(
        "[package]\nname = \"melo-musescore-bridge\"\nversion = \"0.0.0\"\n",
        encoding="utf-8",
    )


def write_fake_cargo(directory: Path) -> None:
    cargo = directory / "cargo"
    cargo.write_text(
        "#!/bin/sh\n"
        "set -eu\n"
        "if [ \"${1:-}\" = --version ]; then echo fixture-cargo; exit 0; fi\n"
        "test -f .melo-configure-fixture || { echo 'refusing non-fixture workspace' >&2; exit 1; }\n"
        "mkdir -p target/release\n"
        "cat crates/melo-musescore-bridge/src/lib.rs > target/release/libmelo_musescore_bridge.a\n",
        encoding="utf-8",
    )
    cargo.chmod(cargo.stat().st_mode | stat.S_IXUSR | stat.S_IXGRP | stat.S_IXOTH)


def write_fixture_project(directory: Path, bridge_cmake: Path) -> None:
    cmake_path = bridge_cmake.resolve().as_posix()
    (directory / "CMakeLists.txt").write_text(
        "cmake_minimum_required(VERSION 3.18)\n"
        "project(melo_bridge_configure_fixture C)\n"
        f"include(\"{cmake_path}\")\n"
        "add_library(consumer STATIC consumer.c)\n"
        "setup_melo_bridge(consumer)\n"
        "get_target_property(MELO_BRIDGE_LIBRARY melo_musescore_bridge IMPORTED_LOCATION)\n"
        "get_filename_component(SELECTED_ROOT \"${MELO_BRIDGE_LIBRARY}/../../../../..\" ABSOLUTE)\n"
        "file(WRITE \"${CMAKE_BINARY_DIR}/selected-root.txt\" \"${SELECTED_ROOT}\")\n",
        encoding="utf-8",
    )
    (directory / "consumer.c").write_text("void melo_bridge_fixture(void) {}\n", encoding="utf-8")


def configure(project: Path, build: Path, cargo_bin: Path, melo_root: Optional[Path], explicit_root: Optional[Path] = None, workflow_env: Optional[dict[str, str]] = None) -> subprocess.CompletedProcess[str]:
    environment = os.environ.copy()
    environment["PATH"] = str(cargo_bin) + os.pathsep + environment.get("PATH", "")
    if melo_root is None:
        environment.pop("MELO_ROOT", None)
    else:
        environment["MELO_ROOT"] = str(melo_root)

    if workflow_env is not None:
        environment.update(workflow_env)

    command = ["cmake", "-S", str(project), "-B", str(build)]
    command.append(f"-DCARGO_EXECUTABLE={cargo_bin / 'cargo'}")
    if explicit_root is not None:
        command.append(f"-DMELO_ROOT={explicit_root}")
    return subprocess.run(command, text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, env=environment)


def workflow_environment(workspace: Path, cargo_bin: Path) -> dict[str, str]:
    """Execute the actual workflow export step, then consume GITHUB_ENV as Actions does."""
    workflow = (REPOSITORY_ROOT / ".github/workflows/check_unit_tests.yml").read_text(encoding="utf-8")
    match = re.search(r"    - name: Point the build at the Kernel\n      run: \|\n((?:        .*\n|\n)+)", workflow)
    if match is None:
        fail("Kernel environment step was not found in the unit-test workflow")
    script = "\n".join(line[8:] for line in match.group(1).splitlines())
    script = script.replace("${{ github.workspace }}", str(workspace))
    environment_file = workspace / "github-env"
    environment = os.environ.copy()
    environment.pop("MELO_ROOT", None)
    environment.pop("JIMS_ROOT", None)
    environment["GITHUB_ENV"] = str(environment_file)
    environment["PATH"] = str(cargo_bin) + os.pathsep + environment.get("PATH", "")
    result = subprocess.run(["bash", "-eu", "-c", script], env=environment, text=True,
                            stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    require_success(result, "workflow Kernel environment export")
    return dict(line.split("=", 1) for line in environment_file.read_text().splitlines() if line)


def selected_root(build: Path) -> Path:
    result = build / "selected-root.txt"
    if not result.is_file():
        fail(f"configuration did not write {result}")
    return Path(result.read_text(encoding="utf-8")).resolve()


def require_success(result: subprocess.CompletedProcess[str], label: str) -> None:
    if result.returncode:
        fail(f"{label} failed:\n{result.stdout}")


def run_checks(bridge_cmake: Path, expect_env_reset: bool) -> None:
    if not shutil.which("cmake"):
        fail("cmake was not found on PATH")
    if not bridge_cmake.is_file():
        fail(f"bridge CMake file does not exist: {bridge_cmake}")

    with tempfile.TemporaryDirectory(prefix="melo-bridge-configure-") as temporary:
        temporary_path = Path(temporary)
        checkout_a = temporary_path / "checkout-a"
        checkout_b = temporary_path / "checkout-b"
        missing_checkout = temporary_path / "missing-checkout"
        write_checkout(checkout_a)
        write_checkout(checkout_b)

        cargo_bin = temporary_path / "bin"
        cargo_bin.mkdir()
        write_fake_cargo(cargo_bin)

        project = temporary_path / "fixture"
        project.mkdir()
        write_fixture_project(project, bridge_cmake)
        build = temporary_path / "build"

        workflow_checkout = temporary_path / "melo-kernel"
        write_checkout(workflow_checkout)
        workflow_env = workflow_environment(temporary_path, cargo_bin)
        workflow_build = temporary_path / "workflow-build"
        require_success(configure(project, workflow_build, cargo_bin, None, workflow_env=workflow_env),
                        "workflow-to-CMake selection")
        if selected_root(workflow_build) != workflow_checkout.resolve():
            fail("workflow did not select its checked-out Kernel")
        require_success(configure(project, workflow_build, cargo_bin, checkout_b),
                        "workflow cache survives changed environment")
        if selected_root(workflow_build) != workflow_checkout.resolve():
            fail("changed environment overrode the workflow's cached selection")


        require_success(configure(project, build, cargo_bin, checkout_a), "initial environment selection")
        if selected_root(build) != checkout_a.resolve():
            fail("initial MELO_ROOT environment selection was not recorded")

        if expect_env_reset:
            require_success(configure(project, build, cargo_bin, checkout_b), "historical environment-changed reconfiguration")
            if selected_root(build) != checkout_b.resolve():
                fail("historical setup did not replace the root from changed MELO_ROOT")
            return

        require_success(configure(project, build, cargo_bin, None), "environment-unset reconfiguration")
        unset_selection = selected_root(build)
        if unset_selection != checkout_a.resolve():
            fail(f"unexpected root after unsetting MELO_ROOT: {unset_selection}")

        require_success(configure(project, build, cargo_bin, checkout_b), "environment-changed reconfiguration")
        changed_selection = selected_root(build)
        if changed_selection != checkout_a.resolve():
            fail(f"unexpected root after changing MELO_ROOT: {changed_selection}")

        require_success(
            configure(project, build, cargo_bin, checkout_a, explicit_root=checkout_b),
            "explicit -DMELO_ROOT switch",
        )
        if selected_root(build) != checkout_b.resolve():
            fail("explicit -DMELO_ROOT switch did not replace the cached checkout")

        source = checkout_b / "Libraries/melo/crates/melo-musescore-bridge/src/lib.rs"
        source.write_text("changed-after-configuration\n", encoding="utf-8")
        build_result = subprocess.run(["cmake", "--build", str(build), "--target", "consumer"], text=True,
                                      stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        require_success(build_result, "build after a Kernel source edit")
        library = checkout_b / "Libraries/melo/target/release/libmelo_musescore_bridge.a"
        if library.read_text(encoding="utf-8") != source.read_text(encoding="utf-8"):
            fail("a normal Score build retained stale Kernel code after the selected source changed")

        missing_build = temporary_path / "missing-build"
        missing_result = configure(project, missing_build, cargo_bin, checkout_a, explicit_root=missing_checkout)
        if missing_result.returncode == 0:
            fail("missing checkout unexpectedly configured successfully")
        if "MeloPresto bridge crate not found" not in missing_result.stdout:
            fail(f"missing checkout did not produce the expected diagnostic:\n{missing_result.stdout}")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--bridge-cmake", type=Path, default=DEFAULT_BRIDGE_CMAKE,
                        help="bridge setup file to configure (default: checked-in file)")
    parser.add_argument("--expect-env-reset", action="store_true",
                        help="expect historical behavior that replaces the root from MELO_ROOT on reconfigure")
    arguments = parser.parse_args()
    try:
        run_checks(arguments.bridge_cmake, arguments.expect_env_reset)
    except AssertionError as error:
        print(f"FAIL: {error}", file=sys.stderr)
        return 1
    print("PASS: MeloPresto bridge checkout selection and missing-checkout diagnostics")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
