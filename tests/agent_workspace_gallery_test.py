import importlib.util
import os
import subprocess
from pathlib import Path
import sys
import tempfile
import unittest

path = Path(__file__).resolve().parents[1] / 'tools' / 'agent_workspace_gallery.py'
spec = importlib.util.spec_from_file_location('gallery', path)
gallery = importlib.util.module_from_spec(spec)
sys.modules[spec.name] = gallery
spec.loader.exec_module(gallery)

A = 'special:omp-' + 'a' * 24
B = 'special:omp-' + 'b' * 24
C = 'special:omp-' + 'c' * 24


def client(address, workspace, title='terminal'):
    return {'address': address, 'workspace': {'name': workspace}, 'title': title,
            'class': 'kitty', 'pid': 100, 'mapped': True, 'hidden': False}


class GalleryTests(unittest.TestCase):
    def test_grouping_excludes_normal_and_owner_reserved_special_windows(self):
        cards = gallery.group_clients([
            client('0x1', '1'), client('0x2', 'special:magic'), client('0x3', 'special:pult'),
            client('0x4', B), client('0x5', A), client('0x6', A),
        ])
        self.assertEqual([card.name for card in cards], [A, B])
        self.assertEqual([item['address'] for item in cards[0].windows], ['0x5', '0x6'])

    def test_reported_omp_and_kitty_workspaces_are_not_lost_by_hash_only_filter(self):
        cards = gallery.group_clients([
            dict(client('0x558a47aa4f70', 'special:agent-tests', 'OMP'), **{'class': 'omp-workspace'}),
            dict(client('0x558a47bc4410', 'special:kitty'), **{'class': 'special-kitty'}),
            client('0x3', 'special:omp'), client('0x4', 'special:custom-name'),
        ])
        self.assertEqual([card.name for card in cards],
                         ['special:agent-tests', 'special:custom-name', 'special:kitty', 'special:omp'])
        self.assertEqual(gallery.selection_target('special:kitty', [client('0x1', 'special:kitty')]), 'kitty')

    def test_empty_or_non_special_workspace_names_are_not_gallery_targets(self):
        self.assertEqual(gallery.group_clients([client('0x1', 'special:'), client('0x2', 'kitty')]), [])

    def test_gallery_window_never_captures_or_counts_itself_on_a_special_workspace(self):
        own = dict(client('0x2', 'special:kitty'), **{'class': 'omp-agent-gallery'})
        cards = gallery.group_clients([client('0x1', 'special:kitty'), own])
        self.assertEqual([window['address'] for window in cards[0].windows], ['0x1'])

    def test_duplicate_titles_stay_distinct_by_exact_address(self):
        cards = gallery.group_clients([client('0x8', A), client('0x9', A)])
        self.assertEqual(len(cards[0].windows), 2)

    def test_invalid_or_unmapped_windows_cannot_become_capture_targets(self):
        unmapped = dict(client('0x1', A), mapped=False)
        cards = gallery.group_clients([unmapped, client('";bad', A), {'workspace': None}])
        self.assertEqual(cards, [])

    def test_hjkl_arrows_and_russian_physical_twins_have_equal_actions(self):
        for names, action in [(['h', 'H', 'р', 'Left'], 'left'),
                              (['j', 'J', 'о', 'Down'], 'down'),
                              (['k', 'K', 'л', 'Up'], 'up'),
                              (['l', 'L', 'д', 'Right'], 'right')]:
            for name in names:
                with self.subTest(name=name):
                    self.assertEqual(gallery.key_action(name), action)
        self.assertEqual(gallery.key_action('Return'), 'choose')
        self.assertEqual(gallery.key_action('Escape'), 'cancel')
        self.assertIsNone(gallery.key_action('q'))

    def test_horizontal_navigation_does_not_wrap_to_another_row(self):
        self.assertEqual(gallery.navigate(2, 7, 3, 'right'), 2)
        self.assertEqual(gallery.navigate(3, 7, 3, 'left'), 3)
        self.assertEqual(gallery.navigate(1, 7, 3, 'right'), 2)

    def test_vertical_navigation_reaches_incomplete_final_row_and_stops(self):
        self.assertEqual(gallery.navigate(5, 7, 3, 'down'), 6)
        self.assertEqual(gallery.navigate(6, 7, 3, 'down'), 6)
        self.assertEqual(gallery.navigate(0, 7, 3, 'up'), 0)
        self.assertEqual(gallery.navigate(4, 7, 3, 'up'), 1)
        self.assertEqual(gallery.navigate(0, 0, 3, 'down'), 0)

    def test_refresh_preserves_selected_identity_not_old_grid_index(self):
        state = gallery.GalleryState()
        state.update([client('0x2', B), client('0x3', C)])
        state.selected = 1
        state.update([client('0x1', A), client('0x2', B), client('0x3', C)])
        self.assertEqual(state.current.name, C)
        self.assertEqual(state.selected, 2)

    def test_disappearing_selection_clamps_and_empty_gallery_has_no_target(self):
        state = gallery.GalleryState()
        state.update([client('0x1', A), client('0x2', B)])
        state.selected = 1
        state.update([client('0x1', A)])
        self.assertEqual(state.current.name, A)
        state.update([])
        self.assertIsNone(state.current)

    def test_selection_revalidates_exact_workspace_and_does_not_open_reused_index(self):
        self.assertIsNone(gallery.selection_target(B, [client('0x1', A)]))
        self.assertEqual(gallery.selection_target(A, [client('0x1', A)]), 'omp-' + 'a' * 24)
        self.assertIsNone(gallery.selection_target('special:magic', [client('0x1', 'special:magic')]))

    def test_cancel_restores_focus_only_when_gallery_still_owns_it(self):
        self.assertTrue(gallery.should_restore({'pid': 12}, 12, '0x1', [client('0x1', '1')]))
        self.assertFalse(gallery.should_restore({'pid': 99}, 12, '0x1', [client('0x1', '1')]))
        self.assertFalse(gallery.should_restore({'pid': 12}, 12, '0x1', []))

    def test_thumbnail_bounds_preserve_portrait_landscape_and_small_window_aspect(self):
        self.assertEqual(gallery.thumbnail_size(1920, 1080), (300, 169))
        self.assertEqual(gallery.thumbnail_size(600, 1200), (90, 180))
        self.assertEqual(gallery.thumbnail_size(80, 40), (80, 40))


