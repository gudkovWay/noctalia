"""Opt-in finite native scenarios; guarded nested display or explicitly authorized owned live gallery."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys
import time
import unittest

ROOT = Path(__file__).resolve().parents[1]


def run(command, **kwargs):
    return subprocess.run(command, check=True, capture_output=True, text=True, timeout=5, **kwargs).stdout


def ready(probe, predicate, label):
    until = time.monotonic() + 10
    while time.monotonic() < until:
        value = probe()
        if predicate(value):
            return value
        time.sleep(0.1)
    raise AssertionError('not ready: ' + label)


class NativeGallery(unittest.TestCase):
    def test_hidden_previews_navigation_selection_cancel_and_toggle(self):
        root = Path(ARGS.output)
        signature = os.environ['HYPRLAND_INSTANCE_SIGNATURE']
        self.assertNotEqual(signature, os.environ['OMP_GALLERY_PARENT'])
        instance = next(item for item in json.loads(run(['hyprctl', '-j', 'instances'])) if item['instance'] == signature)
        cmdline = Path('/proc/%s/cmdline' % instance['pid']).read_bytes().split(b'\0')
        self.assertIn(str(root / 'hyprland.lua').encode(), cmdline)
        parent = next(line.split()[1] for line in Path('/proc/%s/status' % instance['pid']).read_text().splitlines() if line.startswith('PPid:'))
        self.assertEqual(int(parent), os.getppid())
        ctl = ['hyprctl', '-i', signature]
        records = {'signature': signature, 'source_revision': '1d664a3b + dirty hashes', 'scenarios': [],
                   'hashes': {str(p.relative_to(ROOT)): hashlib.sha256(p.read_bytes()).hexdigest() for p in
                              [ROOT / 'meson.build', ROOT / 'tools/agent_workspace_gallery.py',
                               ROOT / 'src/capture/toplevel_identity_cli.cpp', Path(ARGS.helper)]}}
        children, logs = [], []

        def get(name):
            return json.loads(run(ctl + ['-j', name]))

        def dispatch(expression):
            self.assertEqual(run(ctl + ['dispatch', expression]).strip(), 'ok')

        def start(command, name, env=None):
            log = (root / (name + '.log')).open('w')
            logs.append(log)
            process = subprocess.Popen(command, env=env, stdout=log, stderr=log)
            children.append(process)
            return process

        def client(process):
            return ready(lambda: next((w for w in get('clients') if w['pid'] == process.pid), None), bool,
                         'window pid ' + str(process.pid))

        def key(window, name):
            dispatch('hl.dsp.send_shortcut({mods="",key=%s,window="address:%s"})' %
                     (json.dumps(name), window['address']))

        def capture(address, name):
            mapping = json.loads(run([ARGS.helper, address]))
            self.assertIn(address, mapping, 'missing exact native toplevel identity')
            path = root / (name + '.png')
            result = subprocess.run(['grim', '-T', mapping[address], '-s', '0.5', str(path)],
                                    check=True, capture_output=True, timeout=3)
            self.assertTrue(path.exists())
            self.assertGreater(path.stat().st_size, 100)
            records.setdefault('frames', []).append({'file': str(path), 'sha256': hashlib.sha256(path.read_bytes()).hexdigest()})

        try:
            baseline = start(['kitty', '--config', 'NONE', '--session', 'none', '--single-instance=no',
                '--class', 'gallery-background', '--title', 'Owned gallery baseline', '-e', 'sleep', '120'], 'baseline')
            base = client(baseline)
            dispatch('hl.dsp.focus({window="address:%s"})' % base['address'])
            agent_windows = []
            for letter in 'abcd':
                name = 'special:omp-' + letter * 24
                env = dict(os.environ, OMP_GUI_KEY=letter * 24)
                process = start(['kitty', '--config', 'NONE', '--session', 'none', '--single-instance=no',
                    '--class', 'agent-test', '--title', 'Owned agent ' + letter,
                    '-e', 'sh', '-c', 'printf "Agent %s — native gallery preview\\n"; sleep 120' % letter], 'agent-' + letter, env)
                item = ready(lambda: next((w for w in get('clients') if w['pid'] == process.pid), None),
                             lambda w: w and w['workspace']['name'] == name, 'hidden agent ' + letter)
                agent_windows.append(item)
            self.assertEqual(get('activewindow')['address'], base['address'])
            self.assertTrue(all((m.get('specialWorkspace') or {}).get('name', '') == '' for m in get('monitors')))
            capture(agent_windows[0]['address'], 'hidden-agent')
            records['scenarios'].append('native per-window snapshot of hidden agent, no workspace exposure or moves')

            def open_gallery(label):
                process = start([sys.executable, str(ROOT / 'tools/agent_workspace_gallery.py'), '--helper', ARGS.helper], label)
                item = client(process)
                ready(lambda: get('activewindow'), lambda w: w.get('pid') == process.pid, 'gallery focus')
                self.assertEqual(Path('/proc/%s/cgroup' % process.pid).read_text(), Path('/proc/self/cgroup').read_text())
                item = ready(lambda: next((w for w in get('clients') if w['pid'] == process.pid), None),
                             lambda w: w and 'снимков: 4' in w.get('title', ''), 'four decoded gallery previews')
                return process, item

            process, view = open_gallery('gallery-cancel')
            capture(view['address'], 'gallery')
            key(view, 'Escape')
            process.wait(timeout=5)
            self.assertEqual(get('activewindow')['address'], base['address'])
            records['scenarios'].append('Escape closes gallery and restores previous focused window')

            columns = max(1, min(3, (int(view['size'][0]) - 40) // 340))
            for label, keys, expected in [
                ('vim-right', ['l'], 1),
                ('vim-left', ['l', 'h'], 0),
                ('vim-down', ['j'], columns),
                ('vim-up', ['j', 'k'], 0),
                ('arrow-right', ['Right'], 1),
            ]:
                process, view = open_gallery(label)
                for symbol in keys:
                    key(view, symbol)
                key(view, 'Return')
                process.wait(timeout=5)
                wanted = 'special:omp-' + 'abcd'[min(expected, 3)] * 24
                ready(lambda: get('monitors'), lambda ms: any((m.get('specialWorkspace') or {}).get('name') == wanted for m in ms), label)
                dispatch('hl.dsp.workspace.toggle_special(%s)' % json.dumps(wanted.removeprefix('special:')))
                dispatch('hl.dsp.focus({window="address:%s"})' % base['address'])
                records['scenarios'].append(label + ' selects ' + wanted)

            process, view = open_gallery('russian-vim')
            self.assertEqual(run(ctl + ['switchxkblayout', 'all', '1']).strip(), 'ok')
            ready(lambda: get('devices')['keyboards'],
                  lambda keyboards: keyboards and all('Russian' in k.get('active_keymap', '') for k in keyboards),
                  'owned Russian keyboard layout')
            key(view, 'Cyrillic_de')
            key(view, 'Return')
            process.wait(timeout=5)
            wanted = 'special:omp-' + 'b' * 24
            ready(lambda: get('monitors'), lambda ms: any((m.get('specialWorkspace') or {}).get('name') == wanted for m in ms), 'RU physical L')
            records['scenarios'].append('RU layout physical L navigates like English L')
            dispatch('hl.dsp.workspace.toggle_special(%s)' % json.dumps(wanted.removeprefix('special:')))
            dispatch('hl.dsp.focus({window="address:%s"})' % base['address'])
            process, view = open_gallery('gallery-toggle')
            run([sys.executable, str(ROOT / 'tools/agent_workspace_gallery.py'), '--helper', ARGS.helper])
            process.wait(timeout=5)
            self.assertEqual(get('activewindow')['address'], base['address'])
            self.assertEqual([(w['address'], w['workspace']['name']) for w in get('clients') if w['class'] == 'agent-test'],
                             [(w['address'], w['workspace']['name']) for w in agent_windows])
            records['scenarios'].append('second invocation toggles off; all agent windows remain in original workspace')
            records['status'] = 'verified'
        except BaseException as error:
            records['status'] = 'blocked' if isinstance(error, subprocess.TimeoutExpired) else 'failed'
            records['error'] = str(error)
            raise
        finally:
            for child in reversed(children):
                if child.poll() is None:
                    child.terminate()
                    child.wait(timeout=5)
            for log in logs:
                log.close()
            records['teardown'] = 'owned children exited'
            (root / 'result.json').write_text(json.dumps(records, indent=2, ensure_ascii=False) + '\n')


class LiveOwnedGallery(unittest.TestCase):
    def test_custom_workspace_discovery_fullscreen_navigation_and_cancel(self):
        root = Path(ARGS.output).resolve()
        root.mkdir(mode=0o700, parents=True, exist_ok=False)
        self.assertIn('omp-heavy-job-', Path('/proc/self/cgroup').read_text())
        ctl = ['hyprctl', '-i', ARGS.live_owned]
        def get(name):
            return json.loads(run(ctl + ['-j', name]))
        def dispatch(expression):
            self.assertEqual(run(ctl + ['dispatch', expression]).strip(), 'ok')
        before = get('activewindow')
        monitor_before = get('monitors')
        names = ['special:gallery-check-' + str(os.getpid()) + suffix for suffix in ('-kitty', '-omp')]
        children, logs = [], []
        record = {'source_sha256': hashlib.sha256((ROOT / 'tools/agent_workspace_gallery.py').read_bytes()).hexdigest(),
                  'scenarios': [], 'status': 'unverified'}
        def start(command, label, env=None):
            log = (root / (label + '.log')).open('w')
            logs.append(log)
            process = subprocess.Popen(command, env=env, stdout=log, stderr=log)
            children.append(process)
            return process
        def own_window(process):
            return ready(lambda: next((w for w in get('clients') if w['pid'] == process.pid), None),
                         bool, 'owned window ' + str(process.pid))
        def key(window, symbol):
            symbol = {'l': 'code:46', 'h': 'code:43', 'Escape': 'code:9'}.get(symbol, symbol)
            dispatch('hl.dsp.send_shortcut({mods="",key=%s,window="address:%s"})' %
                     (json.dumps(symbol), window['address']))
        try:
            self.assertFalse(any(w['class'] == 'omp-agent-gallery' for w in get('clients')),
                             'existing gallery must not be replaced by test')
            fixtures = []
            for index, name in enumerate(names):
                env = dict(os.environ, OMP_GUI_KEY=hashlib.sha256(name.encode()).hexdigest()[:24])
                process = start(['kitty', '--config', 'NONE', '--single-instance=no', '--class', 'agent-test',
                    '-e', 'sh', '-c', 'printf "Owned hidden workspace\\nNative preview fixture\\n\\033]2;Ready preview fixture\\007"; sleep 90'], 'fixture-' + str(index), env)
                window = own_window(process)
                self.assertTrue(window['workspace']['name'].startswith('special:'))
                dispatch('hl.dsp.window.move({window="address:%s",workspace=%s,follow=false})' %
                         (window['address'], json.dumps(name)))
                fixtures.append(window['address'])
            ready(lambda: [w for w in get('clients') if w['address'] in fixtures],
                  lambda ws: len(ws) == 2 and all(w['title'] == 'Ready preview fixture' for w in ws),
                  'Kitty consumed fixture terminal contents')
            time.sleep(0.2)  # Allow the parsed terminal update to reach its first rendered frame.
            self.assertEqual(get('activewindow').get('address'), before.get('address'))
            self.assertEqual([(m['name'], m['activeWorkspace'], m['specialWorkspace']) for m in get('monitors')],
                             [(m['name'], m['activeWorkspace'], m['specialWorkspace']) for m in monitor_before])
            from importlib.util import module_from_spec, spec_from_file_location
            spec = spec_from_file_location('live_gallery_model', ROOT / 'tools/agent_workspace_gallery.py')
            model = module_from_spec(spec)
            sys.modules[spec.name] = model
            spec.loader.exec_module(model)
            cards = model.group_clients(get('clients'))
            expected = len(cards)
            self.assertTrue(set(names).issubset({card.name for card in cards}))
            process = start([sys.executable, str(ROOT / 'tools/agent_workspace_gallery.py'),
                             '--helper', ARGS.helper], 'gallery')
            view = own_window(process)
            view = ready(lambda: next((w for w in get('clients') if w['pid'] == process.pid), None),
                         lambda w: w and ('столов: ' + str(expected)) in w.get('title', ''),
                         'real custom special workspaces in gallery')
            self.assertEqual(Path('/proc/%s/cgroup' % process.pid).read_text(), Path('/proc/self/cgroup').read_text())
            self.assertEqual(view['fullscreen'], 2)
            monitor = next(m for m in get('monitors') if m['id'] == view['monitor'])
            self.assertAlmostEqual(view['size'][0], monitor['width'] / monitor['scale'], delta=2)
            self.assertAlmostEqual(view['size'][1], monitor['height'] / monitor['scale'], delta=2)
            record['window'] = {k: view[k] for k in ('address', 'pid', 'title', 'size', 'fullscreen', 'monitor')}
            first_title = view['title']
            key(view, 'l')
            ready(lambda: get('activewindow'), lambda w: w.get('pid') == process.pid and w['title'] != first_title,
                  'vim navigation changes selected workspace')
            key(view, 'h')
            ready(lambda: get('activewindow'), lambda w: w.get('title') == first_title, 'vim back')
            record['scenarios'].extend(['custom special workspaces counted', 'fullscreen monitor geometry',
                                        'targeted l/h navigation; no workspace exposed'])
            mapping = json.loads(run([ARGS.helper, view['address']]))
            path = root / 'gallery.png'
            subprocess.run(['grim', '-T', mapping[view['address']], '-s', '0.5', str(path)],
                           check=True, capture_output=True, timeout=3)
            record['frame'] = {'path': str(path), 'sha256': hashlib.sha256(path.read_bytes()).hexdigest()}
            key(view, 'Escape')
            process.wait(timeout=5)
            self.assertEqual(get('activewindow').get('address'), before.get('address'))
            record['scenarios'].append('Escape restores original owner focus')
            self.assertEqual([(w['address'], w['workspace']['name']) for w in get('clients') if w['address'] in fixtures],
                             list(zip(fixtures, names)))
            record['status'] = 'verified'
        except BaseException as error:
            record['status'] = 'blocked' if isinstance(error, subprocess.TimeoutExpired) else 'failed'
            record['error'] = str(error)
            raise
        finally:
            for child in reversed(children):
                if child.poll() is None:
                    child.terminate()
                    child.wait(timeout=5)
            for log in logs:
                log.close()
            record['teardown'] = 'only owned children terminated'
            (root / 'result.json').write_text(json.dumps(record, ensure_ascii=False, indent=2) + '\n')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--helper', required=True)
    parser.add_argument('--output', required=True)
    parser.add_argument('--inner', action='store_true')
    parser.add_argument('--live-owned', metavar='SIGNATURE', help='Explicitly authorized current compositor; only owned windows')
    global ARGS
    ARGS = parser.parse_args()
    ARGS.helper = str(Path(ARGS.helper).resolve())
    if ARGS.live_owned:
        return 0 if unittest.TextTestRunner(verbosity=2).run(unittest.defaultTestLoader.loadTestsFromTestCase(LiveOwnedGallery)).wasSuccessful() else 1
    if ARGS.inner:
        return 0 if unittest.TextTestRunner(verbosity=2).run(unittest.defaultTestLoader.loadTestsFromTestCase(NativeGallery)).wasSuccessful() else 1
    root = Path(ARGS.output).resolve()
    root.mkdir(mode=0o700, parents=True, exist_ok=False)
    ARGS.output = str(root)
    home = root / 'home'
    home.mkdir(mode=0o700)
    config = root / 'hyprland.lua'
    config.write_text('''hl.monitor({ output = "", mode = "1280x800@60", position = "0x0", scale = 1 })
hl.config({ animations = { enabled = false }, input = { follow_mouse = 0, kb_layout = "us,ru" }, misc = { disable_autoreload = true, disable_hyprland_logo = true, disable_splash_rendering = true } })
package.preload["config.agent_workspace"] = function()
 local original = dofile("/home/q/.config/hypr/config/agent_workspace.lua")
 return { destination = function(w)
  if w.class == "omp-agent-gallery" or w.class == "gallery-background" then return nil end
  return original.destination(w)
 end }
end
local silent = dofile("/home/q/.config/hypr/config/silent.lua")
hl.window_rule({ match = { class = "^(omp-agent-gallery|gallery-background)$" }, float = true, center = true })
''')
    bus = root / 'bus.conf'
    bus.write_text('''<busconfig><type>session</type><listen>unix:tmpdir=/tmp</listen><auth>EXTERNAL</auth>
<policy context="default"><allow send_destination="*" eavesdrop="true"/><allow eavesdrop="true"/><allow own="*"/></policy></busconfig>''')
    env = dict(os.environ, HOME=str(home), VD_HYPR_CFG=str(config), GDK_BACKEND='wayland', NO_AT_BRIDGE='1',
               KITTY_CONFIG_DIRECTORY=str(home / 'kitty'), OMP_GALLERY_PARENT=os.environ.get('HYPRLAND_INSTANCE_SIGNATURE', ''))
    for name, value in [('XDG_CONFIG_HOME', 'config'), ('XDG_DATA_HOME', 'data'), ('XDG_STATE_HOME', 'state'), ('XDG_CACHE_HOME', 'cache')]:
        directory = root / value
        directory.mkdir(mode=0o700)
        env[name] = str(directory)
    env.pop('DISPLAY', None)
    env.pop('XAUTHORITY', None)
    verified = subprocess.run(['Hyprland', '--verify-config', '-c', str(config)], env=env)
    if verified.returncode:
        return verified.returncode
    return subprocess.run(['dbus-run-session', '--config-file=' + str(bus), '--',
        '/home/q/.agents/skills/desktop-sandbox/scripts/vd-run.sh', '--hypr', sys.executable,
        str(Path(__file__).resolve()), '--helper', ARGS.helper, '--output', str(root), '--inner'], env=env).returncode


if __name__ == '__main__':
    sys.exit(main())
