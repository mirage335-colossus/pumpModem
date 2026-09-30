# Release certification: failed

Release `v001_00-2026-09-30-0505CDT`; source `79bf20c840ce279af226a36bc698db3280664ea8`; [workflow run 36695310257, attempt 1](https://github.com/mirage335-colossus/pumpModem/actions/runs/36695310257/attempts/1).

Hosted source contract, GUI and packaging tests, plus checks of the exact published archives. Linux containers share the runner kernel; Windows uses the selected Windows x64 hosted runner, with its image recorded in the workflow logs. Physical audio devices, Raspberry Pi and Chromebook hardware, and Windows 10/11 client installations are not qualified by this report.

TUI and framebuffer delivery and independent source checks are required by this release inventory.

Standalone HTML/Wasm delivery and independent Linux worker/Wasm source checks are required. Physical browser microphone, playback and download behavior remains unqualified.

**Windows Rev graphics coverage unavailable.** Hosted certification failed. The recognized hosted graphics exclusion does not block certification or Latest eligibility. The listed graphics checks remain untested; all other required checks remain mandatory. Failed or incomplete required checks keep this release ineligible for Latest.

WARNING windows-rev-wgl-unavailable: The runner cannot create the Rev OpenGL context. Native Windows Rev GUI tests and published GUI smoke are untested; compilation, headless GUI/CLI, modem, archive integrity and relocation checks remain mandatory. A Windows machine with the same graphics limitation cannot open the Rev GUI.
Omitted checks for windows-x86_64-rev: source:gui_workflow, source:gui_adapter_conformance, source:gui_coordinates_1x, source:gui_coordinates_2x, published:gui_smoke
Observed probe output: [NativeWindow] Required WGL ARB extensions not available

- apt-repository: **success**
- compatibility: **success**
- distro-recipes: **success**
- linux-tests: **failure**
- web-tests: **failure**
- windows-tests: **success**

Recorded application targets: `linux-x86_64-fltk`, `linux-x86_64-rev`, `linux-aarch64-fltk`, `linux-aarch64-rev`, `windows-x86_64-fltk`, `windows-x86_64-rev`. Missing targets: none.

Display cadence warnings do not fail certification; content/physical/pending checks remain mandatory. Known limitations: [warning.log](https://github.com/mirage335-colossus/pumpModem/releases/download/v001_00-2026-09-30-0505CDT/warning.log).

Hosted certification succeeds with status **passed** or **passed_with_warnings**. Documented exclusions remain untested; incomplete smoke coverage remains unqualified. All other required coverage must pass. Source suites rebuild the recorded release commit, including the full calibration tests. Archive checks run the published bytes identified by the hashes below.

- Source `linux-x86_64-fltk`: Debian 12 (source SDK); groups build, contract, gui, packaging, frontends (independent job), web worker (independent job), Wasm web (independent job).
- Source `linux-x86_64-rev`: Debian 12 (source SDK); groups build, contract, gui, packaging, frontends (independent job), web worker (independent job), Wasm web (independent job).
- Source `linux-aarch64-fltk`: Ubuntu 22.04; groups build, contract, gui, packaging, frontends (independent job), web worker (independent job).
- Source `linux-aarch64-rev`: Ubuntu 22.04; groups build, contract, gui, packaging, frontends (independent job), web worker (independent job).
- Source `windows-x86_64-fltk`: Selected Windows x64 hosted runner; groups contract, gui, packaging, frontends (independent job).
- Source `windows-x86_64-rev`: Selected Windows x64 hosted runner; groups contract, gui, packaging, frontends (independent job).

- Published `linux-x86_64-fltk`: Debian 12, Debian 13, Ubuntu 24.04, Ubuntu 26.04, Arch Linux; SHA-256, package manifest, CLI commands, GUI self-check and smoke, relocation to a path with spaces, runtime dependency closure, required TUI and framebuffer binaries/manuals, TUI and framebuffer self-checks, required standalone HTML/Wasm payload, source/SDK identity, notices and checksums, ELF ABI ceiling; GLIBC <= 2.36.
- Published `linux-x86_64-rev`: Debian 12, Debian 13, Ubuntu 24.04, Ubuntu 26.04, Arch Linux; SHA-256, package manifest, CLI commands, GUI self-check and smoke, relocation to a path with spaces, runtime dependency closure, required TUI and framebuffer binaries/manuals, TUI and framebuffer self-checks, required standalone HTML/Wasm payload, source/SDK identity, notices and checksums, ELF ABI ceiling; GLIBC <= 2.36.
- Published `linux-aarch64-fltk`: Debian 12, Debian 13, Ubuntu 24.04, Ubuntu 26.04, Ubuntu 22.04; SHA-256, package manifest, CLI commands, GUI self-check and smoke, relocation to a path with spaces, runtime dependency closure, required TUI and framebuffer binaries/manuals, TUI and framebuffer self-checks, required standalone HTML/Wasm payload, source/SDK identity, notices and checksums, ELF ABI ceiling; GLIBC <= 2.35.
- Published `linux-aarch64-rev`: Debian 12, Debian 13, Ubuntu 24.04, Ubuntu 26.04, Ubuntu 22.04; SHA-256, package manifest, CLI commands, GUI self-check and smoke, relocation to a path with spaces, runtime dependency closure, required TUI and framebuffer binaries/manuals, TUI and framebuffer self-checks, required standalone HTML/Wasm payload, source/SDK identity, notices and checksums, ELF ABI ceiling; GLIBC <= 2.35.
- Published `windows-x86_64-fltk`: Selected Windows x64 hosted runner; SHA-256, package manifest, CLI commands, GUI self-check and smoke, relocation to a path with spaces, runtime dependency closure, required TUI and framebuffer binaries/manuals, TUI and framebuffer self-checks, required standalone HTML/Wasm payload, source/SDK identity, notices and checksums.
- Published `windows-x86_64-rev`: Selected Windows x64 hosted runner; SHA-256, package manifest, CLI commands, GUI self-check and smoke, relocation to a path with spaces, runtime dependency closure, required TUI and framebuffer binaries/manuals, TUI and framebuffer self-checks, required standalone HTML/Wasm payload, source/SDK identity, notices and checksums.

Signed APT repository: signatures, indexes, package metadata and payloads must pass the apt-repository job against this same release inventory.


Published application SHA-256 values:

- `DataPump-v001_00-2026-09-30-0505CDT-linux-x86_64-fltk.tar.gz`: `14e4280c32dc10fed5f709ddd004dea70f96792944bbd6923a7061a05db948ba`
- `DataPump-v001_00-2026-09-30-0505CDT-linux-x86_64-rev.tar.gz`: `6e8e5d9abbd131c97bb2273d24da01a516995d5f152a5cf8dfb94ae2c26fd398`
- `DataPump-v001_00-2026-09-30-0505CDT-linux-aarch64-fltk.tar.gz`: `4c3418346057bbd6d7e56ac826d371e84fd596f749d5ac0d3ba733227cdbe90e`
- `DataPump-v001_00-2026-09-30-0505CDT-linux-aarch64-rev.tar.gz`: `690fa2ead560265551a243efac918d1d9d855092aa21e0311b99e33735e4bdda`
- `DataPump-v001_00-2026-09-30-0505CDT-windows-x86_64-fltk.zip`: `5c7c34eb5a28efe466c7d948814f7756823beb47adadc82e5ade7b160ec6202d`
- `DataPump-v001_00-2026-09-30-0505CDT-windows-x86_64-rev.zip`: `b2364b7259d3c2b2ad405532ba8d93a1452eea75d6547d4cdb22ebc4674cb60c`

Published APT asset SHA-256 values:

- `InRelease`: `e38fa2fb1bc001eaa0c8feac12d4c103f5dbcbdcdd8b01cff8167e7fed898482`
- `Packages`: `b524cc6f6a824802ff5471703c1c28ccf38e98a7a0678c0d3fa8c28409adeaea`
- `Packages.gz`: `fc09742bc12166827795daaa20975efae816737ef26dede939279f034ab42a39`
- `Release`: `8a2a267a307a651c1f327b4c72ccc48c47ddec0f5cd1e4362e9a7e1df04ce9dc`
- `Release.gpg`: `6a4a5ab52ba866fed198abbb69527ceefda61a8d71dbc7ee1de74c664a383215`
- `apt-repository.json`: `706f5f496eddcc191ed349040363db45440c094cb5c198572a9e1aa6034ee934`
- `datapump-archive-keyring.gpg`: `b240fb9919864dc242e37964ac6bc87c61751f08c179de3b54774e318cb111ee`
- `datapump-fltk_1.0.0+20260930100523.r36695310257.a1_amd64.deb`: `5e4f41df8d812c09a89220a5a68b98ef0faa45a2dcd477731361ece149c0654a`
- `datapump-fltk_1.0.0+20260930100523.r36695310257.a1_arm64.deb`: `e252501e2b13e1e6ad3b02a2278e302012010c0452134f48c75ab71752fec828`
- `datapump-rev_1.0.0+20260930100523.r36695310257.a1_amd64.deb`: `2bd1cd19e471d9eebb4e75fbe82a12aa23ad32ed604b2b5c3e9a4f5e3a3c52e7`
- `datapump-rev_1.0.0+20260930100523.r36695310257.a1_arm64.deb`: `51ed51f34539665a310dc3c412d0d0de3c14aea992e68e9affc990ce2b01e3d9`
- `datapump.sources`: `cf8786676c70a57bcb08a2781368647e8bffb916b8bdb6e98a76bfd6462f07bd`

Published distribution delivery SHA-256 values (distro-recipes job required):

- `arch-repository.json`: `5c8acf15da2b0766f11ca3c4a4e1983e90be6f1ea0bbcdebe1509f5ca2e18cf0`
- `arch-repository.json.sig`: `a18c7641ea9c7e0a31b58baaffc825671ebe5535c9e81206fd4bdcdb3e1fb4c9`
- `datapump-aarch64.db`: `5e03b4e0a4950042c6142ad83aeb8af807ba32855cfaee4fdea1f3f7387b5673`
- `datapump-aarch64.db.sig`: `0d564c11da02b4f6139040519794423803979fb266ac54122609d67057c41953`
- `datapump-aarch64.files`: `0e05ab5017d2148aa22f2cf04390b5929f8d9b953283c03782b6cd7a1637e02d`
- `datapump-aarch64.files.sig`: `c2e18a10774fb919e6f47eae9ace458c722596ad1f6e4e2b5df5d041edcf3d92`
- `datapump-arch-recipes.tar.gz`: `b7d7df8253cc3a014b855da978727413cec2562ce80bdec20a70de9a51f0fa96`
- `datapump-fltk-bin-1.0.0_p20260930100523_p36695310257_p1-1-aarch64.pkg.tar.gz`: `009b7304485af57d1680c9db31fd2bf2a5860988c18108a23841e8bd947be3ec`
- `datapump-fltk-bin-1.0.0_p20260930100523_p36695310257_p1-1-aarch64.pkg.tar.gz.sig`: `ed67adfd0f38c91a352059891555eaa1f785e2c448f7d196a478f761494a88e5`
- `datapump-fltk-bin-1.0.0_p20260930100523_p36695310257_p1-1-x86_64.pkg.tar.gz`: `cf5a1343f3cc1f857397970a48dc0121458b9e3125391e9ceca79b154b4a1486`
- `datapump-fltk-bin-1.0.0_p20260930100523_p36695310257_p1-1-x86_64.pkg.tar.gz.sig`: `403090ffabe612718a8ee35e6439ed58abf6077df16460e2eef4bcc5995a2d98`
- `datapump-gentoo-channel.json`: `6a88e77be4471fe5d37d84b2bde0af0d869843efa6ad1e13527c3aab5de7cbf0`
- `datapump-gentoo-channel.json.asc`: `95fe5c16f06789c33bf46be3082b0c39cc589b9e29f1251b6225cd19c133964c`
- `datapump-gentoo-overlay.tar.gz`: `ea0cc2bba4471bd9ca2d49e4cf7802b13189767a94e6abfca4ac7800df243598`
- `datapump-gentoo-portage-sync.py`: `bffd98ff28ddd75a483ded6ebac6c23db055f3e4419151d64e2b52e038c4922d`
- `datapump-gentoo-sync.py`: `1987b4b71ba133fec56f2b555c19cb44b4ec8165a6943fb2d5ebe71478390496`
- `datapump-pacman-aarch64.conf`: `8e100a59380759ed36fb1b760f2cd5f507f94a7a3df320c611f6505a178b3804`
- `datapump-pacman-keyring.gpg`: `b240fb9919864dc242e37964ac6bc87c61751f08c179de3b54774e318cb111ee`
- `datapump-pacman-x86_64.conf`: `c300e44561484a5753d3340bb6583c90975bc65f5dcb7c05970b0e8d673bf2d5`
- `datapump-rev-bin-1.0.0_p20260930100523_p36695310257_p1-1-aarch64.pkg.tar.gz`: `821208134db0c71b9e8c49ab7db658dc7c972f5eaa437debcca4c252bff4c622`
- `datapump-rev-bin-1.0.0_p20260930100523_p36695310257_p1-1-aarch64.pkg.tar.gz.sig`: `d835b36f7c605ebf77182e0ba6b7d4b090d0929ccc3104fe2fa6a3d87d506606`
- `datapump-rev-bin-1.0.0_p20260930100523_p36695310257_p1-1-x86_64.pkg.tar.gz`: `448a5c35fb1895d08904cad1cb4e73b3f23630567738776f676bd8b023505938`
- `datapump-rev-bin-1.0.0_p20260930100523_p36695310257_p1-1-x86_64.pkg.tar.gz.sig`: `857cac8d73b18d4f1dab5aa73aa61eb3025dcac3e417622d0a98de137e84b6cd`
- `datapump-x86_64.db`: `b699d8789b91d56eec2da6dbb29e10376c4c3d38920900896dc16904194e0e39`
- `datapump-x86_64.db.sig`: `c97989ae78f7daae5a9a9eb62be5dfed5029c2631f7c16b1dbdde6af81c91df7`
- `datapump-x86_64.files`: `3af48891dd1d6fe86b4ac9f047d030692bc85e21ad1a71d53e0b7b0249bb2f00`
- `datapump-x86_64.files.sig`: `0e2d7c2ba68ced126e2cbcf1320b3e13c4ce8ddaf368552cbdbba2a5c4611297`
- `distro-packages.json`: `d895974c56a852195871a3f632811fb2045d9d4c89fadcc705d8f791bea6664e`

Preserved SDK/dependency SHA-256 values:

- `datapump-sdk-6c4884fdff9c745ab0a0-linux-x86_64.tar.gz`: `a1050115f278d298947a6516a52ec5e848ffec370649cdb24985e2f16fde7985`
- `datapump-sdk-sources-6c4884fdff9c745ab0a0.tar.gz`: `26ef9fb79f5b0b7ebac17498dd9ec847dd488e796a5a8578e59477ecec26d5a5`
- `datapump-wasm-sdk-e66e98abb90b466cab32-linux-x86_64.tar.gz`: `bae40c03d2aea930a9755a272c95bfc4bdcfa8c19dcc2b223c0b704c5afab224`
- `datapump-wasm-sdk-sources-e66e98abb90b466cab32.tar.gz`: `4309fce79edc795a99d1bc6c1114071a679b172fcb2244a24ddf78a8155d9c07`
- `sdk-6c4884fdff9c745ab0a0-SHA256SUMS.txt`: `63e2e4492870287a3799d571c9f670d92c9c08564906f7b1c32a695d73e0b127`
- `wasm-sdk-e66e98abb90b466cab32-SHA256SUMS.txt`: `e42478883250ef4bacc942b39d7eb66d6f54f34a1b981663276a2b5d7964c48e`
- `windows-base-929016c86c9609704043-SHA256SUMS.txt`: `0d5d6e993eef43ad3f7002ec0e9710097366bc98b21adc22637dfd78d48e0f1b`
- `windows-base-929016c86c9609704043-x64-windows-static.zip`: `fbf4b93aeeec72b40a44a2c2c22a1732a7a160674621452e5fca5e7a793ada87`
- `windows-base-sources-929016c86c9609704043.zip`: `b40028617506d1db67bdf384bbc5d85466b9002a6bbbf8d8689f8af9040c887a`
