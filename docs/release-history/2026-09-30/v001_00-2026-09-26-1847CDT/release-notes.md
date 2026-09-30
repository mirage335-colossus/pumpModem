> **Experimental build:** this prerelease is for evaluation. Users needing assurance should use a qualified regular release.

> **Certification pending:** this release has not completed release certification and is not marked Latest.

Build **v001_00-2026-09-26-1847CDT**

- Source commit: `800da8c67611819c22236c7ef310cb518348ae2b`
- Build date: `2026-09-26-1847CDT` (America/Chicago)
- Workflow run: `36280483610`, attempt `1`
- Package version: `1.0.0`
- GUI downloads: fltk, rev
- Linux ABI: x86-64: glibc 2.36 (Bookworm source SDK); AArch64: glibc 2.35

Download the archive matching your operating system, architecture and GUI backend (fltk or rev), or install the signed Debian packages from this release's flat APT repository. Each backend has a separate bundle and extraction directory. Unpack the whole archive and keep bin/, lib/ and share/ together; required application libraries are bundled. Host audio/graphics services and drivers remain required. Debian installation instructions are in the release documentation linked below.

The attached warning.log describes known nonfatal Rev replay/waterfall display-cadence warnings. They do not fail builds or certification; data integrity, pending-message identity and physical-completion checks remain mandatory.

Packaging checks have passed when this release is published. Extensive contract, GUI and copied-binary checks run separately through the Certify published release workflow; its hash-bound reports will be attached here later. Hosted checks do not qualify physical audio hardware.

The ARM64 bundle requires a compatible 64-bit Linux installation, including Raspberry Pi OS or Velvet OS. Windows requires Windows 10 1903+/11 x64. See [requirements and certification scope](https://github.com/mirage335-colossus/pumpModem/blob/800da8c67611819c22236c7ef310cb518348ae2b/docs/releases.md).

This release includes copies of the prepared developer SDKs and dependency bundles it used, their complete source archives, and recipe checksums. Copies reused from the [base release](https://github.com/mirage335-colossus/pumpModem/releases/tag/base) remain available here even if older releases are deleted. Application releases never rebuild an SDK automatically. The runner supplies MSVC and the Windows SDK separately; they are not redistributed.
