#!/usr/bin/env python3
"""Install the noctalia skin-runtime product: native binary, assets, plugin.

Standard library only. Copies:
  <binary>          -> <prefix>/bin/noctalia
  <repo>/assets     -> <prefix>/share/noctalia/assets
  <repo>/integrations/skin-runtime -> <plugin-root>/skin-runtime
  <repo>/integrations/tmus/desktop_skin.luau -> <plugin-root>/tmus/desktop_skin.luau
  <repo>/integrations/tmus/panel.luau        -> <plugin-root>/tmus/panel.luau
  <repo>/integrations/tmus/plugin.toml       -> <plugin-root>/tmus/plugin.toml
  <repo>/integrations/tmus/service.luau,widget.luau,hint_navigation.luau
      -> <plugin-root>/tmus/<same names>
  <repo>/integrations/tmus/skin-assets       -> <plugin-root>/tmus/skin-assets
  record            -> $XDG_DATA_HOME/wallpaper-studio/native-runtime.json
                       {"schema_version": 1, "binary": <prefix>/bin/noctalia}
  [--ai-plugin]/plugin.toml,widget.luau,panel.luau,service.luau,
      desktop_skin.luau       -> <plugin-root>/ai-triage/<same names>

The skin-runtime presentation tree and the tmus skin surface ship together:
the shared desktop_skin module, the tmus panel that consumes it, its manifest
and its real skin-assets (including the wallpaper-family asset directories)
must reach the deployed plugin, not the module alone.

Safety rules:
  - full preflight before the first mutation; any error aborts the run;
  - symlinks are never followed or overwritten; nonregular sources rejected;
  - every overwritten destination file is first preserved as a timestamped
    sidecar ``<name>.bak.<UTC stamp>`` (Studio precedent);
  - individual files are replaced atomically (temp file + os.replace) with the
    original file mode preserved;
  - nothing outside the listed destinations is touched: no shell commands, no
    systemd, no settings, no autostart, no launches; other prefix files are
    never deleted.

Emits a JSON report on stdout: {"dry_run", "installed", "backups", "errors"}.
"""

import argparse
import json
import os
import shutil
import stat
import sys
import tempfile
import time

# Scratch/backup names never to copy from the source product tree.
EXCLUDED_NAMES = {
    ".git",
    ".superpowers",
    "__pycache__",
}
EXCLUDED_SUFFIXES = (
    ".bak",  # also matches ".bak.<stamp>" sidecars
    ".orig",
    ".rej",
    ".swp",
    "~",
)

SKIN_RUNTIME_DIR = "skin-runtime"
TMUS_SKIN_SRC = ("integrations", "tmus", "desktop_skin.luau")
TMUS_SKIN_DST = ("tmus", "desktop_skin.luau")
# The shared module is meaningless without the tmus panel that requires it, the
# manifest that declares that panel, and the real skin-assets (including the
# wallpaper-family directories). All ship to the same deployed plugin tree.
TMUS_PANEL_SRC = ("integrations", "tmus", "panel.luau")
TMUS_PANEL_DST = ("tmus", "panel.luau")
TMUS_MANIFEST_SRC = ("integrations", "tmus", "plugin.toml")
TMUS_MANIFEST_DST = ("tmus", "plugin.toml")
TMUS_RUNTIME_FILES = ("service.luau", "widget.luau", "hint_navigation.luau")
TMUS_SKIN_ASSETS_SRC = ("integrations", "tmus", "skin-assets")
TMUS_SKIN_ASSETS_DST = ("tmus", "skin-assets")

# Durable record of the installed native runtime, consumed by the standalone
# Studio resolver (nonempty NOCTALIA_SKIN_BINARY still wins there).
RECORD_RELATIVE = ("wallpaper-studio", "native-runtime.json")
RECORD_SCHEMA_VERSION = 1
RECORD_MODE = 0o644

# Only plugin sources and the explicitly owned cassette asset subdirectory;
# unrelated existing assets and owner AI data are never traversed or touched.
AI_PLUGIN_FILES = (
    "plugin.toml",
    "widget.luau",
    "panel.luau",
    "service.luau",
    "desktop_skin.luau",
    "cassette.luau",
)

# Native resource sentinel files that asset discovery depends on.
ASSET_SENTINELS = (
    "emoji.json",
    "fonts/noctalia-tabler.ttf",
    "templates/builtin.toml",
    "translations/en.json",
)

