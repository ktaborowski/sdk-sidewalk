# Copyright (c) 2026 Nordic Semiconductor ASA
# SPDX-License-Identifier: LicenseRef-Nordic-5-Clause

"""Workspace and artifact path helpers for west provision."""

from __future__ import annotations

from pathlib import Path
from typing import TYPE_CHECKING

if TYPE_CHECKING:
    from west.commands import WestCommand


def get_sidewalk_dir(cmd: WestCommand) -> Path:
    projects = cmd.manifest.get_projects(["sidewalk"])
    if projects:
        return Path(projects[0].abspath)
    return Path(cmd.manifest.topdir) / "sidewalk"


def get_workspace_dir(cmd: WestCommand) -> Path:
    return Path(cmd.manifest.topdir)


def default_provision_dir(cmd: WestCommand) -> Path:
    return get_workspace_dir(cmd) / ".sidewalk" / "provision"


def device_dir(provision_root: Path, wireless_device_id: str) -> Path:
    return provision_root / "devices" / wireless_device_id
