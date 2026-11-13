"""Wiring guards complement C++ policy tests; not a rendering/GPU test."""
from pathlib import Path
import configparser
import unittest

ROOT = Path(__file__).resolve().parents[1]


class NativeContract(unittest.TestCase):
    def test_mo2_profile_preserves_native_quality(self):
        config = configparser.ConfigParser()
        config.read(ROOT / 'profiles/SE1597-Native-DLAA.ini')
        cfg = config['FrameGeneration']
        self.assertTrue(cfg.getboolean('Enable'))
        self.assertFalse(cfg.getboolean('FrameGeneration'))
        self.assertTrue(cfg.getboolean('EnableUpscale'))
        self.assertEqual(cfg.getint('QualityMode'), 0)
        self.assertEqual(cfg.getfloat('RenderResolutionScale'), 1)
        self.assertEqual(cfg.getfloat('NRRenderScale'), 1)
        self.assertEqual(cfg.getint('NRLayers'), 1)

    def test_no_runtime_identity_spoof(self):
        source = (ROOT / 'src/NeuralRendering/Runtime.cpp').read_text()
        self.assertNotIn('SignedRuntimePathScope', source)
        self.assertNotIn('VirtualProtect', source)
        self.assertIn('WinVerifyTrust', source)
        self.assertLess(source.index('WinVerifyTrust'), source.index('module_ = LoadLibraryExW'))

    def test_native_pipeline_is_live(self):
        source = (ROOT / 'src/NativeDlaa.cpp').read_text()
        self.assertIn('sl::DLSSMode::eDLAA', (ROOT / 'src/Streamline.cpp').read_text())
        self.assertIn('InstallNativeDlaaHooks();', (ROOT / 'src/main.cpp').read_text())
        self.assertIn('context->SwapDeviceContextState(saved.Get(), nullptr)', source)
        self.assertIn('if (ok) d3d11Context->CopyResource', source)


if __name__ == '__main__':
    unittest.main()