# Named entry files declared in integrations/skin-runtime/plugin.toml.
PLUGIN_ENTRY_FILES = (
    "plugin.toml",
    "widget.luau",
    "music-preview.luau",
    "dashboard.luau",
    "music.luau",
    "desktop-status.luau",
    "summary.luau",
    "service.luau",
)


def fail(errors, message):
    errors.append(message)
    return False


def utc_stamp():
    return time.strftime("%Y%m%dT%H%M%SZ", time.gmtime())


def is_regular_file(path):
    try:
        st = os.lstat(path)
    except OSError:
        return False
    return stat.S_ISREG(st.st_mode)


def check_source_file(path, errors):
    """Regular, non-symlink source file check."""
    if os.path.islink(path):
        return fail(errors, "source is a symlink: %s" % path)
    if not os.path.exists(path):
        return fail(errors, "source is missing: %s" % path)
    if not is_regular_file(path):
        return fail(errors, "source is not a regular file: %s" % path)
    return True


def check_dest_free(path, errors):
    """Reject symlink destinations and ancestors before planning any writes."""
    parent = os.path.dirname(os.path.abspath(path))
    while True:
        if os.path.islink(parent):
            return fail(errors, "destination ancestor is a symlink: %s" % parent)
        if os.path.lexists(parent) and not os.path.isdir(parent):
            return fail(errors, "destination ancestor is not a directory: %s" % parent)
        next_parent = os.path.dirname(parent)
        if next_parent == parent:
            break
        parent = next_parent
    if os.path.islink(path):
        return fail(errors, "destination is a symlink: %s" % path)
    if os.path.exists(path) and not is_regular_file(path):
        return fail(errors, "destination exists and is not a regular file: %s" % path)
    return True


def excluded(name):
    return name in EXCLUDED_NAMES or ".bak." in name or name.endswith(EXCLUDED_SUFFIXES)


def enumerate_tree(src_root, errors):
    """List all regular non-symlink files under src_root (relative paths),
    excluding scratch/backup names. Directories are never followed through
    symlinks."""
    files = []
    for dirpath, dirnames, filenames in os.walk(src_root, followlinks=False):
        rel_dir = os.path.relpath(dirpath, src_root)
        keep = []
        for d in sorted(dirnames):
            full = os.path.join(dirpath, d)
            if excluded(d):
                continue
            if os.path.islink(full) or not os.path.isdir(full):
                fail(errors, "source tree entry is not a real directory: %s" % full)
                continue
            keep.append(d)
        dirnames[:] = keep
        for f in sorted(filenames):
            if excluded(f):
                continue
            full = os.path.join(dirpath, f)
            if not check_source_file(full, errors):
                continue
            rel = os.path.normpath(os.path.join(rel_dir, f)) if rel_dir != "." else f
            files.append((full, rel))
    return files


def plan_copy_file(src, dst, plan, errors):
    if not check_source_file(src, errors):
        return
    if not check_dest_free(dst, errors):
        return
    plan.append((src, dst))


def plan_copy_tree(src_root, dst_root, plan, errors):
    files = enumerate_tree(src_root, errors)
    for full, rel in files:
        plan_copy_file(full, os.path.join(dst_root, rel), plan, errors)


def atomic_install(src, dst, mode, dry_run):
    """Atomically place src at dst, backing up an existing dst first.
    Returns (applied, backup_path_or_None, had_old)."""
    if os.path.exists(dst):
        backup = dst + ".bak." + utc_stamp() + "." + str(time.time_ns())
        if not dry_run:
            shutil.copyfile(dst, backup)
            shutil.copystat(dst, backup)
        else:
            backup = None
        had_old = True
    else:
        backup = None
        had_old = False
    if dry_run:
        return True, backup, had_old
    os.makedirs(os.path.dirname(dst), exist_ok=True)
    fd, tmp = tempfile.mkstemp(dir=os.path.dirname(dst), prefix="." + os.path.basename(dst) + ".")
    os.close(fd)
    try:
        shutil.copyfile(src, tmp)
        os.chmod(tmp, mode)
        os.replace(tmp, dst)
    except OSError:
        try:
            os.unlink(tmp)
        except OSError:
            pass
        raise
    return True, backup, had_old


def source_mode(path):
    return stat.S_IMODE(os.lstat(path).st_mode)


