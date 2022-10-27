# wl9

wl9 is a POSIX wayland server that presents its windows to Plan 9's
rio.

It communicates over 9p to an `exportfs` instance running host Plan
9 system through a pair of file descriptors (one for reading, and
one for writing).

A lightly patched version of sigrid's [c9] library is used as a 9p
client.

[c9]: http://shithub.us/sigrid/c9/HEAD/info.html

## Usage

```
wl9 [-t rfd[,wfd]] [cmd [args...]]
```

The `-t` option specifies the file descriptors for the 9p connection.
They should be set up in advance before running wl9, or if not
specified, `/dev/virtio-ports/term` is opened for reading and
writing.

If `cmd [args...]` is given, it is launched as a child process after
wl9 sets up its sockets. The first window created by the child
will run in the existing `/mnt/wsys` instead of mounting `$wsys`.
This has the effect of replacing the window running `exportfs`.
Additionally, wl9 will stop automatically when it has no clients.

## Examples

### vmx

You can connect through a virtio-serial pipe from a unix guest to
a host 9front system.

```
vmx -c virtio:name:term!^<>{exportfs -r / -m 32768 >[1=0]}
```

If the guest system has hotplug rules to create named symlinks for
virtio pipes, you can just run `wl9`. Otherwise, you will need to
setup the appropriate redirections.

```
wl9 -t 0 <>/dev/vportNpM
```

### ssh

You can use a pipeline with `exportfs` and `ssh` to remotely run a
wayland application on a unix server that has wl9 installed.

```
exportf -r / <[0=1] | ssh host wl9 -t 0,1 cmd >[1=0]
```

## Wsys

rio windows can be created in two ways: by writing a `new` message
to `/dev/wctl`, or by attaching to `$wsys` with a `new` message as
the `aname`. Unfortunately, with the first method we can't find the
`winid` of the created window, so wl9 uses the second method.

However, attaching to `$wsys` poses an additional problem: we either
need a separate 9p channel for `exportfs -S $wsys`, or we need to
speak 9p over 9p to a separate `exportfs` process spawned by the
original.

At least for now, wl9 uses a third approach. Using a small [exportfs
patch], we can attach to services by walking to their fid, and
attaching with a 12 character aname prefix consisting of an 11
character decimal fid number, and a blank. The prefix is stripped
for the mount of the service.

[exportfs patch]: https://git.sr.ht/~mcf/wl9/blob/main/exportfs.patch

Once we mount `$wsys`, wl9 opens several files:

- `winname`: used to locate the draw(3) image for the window
- `label`: `xdg_toplevel.set_title` requests are translated to
  writes to `label`
- `wctl`: used to monitor window coordinates and status
- `mouse`: used to read mouse events
- `kbd`: used to read keyboard events

## Draw

## Snarf

Still kind of buggy with some applications.

## Mouse

### Cursor

Not implemented yet.

## Keyboard

### Keyboard map

For key codes, we use the 'k' and 'K' events from `/dev/kbd`. These
codes are the unshifted unicode characters corresponding to the
keys pressed and released. If there is no unshifted character
corresponding to that scancode, or the scancode is escaped, the
shifted character is used instead.

In typical keymaps, some keys are mapped to same unshifted character,
i.e. the mapping from scancode to character is not 1-1. Unfortunately,
This means that we can't distinguish which of the keys were pressed.
If there is a conflict, we choose the first one we encounter.

In particular, for the default kbmap, we have

| rune | key (scan) ... |
| --- | --- |
| \n | ENTER (`1c`) KPENTER (`e0 1c`) |
| - | MINUS (`0c`) KPMINUS (`4a`) |
| . | DOT (`34`) KPDOT (`53`) |
| / | SLASH (`35`) KPSLASH (`e0 35`) |
| 0 | 0 (`0b`) KP0 (`52`) |
| 1 | 1 (`02`) KP1 (`4f`) |
| 2 | 2 (`03`) KP2 (`50`) |
| 3 | 3 (`04`) KP3 (`51`) |
| 4 | 4 (`05`) KP4 (`4b`) |
| 5 | 5 (`06`) KP5 (`4c`) |
| 6 | 6 (`07`) KP6 (`4d`) |
| 7 | 7 (`08`) KP7 (`47`) |
| 8 | 8 (`09`) KP8 (`48`) |
| 9 | 9 (`0a`) KP9 (`49`) |
| Kup | UP (`e0 48`) ? (`7b`) ? (`e0 79`) |
| Kshift | LEFTSHIFT (`2a`) RIGHTSHIFT (`36`) |
| Kctl | LEFTCTRL (`1d`) RIGHTCTRL (`e0 1d`) CAPSLOCK (`3a`) |
| Kdown | DOWN (`e0 50`) ? (`79`) |

At startup, /dev/kbmap is translated to an XKB keymap as follows:

For every keymap entry with table 0 (unshifted) 2 (escaped unshifted),
5 (escaped with control), or 6 (escaped with shift), we add a keycode
`<U+XXXX>` to the `xkb_keycodes` section with value `0xXXXX + 8`,
where `U+XXXX` is the value of mapped unicode character. The 8
offset is due to historical reasons to translate an kernel event
code to an XKB keycode.

We use the standard 2-level `xkb_types` (excluding `KEYPAD` since
we can't uniquely identify those keys). Currently, tables 3-9 in
`/dev/kbmap` are ignored, which means that shift only affects
non-escaped keycodes, and control, alt, and mod4.

```
xkb_types "plan9" {
	type "ONE_LEVEL" {
		modifiers = none;
		level_name[1] = "Any";
	};
	type "TWO_LEVEL" {
		modifiers = Shift;
		map[Shift] = 2;
		level_name[1] = "Base";
		level_name[2] = "Shift";
	};
	type "ALPHABETIC" {
		modifiers = Shift+Lock;
		map[Shift] = 2;
		map[Lock] = 2;
		level_name[1] = "Base";
		level_name[2] = "Caps";
	};
};
```

We use the following `xkb_compat` rules for standard shift/caps
lock handling.

```
xkb_compat "plan9" {
	interpret Shift_L {
		action = SetMods(modifiers=Shift,clearLocks);
	};
	interpret Caps_Lock {
		action = LockMods(modifiers=Lock);
	};
};
```

The `xkb_symbols` section is constructed with a key for each keycode.
Since XKB keysyms are more granular than Plan 9 key characters, for
the ambiguous cases, we choose keysyms corresponding to first key
in the table above. Additionally, though Plan 9 doesn't have a
keymap entry the right meta key (bug?), `Kmod4` is mapped to `Super_L`
rather than `Super_R`.

If that keycode corresponds to an non-escaped scancode and that
scancode has an entry in table 1 (shift) as well, we add a two-level
key, `key <U+XXXX> {[KEY0, KEY1]}`, where `KEY0` and `KEY1` are the
names of the XKB symbols we mapped the table 0 and table 1 characters
to. Otherwise, we add a one-level key f`key <U+XXXX> {[KEY02]}`.
If there is no XKB name for the character, we use an XKB unicode
keysym.
