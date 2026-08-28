import tempfile
import unittest
from pathlib import Path

import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))

import starfox_enhanced_compare as compare
import starfox_enhanced_symbols as symbols


class StarFoxEnhancedSymbolsTests(unittest.TestCase):
    def test_parse_common_symbol_formats(self):
        text = """
        $01:AC1D RenderObjects
        LoadAudio = $03B109
        $83:8123 HighMirror
        03C000 BankFirst
        IgnoredLowRam = $001234
        """

        parsed = symbols.parse_symbols_text(text)
        labels = {(entry.name, entry.bank, entry.addr) for entry in parsed}

        self.assertIn(("RenderObjects", 0x01, 0xAC1D), labels)
        self.assertIn(("LoadAudio", 0x03, 0xB109), labels)
        self.assertIn(("HighMirror", 0x03, 0x8123), labels)
        self.assertIn(("BankFirst", 0x03, 0xC000), labels)
        self.assertIn(("IgnoredLowRam", 0x00, 0x1234), labels)
        self.assertEqual(
            "constant_or_direct",
            next(entry.space for entry in parsed if entry.name == "IgnoredLowRam"),
        )

    def test_compare_reports_matching_address_name(self):
        symbol_entries = symbols.parse_symbols_text("LoadAudio = $03B109\n")
        cfg_funcs = [
            compare.CfgFunc(
                name="LoadAudio",
                bank=0x03,
                addr=0xB109,
                source="recomp/bank03.cfg",
                source_line=6,
            )
        ]

        result = compare.compare(symbol_entries, cfg_funcs)

        self.assertEqual(1, len(result["matched"]))
        self.assertEqual([], result["name_mismatches"])
        self.assertEqual([], result["missing_cfg"])
        self.assertEqual([], result["cfg_only"])

    def test_filter_symbols_can_select_rom_only(self):
        parsed = symbols.parse_symbols_text(
            "DoThing = $03B109\n"
            "RamThing = $7E81EF\n"
            "ConstantThing = $000003\n"
        )

        rom_only = symbols.filter_symbols(parsed, {"rom"})

        self.assertEqual(["DoThing"], [entry.name for entry in rom_only])

    def test_load_cfg_funcs_reads_bank_context(self):
        with tempfile.TemporaryDirectory() as temp_dir:
            root = Path(temp_dir)
            recomp = root / "recomp"
            recomp.mkdir()
            (recomp / "bank03.cfg").write_text(
                "bank = 3\nfunc LoadAudio b109 end:b269\n",
                encoding="utf-8",
            )

            funcs = compare.load_cfg_funcs(root)

        self.assertEqual(1, len(funcs))
        self.assertEqual(("LoadAudio", 0x03, 0xB109), (funcs[0].name, funcs[0].bank, funcs[0].addr))


if __name__ == "__main__":
    unittest.main()
