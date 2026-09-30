# Release certification: failed

Release `v001_00-2026-09-28-1246CDT`; source `2382f4e68607c4b0616ae11dc24511985121418a`; [workflow run 36466691708, attempt 1](https://github.com/mirage335-colossus/pumpModem/actions/runs/36466691708/attempts/1).

Hosted source contract, GUI and packaging tests, plus checks of the exact published archives. Linux containers share the runner kernel; Windows uses the selected Windows x64 hosted runner, with its image recorded in the workflow logs. Physical audio devices, Raspberry Pi and Chromebook hardware, and Windows 10/11 client installations are not qualified by this report.

TUI and framebuffer delivery and independent source checks are required by this release inventory.

**Windows Rev graphics coverage unavailable.** Hosted certification failed. The recognized hosted graphics exclusion does not block certification or Latest eligibility. The listed graphics checks remain untested; all other required checks remain mandatory. Failed or incomplete required checks keep this release ineligible for Latest.

WARNING windows-rev-wgl-unavailable: The runner cannot create the Rev OpenGL context. Native Windows Rev GUI tests and published GUI smoke are untested; compilation, headless GUI/CLI, modem, archive integrity and relocation checks remain mandatory. A Windows machine with the same graphics limitation cannot open the Rev GUI.
Omitted checks for windows-x86_64-rev: source:gui_workflow, source:gui_adapter_conformance, source:gui_coordinates_1x, source:gui_coordinates_2x, published:gui_smoke
Observed probe output: [NativeWindow] Required WGL ARB extensions not available

- apt-repository: **success**
- compatibility: **success**
- distro-recipes: **failure**
- linux-tests: **success**
- windows-tests: **failure**

Recorded application targets: `linux-x86_64-fltk`, `linux-x86_64-rev`, `linux-aarch64-fltk`, `linux-aarch64-rev`, `windows-x86_64-fltk`, `windows-x86_64-rev`. Missing targets: none.

Display cadence warnings do not fail certification; content/physical/pending checks remain mandatory. Known limitations: [warning.log](https://github.com/mirage335-colossus/pumpModem/releases/download/v001_00-2026-09-28-1246CDT/warning.log).

Hosted certification succeeds with status **passed** or **passed_with_warnings**. Documented exclusions remain untested; incomplete smoke coverage remains unqualified. All other required coverage must pass. Source suites rebuild the recorded release commit, including the full calibration tests. Archive checks run the published bytes identified by the hashes below.

- Source `linux-x86_64-fltk`: Debian 12 (source SDK); groups build, contract, gui, packaging, frontends (independent job).
- Source `linux-x86_64-rev`: Debian 12 (source SDK); groups build, contract, gui, packaging, frontends (independent job).
- Source `linux-aarch64-fltk`: Ubuntu 22.04; groups build, contract, gui, packaging, frontends (independent job).
- Source `linux-aarch64-rev`: Ubuntu 22.04; groups build, contract, gui, packaging, frontends (independent job).
- Source `windows-x86_64-fltk`: Selected Windows x64 hosted runner; groups contract, gui, packaging, frontends (independent job).
- Source `windows-x86_64-rev`: Selected Windows x64 hosted runner; groups contract, gui, packaging, frontends (independent job).

- Published `linux-x86_64-fltk`: Debian 12, Debian 13, Ubuntu 24.04, Ubuntu 26.04, Arch Linux; SHA-256, package manifest, CLI commands, GUI self-check and smoke, relocation to a path with spaces, runtime dependency closure, required TUI and framebuffer binaries/manuals, TUI and framebuffer self-checks, ELF ABI ceiling; GLIBC <= 2.36.
- Published `linux-x86_64-rev`: Debian 12, Debian 13, Ubuntu 24.04, Ubuntu 26.04, Arch Linux; SHA-256, package manifest, CLI commands, GUI self-check and smoke, relocation to a path with spaces, runtime dependency closure, required TUI and framebuffer binaries/manuals, TUI and framebuffer self-checks, ELF ABI ceiling; GLIBC <= 2.36.
- Published `linux-aarch64-fltk`: Debian 12, Debian 13, Ubuntu 24.04, Ubuntu 26.04, Ubuntu 22.04; SHA-256, package manifest, CLI commands, GUI self-check and smoke, relocation to a path with spaces, runtime dependency closure, required TUI and framebuffer binaries/manuals, TUI and framebuffer self-checks, ELF ABI ceiling; GLIBC <= 2.35.
- Published `linux-aarch64-rev`: Debian 12, Debian 13, Ubuntu 24.04, Ubuntu 26.04, Ubuntu 22.04; SHA-256, package manifest, CLI commands, GUI self-check and smoke, relocation to a path with spaces, runtime dependency closure, required TUI and framebuffer binaries/manuals, TUI and framebuffer self-checks, ELF ABI ceiling; GLIBC <= 2.35.
- Published `windows-x86_64-fltk`: Selected Windows x64 hosted runner; SHA-256, package manifest, CLI commands, GUI self-check and smoke, relocation to a path with spaces, runtime dependency closure, required TUI and framebuffer binaries/manuals, TUI and framebuffer self-checks.
- Published `windows-x86_64-rev`: Selected Windows x64 hosted runner; SHA-256, package manifest, CLI commands, GUI self-check and smoke, relocation to a path with spaces, runtime dependency closure, required TUI and framebuffer binaries/manuals, TUI and framebuffer self-checks.

Signed APT repository: signatures, indexes, package metadata and payloads must pass the apt-repository job against this same release inventory.


Published application SHA-256 values:

- `DataPump-v001_00-2026-09-28-1246CDT-linux-x86_64-fltk.tar.gz`: `61257988390873f8afe16ca67eca1499df04097f47bbd7e84936e3b4c2759561`
- `DataPump-v001_00-2026-09-28-1246CDT-linux-x86_64-rev.tar.gz`: `8963e89cc154cacb5e7ffe93561794e009a635aab7a92e0f5cbbc664962bf94a`
- `DataPump-v001_00-2026-09-28-1246CDT-linux-aarch64-fltk.tar.gz`: `cef1baa4b010196aa6736064a0ff8cf580348728582b40310f21d4a3334eec97`
- `DataPump-v001_00-2026-09-28-1246CDT-linux-aarch64-rev.tar.gz`: `dad2a8db288ab8776e5dd9fd229af81027a23d9665998e800cb1de8fef2326b3`
- `DataPump-v001_00-2026-09-28-1246CDT-windows-x86_64-fltk.zip`: `e6141b834ad6461d1a6fb608dd74df89a2bd82006753794d23d51a49de14725d`
- `DataPump-v001_00-2026-09-28-1246CDT-windows-x86_64-rev.zip`: `e3312869cbb49e0afab60c00dfa379c3fffce23a202c654017a6d0efebf324b1`

Published APT asset SHA-256 values:

- `InRelease`: `df2b73baad7f658bf566f7c23e8546448321423d2f306bdee56fe5b01521daf1`
- `Packages`: `398fe7d730df32f23ab347e78bb3b2ab8b8927c82ca4385cc0bf4499d7f07f89`
- `Packages.gz`: `8d8d1a378504f1a7813197e9e664be628c90a50c2c30ba251571df9fedd75f8b`
- `Release`: `0241b3d9350ef326934a0427e323ea6d08171bc1bd99fbf60fa0f9aa11daa9d5`
- `Release.gpg`: `ed653ee98bedf68c2a37857d450e7f65c4ea14510b8a557a1d73a90aa0ad2b69`
- `apt-repository.json`: `f3e6e5cf2b94cf93ffb5f23a3f07bdb87c704bba85972f4c953f00c3611ab587`
- `datapump-archive-keyring.gpg`: `b240fb9919864dc242e37964ac6bc87c61751f08c179de3b54774e318cb111ee`
- `datapump-fltk_1.0.0+20260928174606.r36455727999.a1_amd64.deb`: `ee80660f53fea1631be31ecaf051047790273114ce33621231ecddafbf6e657b`
- `datapump-fltk_1.0.0+20260928174606.r36455727999.a1_arm64.deb`: `5844ab1857d7732566f86b446a90ccb26b81e24563e332a07de23fece06afcdd`
- `datapump-rev_1.0.0+20260928174606.r36455727999.a1_amd64.deb`: `2d946cd3b0407b56658e23bbb64412dfb7b69e8a950b5849fb69b8f9e9e6b703`
- `datapump-rev_1.0.0+20260928174606.r36455727999.a1_arm64.deb`: `3aab26e426b429cd4ba458fb774c159781187e275d350f3c70f151e53de53293`
- `datapump.sources`: `cf8786676c70a57bcb08a2781368647e8bffb916b8bdb6e98a76bfd6462f07bd`

Published distribution delivery SHA-256 values (distro-recipes job required):

- `arch-repository.json`: `074c8ed033964adb850ab843165eec6dd615e50472a8ddcbfb813ab797f57668`
- `arch-repository.json.sig`: `ce756842c7e63cda70c090c9286b0ebd673844c21eb4cb2c73b7e43607ccfb4f`
- `datapump-aarch64.db`: `70319609817e22875866b79dd1f22e7b82c025dca3dd60ce9c1c56b5cc2e9d3b`
- `datapump-aarch64.db.sig`: `dc29cbdbaa0c7aedf11ff43f9b939d2f747135edbbedda3a562984118810929c`
- `datapump-aarch64.files`: `a6ff8fd5b063d1f83621b4f66d3bde74ffc0f6335cb8b9d4b0a9a1151e34826e`
- `datapump-aarch64.files.sig`: `a5b2f367e202b4c1e0e6bbf2dc1de794986413b1bdf346175d8566b57ad96b23`
- `datapump-arch-recipes.tar.gz`: `3067a283a7730ba6a3f1fd89615dd8099c0ff7aa11858c0ce050d89497f4bf0d`
- `datapump-fltk-bin-1.0.0_p20260928174606_p36455727999_p1-1-aarch64.pkg.tar.gz`: `3a6bb6e67a20a8d56a25f5338e81acf15be9d07c3c680b63eed5aedb95183017`
- `datapump-fltk-bin-1.0.0_p20260928174606_p36455727999_p1-1-aarch64.pkg.tar.gz.sig`: `3b18536f30b751878f8a4158907f8a2d50b87360bd1c24e6eeb9dc5ee2bc9887`
- `datapump-fltk-bin-1.0.0_p20260928174606_p36455727999_p1-1-x86_64.pkg.tar.gz`: `1575812ea2e66271681aa8d0b04289c3c0782cc622d3510acb6b8390b03e7d5b`
- `datapump-fltk-bin-1.0.0_p20260928174606_p36455727999_p1-1-x86_64.pkg.tar.gz.sig`: `bddb356c285bec020272d41737293992415c9b2be1ff3b44917bc02eb8394185`
- `datapump-gentoo-channel.json`: `18494a56b4cc02e3578d70aae3b2bcba387ef47c331031950af20af0a16b1ffb`
- `datapump-gentoo-channel.json.asc`: `8e54f24b75bcb673e3d3050cb3b0b7578123cff1b3500ed9562143b7666b667d`
- `datapump-gentoo-overlay.tar.gz`: `411b981f279b72f769e4b11ef5cfa10d1751bb519f3bd5936a4cf6712f6a9076`
- `datapump-gentoo-portage-sync.py`: `bffd98ff28ddd75a483ded6ebac6c23db055f3e4419151d64e2b52e038c4922d`
- `datapump-gentoo-sync.py`: `1987b4b71ba133fec56f2b555c19cb44b4ec8165a6943fb2d5ebe71478390496`
- `datapump-pacman-aarch64.conf`: `38026d5bf0f20a59fcc2272e58805e7fd74a2a770c3e5c2c11a6b6be1654e6cb`
- `datapump-pacman-keyring.gpg`: `b240fb9919864dc242e37964ac6bc87c61751f08c179de3b54774e318cb111ee`
- `datapump-pacman-x86_64.conf`: `4c52d567c0a9042e3e7bf2f3ec4c5633f6852b1a0f669b9b601b8beb79170a58`
- `datapump-rev-bin-1.0.0_p20260928174606_p36455727999_p1-1-aarch64.pkg.tar.gz`: `c96d42f1e3bad4ecf1d7050d729b7c77649434ab82ca44c998f782e64f9fb878`
- `datapump-rev-bin-1.0.0_p20260928174606_p36455727999_p1-1-aarch64.pkg.tar.gz.sig`: `bf3b47dab8cecc5c71e482628ca057872f3b8e1393781f47eb1d220bc4541ca8`
- `datapump-rev-bin-1.0.0_p20260928174606_p36455727999_p1-1-x86_64.pkg.tar.gz`: `69820894dd4f121e61eb3ca46be553bdab133262dad8f6f9bdf539a4c6c6fd55`
- `datapump-rev-bin-1.0.0_p20260928174606_p36455727999_p1-1-x86_64.pkg.tar.gz.sig`: `29987530e0ca44a1bdbba0e02e098ad4a4006147a5f77fd6466f48745a2fa0a5`
- `datapump-x86_64.db`: `ad5afb91222566c023e0989f174a67aaf0a10581e9a750047080663b39f4093b`
- `datapump-x86_64.db.sig`: `f65d57debb664871bf647e328ed50286c7da360cdabb5882d0aeea8d9ae2f8c1`
- `datapump-x86_64.files`: `8605688e93458f97b4bc616fa6b521c19bb8823ef83247bc452873c5ef9f1b82`
- `datapump-x86_64.files.sig`: `e369fe8f94d3f69da443fa31574cfeaf8123701864773ffb7a065a48c470cd9b`
- `distro-packages.json`: `5cc91992fa123997fd18c0d325e0802129fe5c0d7f934e039fdca65fb701003f`

Preserved SDK/dependency SHA-256 values:

- `datapump-sdk-6c4884fdff9c745ab0a0-linux-x86_64.tar.gz`: `a1050115f278d298947a6516a52ec5e848ffec370649cdb24985e2f16fde7985`
- `datapump-sdk-sources-6c4884fdff9c745ab0a0.tar.gz`: `26ef9fb79f5b0b7ebac17498dd9ec847dd488e796a5a8578e59477ecec26d5a5`
- `sdk-6c4884fdff9c745ab0a0-SHA256SUMS.txt`: `63e2e4492870287a3799d571c9f670d92c9c08564906f7b1c32a695d73e0b127`
- `windows-base-929016c86c9609704043-SHA256SUMS.txt`: `0d5d6e993eef43ad3f7002ec0e9710097366bc98b21adc22637dfd78d48e0f1b`
- `windows-base-929016c86c9609704043-x64-windows-static.zip`: `fbf4b93aeeec72b40a44a2c2c22a1732a7a160674621452e5fca5e7a793ada87`
- `windows-base-sources-929016c86c9609704043.zip`: `b40028617506d1db67bdf384bbc5d85466b9002a6bbbf8d8689f8af9040c887a`
