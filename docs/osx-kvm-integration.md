# OSX-KVM integration notes (macOS 15 + Symphony-in-c)

This repository now includes `scripts/prepare-osx-kvm-symphony.sh` to patch an existing `ludoplex/OSX-KVM` checkout with:

- macOS 15 / Sequoia-oriented defaults,
- SickCodes-style CPU/QEMU optimization flags,
- SMBIOS serial placeholders,
- a guest-side `symphony-in-c` provisioning script.

## Quick start

```bash
git clone https://github.com/ludoplex/OSX-KVM.git
scripts/prepare-osx-kvm-symphony.sh ./OSX-KVM
```

Then follow `OSX-KVM/SYMPHONY_SETUP.md` produced by the script.
