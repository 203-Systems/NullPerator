import importlib.util
from pathlib import Path
import unittest

spec = importlib.util.spec_from_file_location("architecture", Path(__file__).parents[1] / "check_architecture.py")
architecture = importlib.util.module_from_spec(spec)
spec.loader.exec_module(architecture)


class ArchitectureTests(unittest.TestCase):
    def test_core_concrete_includes_are_rejected(self):
        for header in ['Adapters/wasm/a.h', 'esp_heap_caps.h', 'platform.h', '../../Adapters/ios/a.h']:
            self.assertTrue(architecture.violations('Application/Model/a.cpp', f'#include "{header}"'))

    def test_relative_cross_adapter_includes_are_rejected(self):
        self.assertTrue(architecture.violations('Adapters/ios/a.cpp', '#include "../wasm/a.h"'))

    def test_common_does_not_hide_target_branches(self):
        self.assertTrue(architecture.violations('Adapters/common/a.cpp', '#ifdef __EMSCRIPTEN__'))

    def test_audio_cannot_import_application_policy_by_relative_path(self):
        for header in ['Application/Model/Config.h', '../../Application/Model/Config.h']:
            self.assertTrue(architecture.violations('Services/Audio/a.cpp', f'#include "{header}"'))

    def test_shared_contracts_and_explicit_posix_reuse_are_allowed(self):
        self.assertFalse(architecture.violations('Application/Model/a.cpp', '#include "System/Memory/Memory.h"'))
        self.assertFalse(architecture.violations('Adapters/ios/a.cpp', '#include "Adapters/posix/filesystem/PosixFile.h"'))

    def test_cmake_cannot_reintroduce_cross_adapter_sources(self):
        self.assertTrue(architecture.violations('Adapters/ios/CMakeLists.txt', '${PROJECT_SOURCE_DIR}/Adapters/wasm/a.cpp'))
        self.assertTrue(architecture.violations('Adapters/wasm/CMakeLists.txt', 'add_compile_definitions(NODE)'))


if __name__ == '__main__':
    unittest.main()
