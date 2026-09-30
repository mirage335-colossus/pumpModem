# Release certification: failed

Release `v001_00-2026-09-24-0856CDT`; source `9c83f5ec4ec9671931ffeb8069c5502c004ab495`; [workflow run 36010161027, attempt 1](https://github.com/mirage335-colossus/pumpModem/actions/runs/36010161027/attempts/1).

Hosted source contract, GUI and packaging tests, plus checks of the exact published archives. Linux containers share the runner kernel; Windows uses the selected Windows x64 hosted runner, with its image recorded in the workflow logs. Physical audio devices, Raspberry Pi and Chromebook hardware, and Windows 10/11 client installations are not qualified by this report.

**Windows Rev graphics coverage unavailable.** A green workflow includes a scoped environment warning; this release is not eligible for Latest until native graphics can be qualified.

WARNING windows-rev-wgl-unavailable: The runner cannot create the Rev OpenGL context. Native Windows Rev GUI tests and published GUI smoke are untested; compilation, headless GUI/CLI, modem, archive integrity and relocation checks remain mandatory. A Windows machine with the same graphics limitation cannot open the Rev GUI.
Omitted checks for windows-x86_64-rev: source:gui_workflow, source:gui_adapter_conformance, source:gui_coordinates_1x, source:gui_coordinates_2x, published:gui_smoke
Observed probe output: [NativeWindow] Required WGL ARB extensions not available

- apt-repository: **success**
- compatibility: **failure**
- distro-recipes: **success**
- linux-tests: **success**
- windows-tests: **success**

Recorded application targets: `linux-x86_64-fltk`, `linux-x86_64-rev`, `linux-aarch64-fltk`, `linux-aarch64-rev`, `windows-x86_64-fltk`, `windows-x86_64-rev`. Missing targets: none.

Display cadence warnings do not fail certification; content/physical/pending checks remain mandatory. Known limitations: [warning.log](https://github.com/mirage335-colossus/pumpModem/releases/download/v001_00-2026-09-24-0856CDT/warning.log).

Required coverage below is complete only when the report status is **passed**. Source suites rebuild the recorded release commit, including the full calibration tests. Archive checks run the published bytes identified by the hashes below.

- Source `linux-x86_64-fltk`: Debian 12 (source SDK); groups build, contract, gui, packaging.
- Source `linux-x86_64-rev`: Debian 12 (source SDK); groups build, contract, gui, packaging.
- Source `linux-aarch64-fltk`: Ubuntu 22.04; groups build, contract, gui, packaging.
- Source `linux-aarch64-rev`: Ubuntu 22.04; groups build, contract, gui, packaging.
- Source `windows-x86_64-fltk`: Selected Windows x64 hosted runner; groups contract, gui, packaging.
- Source `windows-x86_64-rev`: Selected Windows x64 hosted runner; groups contract, gui, packaging.

- Published `linux-x86_64-fltk`: Debian 12, Debian 13, Ubuntu 24.04, Ubuntu 26.04, Arch Linux; SHA-256, package manifest, CLI commands, GUI self-check and smoke, relocation to a path with spaces, runtime dependency closure, ELF ABI ceiling; GLIBC <= 2.36.
- Published `linux-x86_64-rev`: Debian 12, Debian 13, Ubuntu 24.04, Ubuntu 26.04, Arch Linux; SHA-256, package manifest, CLI commands, GUI self-check and smoke, relocation to a path with spaces, runtime dependency closure, ELF ABI ceiling; GLIBC <= 2.36.
- Published `linux-aarch64-fltk`: Debian 12, Debian 13, Ubuntu 24.04, Ubuntu 26.04, Ubuntu 22.04; SHA-256, package manifest, CLI commands, GUI self-check and smoke, relocation to a path with spaces, runtime dependency closure, ELF ABI ceiling; GLIBC <= 2.35.
- Published `linux-aarch64-rev`: Debian 12, Debian 13, Ubuntu 24.04, Ubuntu 26.04, Ubuntu 22.04; SHA-256, package manifest, CLI commands, GUI self-check and smoke, relocation to a path with spaces, runtime dependency closure, ELF ABI ceiling; GLIBC <= 2.35.
- Published `windows-x86_64-fltk`: Selected Windows x64 hosted runner; SHA-256, package manifest, CLI commands, GUI self-check and smoke, relocation to a path with spaces, runtime dependency closure.
- Published `windows-x86_64-rev`: Selected Windows x64 hosted runner; SHA-256, package manifest, CLI commands, GUI self-check and smoke, relocation to a path with spaces, runtime dependency closure.

Signed APT repository: signatures, indexes, package metadata and payloads must pass the apt-repository job against this same release inventory.


Published application SHA-256 values:

- `DataPump-v001_00-2026-09-24-0856CDT-linux-x86_64-fltk.tar.gz`: `6d16ad53b59049fc3c3992111e657c0ada5140f694bdfead97a70d5a47e32959`
- `DataPump-v001_00-2026-09-24-0856CDT-linux-x86_64-rev.tar.gz`: `4591a19c56e9d3e38a6408087a9588583ee1f989b89eac8d9e003416ab3f12e0`
- `DataPump-v001_00-2026-09-24-0856CDT-linux-aarch64-fltk.tar.gz`: `02cdf112c9addb82d357ea02589e8f3c64e388722cbc15a76ff962d21f537967`
- `DataPump-v001_00-2026-09-24-0856CDT-linux-aarch64-rev.tar.gz`: `fa46fd286a94854d5d34b2143729fc074ea8108ca0bb71ac5b5b38e10106823b`
- `DataPump-v001_00-2026-09-24-0856CDT-windows-x86_64-fltk.zip`: `e677117c4387daf26a17bae5f83a1897da978dfd00a34da7c98f964f822f7bb3`
- `DataPump-v001_00-2026-09-24-0856CDT-windows-x86_64-rev.zip`: `5504906a397308d424aff5e59e98a09bfc638d1d60de113b041bad87c8cfbdc3`

Published APT asset SHA-256 values:

- `InRelease`: `1fbbd62337541f2b412ed7601a746ca3b39e9a20a8a1984439441cb671c6b342`
- `Packages`: `8785842cc4f476dce35e6f4bac5458c9d81cfde8a8a14f54851ba09c2abc9334`
- `Packages.gz`: `139d0fb3244abeaadfdf258a62fc1d3e4d66b62aaf231069cc228b30bbb1119d`
- `Release`: `bf98fe4668581429dea5eb4aac106887d5d4129779cbf239b9f3348359aa3a00`
- `Release.gpg`: `c830a92a115aa3ba74dd5e0df760f4a5b9eee3b0f16a7055089d0e001198bcfb`
- `apt-repository.json`: `818e520d6b297139882515204d6ac6bd07d96ea1745bb6de1f9cc8340c8247aa`
- `datapump-archive-keyring.gpg`: `b240fb9919864dc242e37964ac6bc87c61751f08c179de3b54774e318cb111ee`
- `datapump-fltk_1.0.0+20260924135629.r36009020695.a1_amd64.deb`: `a8546e59a9866a797534ebd3f0b9ac51e5e59b483a0ec6e7b300ffacbb3f7d87`
- `datapump-fltk_1.0.0+20260924135629.r36009020695.a1_arm64.deb`: `e829dcbeecd206e8903227cb04dadc3b5424133795bc51dee4b7eec4ccba4b5e`
- `datapump-rev_1.0.0+20260924135629.r36009020695.a1_amd64.deb`: `ae1a8709b5f9a9d88fca7d5bbee7f291a3d65fe66bb7f5e9f307d2296745e46e`
- `datapump-rev_1.0.0+20260924135629.r36009020695.a1_arm64.deb`: `9fb15010fe18d66c323ebd23b080c7fb9f45788a74ac549e9df8e47fff037c36`
- `datapump.sources`: `cf8786676c70a57bcb08a2781368647e8bffb916b8bdb6e98a76bfd6462f07bd`

Published distribution delivery SHA-256 values (distro-recipes job required):

- `arch-repository.json`: `6cf1cf8261e12123a447253561e2f28fbc408595f4abe6a3e7115c936253fc37`
- `arch-repository.json.sig`: `d5a9e2af4983e34316e6546f9074cd0946b618a50f85251666e773e2f368bcc7`
- `datapump-aarch64.db`: `a5fa1cd1f3954b28f54e5aafba7012f1ca2687a34807216af21048fc4c0e0779`
- `datapump-aarch64.db.sig`: `794107852ae9809fe992a969f9cd3953b735a54203a5157bb0ea4583ab43ca23`
- `datapump-aarch64.files`: `db7ffa9901873886fe1741f42e4d6601398bbc7ca026650a92f1b59e9f660121`
- `datapump-aarch64.files.sig`: `35bd2232294cd9397f0c218381d27d16c1f97e664094535cdb93b19d9ed9046d`
- `datapump-arch-recipes.tar.gz`: `bc59c96257c2ceb0e4595d9b3fee1b62bce43a3a1806f91dd64e9b11102bbf68`
- `datapump-fltk-bin-1.0.0_p20260924135629_p36009020695_p1-1-aarch64.pkg.tar.gz`: `511fba7fe751cd882180a05b9ee1d75b989b4dba8fb97dba2dbac0ae884c0a2f`
- `datapump-fltk-bin-1.0.0_p20260924135629_p36009020695_p1-1-aarch64.pkg.tar.gz.sig`: `94f0cfcdea12a5e07a3e2bbea92991af5a3fd2cf5d3fe879caad240a1b25494d`
- `datapump-fltk-bin-1.0.0_p20260924135629_p36009020695_p1-1-x86_64.pkg.tar.gz`: `e516a32f2c126a104c4cd4bfd271df4a8808edde9de03ca526426777932d1754`
- `datapump-fltk-bin-1.0.0_p20260924135629_p36009020695_p1-1-x86_64.pkg.tar.gz.sig`: `94ed3fe88b06dd9ca153d64116b7230f9b44055dd467d1230397d8131c686a07`
- `datapump-gentoo-channel.json`: `23f3e80fe70d694324ed911bca68e07bbed018b1e5b2c1628666441510327cad`
- `datapump-gentoo-channel.json.asc`: `25deafd09f86ea1a3d4371724f9860ea3ccf8cd2e8d65695d892302685f5f781`
- `datapump-gentoo-overlay.tar.gz`: `5a5d83a17aa8236c327343f3b296a74876560747ea58af8c022024dfb96541f4`
- `datapump-gentoo-portage-sync.py`: `bffd98ff28ddd75a483ded6ebac6c23db055f3e4419151d64e2b52e038c4922d`
- `datapump-gentoo-sync.py`: `1987b4b71ba133fec56f2b555c19cb44b4ec8165a6943fb2d5ebe71478390496`
- `datapump-pacman-aarch64.conf`: `9363d1643c9e687f4526f5ec7d9ffbc226f25f2f61306efe8570a162bc4c0f6a`
- `datapump-pacman-keyring.gpg`: `b240fb9919864dc242e37964ac6bc87c61751f08c179de3b54774e318cb111ee`
- `datapump-pacman-x86_64.conf`: `f84f5f22cfb58f2d92ba7ea746ffe3b5fd4d892232c163d6c7fc7f72bee5f79d`
- `datapump-rev-bin-1.0.0_p20260924135629_p36009020695_p1-1-aarch64.pkg.tar.gz`: `5ce9c3407c03e331f5823945b7feeb93700e5f45c4054b07272804ebeffdc00b`
- `datapump-rev-bin-1.0.0_p20260924135629_p36009020695_p1-1-aarch64.pkg.tar.gz.sig`: `6aecd29b0261e0c22f8dfb16b452092feebb065a621eb0d16cf59dff29f08d75`
- `datapump-rev-bin-1.0.0_p20260924135629_p36009020695_p1-1-x86_64.pkg.tar.gz`: `9aadf87706cc3d00e4879d8335b68b7a1ee56c5762c0fc2e4af1482d8d3c15fd`
- `datapump-rev-bin-1.0.0_p20260924135629_p36009020695_p1-1-x86_64.pkg.tar.gz.sig`: `ed4db05362c383a358805f5e644b31cb3607190bcd2adac1e088bf33f1af4cb2`
- `datapump-x86_64.db`: `468e4f1b64b82233812d1b0e9138d2b94a1507f1b790194aa849ac5d5d6571c5`
- `datapump-x86_64.db.sig`: `9410d68790f517ce4762e631b09dae22558f2c051e004d90e4eb1f033a121878`
- `datapump-x86_64.files`: `4c375ef8a48e75c19a3a4f01a6301ffd1b2ad87ec05c8759be0a4525507ceb86`
- `datapump-x86_64.files.sig`: `ea60ca7cc987c8c641198d614b52a806917b89bf2070d7f2b06733d53f8b9030`
- `distro-packages.json`: `4d1c1c59399482e3daaa18d1f3946c25231246ecc8c1f16dccd5cfb01463e6a2`
