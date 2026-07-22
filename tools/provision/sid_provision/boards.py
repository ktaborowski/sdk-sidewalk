#!/usr/bin/env python3
#
# Copyright 2023 Amazon.com, Inc. or its affiliates. All Rights Reserved.
# SPDX-License-Identifier: MIT-0
#
"""West board name to Sidewalk MFG page configuration."""

from __future__ import annotations

from dataclasses import dataclass
from typing import Dict, List, Optional


@dataclass(frozen=True)
class BoardConfig:
    board: str
    chip: str
    mfg_addr: int
    flash_tool: str  # "nrfutil" or "nrfjprog"
    nrfutil_family: Optional[str] = None  # e.g. "nrf54l"


BOARD_CONFIGS: Dict[str, BoardConfig] = {
    "nrf54l15dk/nrf54l15/cpuapp": BoardConfig(
        board="nrf54l15dk/nrf54l15/cpuapp",
        chip="nrf54l15",
        mfg_addr=0x17C000,
        flash_tool="nrfutil",
        nrfutil_family="nrf54l",
    ),
    "nrf54l15dk/nrf54l15/cpuapp/ns": BoardConfig(
        board="nrf54l15dk/nrf54l15/cpuapp/ns",
        chip="nrf54l15",
        mfg_addr=0x17C000,
        flash_tool="nrfutil",
        nrfutil_family="nrf54l",
    ),
    "nrf54l15dk/nrf54l10/cpuapp": BoardConfig(
        board="nrf54l15dk/nrf54l10/cpuapp",
        chip="nrf54l10",
        mfg_addr=0xFC000,
        flash_tool="nrfutil",
        nrfutil_family="nrf54l",
    ),
    "nrf54lv10dk/nrf54lv10a/cpuapp": BoardConfig(
        board="nrf54lv10dk/nrf54lv10a/cpuapp",
        chip="nrf54l10",
        mfg_addr=0xFC000,
        flash_tool="nrfutil",
        nrfutil_family="nrf54l",
    ),
    "nrf54lm20dk/nrf54lm20a/cpuapp": BoardConfig(
        board="nrf54lm20dk/nrf54lm20a/cpuapp",
        chip="nrf54lm20",
        mfg_addr=0x1DE000,
        flash_tool="nrfutil",
        nrfutil_family="nrf54l",
    ),
    "nrf54lm20dk/nrf54lm20b/cpuapp": BoardConfig(
        board="nrf54lm20dk/nrf54lm20b/cpuapp",
        chip="nrf54lm20",
        mfg_addr=0x1DE000,
        flash_tool="nrfutil",
        nrfutil_family="nrf54l",
    ),
    "nrf52840dk/nrf52840": BoardConfig(
        board="nrf52840dk/nrf52840",
        chip="nrf52840",
        mfg_addr=0xFD000,
        flash_tool="nrfjprog",
        nrfutil_family=None,
    ),
}


def get_board_config(board: str) -> BoardConfig:
    """Return board configuration or raise KeyError with supported boards."""
    try:
        return BOARD_CONFIGS[board]
    except KeyError as exc:
        supported = ", ".join(sorted(BOARD_CONFIGS))
        raise KeyError(f"Unsupported board '{board}'. Supported: {supported}") from exc


def list_boards() -> List[str]:
    return sorted(BOARD_CONFIGS)