class GalleryRecoveryTests(unittest.TestCase):
    def test_missing_helper_does_not_create_or_modify_the_prefix(self):
        root = path.parents[1]
        with tempfile.TemporaryDirectory() as directory:
            prefix = Path(directory) / 'prefix'
            result = subprocess.run(['sh', str(root / 'integrations/agent-workspaces/install.sh'),
                                     str(Path(directory) / 'missing-helper')],
                                    env=dict(os.environ, PREFIX=str(prefix)), capture_output=True, text=True)
            self.assertEqual(result.returncode, 2, result.stderr)
            self.assertFalse(prefix.exists())

    def test_recovery_copies_standalone_bytes_and_preserves_live_configuration(self):
        root = path.parents[1]
        with tempfile.TemporaryDirectory() as directory:
            home = Path(directory)
            helper = home / 'built-helper'
            helper.write_bytes(b'owned helper fixture bytes\n')
            helper.chmod(0o755)
            config = home / '.config/hypr/hyprland.lua'
            config.parent.mkdir(parents=True)
            config.write_text('-- owner configuration\n')
            prefix = home / 'prefix'
            command = ['sh', str(root / 'integrations/agent-workspaces/install.sh'), str(helper)]
            for _ in range(2):
                result = subprocess.run(command, env=dict(os.environ, HOME=str(home), PREFIX=str(prefix)),
                                        capture_output=True, text=True)
                self.assertEqual(result.returncode, 0, result.stderr)
            self.assertEqual((prefix / 'bin/agent-workspace-gallery').read_bytes(), path.read_bytes())
            installed_helper = prefix / 'libexec/agent-workspace-toplevels'
            self.assertEqual(installed_helper.read_bytes(), helper.read_bytes())
            self.assertTrue(os.access(installed_helper, os.X_OK))
            for name in ['agent-app', 'agent-workspace.js', 'agent_workspace.lua', 'silent.lua',
                         'hypr-agent-guard', 'gallery.lua']:
                self.assertEqual((prefix / 'share/noctalia/agent-workspaces' / name).read_bytes(),
                                 (root / 'integrations/agent-workspaces' / name).read_bytes())
            self.assertEqual(config.read_text(), '-- owner configuration\n')



if __name__ == '__main__':
    unittest.main()
