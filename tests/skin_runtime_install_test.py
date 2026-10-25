import contextlib
import importlib.util
import io
import json
import os
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch


source = Path(__file__).resolve().parents[1] / 'tools/install-skin-runtime.py'
spec = importlib.util.spec_from_file_location('skin_runtime_installer', source)
installer = importlib.util.module_from_spec(spec)
spec.loader.exec_module(installer)


class SkinRuntimeInstallTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.repo = self.root / 'source'
        self.prefix = self.root / 'prefix'
        self.plugins = self.root / 'plugins'
        (self.plugins / 'tmus').mkdir(parents=True)
        (self.plugins / 'tmus/owner-file').write_text('keep owner data\n')
        self.binary = self.root / 'built-noctalia'
        self.binary.write_bytes(b'owned binary fixture\n')
        self.binary.chmod(0o755)
        files = [Path('assets') / name for name in installer.ASSET_SENTINELS]
        files += [Path('integrations/skin-runtime') / name for name in installer.PLUGIN_ENTRY_FILES]
        files += [Path(*value) for value in [installer.TMUS_SKIN_SRC, installer.TMUS_PANEL_SRC,
                                           installer.TMUS_MANIFEST_SRC]]
        files += [Path('integrations/tmus') / name for name in installer.TMUS_RUNTIME_FILES]
        files += [Path('integrations/tmus/skin-assets/fixture.png')]
        for relative in files:
            path = self.repo / relative
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(('source fixture: ' + str(relative) + '\n').encode())

    def invoke(self):
        output = io.StringIO()
        with patch.dict(os.environ, {'XDG_DATA_HOME': str(self.root / 'data')}), contextlib.redirect_stdout(output):
            code = installer.main(['--source-root', str(self.repo), '--binary', str(self.binary),
                                   '--prefix', str(self.prefix), '--plugin-root', str(self.plugins)])
        return code, json.loads(output.getvalue())

    def test_installs_matching_panel_service_and_required_hint_module(self):
        code, report = self.invoke()
        self.assertEqual(code, 0, report['errors'])
        for name in ['desktop_skin.luau', 'panel.luau', 'plugin.toml', *installer.TMUS_RUNTIME_FILES]:
            target = self.plugins / 'tmus' / name
            self.assertEqual(target.read_bytes(), (self.repo / 'integrations/tmus' / name).read_bytes())
            self.assertFalse(target.is_symlink())
        self.assertEqual((self.plugins / 'tmus/owner-file').read_text(), 'keep owner data\n')
        record = json.loads((self.root / 'data/wallpaper-studio/native-runtime.json').read_text())
        self.assertEqual(record['binary'], str(self.prefix / 'bin/noctalia'))

    def test_missing_required_hint_module_fails_before_any_deployment(self):
        (self.repo / 'integrations/tmus/hint_navigation.luau').unlink()
        code, report = self.invoke()
        self.assertEqual(code, 1)
        self.assertTrue(any('hint_navigation.luau' in error for error in report['errors']))
        self.assertFalse(self.prefix.exists())
        self.assertFalse((self.root / 'data').exists())
        self.assertFalse((self.plugins / 'tmus/panel.luau').exists())
        self.assertEqual((self.plugins / 'tmus/owner-file').read_text(), 'keep owner data\n')


if __name__ == '__main__':
    unittest.main()
