# Fingerprint enrolment overlay for Omarchy

An Omarchy shell plugin that turns `fprintd-enroll` into the enrolment flow
phones have: a fingerprint glyph that fills in from the bottom with every
accepted press, a "7 of 20" counter, a plain-language message for each
result the reader reports, and a hint for where to press next.

The hint is the point. On a small press sensor a print only verifies when
the probe overlaps something enrolled, so enrolment must walk the finger
around; a terminal printing `enroll-stage-passed` twenty times does not
tell anyone that. The overlay walks a spiral: centre, above, right, below,
left, then the four corners, and repeats.

```
tools/omarchy-fingerprint-enroll/
  manifest.json                 plugin manifest, id inauman.fingerprint-enroll
  Enroll.qml                    the overlay
  EnrollModel.js                result parsing and hints, no QML
  install.sh                    copy into ~/.config/omarchy/plugins and enable
  omarchy-fingerprint-enroll    wrapper: no argument opens the finger picker,
                                a finger name enrols it and waits; falls back
                                to fprintd-enroll without a shell
  upstream/omarchy-setup-security-fingerprint
                                proposed replacement for Omarchy's setup script
  upstream/omarchy-fingerprint-setup-helper
                                root helper: install, enable-pam, disable-pam, status
  upstream/org.omarchy.fingerprint.policy
                                polkit action for the helper (admin password, kept)
  upstream/49-omarchy-fingerprint-enroll.rules
                                enrolment without a password; the helper installs it
```

## How it fits Omarchy's menu

One row, no terminal. Setup > Security > Fingerprint always opens the
overlay:

- **First run** ("Fingerprint login is off"): Enter asks for the admin
  password once, in Omarchy's own polkit dialog, then a root helper installs
  the packages and the enrolment polkit rule; the overlay enrols the right
  index with on-screen guidance, asks for one touch to confirm the print,
  and the helper turns PAM on for sudo, polkit and the lock screen.
- **Every later run**: the finger picker, with no prompt of any kind.

The root half is `upstream/omarchy-fingerprint-setup-helper`, called
through `pkexec` under the polkit action in `upstream/org.omarchy.fingerprint.policy`
(`auth_admin_keep`, so both root steps of the first run cost one password).
`upstream/omarchy-setup-security-fingerprint` is the proposed replacement
for Omarchy's script: it launches the overlay when the shell is running and
keeps a text flow for TTY or SSH.

On this machine the row is overridden in
`~/.config/omarchy/extensions/omarchy-menu.jsonc` to call the wrapper, and
the helper lives in `/usr/local/bin` with `org.omarchy.fingerprint.local.policy`
pointing at it:

```sh
sudo install -m 755 tools/omarchy-fingerprint-enroll/upstream/omarchy-fingerprint-setup-helper /usr/local/bin/
sudo install -m 644 tools/omarchy-fingerprint-enroll/upstream/org.omarchy.fingerprint.local.policy /usr/share/polkit-1/actions/org.omarchy.fingerprint.policy
```

## Use

```sh
tools/omarchy-fingerprint-enroll/install.sh
omarchy-shell shell summon inauman.fingerprint-enroll '{"finger":"right-index-finger"}'
# or, script-friendly, exit 0 on success:
tools/omarchy-fingerprint-enroll/omarchy-fingerprint-enroll right-index-finger
```

Payload keys: `finger` (fprintd name, default `right-index-finger`),
`user`, `doneFile` (gets `ok` or `failed` written when the overlay
closes), `totalStages` (otherwise read from fprintd over D-Bus). Esc or a
click outside cancels; fprintd keeps the previous print for that finger.

Polkit asks for your own password once before enrolment
(`net.reactivated.fprint.device.enroll` is `auth_self_keep`) and remembers
it for a few minutes; the overlay deliberately never passes a username,
because naming the user, even yourself, trips the stricter `setusername`
rule that wants an admin password every time. The setup script avoided
both by running `fprintd-enroll` under sudo; the wrapper runs it as you.

To make it phone-like, no prompt for anyone logged in at the machine,
install the rule in `upstream/`; polkit picks it up immediately. The
proposed setup-script patch installs it during first-time setup, which
already runs under sudo, so Omarchy users would never see a prompt:

```sh
sudo install -m 644 tools/omarchy-fingerprint-enroll/upstream/49-omarchy-fingerprint-enroll.rules /etc/polkit-1/rules.d/
```

## Works with any reader

Nothing here is specific to the Elan pad; the plugin only speaks
`fprintd-enroll`'s output and fprintd's `num-enroll-stages` property. On a
five-stage swipe reader it fills in fifths.
