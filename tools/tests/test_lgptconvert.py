import importlib.util
from pathlib import Path
import unittest


REPOSITORY_ROOT = Path(__file__).resolve().parents[2]
MODULE_PATH = REPOSITORY_ROOT / "util" / "lgptconvert.py"
SPEC = importlib.util.spec_from_file_location("lgptconvert", MODULE_PATH)
assert SPEC is not None and SPEC.loader is not None
lgptconvert = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(lgptconvert)


class LgptInstrumentCapacityTests(unittest.TestCase):
    @staticmethod
    def make_project(references: list[str], definitions: list[str]):
        xml = lgptconvert.ET
        root = xml.Element("LITTLEGPTRACKER")
        xml.SubElement(root, "PROJECT")
        song = xml.SubElement(root, "SONG")
        for section_name in ("SONG", "CHAINS", "TRANSPOSES", "NOTES"):
            xml.SubElement(song, section_name)
        instruments = xml.SubElement(song, "INSTRUMENTS")
        instrument_data = xml.SubElement(instruments, "DATA")
        instrument_data.text = "".join(references)
        for section_name in ("COMMAND1", "PARAM1", "COMMAND2", "PARAM2"):
            xml.SubElement(song, section_name)
        bank = xml.SubElement(root, "INSTRUMENTBANK")
        for instrument_id in definitions:
            xml.SubElement(bank, "INSTRUMENT", attrib={"ID": instrument_id})
        xml.SubElement(root, "TABLES")
        return xml.ElementTree(root)

    def test_sample_and_midi_ranges_are_disjoint(self) -> None:
        used = {f"{index:02X}" for index in range(lgptconvert.sample_num)}
        used.update(
            {f"{index + 0x80:02X}" for index in range(lgptconvert.midi_num)}
        )

        mapping = lgptconvert.build_instrument_map(used)

        self.assertEqual(mapping["00"], "00")
        self.assertEqual(mapping["1F"], "1F")
        self.assertEqual(mapping["80"], "20")
        self.assertEqual(mapping["8F"], "2F")
        self.assertEqual(len(set(mapping.values())), 48)
        self.assertLess(max(int(value, 16) for value in mapping.values()), 0x40)

    def test_sample_overflow_is_rejected(self) -> None:
        used = {f"{index:02X}" for index in range(lgptconvert.sample_num + 1)}

        with self.assertRaisesRegex(ValueError, "sample instrument capacity"):
            lgptconvert.build_instrument_map(used)

    def test_conversion_persists_explicit_instrument_types(self) -> None:
        project = self.make_project(["00", "80"], ["00", "80"])

        converted = lgptconvert.ET.fromstring(lgptconvert.convert(project))
        instruments = converted.findall("./INSTRUMENTBANK/INSTRUMENT")

        self.assertEqual(
            [(item.attrib["ID"], item.attrib["TYPE"]) for item in instruments],
            [("00", "SAMPLE"), ("20", "MIDI")],
        )
        self.assertEqual(converted.findtext("./SONG/INSTRUMENTS/DATA"), "0020")

    def test_conversion_normalizes_lowercase_instrument_ids(self) -> None:
        project = self.make_project(["0a", "8f"], ["0A", "8F"])

        converted = lgptconvert.ET.fromstring(lgptconvert.convert(project))
        instruments = converted.findall("./INSTRUMENTBANK/INSTRUMENT")

        self.assertEqual(
            [(item.attrib["ID"], item.attrib["TYPE"]) for item in instruments],
            [("00", "SAMPLE"), ("2F", "MIDI")],
        )
        self.assertEqual(converted.findtext("./SONG/INSTRUMENTS/DATA"), "002F")

    def test_analysis_rejects_undefined_instrument_references(self) -> None:
        project = self.make_project(["00", "80"], ["00"])

        with self.assertRaisesRegex(ValueError, "undefined instrument"):
            lgptconvert.analyze(project)

    def test_analysis_rejects_instrument_without_id(self) -> None:
        project = self.make_project([], [])
        lgptconvert.ET.SubElement(project.find("INSTRUMENTBANK"), "INSTRUMENT")

        with self.assertRaisesRegex(ValueError, "missing an ID"):
            lgptconvert.analyze(project)

    def test_mapping_rejects_ids_outside_lgpt_midi_range(self) -> None:
        with self.assertRaisesRegex(ValueError, "outside the LGPT"):
            lgptconvert.build_instrument_map({"90"})

    def test_mapping_rejects_non_hexadecimal_ids(self) -> None:
        for instrument_id in ("+1", "-1", "G0"):
            with self.subTest(instrument_id=instrument_id):
                with self.assertRaisesRegex(ValueError, "not hexadecimal"):
                    lgptconvert.build_instrument_map({instrument_id})


if __name__ == "__main__":
    unittest.main()
