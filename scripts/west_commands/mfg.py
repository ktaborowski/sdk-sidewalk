# Copyright (c) 2026 Nordic Semiconductor ASA
# SPDX-License-Identifier: LicenseRef-Nordic-5-Clause

"""Invoke tools/provision/provision.py to generate MFG pages."""

from __future__ import annotations

import subprocess
import sys
from pathlib import Path
from typing import Optional

_PROVISION_DIR = Path(__file__).resolve().parents[2] / "tools" / "provision"
if str(_PROVISION_DIR) not in sys.path:
    sys.path.insert(0, str(_PROVISION_DIR))

from sid_provision.boards import BoardConfig  # noqa: E402


class MfgGenerateError(Exception):
    pass


def generate_mfg_hex(
    sidewalk_dir: Path,
    board_cfg: BoardConfig,
    wireless_device_json: Path,
    device_profile_json: Path,
    output_hex: Path,
    output_bin: Optional[Path] = None,
) -> Path:
    provision_py = sidewalk_dir / "tools" / "provision" / "provision.py"
    if not provision_py.is_file():
        raise MfgGenerateError(f"provision.py not found: {provision_py}")

    cmd = [
        sys.executable,
        str(provision_py),
        "nordic",
        "aws",
        "--board",
        board_cfg.board,
        "--wireless_device_json",
        str(wireless_device_json),
        "--device_profile_json",
        str(device_profile_json),
        "--output_hex",
        str(output_hex),
    ]
    if output_bin is not None:
        cmd.extend(["--output_bin", str(output_bin)])

    subprocess.run(cmd, check=True, cwd=str(provision_py.parent))
    if not output_hex.is_file():
        raise MfgGenerateError(
            f"Expected MFG hex was not created: {output_hex}")
    return output_hex
