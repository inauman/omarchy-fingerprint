# Fingerprint enrollment overlay for Omarchy

An Omarchy shell plugin that turns `fprintd-enroll` into the enrollment flow
phones have: a fingerprint glyph that fills in from the bottom with every
accepted press, a "7 of 20" counter, a plain-language message for each
result the reader reports, and a hint for where to press next.

The hint is the point. On a small press sensor a print only verifies when
the probe overlaps something enrolled, so enrollment must walk the finger
around; a terminal printing `enroll-stage-passed` twenty times does not
tell anyone that. The overlay walks a spiral: centre, above, right, below,
left, then the four corners, and repeats.

```
tools/omarchy-fingerprint-enroll/
  manifest.json                 plugin manifest, id inauman.fingerprint-enroll
  Enroll.qml                    the overlay
  FingerprintGlyph.qml          the fingerprint drawing
  EnrollModel.js                result parsing and hints, no QML
  install.sh                    copy into ~/.config/omarchy/plugins and enable
  omarchy-fingerprint-enroll    summon the overlay (picker, or one finger and wait)
  upstream/                     the setup helper and setup script as in the PR
```

## How it fits Omarchy

One menu row, no terminal. Setup > Security > Fingerprint opens the overlay
when the shell is running:

- **First run**: install the packages, pick a finger, enroll it with the
  guide, touch once to confirm, and fingerprint login is turned on for sudo,
  admin prompts and the lock screen.
- **Later runs**: the finger picker.

Enrolling asks for your own password (fprintd's default polkit policy,
remembered for a few minutes), so an unlocked session alone cannot add a
fingerprint to your account.

The privileged steps go through `upstream/omarchy-fingerprint-setup-helper`,
which re-runs itself through `pkexec` from `/usr/bin` with `PATH` pinned,
the same pattern Omarchy's `omarchy-dns` uses. It adds no setup logic of its
own: turning login on runs `omarchy-setup-security-fingerprint
--enable-login` (`upstream/omarchy-setup-security-fingerprint`), which reuses
that script's PAM step and `omarchy-apply-lock`. Without a running shell the
setup script's text flow is unchanged.

This is what [omacom/omarchy#10689](https://github.com/omacom/omarchy/pull/10689)
proposes. On this machine the row is overridden in
`~/.config/omarchy/extensions/omarchy-menu.jsonc` to call the local wrapper.

## Use

```sh
tools/omarchy-fingerprint-enroll/install.sh
omarchy-shell shell summon inauman.fingerprint-enroll '{"finger":"right-index-finger"}'
# or, script-friendly, exit 0 on success:
tools/omarchy-fingerprint-enroll/omarchy-fingerprint-enroll right-index-finger
```

Payload keys: `finger` (fprintd name; without it the picker opens) and
`doneFile` (gets `ok` or `failed` written when the overlay closes). Esc or a
click outside cancels; fprintd keeps the previous print for that finger. The
overlay never passes a username to fprintd, which would trip the stricter
`setusername` rule that wants an admin password.

## Works with any reader

Nothing here is specific to the Elan pad; the plugin only speaks
`fprintd-enroll`'s output and fprintd's `num-enroll-stages` property. On a
five-stage swipe reader it fills in fifths.
