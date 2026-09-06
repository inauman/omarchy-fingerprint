#!/bin/bash
# Replicates the PAM half of `omarchy setup security fingerprint`.
# Skips the enroll step, since a fingerprint is already enrolled.
set -e

GATE="auth      [success=1 default=ignore] pam_exec.so quiet /usr/bin/omarchy-hw-laptop-closed"

# --- sudo ---
if ! grep -q pam_fprintd.so /etc/pam.d/sudo; then
  echo "Configuring sudo for fingerprint authentication..."
  sed -i '1i auth      sufficient pam_fprintd.so' /etc/pam.d/sudo
fi
if ! grep -q 'omarchy-hw-laptop-closed' /etc/pam.d/sudo; then
  echo "Adding clamshell gate to sudo..."
  sed -i "/pam_fprintd\.so/i $GATE" /etc/pam.d/sudo
fi

# --- polkit (file does not exist on this machine, so create it) ---
if [[ ! -f /etc/pam.d/polkit-1 ]]; then
  echo "Creating polkit configuration with fingerprint authentication..."
  cat > /etc/pam.d/polkit-1 <<EOF
$GATE
auth      sufficient pam_fprintd.so
auth      required pam_unix.so

account   required pam_unix.so
password  required pam_unix.so
session   required pam_unix.so
EOF
fi

# --- lock screen ---
echo "Configuring lock screen for fingerprint authentication..."
cat > /etc/pam.d/omarchy-lock-fingerprint <<'EOF'
#%PAM-1.0
auth       required                    pam_fprintd.so
account    include                     system-local-login
EOF

echo
echo "Done. Resulting /etc/pam.d/sudo:"
echo "---"
cat /etc/pam.d/sudo
