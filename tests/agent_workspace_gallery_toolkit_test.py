"""Native GTK3 bootstrap regression; run explicitly on the gallery's GTK/PyGObject host."""
import os
from pathlib import Path
import subprocess
import sys
import unittest


class GalleryToolkitTests(unittest.TestCase):
    def test_toolkit_bootstrap_uses_matching_gtk_and_gdk_without_a_display(self):
        tools = Path(__file__).resolve().parents[1] / 'tools'
        code = ('import sys; sys.path.insert(0, ' + repr(str(tools)) + '); '
                'from agent_workspace_gallery import gtk_modules; '
                'Gdk, _, _, Gtk, _ = gtk_modules(); print(Gtk.MAJOR_VERSION, Gdk._version)')
        env = dict(os.environ, WAYLAND_DISPLAY='omp-gallery-no-display', GDK_BACKEND='wayland',
                   NO_AT_BRIDGE='1', DBUS_SESSION_BUS_ADDRESS='unix:path=/nonexistent/omp-gallery-test')
        env.pop('DISPLAY', None)
        result = subprocess.run([sys.executable, '-c', code], env=env, capture_output=True, text=True, timeout=5)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stdout.strip(), '3 3.0')


if __name__ == '__main__':
    unittest.main()
