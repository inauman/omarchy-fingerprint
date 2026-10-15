The overlay as proposed in omacom/omarchy#10689 after Omarchy's translation
system (#15326): `../Enroll.qml` and `../EnrollModel.js` route every string
through `I18n.t()`, and `en-fingerprint.json` lists the English catalog
entries the PR adds to `default/i18n/en/shell.json`. The plugin at the top of
this directory stays English-only so it keeps running on Omarchy 4.0.4, which
predates `I18n`.
