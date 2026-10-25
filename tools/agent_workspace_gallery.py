#!/usr/bin/env python3
"""Visual chooser for OMP special workspaces; never moves their windows."""
import argparse
from concurrent.futures import ThreadPoolExecutor
from dataclasses import dataclass
import fcntl
import hashlib
import json
import os
from pathlib import Path
import re
import socket
import stat
import subprocess
import sys

WORKSPACE = re.compile(r'special:[^\x00-\x1f]+\Z')
RESERVED_WORKSPACES = frozenset({'special:magic', 'special:pult'})
ADDRESS = re.compile(r'0x[0-9a-fA-F]{1,16}\Z')


@dataclass
class Card:
    name: str
    windows: list


def group_clients(clients):
    groups = {}
    for item in clients:
        name = (item.get('workspace') or {}).get('name', '')
        address = item.get('address', '')
        if (not WORKSPACE.fullmatch(name) or name in RESERVED_WORKSPACES
                or item.get('class') == 'omp-agent-gallery'
                or not ADDRESS.fullmatch(address) or not item.get('mapped', False)):
            continue
        groups.setdefault(name, []).append(item)
    return [Card(name, sorted(windows, key=lambda w: int(w['address'], 16)))
            for name, windows in sorted(groups.items())]


def key_action(key):
    return {'left': 'left', 'h': 'left', 'р': 'left', 'right': 'right', 'l': 'right', 'д': 'right',
            'up': 'up', 'k': 'up', 'л': 'up', 'down': 'down', 'j': 'down', 'о': 'down',
            'return': 'choose', 'kp_enter': 'choose', 'escape': 'cancel'}.get(key.casefold())


