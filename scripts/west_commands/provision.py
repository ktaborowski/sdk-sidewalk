# Copyright (c) 2026 Nordic Semiconductor ASA
# SPDX-License-Identifier: LicenseRef-Nordic-5-Clause

"""west provision — provision Nordic Sidewalk devices."""

from __future__ import annotations

import argparse
import subprocess
import sys
from pathlib import Path
from textwrap import dedent

_WEST_CMD_DIR = Path(__file__).resolve().parent
if str(_WEST_CMD_DIR) not in sys.path:
    sys.path.insert(0, str(_WEST_CMD_DIR))

_PROVISION_DIR = _WEST_CMD_DIR.parents[1] / "tools" / "provision"
if str(_PROVISION_DIR) not in sys.path:
    sys.path.insert(0, str(_PROVISION_DIR))

from west.commands import WestCommand  # noqa: E402

from artifacts import DeviceMetadata, list_cached_devices, save_state  # noqa: E402
from aws_resources import (  # noqa: E402
    AwsProvisionError,
    create_wireless_device,
    ensure_credentials,
    ensure_destination,
    ensure_device_profile,
    ensure_iam_role,
    save_json,
)
from flash import FlashError, flash_mfg_hex  # noqa: E402
from mfg import MfgGenerateError, generate_mfg_hex  # noqa: E402
from paths import default_provision_dir, device_dir, get_sidewalk_dir  # noqa: E402
from sid_provision.boards import get_board_config, list_boards  # noqa: E402