def atomic_write_record(dst, payload, dry_run):
    """Atomically place a text payload at dst, backing up an existing dst first.
    Same safety shape as atomic_install. Returns (applied, backup_path_or_None)."""
    if os.path.exists(dst):
        backup = dst + ".bak." + utc_stamp() + "." + str(time.time_ns())
        if not dry_run:
            shutil.copyfile(dst, backup)
            shutil.copystat(dst, backup)
        else:
            backup = None
    else:
        backup = None
    if dry_run:
        return True, backup
    os.makedirs(os.path.dirname(dst), exist_ok=True)
    fd, tmp = tempfile.mkstemp(dir=os.path.dirname(dst), prefix="." + os.path.basename(dst) + ".")
    try:
        with os.fdopen(fd, "wb") as handle:
            handle.write(payload)
        os.chmod(tmp, RECORD_MODE)
        os.replace(tmp, dst)
    except OSError:
        try:
            os.unlink(tmp)
        except OSError:
            pass
        raise
    return True, backup


def main(argv=None):
    ap = argparse.ArgumentParser(
        description="Install noctalia native binary, assets and skin-runtime plugin."
    )
    ap.add_argument("--binary", required=True,
                    help="explicit path to the existing compiled noctalia binary")
    ap.add_argument("--prefix",
                    default=os.path.expanduser("~/.local/opt/noctalia"),
                    help="install prefix (default: ~/.local/opt/noctalia)")
    ap.add_argument("--plugin-root",
                    default=os.path.join(os.environ.get("XDG_CONFIG_HOME",
                                        os.path.expanduser("~/.config")),
                                        "noctalia", "plugins"),
                    help="noctalia plugins root (default: $XDG_CONFIG_HOME/noctalia/plugins)")
    ap.add_argument("--ai-plugin", default=None,
                    help="path to ai-triage sources; installs the six source files "
                         "and only skin-assets/ai-cassette to <plugin-root>/ai-triage")
    ap.add_argument("--dry-run", action="store_true",
                    help="validate and plan without writing anything")
    ap.add_argument("--source-root", default=None,
                    help=argparse.SUPPRESS)  # repo root; auto-detected by default
    args = ap.parse_args(argv)

    here = os.path.dirname(os.path.abspath(__file__))
    repo = args.source_root or os.path.dirname(here)

    report = {
        "dry_run": args.dry_run,
        "installed": [],  # {src, dst}
        "backups": [],    # {dst, backup}
        "errors": [],
    }
    errors = report["errors"]

    binary = os.path.abspath(args.binary)
    prefix = os.path.abspath(args.prefix)
    plugin_root = os.path.abspath(args.plugin_root)

    assets_src = os.path.join(repo, "assets")
    plugin_src = os.path.join(repo, "integrations", SKIN_RUNTIME_DIR)
    tmus_src = os.path.join(repo, *TMUS_SKIN_SRC)
    tmus_panel_src = os.path.join(repo, *TMUS_PANEL_SRC)
    tmus_manifest_src = os.path.join(repo, *TMUS_MANIFEST_SRC)
    tmus_skin_assets_src = os.path.join(repo, *TMUS_SKIN_ASSETS_SRC)

    bin_dst = os.path.join(prefix, "bin", "noctalia")
    assets_dst = os.path.join(prefix, "share", "noctalia", "assets")
    plugin_dst = os.path.join(plugin_root, SKIN_RUNTIME_DIR)
    tmus_dst = os.path.join(plugin_root, *TMUS_SKIN_DST)
    tmus_panel_dst = os.path.join(plugin_root, *TMUS_PANEL_DST)
    tmus_manifest_dst = os.path.join(plugin_root, *TMUS_MANIFEST_DST)
    tmus_skin_assets_dst = os.path.join(plugin_root, *TMUS_SKIN_ASSETS_DST)

    xdg_data = os.environ.get("XDG_DATA_HOME", "").strip()
    if not xdg_data or not os.path.isabs(xdg_data):
        xdg_data = os.path.expanduser("~/.local/share")
    record_dst = os.path.join(os.path.abspath(xdg_data), *RECORD_RELATIVE)
    report["record"] = {"dst": record_dst, "binary": bin_dst}

    # ── Preflight (no mutation before this passes) ────────────────────────────
    check_source_file(binary, errors)

    for rel in ASSET_SENTINELS:
        check_source_file(os.path.join(assets_src, rel), errors)
    for rel in PLUGIN_ENTRY_FILES:
        check_source_file(os.path.join(plugin_src, rel), errors)
    for shared in sorted(os.listdir(os.path.join(plugin_src, "shared"))) \
            if os.path.isdir(os.path.join(plugin_src, "shared")) else []:
        check_source_file(os.path.join(plugin_src, "shared", shared), errors)
    check_source_file(tmus_src, errors)
    check_source_file(tmus_panel_src, errors)
    check_source_file(tmus_manifest_src, errors)
    for name in TMUS_RUNTIME_FILES:
        check_source_file(os.path.join(repo, "integrations", "tmus", name), errors)
    if not os.path.isdir(tmus_skin_assets_src):
        fail(errors, "tmus skin-assets directory not found: %s" % tmus_skin_assets_src)

    if not os.path.isdir(plugin_root):
        fail(errors, "plugin root does not exist: %s" % plugin_root)
    elif not os.path.islink(plugin_root) and not os.path.isdir(
            os.path.join(plugin_root, "tmus")):
        fail(errors, "existing tmus plugin directory not found under plugin root: %s"
             % os.path.join(plugin_root, "tmus"))

    plan = []
    plan_copy_file(binary, bin_dst, plan, errors)
    # The record destination is validated like any other destination (symlinked
    # ancestors rejected before any mutation); the payload is written in apply.
    check_dest_free(record_dst, errors)
    plan_copy_tree(assets_src, assets_dst, plan, errors)
    plan_copy_tree(plugin_src, plugin_dst, plan, errors)
    plan_copy_file(tmus_src, tmus_dst, plan, errors)
    plan_copy_file(tmus_panel_src, tmus_panel_dst, plan, errors)
    plan_copy_file(tmus_manifest_src, tmus_manifest_dst, plan, errors)
    for name in TMUS_RUNTIME_FILES:
        plan_copy_file(os.path.join(repo, "integrations", "tmus", name),
                       os.path.join(plugin_root, "tmus", name), plan, errors)
    plan_copy_tree(tmus_skin_assets_src, tmus_skin_assets_dst, plan, errors)

    ai_plugin_dst = None
    if args.ai_plugin:
        ai_src = os.path.abspath(args.ai_plugin)
        if not os.path.isdir(ai_src):
            fail(errors, "--ai-plugin is not an existing directory: %s" % ai_src)
        else:
            for name in AI_PLUGIN_FILES:
                check_source_file(os.path.join(ai_src, name), errors)
            ai_plugin_dst = os.path.join(plugin_root, "ai-triage")
            for name in AI_PLUGIN_FILES:
                plan_copy_file(os.path.join(ai_src, name),
                               os.path.join(ai_plugin_dst, name), plan, errors)
            cassette_src = os.path.join(ai_src, "skin-assets", "ai-cassette")
            cassette_dst = os.path.join(ai_plugin_dst, "skin-assets", "ai-cassette")
            plan_copy_tree(cassette_src, cassette_dst, plan, errors)

    if errors:
        report["errors"] = errors
        print(json.dumps(report, indent=2))
        return 1

    # ── Apply: create directories, then atomically place each file ────────────
    # (dry_run writes nothing, including directories)
    if not args.dry_run:
        apply_dirs = [os.path.dirname(bin_dst), assets_dst, plugin_dst,
                      os.path.dirname(tmus_dst), tmus_skin_assets_dst]
        if ai_plugin_dst:
            apply_dirs.append(ai_plugin_dst)
        for d in apply_dirs:
            os.makedirs(d, exist_ok=True)

    ok = True
    for src, dst in plan:
        mode = source_mode(src)
        if dst == bin_dst:
            # Compiled binary: keep executable regardless of source mode.
            mode = mode | stat.S_IXUSR | stat.S_IXGRP | stat.S_IXOTH
        try:
            _, backup, _had_old = atomic_install(src, dst, mode, args.dry_run)
        except OSError as exc:
            fail(errors, "failed to install %s -> %s: %s" % (src, dst, exc))
            ok = False
            break
        report["installed"].append({"src": src, "dst": dst})
        if backup:
            report["backups"].append({"dst": dst, "backup": backup})

    # Durable record of the installed native runtime; only the installed
    # destination path appears in it, never a scratch or worktree binary.
    if ok:
        record_payload = json.dumps({
            "schema_version": RECORD_SCHEMA_VERSION,
            "binary": bin_dst,
        }, indent=2) + "\n"
        try:
            _, record_backup = atomic_write_record(
                record_dst, record_payload.encode("utf-8"), args.dry_run)
        except OSError as exc:
            fail(errors, "failed to write %s: %s" % (record_dst, exc))
            ok = False
        else:
            report["installed"].append({"src": "<native-runtime record>",
                                        "dst": record_dst})
            if record_backup:
                report["backups"].append({"dst": record_dst, "backup": record_backup})

    report["errors"] = errors
    print(json.dumps(report, indent=2))
    return 0 if ok and not errors else 1


if __name__ == "__main__":
    sys.exit(main())
