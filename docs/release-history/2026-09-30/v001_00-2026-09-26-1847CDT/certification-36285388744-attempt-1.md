# Release certification: failed

Release `v001_00-2026-09-26-1847CDT`; source `800da8c67611819c22236c7ef310cb518348ae2b`; [workflow run 36285388744, attempt 1](https://github.com/mirage335-colossus/pumpModem/actions/runs/36285388744/attempts/1).

Hosted source contract, GUI and packaging tests, plus checks of the exact published archives. Linux containers share the runner kernel; Windows uses the selected Windows x64 hosted runner, with its image recorded in the workflow logs. Physical audio devices, Raspberry Pi and Chromebook hardware, and Windows 10/11 client installations are not qualified by this report.

- apt-repository: **success**
- compatibility: **cancelled**
- distro-recipes: **cancelled**
- linux-tests: **cancelled**
- windows-tests: **failure**

Recorded application targets: `linux-x86_64-fltk`, `linux-x86_64-rev`, `linux-aarch64-fltk`, `linux-aarch64-rev`, `windows-x86_64-fltk`, `windows-x86_64-rev`. Missing targets: none.

Display cadence warnings do not fail certification; content/physical/pending checks remain mandatory. Known limitations: [warning.log](https://github.com/mirage335-colossus/pumpModem/releases/download/v001_00-2026-09-26-1847CDT/warning.log).

Hosted certification succeeds with status **passed** or **passed_with_warnings**. Documented exclusions remain untested; incomplete smoke coverage remains unqualified. All other required coverage must pass. Source suites rebuild the recorded release commit, including the full calibration tests. Archive checks run the published bytes identified by the hashes below.

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

- `DataPump-v001_00-2026-09-26-1847CDT-linux-x86_64-fltk.tar.gz`: `b0c6e4646b23a7318ce7657c8d7968adf570412633d80d0e22ca1dd4448c5a65`
- `DataPump-v001_00-2026-09-26-1847CDT-linux-x86_64-rev.tar.gz`: `5a2beae8867b0894d2fa5d79151c73485c37c806db2951d3d282c4d70dcba479`
- `DataPump-v001_00-2026-09-26-1847CDT-linux-aarch64-fltk.tar.gz`: `891e9a13ead6bf779d90ae4af77a5b183caf0fbb42074f363f392ae19adf5519`
- `DataPump-v001_00-2026-09-26-1847CDT-linux-aarch64-rev.tar.gz`: `1079d2654109c8ae7506a420f04ffcfe277a5dd684aa13d2730360b166fa33b0`
- `DataPump-v001_00-2026-09-26-1847CDT-windows-x86_64-fltk.zip`: `89b8de97f728c3ebe3a6fb42f48a11b489aaad1195464f6fcb809b8fd0ceb053`
- `DataPump-v001_00-2026-09-26-1847CDT-windows-x86_64-rev.zip`: `7413c16566b3234321bc1f820e7c2ab6544bc481c76315cdce307fb99e505c87`

Published APT asset SHA-256 values:

- `InRelease`: `14bb3e42970350c0ff4d7936c0e72a81cc3839aa17e5d93ab5e8300467418c48`
- `Packages`: `b66167159a97eebac6601f6dc32367a0be76bc1efa1ebaff7930aa6f3f2c9eba`
- `Packages.gz`: `232516cdcd5b7600446981087d352d8072160f7fe7ccb6e990f5e55b3d1b60a9`
- `Release`: `671ecd88a1b7c3ec48368b70ced1366c15bbcacb92e584b848696bf13a889d2f`
- `Release.gpg`: `759973fd61d59ac11e349b50cec6d1d6f9a34340624161abcda47effdab54f93`
- `apt-repository.json`: `722b68a1c11bf4deef5f40ac609e9cf8ac0fd836f69ab0da2789fc7092f4cdfc`
- `datapump-archive-keyring.gpg`: `b240fb9919864dc242e37964ac6bc87c61751f08c179de3b54774e318cb111ee`
- `datapump-fltk_1.0.0+20260926234704.r36280483610.a1_amd64.deb`: `bf849057ebbe1ad82abbaabf7e5de317c011e59b8af806e0c657b28d6827cfe6`
- `datapump-fltk_1.0.0+20260926234704.r36280483610.a1_arm64.deb`: `f103cca63cf4dc189c51eab8eeac53f9e2294a7ff6e4f177e2f251a4c1b002e9`
- `datapump-rev_1.0.0+20260926234704.r36280483610.a1_amd64.deb`: `3f0b49329f3f5e6c95086f363e2f62e8b48cd01ea89dd803bdf54fbc99b4a3bf`
- `datapump-rev_1.0.0+20260926234704.r36280483610.a1_arm64.deb`: `c695fc9fda7e86b74227102fed5453c77bceef1ceb66f15c01fdb1a329b401ac`
- `datapump.sources`: `3b727b4579b09166af3f475c96efc84e544bc63b25c71a55881729a1fd826ca2`

Published distribution delivery SHA-256 values (distro-recipes job required):

- `arch-repository.json`: `7f0d524e17c7efbc5edf6c7ff0a1ba19bc0edff2a5aa3078273308a4559bf4d4`
- `arch-repository.json.sig`: `ab2879507a5cee023ad00c6d636f56ffcaa15d37df99f18f9f99ac49e5a5281f`
- `datapump-aarch64.db`: `b13bcd4e94bec17a042fef1861666b4327c656c2a135ba913fdc6a731b14bca2`
- `datapump-aarch64.db.sig`: `aea0c45ad6cdc6245bc84ec02ce3e7d10a38efcb62dedc5605fdd0d36d807ac5`
- `datapump-aarch64.files`: `15f46dcb71ca5e59d76f6713942c3887f690a876d5d88721023c28e62adc8563`
- `datapump-aarch64.files.sig`: `f974e2a174f6b2cfac4b487faa24d97597b72c81e77a28a2d4e316e76f403ea9`
- `datapump-arch-recipes.tar.gz`: `77a36ac16d9f3cff6cda35aeb064485e21cd2bd5e99ed9b0f932df14050cf4dd`
- `datapump-fltk-bin-1.0.0_p20260926234704_p36280483610_p1-1-aarch64.pkg.tar.gz`: `51dc485a03aa0d2ec4de191465974cebbf55354f8761c4f4e258f889fba41923`
- `datapump-fltk-bin-1.0.0_p20260926234704_p36280483610_p1-1-aarch64.pkg.tar.gz.sig`: `4837120804d42122acd7d01be3f44218458953b3380e7b525d2d5f5f0e36a62a`
- `datapump-fltk-bin-1.0.0_p20260926234704_p36280483610_p1-1-x86_64.pkg.tar.gz`: `52f81ea58f21b3fde5b8f7c1994592151046968bb1cdbcde5a46b75d59a95f4a`
- `datapump-fltk-bin-1.0.0_p20260926234704_p36280483610_p1-1-x86_64.pkg.tar.gz.sig`: `fa8f6d414993d2ad007b4a1a5caad493723a2912830a56790efb6bfbb6b44723`
- `datapump-gentoo-channel.json`: `777cca8d8b8a32125627fe18ed46596973e2b9b0a939a1792f99374c7e827a46`
- `datapump-gentoo-channel.json.asc`: `d4fa5455733aba9b36b9025660c7faa2b7a3250700ad9c1e57282c2db5d9ee5f`
- `datapump-gentoo-overlay.tar.gz`: `2d2a02c88c77cc43d3bcc8ee0e02e7b3dfc71598842edfec1654af3c1c6e70c6`
- `datapump-gentoo-portage-sync.py`: `bffd98ff28ddd75a483ded6ebac6c23db055f3e4419151d64e2b52e038c4922d`
- `datapump-gentoo-sync.py`: `1987b4b71ba133fec56f2b555c19cb44b4ec8165a6943fb2d5ebe71478390496`
- `datapump-pacman-aarch64.conf`: `db6fbab5c30144c6e35f6f0e15718489a60edf34330dd0fe7884c73b3c21bedc`
- `datapump-pacman-keyring.gpg`: `b240fb9919864dc242e37964ac6bc87c61751f08c179de3b54774e318cb111ee`
- `datapump-pacman-x86_64.conf`: `4707f52f5a80c687811467cb56f355cd9f2740d5e0b08b90f8127195734ba12f`
- `datapump-rev-bin-1.0.0_p20260926234704_p36280483610_p1-1-aarch64.pkg.tar.gz`: `3d4293ffe0dcf8c82a237a9fbe6a72930dc4aec74474ab72090eebdb5d37a23a`
- `datapump-rev-bin-1.0.0_p20260926234704_p36280483610_p1-1-aarch64.pkg.tar.gz.sig`: `fe6084390db6c5f447a888614a6ce9407fe9df528b335b22a64d4c9ebeae54b7`
- `datapump-rev-bin-1.0.0_p20260926234704_p36280483610_p1-1-x86_64.pkg.tar.gz`: `610eb9055ece35987158fa9ffe447b4aace3c19459da6370d3a673b8a045364c`
- `datapump-rev-bin-1.0.0_p20260926234704_p36280483610_p1-1-x86_64.pkg.tar.gz.sig`: `76f28c14702d19f9df01e9fa335464569f62c742b73f5b3f05ea8386b11e8bff`
- `datapump-x86_64.db`: `966142fafd0f61a41f45cf70966ed9f35a78692b2b27bdd9edcdac7f58f29325`
- `datapump-x86_64.db.sig`: `3e5ecdd3adbfdb8a64865d2cddf85be886ebdba242ff37baec1824cb6ac6198b`
- `datapump-x86_64.files`: `6a502367999c77459f54cb1f48ab0ecd74847d0704666274fe852e38c4b87c7c`
- `datapump-x86_64.files.sig`: `4af80dd681a56c7e3803f00b1f58aa482d748265c4925758ee8d99d6526b4696`
- `distro-packages.json`: `e1f1a151bab121ad1d0cb1fdc6a00a6cc767ec2ae426639861e009349309a3d2`

Preserved SDK/dependency SHA-256 values:

- `datapump-sdk-b8685ab239d7ac8650e6-linux-x86_64.tar.gz`: `e32ac3a85c1762f54be8aa4a73de692f4364247d2bb62338441636513b32e3ed`
- `datapump-sdk-sources-b8685ab239d7ac8650e6.tar.gz`: `f7e4b0d858f6aeaa08d94120b0abde61d762b31dadd24e505b0ed11926f19680`
- `sdk-b8685ab239d7ac8650e6-SHA256SUMS.txt`: `97f38a934759c1841de72f4b6ba22b1eaebd1402006769094160a7a1b1898b04`
- `windows-base-1be51afd94ed0cb4555a-SHA256SUMS.txt`: `af5fd64a4087948ff9f52813ca56ddeee921ddf23221737a786761970027056b`
- `windows-base-1be51afd94ed0cb4555a-x64-windows-static.zip`: `931b8f3ec55648191bfc90d3361273709595b8726c81840f60d3409aa14ba8e6`
- `windows-base-sources-1be51afd94ed0cb4555a.zip`: `19d36b3c9ddae6a2b8db7c8266185c075bba8c067b366b3d5cbe0fef33ac6a2a`
