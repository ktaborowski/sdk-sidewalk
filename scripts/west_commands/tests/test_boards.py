# Copyright (c) 2026 Nordic Semiconductor ASA
# SPDX-License-Identifier: LicenseRef-Nordic-5-Clause

import sys
import unittest
from pathlib import Path

_PROVISION_DIR = Path(__file__).resolve().parents[3] / "tools" / "provision"
sys.path.insert(0, str(_PROVISION_DIR))

from sid_provision.boards import BOARD_CONFIGS, get_board_config, list_boards  # noqa: E402


class TestBoards(unittest.TestCase):
    def test_nrf54l15_board(self):
        cfg = get_board_config("nrf54l15dk/nrf54l15/cpuapp")
        self.assertEqual(cfg.chip, "nrf54l15")
        self.assertEqual(cfg.mfg_addr, 0x17C000)
        self.assertEqual(cfg.flash_tool, "nrfutil")

    def test_nrf52840_board(self):
        cfg = get_board_config("nrf52840dk/nrf52840")
        self.assertEqual(cfg.chip, "nrf52840")
        self.assertEqual(cfg.mfg_addr, 0xFD000)
        self.assertEqual(cfg.flash_tool, "nrfjprog")

    def test_unknown_board_raises(self):
        with self.assertRaises(KeyError):
            get_board_config("unknown/board")

    def test_list_boards_matches_config(self):
        self.assertEqual(len(list_boards()), len(BOARD_CONFIGS))


if __name__ == "__main__":
    unittest.main()
