"""Exercise the iOS release header and plist preprocessing without an Xcode build."""
import pathlib
import plistlib
import shutil
import subprocess
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]


class ProductReleaseTests(unittest.TestCase):
    def test_shared_version_and_build_reach_bundle(self):
        with tempfile.TemporaryDirectory() as directory:
            root = pathlib.Path(directory)
            (root / 'ios/scripts').mkdir(parents=True)
            (root / 'sources').mkdir()
            script = root / 'ios/scripts/product-version.sh'
            shutil.copyfile(ROOT / 'ios/scripts/product-version.sh', script)
            (root / 'sources/ProductVersion.h').write_text(
                'inline constexpr char Version[] = "1.2.3";\n'
                'inline constexpr unsigned Build = 42;\n')
            header = root / 'generated.h'
            subprocess.run(['bash', str(script), str(header)], check=True)
            result = subprocess.run(['clang', '-E', '-P', '-x', 'c', '-include', str(header),
                                     str(ROOT / 'ios/NullPeratorIOS/Info.plist')],
                                    check=True, capture_output=True)
            plist = plistlib.loads(result.stdout)
            self.assertEqual(plist['CFBundleShortVersionString'], '1.2.3')
            self.assertEqual(plist['CFBundleVersion'], '42')
            (root / 'sources/ProductVersion.h').write_text(
                'inline constexpr char Version[] = "1.2.3";\n'
                'inline constexpr unsigned Build = 0;\n')
            self.assertNotEqual(subprocess.run(['bash', str(script), str(header)],
                                              capture_output=True).returncode, 0)


if __name__ == '__main__':
    unittest.main()