class Provision(WestCommand):
    def __init__(self) -> None:
        super().__init__(
            "provision",
            "",
            description=dedent(
                """
                Provision a Nordic Sidewalk device for development.

                Creates AWS IoT Wireless resources (destination, device profile,
                wireless device), generates an MFG hex via tools/provision, and
                optionally flashes the manufacturing page.

                Application firmware is flashed separately with 'west flash'.
                """
            ),
        )

    def do_add_parser(self, parser_adder):
        parser = parser_adder.add_parser(
            self.name,
            description=self.description,
            formatter_class=argparse.RawDescriptionHelpFormatter,
        )
        parser.add_argument(
            "-b",
            "--board",
            help="West board name (same as west build -b)",
        )
        parser.add_argument(
            "--device-id",
            help="Wireless device UUID — reflash cached MFG hex from .sidewalk/provision/devices/<id>/",
        )
        parser.add_argument(
            "--list-devices",
            action="store_true",
            help="List cached wireless device IDs and artifact paths",
        )
        parser.add_argument(
            "--aws-profile",
            default="default",
            help="AWS profile (default: default)",
        )
        parser.add_argument(
            "--destination",
            default="SidewalkDevDestination",
            help="IoT Wireless destination name (default: SidewalkDevDestination)",
        )
        parser.add_argument(
            "--device-profile-id",
            help="Use existing device profile ID instead of creating one",
        )
        parser.add_argument(
            "--device-profile-name",
            help="Name for a new device profile when --device-profile-id is not set",
        )
        parser.add_argument(
            "--mqtt-topic",
            default="sidewalk/dev/#",
            help="MQTT topic expression for the destination (default: sidewalk/dev/#)",
        )
        parser.add_argument(
            "--role-name",
            default="SidewalkDevDestinationRole",
            help="IAM role for the destination (default: SidewalkDevDestinationRole)",
        )
        parser.add_argument(
            "--flash",
            action="store_true",
            help="Flash MFG hex after provision or when used with --device-id",
        )
        parser.add_argument(
            "--hex-file",
            help="Override MFG hex path (bypass cache lookup)",
        )
        parser.add_argument(
            "--snr",
            help="J-Link serial number for nrfutil / nrfjprog",
        )
        parser.add_argument(
            "--output-dir",
            type=Path,
            help="Provision artifact directory (default: <workspace>/.sidewalk/provision)",
        )
        return parser

    def do_run(self, args, unknown):
        provision_root = args.output_dir or default_provision_dir(self)

        if args.list_devices:
            self._list_devices(provision_root)
            return

        if args.device_id:
            self._reflash_device(args, provision_root)
            return

        if not args.board:
            self.err(
                "Provide -b/--board for provisioning, --device-id to reflash, or --list-devices.")
            sys.exit(1)

        self._provision_new_device(args, provision_root)

    def _list_devices(self, provision_root: Path) -> None:
        devices = list_cached_devices(provision_root)
        if not devices:
            self.inf(f"No cached devices under {provision_root / 'devices'}")
            return
        for meta in devices:
            dev_dir = device_dir(provision_root, meta.wireless_device_id)
            self.inf(
                f"{meta.wireless_device_id}  board={meta.board}  "
                f"hex={dev_dir / meta.hex_file}  created={meta.created_at}"
            )

    def _reflash_device(self, args, provision_root: Path) -> None:
        if not args.flash:
            self.err("--device-id requires --flash for reflash mode.")
            sys.exit(1)

        dev_path = device_dir(provision_root, args.device_id)
        try:
            meta = DeviceMetadata.load(dev_path)
        except FileNotFoundError as exc:
            self.err(str(exc))
            sys.exit(1)

        board_cfg = get_board_config(meta.board)
        hex_path = Path(
            args.hex_file) if args.hex_file else dev_path / meta.hex_file

        try:
            flash_mfg_hex(board_cfg, hex_path, snr=args.snr)
        except FlashError as exc:
            self.err(str(exc))
            sys.exit(1)

        self.inf(f"Flashed {hex_path} to {meta.board}")

    def _provision_new_device(self, args, provision_root: Path) -> None:
        try:
            board_cfg = get_board_config(args.board)
        except KeyError as exc:
            self.err(str(exc))
            self.inf(f"Supported boards: {', '.join(list_boards())}")
            sys.exit(1)

        sidewalk_dir = get_sidewalk_dir(self)

        try:
            session = ensure_credentials(args.aws_profile)
            role_arn = ensure_iam_role(
                session, args.role_name, args.mqtt_topic)
            ensure_destination(session, args.destination,
                               role_arn, args.mqtt_topic)
            profile_id, profile_data = ensure_device_profile(
                session,
                args.device_profile_id,
                args.device_profile_name,
            )
            wireless_id, device_data, profile_data = create_wireless_device(
                session,
                args.destination,
                profile_id,
            )
        except AwsProvisionError as exc:
            self.err(str(exc))
            sys.exit(1)

        dev_path = device_dir(provision_root, wireless_id)
        save_json(dev_path / "WirelessDevice.json", device_data)
        save_json(dev_path / "DeviceProfile.json", profile_data)

        hex_name = f"nordic_aws_{board_cfg.chip}.hex"
        hex_path = dev_path / hex_name
        bin_path = dev_path / f"nordic_aws_{board_cfg.chip}.bin"

        try:
            generate_mfg_hex(
                sidewalk_dir=sidewalk_dir,
                board_cfg=board_cfg,
                wireless_device_json=dev_path / "WirelessDevice.json",
                device_profile_json=dev_path / "DeviceProfile.json",
                output_hex=hex_path,
                output_bin=bin_path,
            )
        except (MfgGenerateError, subprocess.CalledProcessError) as exc:
            self.err(f"MFG generation failed: {exc}")
            sys.exit(1)

        meta = DeviceMetadata.create(
            wireless_device_id=wireless_id,
            board=board_cfg.board,
            chip=board_cfg.chip,
            mfg_addr=board_cfg.mfg_addr,
            hex_file=hex_name,
            flash_tool=board_cfg.flash_tool,
            destination=args.destination,
            device_profile_id=profile_id,
        )
        meta.save(dev_path)

        save_state(
            provision_root,
            {
                "aws_profile": args.aws_profile,
                "destination": args.destination,
                "device_profile_id": profile_id,
                "role_name": args.role_name,
                "mqtt_topic": args.mqtt_topic,
            },
        )

        self.inf(f"Wireless device ID: {wireless_id}")
        self.inf(f"MFG hex: {hex_path}")

        if args.flash:
            try:
                flash_mfg_hex(board_cfg, hex_path, snr=args.snr)
            except FlashError as exc:
                self.err(str(exc))
                sys.exit(1)
            self.inf(f"Flashed MFG page to {board_cfg.board}")
