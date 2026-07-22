# Copyright (c) 2026 Nordic Semiconductor ASA
# SPDX-License-Identifier: LicenseRef-Nordic-5-Clause

"""Flash MFG hex images to Nordic boards."""

from __future__ import annotations

import shutil
import subprocess
import sys
from pathlib import Path
from typing import Optional

# Allow importing board config from provision tool.
_PROVISION_DIR = Path(__file__).resolve().parents[2] / "tools" / "provision"
if str(_PROVISION_DIR) not in sys.path:
    sys.path.insert(0, str(_PROVISION_DIR))

from sid_provision.boards import BoardConfig  # noqa: E402


class FlashError(Exception):
    pass


def flash_mfg_hex(
    board_cfg: BoardConfig,
    hex_path: Path,
    snr: Optional[str] = None,
) -> None:
    if not hex_path.is_file():
        raise FlashError(f"MFG hex not found: {hex_path}")

    if board_cfg.flash_tool == "nrfutil":
        _flash_nrfutil(hex_path, board_cfg.nrfutil_family or "nrf54l", snr)
    elif board_cfg.flash_tool == "nrfjprog":
        _flash_nrfjprog(hex_path, snr)
    else:
        raise FlashError(f"Unsupported flash tool: {board_cfg.flash_tool}")


def _flash_nrfutil(hex_path: Path, family: str, snr: Optional[str]) -> None:
    if shutil.which("nrfutil") is None:
        raise FlashError(
            "nrfutil not found in PATH. Install nRF Util / nRF Connect.")

    cmd = [
        "nrfutil",
        "device",
        "program",
        "--x-family",
        family,
        "--options",
        "chip_erase_mode=ERASE_RANGES_TOUCHED_BY_FIRMWARE,reset=RESET_PIN,verify=VERIFY_READ",
        "--traits",
        "jlink",
        "--firmware",
        str(hex_path),
    ]
    if snr:
        cmd.extend(["--serial-number", snr])
    subprocess.run(cmd, check=True)


def _flash_nrfjprog(hex_path: Path, snr: Optional[str]) -> None:
    if shutil.which("nrfjprog") is None:
        raise FlashError(
            "nrfjprog not found in PATH. Install nRF Command Line Tools.")

    cmd = ["nrfjprog", "--program", str(hex_path), "--sectorerase", "--reset"]
    if snr:
        cmd.extend(["--snr", snr])
    subprocess.run(cmd, check=True)
