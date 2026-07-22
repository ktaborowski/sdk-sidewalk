# Copyright (c) 2026 Nordic Semiconductor ASA
# SPDX-License-Identifier: LicenseRef-Nordic-5-Clause

"""Artifact metadata for west provision."""

from __future__ import annotations

from dataclasses import asdict, dataclass
from datetime import datetime, timezone
from pathlib import Path
from typing import Any, Dict, List, Optional

import yaml


@dataclass
class DeviceMetadata:
    wireless_device_id: str
    board: str
    chip: str
    mfg_addr: str
    hex_file: str
    flash_tool: str
    created_at: str
    destination: Optional[str] = None
    device_profile_id: Optional[str] = None

    @classmethod
    def create(
        cls,
        wireless_device_id: str,
        board: str,
        chip: str,
        mfg_addr: int,
        hex_file: str,
        flash_tool: str,
        destination: Optional[str] = None,
        device_profile_id: Optional[str] = None,
    ) -> DeviceMetadata:
        return cls(
            wireless_device_id=wireless_device_id,
            board=board,
            chip=chip,
            mfg_addr=hex(mfg_addr),
            hex_file=hex_file,
            flash_tool=flash_tool,
            created_at=datetime.now(timezone.utc).isoformat(),
            destination=destination,
            device_profile_id=device_profile_id,
        )

    def save(self, device_dir: Path) -> None:
        device_dir.mkdir(parents=True, exist_ok=True)
        path = device_dir / "metadata.yaml"
        with path.open("w", encoding="utf-8") as fh:
            yaml.safe_dump(asdict(self), fh, sort_keys=False)

    @classmethod
    def load(cls, device_dir: Path) -> DeviceMetadata:
        path = device_dir / "metadata.yaml"
        if not path.is_file():
            raise FileNotFoundError(f"metadata.yaml not found in {device_dir}")
        with path.open(encoding="utf-8") as fh:
            data = yaml.safe_load(fh)
        return cls(**data)


def list_cached_devices(provision_root: Path) -> List[DeviceMetadata]:
    devices_root = provision_root / "devices"
    if not devices_root.is_dir():
        return []
    result: List[DeviceMetadata] = []
    for entry in sorted(devices_root.iterdir()):
        if not entry.is_dir():
            continue
        meta_path = entry / "metadata.yaml"
        if meta_path.is_file():
            result.append(DeviceMetadata.load(entry))
    return result


def save_state(provision_root: Path, state: Dict[str, Any]) -> None:
    provision_root.mkdir(parents=True, exist_ok=True)
    path = provision_root / "state.yaml"
    with path.open("w", encoding="utf-8") as fh:
        yaml.safe_dump(state, fh, sort_keys=False)