def navigate(index, count, columns, action):
    if count == 0:
        return 0
    index = min(max(index, 0), count - 1)
    columns = max(1, columns)
    if action == 'left' and index % columns:
        return index - 1
    if action == 'right' and index % columns < columns - 1 and index + 1 < count:
        return index + 1
    if action == 'up' and index >= columns:
        return index - columns
    if action == 'down' and (index // columns + 1) * columns < count:
        return min(index + columns, count - 1)
    return index


class GalleryState:
    def __init__(self):
        self.cards = []
        self.selected = 0

    @property
    def current(self):
        return self.cards[self.selected] if self.cards else None

    def update(self, clients):
        previous = self.current.name if self.current else None
        self.cards = group_clients(clients)
        self.selected = next((i for i, card in enumerate(self.cards) if card.name == previous),
                             min(self.selected, max(0, len(self.cards) - 1)))


def selection_target(name, clients):
    return name.removeprefix('special:') if any(card.name == name for card in group_clients(clients)) else None


def should_restore(active, own_pid, previous, clients):
    return active.get('pid') == own_pid and any(item.get('address') == previous for item in clients)


def thumbnail_size(width, height):
    ratio = min(300 / width, 180 / height, 1)
    return max(1, round(width * ratio)), max(1, round(height * ratio))


class Hyprland:
    def __init__(self):
        self.signature = os.environ.get('HYPRLAND_INSTANCE_SIGNATURE', '')
        if not self.signature:
            raise RuntimeError('HYPRLAND_INSTANCE_SIGNATURE is required')

    def command(self, *args):
        result = subprocess.run(['hyprctl', '-i', self.signature, *args], capture_output=True, text=True,
                                timeout=3, check=True)
        return result.stdout

    def get(self, name):
        return json.loads(self.command('-j', name))

    def dispatch(self, expression):
        result = self.command('dispatch', expression)
        if result.strip() != 'ok':
            raise RuntimeError(result.strip())


class ToggleSocket:
    def __init__(self, signature):
        runtime = Path(os.environ['XDG_RUNTIME_DIR'])
        key = hashlib.sha256(signature.encode()).hexdigest()[:16]
        self.path = runtime / ('omp-gallery-' + key + '.sock')
        self.lock = (runtime / ('omp-gallery-' + key + '.lock')).open('a+')
        self.server = None
        self.inode = None

    def acquire(self):
        try:
            fcntl.flock(self.lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
        except BlockingIOError:
            with socket.socket(socket.AF_UNIX, socket.SOCK_DGRAM) as peer:
                peer.settimeout(1)
                peer.sendto(b'toggle', str(self.path))
            return False
        if self.path.exists() or self.path.is_symlink():
            info = self.path.lstat()
            if not stat.S_ISSOCK(info.st_mode) or info.st_uid != os.getuid():
                raise RuntimeError('refusing non-owned gallery socket path')
            self.path.unlink()
        self.server = socket.socket(socket.AF_UNIX, socket.SOCK_DGRAM)
        self.server.bind(str(self.path))
        self.path.chmod(0o600)
        self.inode = self.path.lstat().st_ino
        self.server.setblocking(False)
        return True

    def close(self):
        if self.server:
            self.server.close()
        try:
            if self.inode is not None and self.path.lstat().st_ino == self.inode:
                self.path.unlink()
        except FileNotFoundError:
            pass
        self.lock.close()


def gtk_modules():
    import gi
    gi.require_version('Gtk', '3.0')
    gi.require_version('Gdk', '3.0')
    from gi.repository import Gdk, GdkPixbuf, GLib, Gtk, Pango
    return Gdk, GdkPixbuf, GLib, Gtk, Pango


def run_gallery(args, hypr, toggle):
    Gdk, GdkPixbuf, GLib, Gtk, Pango = gtk_modules()

    class Gallery:
        def __init__(self):
            self.state = GalleryState()
            self.previous = hypr.get('activewindow').get('address', '')
            monitors = hypr.get('monitors')
            self.monitor = next((m for m in monitors if m.get('focused')), monitors[0])
            self.columns = 1
            self.closed = False
            self.pending = False
            self.preview_count = 0
            self.cache = {}
            self.snapshot_error = None
            self.executor = ThreadPoolExecutor(max_workers=1, thread_name_prefix='gallery-preview')
            self.window = Gtk.Window(title='Обзор специальных рабочих столов')
            self.window.set_wmclass('omp-agent-gallery', 'omp-agent-gallery')
            self.window.set_role('omp-agent-gallery')
            self.window.set_name('omp-overview')
            self.window.set_decorated(False)
            self.window.set_modal(False)
            self.window.set_skip_taskbar_hint(True)
            self.window.set_skip_pager_hint(True)
            visual = self.window.get_screen().get_rgba_visual()
            if visual is not None:
                self.window.set_visual(visual)
            css = Gtk.CssProvider()
            css.load_from_data(b'''
                #omp-overview { background-color: rgba(17,24,38,0.96); color: #dddddd; }
                #omp-overview .overview-heading { font-size: 24px; font-weight: 600; }
                #omp-overview .overview-status { color: #b9c8d9; }
                #omp-overview flowbox, #omp-overview scrolledwindow { background-color: transparent; }
                #omp-overview flowboxchild { padding: 8px; border: 2px solid transparent; border-radius: 12px; }
                #omp-overview flowboxchild:selected { background-color: #182545; border-color: #01ccff; }
                #omp-overview frame { border: none; }
            ''')
            Gtk.StyleContext.add_provider_for_screen(self.window.get_screen(), css, Gtk.STYLE_PROVIDER_PRIORITY_APPLICATION)
            self.window.connect('delete-event', lambda *_: self.cancel())
            self.window.connect('key-press-event', self.key)
            self.window.connect('size-allocate', self.resize)
            outer = Gtk.Box(orientation=Gtk.Orientation.VERTICAL, spacing=24, margin=36)
            self.window.add(outer)
            self.heading = Gtk.Label(label='Специальные рабочие столы', xalign=0)
            self.heading.get_style_context().add_class('overview-heading')
            outer.pack_start(self.heading, False, False, 0)
            self.status = Gtk.Label(label='Читаю скрытые окна…', xalign=0, wrap=True)
            self.status.get_style_context().add_class('overview-status')
            outer.pack_start(self.status, False, False, 0)
            scroll = Gtk.ScrolledWindow()
            scroll.set_policy(Gtk.PolicyType.NEVER, Gtk.PolicyType.AUTOMATIC)
            outer.pack_start(scroll, True, True, 0)
            self.flow = Gtk.FlowBox()
            self.flow.set_homogeneous(True)
            self.flow.set_halign(Gtk.Align.CENTER)
            self.flow.set_valign(Gtk.Align.CENTER)
            self.flow.set_column_spacing(16)
            self.flow.set_row_spacing(16)
            self.flow.set_selection_mode(Gtk.SelectionMode.SINGLE)
            self.flow.set_activate_on_single_click(True)
            self.flow.connect('child-activated', self.clicked)
            scroll.add(self.flow)
            outer.pack_start(Gtk.Label(label='h j k l / стрелки — выбор    Enter — открыть    Esc — назад',
                                       xalign=0, wrap=True), False, False, 0)
            self.window.show_all()
            self.window.fullscreen()
            self.window.present()
            GLib.io_add_watch(toggle.server.fileno(), GLib.IO_IN, self.toggle_event)
            GLib.timeout_add(1500, self.refresh)
            self.refresh()

        def resize(self, _window, allocation):
            columns = max(1, min(3, (allocation.width - 40) // 340))
            if columns != self.columns:
                self.columns = columns
                self.flow.set_min_children_per_line(1)
                self.flow.set_max_children_per_line(columns)

        def toggle_event(self, *_):
            try:
                message = toggle.server.recv(32)
            except BlockingIOError:
                return True
            if message == b'toggle':
                self.cancel()
            return not self.closed

        def refresh(self):
            if self.closed:
                return False
            if not self.pending:
                self.pending = True
                future = self.executor.submit(self.load)
                future.add_done_callback(lambda done: GLib.idle_add(self.loaded, done))
            return True

        def load(self):
            clients = hypr.get('clients')
            cards = group_clients(clients)
            current = {(w['address'], w.get('pid')) for card in cards for w in card.windows}
            cache = {key: value for key, value in self.cache.items() if key in current}
            missing = [key for key in current if key not in cache]
            error = self.snapshot_error
            if missing and not error:
                try:
                    mapping = json.loads(subprocess.run([args.helper, *[key[0] for key in missing]],
                        capture_output=True, text=True, check=True, timeout=3).stdout)
                    for key in missing:
                        if self.closed:
                            break
                        identifier = mapping.get(key[0])
                        if not identifier:
                            cache[key] = (None, 'Окно не предоставляет Wayland-превью')
                            continue
                        result = subprocess.run(['grim', '-T', identifier, '-s', '0.25', '-'],
                            capture_output=True, check=True, timeout=3)
                        cache[key] = (result.stdout, None)
                except (OSError, ValueError, subprocess.SubprocessError) as failure:
                    error = 'Захват превью недоступен: ' + str(failure)
            return clients, cache, error

        def loaded(self, future):
            self.pending = False
            if self.closed:
                return False
            try:
                clients, cache, error = future.result()
            except (OSError, ValueError, subprocess.SubprocessError) as failure:
                self.status.set_text('Не удалось прочитать окна Hyprland: ' + str(failure))
                return False
            old = [(c.name, [(w['address'], w.get('pid'), w.get('title')) for w in c.windows]) for c in self.state.cards]
            self.state.update(clients)
            new = [(c.name, [(w['address'], w.get('pid'), w.get('title')) for w in c.windows]) for c in self.state.cards]
            changed = old != new or self.cache != cache or self.snapshot_error != error
            self.cache, self.snapshot_error = cache, error
            self.heading.set_text('Специальные рабочие столы · ' + str(len(self.state.cards)))
            self.status.set_text(error or ('Выбери рабочий стол; предпросмотр не открывает его окна.' if self.state.cards
                else 'Нет открытых special workspace. Личные magic и pult не включены в обзор.'))
            if changed:
                self.render()
            return False

        def render(self):
            self.preview_count = 0
            for child in self.flow.get_children():
                child.destroy()
            for card in self.state.cards:
                box = Gtk.Box(orientation=Gtk.Orientation.VERTICAL, spacing=10, margin=12)
                box.pack_start(Gtk.Label(label=card.name.removeprefix('special:') + ' · ' + str(len(card.windows)),
                                         xalign=0, selectable=False), False, False, 0)
                for item in card.windows:
                    title = Gtk.Label(label=item.get('title') or item.get('class') or 'Окно', xalign=0)
                    title.set_ellipsize(Pango.EllipsizeMode.END)
                    title.set_max_width_chars(34)
                    title.set_tooltip_text((item.get('class') or '') + '\n' + (item.get('title') or ''))
                    box.pack_start(title, False, False, 0)
                    data, error = self.cache.get((item['address'], item.get('pid')), (None, 'Превью недоступно'))
                    if data:
                        try:
                            loader = GdkPixbuf.PixbufLoader.new()
                            loader.connect('size-prepared', lambda image_loader, w, h:
                                           image_loader.set_size(*thumbnail_size(w, h)))
                            loader.write(data)
                            loader.close()
                            image = Gtk.Image.new_from_pixbuf(loader.get_pixbuf())
                            box.pack_start(image, False, False, 0)
                            self.preview_count += 1
                        except GLib.Error:
                            box.pack_start(Gtk.Label(label='Не удалось декодировать превью'), False, False, 0)
                    else:
                        box.pack_start(Gtk.Label(label=error or 'Превью недоступно', wrap=True), False, False, 0)
                frame = Gtk.Frame()
                frame.add(box)
                self.flow.add(frame)
            self.flow.show_all()
            self.select()

        def select(self):
            children = self.flow.get_children()
            if children:
                child = children[self.state.selected]
                self.flow.select_child(child)
                child.grab_focus()
            selected = self.state.current.name.removeprefix('special:') if self.state.current else 'нет окон'
            self.window.set_title('Обзор — ' + selected + ' · столов: ' + str(len(self.state.cards))
                                  + ' · снимков: ' + str(self.preview_count))

        def key(self, _widget, event):
            if event.state & (Gdk.ModifierType.CONTROL_MASK | Gdk.ModifierType.MOD1_MASK | Gdk.ModifierType.SUPER_MASK):
                return False
            codepoint = Gdk.keyval_to_unicode(event.keyval)
            name = chr(codepoint) if codepoint and chr(codepoint).isprintable() else Gdk.keyval_name(event.keyval)
            action = key_action(name or '')
            if action == 'cancel':
                self.cancel()
            elif action == 'choose':
                self.choose()
            elif action:
                self.state.selected = navigate(self.state.selected, len(self.state.cards), self.columns, action)
                self.select()
            return action is not None

        def clicked(self, _flow, child):
            self.state.selected = child.get_index()
            self.choose()

        def cancel(self):
            if not self.closed:
                try:
                    restore = should_restore(hypr.get('activewindow'), os.getpid(), self.previous, hypr.get('clients'))
                except (OSError, ValueError, subprocess.SubprocessError):
                    restore = False
                self.close()
                if restore and ADDRESS.fullmatch(self.previous):
                    hypr.dispatch('hl.dsp.focus({window="address:' + self.previous + '"})')
            return True

        def choose(self):
            if self.closed or not self.state.current:
                return
            try:
                target = selection_target(self.state.current.name, hypr.get('clients'))
                if target is None:
                    self.status.set_text('Этот рабочий стол уже пуст. Обновляю галерею…')
                    self.refresh()
                    return
                monitor_name = self.monitor['name']
                if not re.fullmatch(r'[A-Za-z0-9_.:-]+', monitor_name):
                    raise ValueError('invalid monitor selector')
                current = next((m for m in hypr.get('monitors') if m['name'] == monitor_name), None)
                if current is None:
                    raise ValueError('monitor disconnected')
                hypr.dispatch('hl.dsp.focus({monitor=' + json.dumps(monitor_name) + '})')
                if (current.get('specialWorkspace') or {}).get('name') != 'special:' + target:
                    hypr.dispatch('hl.dsp.workspace.toggle_special(' + json.dumps(target, ensure_ascii=False) + ')')
                self.close()
            except (OSError, ValueError, subprocess.SubprocessError, RuntimeError) as failure:
                self.status.set_text('Не удалось открыть рабочий стол: ' + str(failure))

        def close(self):
            self.closed = True
            self.executor.shutdown(wait=False, cancel_futures=True)
            self.window.destroy()
            Gtk.main_quit()

    GLib.set_prgname('omp-agent-gallery')
    Gallery()
    Gtk.main()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--helper', default=str(Path.home() / '.local/libexec/agent-workspace-toplevels'))
    args = parser.parse_args()
    hypr = Hyprland()
    toggle = ToggleSocket(hypr.signature)
    try:
        if toggle.acquire():
            run_gallery(args, hypr, toggle)
    finally:
        toggle.close()


if __name__ == '__main__':
    try:
        main()
    except (OSError, ValueError, RuntimeError, subprocess.SubprocessError) as error:
        sys.exit('agent-workspace-gallery: ' + str(error))
