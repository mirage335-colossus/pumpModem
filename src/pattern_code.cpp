#include "datapump/pattern_code.hpp"
#include "datapump/fhss_schedule.hpp"
#include "datapump/pattern_pulse.hpp"
#include "datapump/symbol_schedule.hpp"
#include "datapump/sched_hop.hpp"
#include "datapump/crypto.hpp"
#include "pattern_carrier.hpp"

#include <openssl/crypto.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <numbers>

namespace datapump::modem {
namespace {
constexpr double tau = 2 * std::numbers::pi;
constexpr std::array<std::uint8_t, 32> public_seed{
    'D','a','t','a','P','u','m','p','/','p','a','t','t','e','r','n',
    '/','b','i','n','a','r','y','/','v','1',0,0,0,0,0,0};
// Public alphabet 2026-10: bit 0 retains the public Payload row; bit 1
// uses the same known seed at epoch zero in PatternOneV2, starting at chip
// 1886. This fixed public prefix was selected for short-pattern energy and
// amplitude/complex/shift separation, not normalized or balanced at runtime.
// See docs/public-pattern-alphabet.md. Secret positions are never offset.
constexpr std::uint64_t public_one_first_chip = 1886;
// Preserve low-level Data-only encrypted diagnostics byte for byte. Ordinary
// encrypted messages use independent private rows, never this legacy mask.
constexpr std::array<int, 8> bit_mask{1,-1,-1,1,1,1,-1,-1};

// Immutable public waveform prefix: 4 KiB of read-only image, no allocation,
// secret data, or workspace cache. Values are the mapping below at Payload/i
// and PatternOneV2/(1886+i); tests independently reconstruct all entries.
// Longer rows use the same stream mapping lazily. Trace capture bypasses this
// table so it records the actual public mapper bytes.
constexpr std::array<std::array<std::complex<double>,2>,128> public_prefix{{
    {{{-0x1.c60f9987d4f85p+0,0x1.08bc38e7d42a6p-2},{0x1.24a1d559f58dcp-1,-0x1.383425fabbec1p+0}}},
    {{{-0x1.74b3bf048b8c5p-2,-0x1.77611fda2dec9p-2},{-0x1.7c3397b8d5165p+0,0x1.dcf3d80ec3cb0p-4}}},
    {{{-0x1.2c4bb3f95a78fp+0,-0x1.2043b617cef72p-1},{0x1.f532cdd240a65p-2,0x1.28614a51e0282p-4}}},
    {{{-0x1.e8ca73dff1319p-2,-0x1.e30a6f473c294p-2},{-0x1.d761bf940bce3p-2,-0x1.a1973d5c9d6bdp-5}}},
    {{{-0x1.c35249e3e01b6p-2,-0x1.bab6aa6165d35p-3},{-0x1.a36056f3ad8b4p+0,0x1.badd784b6920ap-3}}},
    {{{0x1.1b3d0243c7c73p+0,-0x1.93d79c3170d12p-1},{-0x1.344aedf6702f4p-1,0x1.05b971f19d21ap-1}}},
    {{{0x1.c6656ac35a5c3p-2,-0x1.b03705059ea66p-2},{-0x1.db8dec2275affp-2,0x1.dfc0fb5a154e5p-2}}},
    {{{0x1.6fb3510964113p-3,-0x1.8c5a20d388e6ap-1},{0x1.1efb29a9fe7ddp+0,0x1.1065f81f78d59p-1}}},
    {{{0x1.4398ccf6aed0ep+0,0x1.38ce94911c35ap+0},{-0x1.f101bc6ebdb3ep-3,0x1.0b2e7004b5cd0p-2}}},
    {{{-0x1.74923ba83f445p-3,0x1.bd20c2caf7db5p-2},{0x1.aa1505f29cbedp-2,0x1.d3416187b6310p-1}}},
    {{{-0x1.c54eca33a94bep-3,0x1.aaca274fdc5bdp-12},{-0x1.1e4c57a64436ap-2,-0x1.019cf2254a0a4p-3}}},
    {{{0x1.6db8304fe1b5ep-3,-0x1.adebcc6461c94p-2},{0x1.77c5eb399055cp-1,-0x1.beccdad4e34edp-1}}},
    {{{0x1.17288e83499edp+0,0x1.206af731ef6eep+0},{0x1.fa26195e54c31p-1,-0x1.5607d514635cap-1}}},
    {{{0x1.8f26c55ebb8adp-3,-0x1.47ad333697678p-1},{-0x1.57532842b2c2ep+0,-0x1.c995b3fe3b844p-2}}},
    {{{-0x1.263deb098b208p-2,0x1.29352d0ad8776p-2},{0x1.0ce09bd9f3a77p+0,0x1.4e7915e0b5ffcp-3}}},
    {{{-0x1.224d65c0e272ap+0,0x1.96a3a80e08bd5p-1},{0x1.51c383f20ad4ap-4,0x1.e1d608f9115dap-3}}},
    {{{0x1.59905145046a8p-1,-0x1.43a90fe72d5dcp+0},{0x1.88f722fe43cbap-2,0x1.00f9201336f1ep-1}}},
    {{{0x1.20d0a538059c4p-1,0x1.cb4454a1d05efp-1},{-0x1.300f222cc43eep+0,0x1.d9fccba3a924ap-3}}},
    {{{0x1.491b228ff62d0p-3,-0x1.e0cf1b37e92bdp-2},{0x1.e64c4eeb688ddp-4,-0x1.0e1cee7d994c1p+0}}},
    {{{-0x1.05c6079102e88p-2,0x1.c62b1207fd9eep+0},{0x1.6dde12b18747dp-1,-0x1.b65eaff914312p-4}}},
    {{{0x1.313118e5e674ap-3,-0x1.c4631fdea35c9p-4},{0x1.541201ddbd4cap-1,-0x1.a6712fad48f81p-3}}},
    {{{-0x1.40dc5f9d7507bp-2,0x1.2467d92f573aep-3},{0x1.6aece0128b5e5p-1,0x1.aeb110825215fp-1}}},
    {{{0x1.d97fa6553e518p-2,-0x1.49867e93ab467p-2},{0x1.c8421dd09fa85p-1,-0x1.cfcfa20088729p-1}}},
    {{{0x1.eb40b1bc24df0p-3,-0x1.32ced36de7d10p-1},{-0x1.65a50ac804006p-1,0x1.252bd343c9ea8p-2}}},
    {{{-0x1.472588c0504abp-2,-0x1.05b5096ffc3fdp-2},{-0x1.b780199cda953p-1,0x1.5b6b035ebeeebp-1}}},
    {{{0x1.4e4fc408637cap-1,-0x1.9413595c735cap+0},{0x1.edf6fb32d529ap-1,-0x1.82b7faf76908dp+0}}},
    {{{-0x1.190091639dc32p-2,0x1.068e5782f826cp-1},{-0x1.eb54bfed93e9ap-1,0x1.840e924efc7c4p-1}}},
    {{{0x1.82af1cec72d6ep-4,-0x1.40d47aac2e2ddp-2},{-0x1.667a5bdfed1abp-1,-0x1.9c7aeeeabdb0fp-1}}},
    {{{0x1.d35528257c5efp-2,-0x1.2d1fa036f8386p-2},{0x1.ebd862337abe2p-1,-0x1.64591e89c8ef6p-2}}},
    {{{-0x1.7b4c85ceb5e19p-7,-0x1.d20cf7d48af87p-1},{0x1.c8650342c8fc5p-1,0x1.1f8d94f23eec4p+0}}},
    {{{-0x1.2c9426dbf92e8p-2,0x1.b95a0bd1b0706p-2},{-0x1.5b12515889968p+0,0x1.08c26b9ba49ecp-1}}},
    {{{-0x1.b037e0926cd20p-3,0x1.a07ccd9c1950ep-6},{-0x1.e202e4e45a7aep-1,0x1.f117792dc6b88p-4}}},
    {{{-0x1.806ec32c3f89cp-2,-0x1.4f7ac382d2e3ep-3},{0x1.4300ed59ef4bdp-1,0x1.15a86ab19c8bep-1}}},
    {{{0x1.08fb8b4d065e4p-2,0x1.eeaae729135f0p-7},{-0x1.2d62e0cfff4d9p-2,-0x1.40c471bf048b2p-1}}},
    {{{-0x1.e3ad8006c30b2p-3,0x1.8c6aec40a36c0p-2},{-0x1.8a8fd756396fbp-1,-0x1.3a54f7df5132bp+0}}},
    {{{0x1.458e113b814b0p-2,-0x1.c39555227ce06p+0},{0x1.ae3b34b2a0758p-1,-0x1.6d56de235b680p-6}}},
    {{{-0x1.09528f6e710e5p+0,-0x1.9f14d797c8d66p-2},{0x1.017bba9ceb8a1p+0,-0x1.03fb3b8acc087p-1}}},
    {{{0x1.0112e048621a9p-4,0x1.236e8e239eea1p-1},{-0x1.cdfa1d915e0d2p-2,0x1.2633bdcd16a28p-1}}},
    {{{-0x1.cae099bf0ff3dp-1,0x1.5c67996a0a57fp-4},{0x1.443d17e3f3cecp+0,0x1.c5792df079315p-2}}},
    {{{-0x1.6a5340ecdc0c4p+0,-0x1.5f85d29e7a4b8p-2},{0x1.79acbb687a675p-3,0x1.dba406dcefcf8p-3}}},
    {{{-0x1.0ef46c8f47921p-1,0x1.3455b64498523p+0},{0x1.6aacbc6fb3721p-4,0x1.1e7aa0d5cbf2fp-3}}},
    {{{-0x1.7a4e65dc51144p-3,0x1.73c78918f7b91p-1},{0x1.d2b5707a19536p-3,-0x1.2393fc3cd2cffp-1}}},
    {{{0x1.5444fe171c966p-1,0x1.8837a9035be84p-4},{-0x1.b06bdf051d79cp+0,0x1.b39f4e2e059b8p-2}}},
    {{{0x1.a497e6a387641p-3,-0x1.c0237a0457d31p-5},{-0x1.035eacc850f2ap-1,0x1.7d98f3951296dp-2}}},
    {{{-0x1.48e457aa8508cp-1,-0x1.f2d1f6d3008b4p-2},{-0x1.4302b98f71721p-3,0x1.c638cfdd5b9eep-6}}},
    {{{-0x1.1490f4efdeee3p-1,0x1.4cb7af7f03f37p-2},{0x1.5dadf30e4b8e0p-2,-0x1.5b1c648ec1be2p+0}}},
    {{{0x1.c479064d7ee24p-2,-0x1.c155a430bd498p-7},{-0x1.da605fbc92fefp-2,-0x1.19dcd350191eep-2}}},
    {{{-0x1.00a116298a5e6p+0,-0x1.8a1ab3d88bf50p-3},{0x1.e168f52bbf9b7p-2,-0x1.603c466cfcbe7p-2}}},
    {{{0x1.2d8ebb099b40ep-2,0x1.043ecc8d3e3e2p-2},{-0x1.fe46512e5511fp-5,0x1.389a7d7a605c8p-1}}},
    {{{0x1.2a7eb27d1c9dap-2,0x1.c4c01a9a70230p+0},{-0x1.029c083b882bap-3,0x1.7be53f1dcddbbp+0}}},
    {{{-0x1.06afd58af292fp-2,-0x1.55bf120981951p+0},{0x1.08fa069f901ccp-4,0x1.084c287f6e9ddp+0}}},
    {{{0x1.f70c46010950dp-1,-0x1.7fc7c21223185p+0},{-0x1.7ac049d6f8d70p-3,-0x1.4128bba843396p+0}}},
    {{{-0x1.331adfcf7f735p-1,-0x1.51cbe566762e9p+0},{-0x1.998eb9f24f14bp-1,-0x1.5a73b49f7cb59p+0}}},
    {{{0x1.0867ba06057b1p+0,-0x1.24d09c7adc7bcp+0},{0x1.c8bdcd89e0a52p-2,-0x1.c19f268b0a410p-3}}},
    {{{0x1.a67c818a7dc7cp-1,-0x1.c4ccc6473e897p-5},{0x1.2431e1e0dde86p+0,-0x1.50528fdf123afp+0}}},
    {{{-0x1.ebf8562b29e1bp-6,0x1.6732cb394f6bdp-4},{0x1.95d9db7cf7984p-2,-0x1.ea9b3678d1dcap-3}}},
    {{{0x1.1c6add0064b00p-1,-0x1.57725f8f0136ep-1},{0x1.48f9e5463032dp-1,0x1.0b57c2e223a34p+0}}},
    {{{0x1.c268ec179b40fp-2,0x1.ec426fa1f8145p-5},{0x1.d1d3734b52ffap-4,0x1.bb0b943860021p-3}}},
    {{{-0x1.8fb05876e0fc8p-2,-0x1.3adb367bfd1a0p+0},{-0x1.bedd96218c0c5p-1,-0x1.411362b9bb86ep-1}}},
    {{{-0x1.70288e6573122p-1,-0x1.f460dbbc7fb89p-2},{0x1.361482509699fp-1,-0x1.bf3929c0cd34fp-1}}},
    {{{0x1.aef026ae94401p+0,0x1.3b3d530902e3cp-1},{-0x1.91ef13ed72afbp-2,-0x1.d965bfff807e7p-3}}},
    {{{-0x1.795b46c143469p-2,0x1.52482844a04ecp-1},{-0x1.9aebfea6ab16ap-2,-0x1.9b237f0dcb83ap-2}}},
    {{{0x1.a7c1e7adfb643p-5,-0x1.9aeb52ac660e1p-3},{0x1.4049bec0a799ep-2,0x1.38709d25185b6p-4}}},
    {{{0x1.a5a71cfa40306p+0,0x1.988bcfdbc57c4p-2},{-0x1.2f672c1991658p+0,-0x1.b4386a9457867p-4}}},
    {{{-0x1.56c708e3e1da6p-1,0x1.8d25ef8d431c1p+0},{-0x1.06062679bbdbbp-1,0x1.f4153e511ba10p-1}}},
    {{{-0x1.17fea60863d8ap-1,0x1.650128c4081e3p-1},{-0x1.33cbdd7e0d4c9p-2,-0x1.9407d7850537ep+0}}},
    {{{0x1.687a9280fd54ep-1,-0x1.e6992fb87245dp-1},{0x1.3c505b2d900efp-1,0x1.d5de204d108f6p-2}}},
    {{{0x1.d1e65d7fcf273p-4,-0x1.7484aab53c17bp-4},{-0x1.13a007a462131p-1,0x1.e0325c17deac2p-2}}},
    {{{-0x1.48560334d19b9p-2,0x1.109a436454305p+0},{0x1.90199566cb7e7p-5,0x1.dcd90633e5951p-6}}},
    {{{0x1.a4150462744bfp-1,0x1.0b68b522ae30fp-1},{-0x1.de6c80f1cc8d3p-5,0x1.ca9d8c596892fp+0}}},
    {{{-0x1.bc39e729ab932p-3,-0x1.cbbc9b667042ap-3},{-0x1.4669b7d942bd2p-2,-0x1.84e0f89627e4ep-2}}},
    {{{0x1.6c6d343dc867ap-1,0x1.56e76bae24300p-2},{-0x1.5be39ba76a810p-2,0x1.f20de63982238p-2}}},
    {{{0x1.4f5b65f9dce33p-4,-0x1.83556076e97f9p-2},{0x1.114430a69eeecp-3,0x1.0c468cce24ae1p+0}}},
    {{{-0x1.ae1174e0275cbp-2,-0x1.308c9780d36b7p+0},{-0x1.ab102dbc2936ap+0,0x1.4fab9feaaad28p-1}}},
    {{{-0x1.50d71fac50418p-3,-0x1.ed51e247d871bp-3},{0x1.f92ae6d228150p-3,-0x1.1bf15cfcf3f92p-4}}},
    {{{-0x1.0f654463af688p-8,0x1.304afae92283fp-4},{0x1.4b43b0afee1b6p-1,0x1.18bf0f6f3a168p-2}}},
    {{{0x1.e7f5419dc697fp-1,0x1.b1c06fd4c0dc9p-14},{-0x1.8dba4b045131cp-2,-0x1.64feb09b3bf98p-1}}},
    {{{0x1.832cb56df913cp-1,0x1.a5fa5bedc47b8p-1},{-0x1.dd526b16f037ep-2,-0x1.0cdfada47ad0cp-2}}},
    {{{0x1.ed69ac6d5b703p-3,0x1.7159ef5930b67p-3},{0x1.4214e715b7c25p-1,0x1.0b2ae6be3f0a4p-3}}},
    {{{0x1.50cb1ae318a03p-5,0x1.f292b8e868696p-1},{-0x1.e1de085799e88p-3,-0x1.c4fbfce45edbep-3}}},
    {{{-0x1.95936977c1eafp-2,-0x1.654b31572abb0p-2},{-0x1.7c19f6479959fp-1,0x1.cc216d710d693p-2}}},
    {{{-0x1.f4df2e20b36e9p-2,-0x1.30ff2eab9ce3bp-2},{0x1.3102d866968a8p-5,-0x1.fa89b10b6251bp-3}}},
    {{{0x1.20c3db46a7072p-2,0x1.b13ef038ac369p-1},{-0x1.ecdd90a0bb5bfp-1,-0x1.7f720e0ddafb6p-4}}},
    {{{-0x1.c3de7417ff29fp-3,-0x1.1b0bc580cfae5p-1},{0x1.6962ddc6b6d15p-1,-0x1.ca0488f494bc8p-2}}},
    {{{0x1.2139044d4f89dp-3,-0x1.2e5e98f9d772dp-1},{0x1.032490e636140p+0,-0x1.a7dd77a74c401p-2}}},
    {{{-0x1.870eab094f6e4p-2,-0x1.906bfab11c0b6p-3},{0x1.bacb049e73b6dp-4,0x1.456720e49ae60p-1}}},
    {{{0x1.84ff350d195abp-3,-0x1.ae6e0de66d098p-1},{0x1.3627fe0c0d6b6p-1,-0x1.71b9748989ae3p-1}}},
    {{{0x1.35b0651c9848dp-1,-0x1.56fd53756d8aap-1},{0x1.0e920943175f9p-1,0x1.5bca37aeecac1p+0}}},
    {{{0x1.1d6d4095a8026p-1,0x1.9941d551279f0p-2},{-0x1.c14dbf60d56d0p-2,-0x1.d4efec9448477p-1}}},
    {{{0x1.47a118814a7bbp-1,-0x1.bf49ed89bcdabp-2},{-0x1.5fba6dabe4dcdp+0,0x1.7ab92307159a9p-3}}},
    {{{0x1.f91a73515ffa8p-1,-0x1.fc764535eadccp-1},{0x1.1c54c16946b6cp-2,-0x1.24d3a26a59adcp-2}}},
    {{{0x1.4cae8a1fdc8d9p-1,-0x1.b44560bd87908p-1},{-0x1.9dd030c322b61p+0,-0x1.8c894139b420bp-1}}},
    {{{0x1.15f75ced66d4dp-1,0x1.b1e02ca49524ap-1},{-0x1.d09e55bc08c28p-1,0x1.8314afec9936ap-1}}},
    {{{0x1.6bd02e5b99e18p-2,-0x1.3304f8642f95ap-1},{-0x1.644e33ca5d579p+0,-0x1.eaf5667163797p-4}}},
    {{{0x1.3d9680917786bp+0,0x1.2090d27f59de3p-3},{-0x1.b5dd184efa9cbp-3,-0x1.c083dab7b1435p-2}}},
    {{{0x1.fd7e2d6324366p-4,0x1.5dd3726a0eb18p+0},{0x1.6b617e3894acbp-5,-0x1.244285236e1d3p-4}}},
    {{{0x1.7bb4d6733dd05p-2,-0x1.394f67e2ec290p+0},{0x1.84cc4c6522a30p+0,-0x1.e764531b695d4p-1}}},
    {{{0x1.ee2ef037f3668p-3,0x1.7238ee7be3a9ap-3},{-0x1.c001418a5c816p-2,0x1.23ec5c773be67p+0}}},
    {{{0x1.0907cadbc1afcp-3,-0x1.0bc4d426aae42p-2},{-0x1.98dde5b5f0093p-7,-0x1.0de6a1e9691c7p-2}}},
    {{{0x1.babc69655634dp-2,-0x1.2598dc0c3e015p+0},{0x1.165345d0d696dp+0,-0x1.be43c0301c986p-3}}},
    {{{-0x1.cd53dddf5da61p-1,-0x1.eaa7b0a05e974p-3},{0x1.a96d06f8b6b4fp-1,0x1.1469e6c505d31p-1}}},
    {{{-0x1.008c93e61a1ebp-1,0x1.1026e7f2f2134p-1},{-0x1.dfd121823b084p-3,-0x1.90c7875173a71p-1}}},
    {{{-0x1.add73ed1f093bp-1,-0x1.be23e858fa6a9p-2},{-0x1.7b96b7501d772p+0,-0x1.eaf4574885fb7p-2}}},
    {{{-0x1.3e4a4f2215048p-2,0x1.26a1593a66436p-2},{-0x1.58ebceaae821ep-3,0x1.f4fa069415906p-1}}},
    {{{0x1.3c611e4ee44e3p-2,-0x1.832289db843e6p+0},{-0x1.7ea2aaa2e649ap-2,0x1.84e770b7a448cp+0}}},
    {{{-0x1.6436bb1ccd72ep-1,0x1.ec9f83a937ac9p-4},{-0x1.3d24c23d0ea6cp-3,-0x1.3e1054987876cp+0}}},
    {{{0x1.6083af00f1b66p-1,0x1.c7f3bef52c078p-3},{0x1.4ad08af0a1709p-3,0x1.c056e9871ed49p-1}}},
    {{{-0x1.588ef4afe0460p+0,-0x1.3e01c25417be2p-1},{-0x1.537650f401cd8p-1,0x1.4f8807507f39dp-1}}},
    {{{-0x1.93ff2d198b6ecp-2,0x1.063553106d2dcp-1},{-0x1.2644b5b9c4b36p-1,0x1.6ce8412d0bdc7p-3}}},
    {{{-0x1.06c7a5867c7e1p-2,-0x1.1ee19edb668c2p-2},{-0x1.79fc3e0b4328fp-4,-0x1.ff56928193ad9p-4}}},
    {{{-0x1.eb7c3872553c5p-6,0x1.03e1dd851f700p-5},{-0x1.8f28195172a93p-1,-0x1.6b17651e908b6p-4}}},
    {{{-0x1.cd2fcd50d82abp-12,-0x1.2fa165ee5bd6ap-3},{0x1.2dd7ec9c7a522p-2,0x1.76f5308288130p-7}}},
    {{{0x1.c84f984909041p+0,-0x1.82530d17e0f6dp-3},{-0x1.cb1ee2d9635afp-1,-0x1.e01aa8f1c946dp-2}}},
    {{{-0x1.4b83f5d79fb3fp-2,-0x1.39bf64c9a3c49p-1},{-0x1.dcd367b8ea619p-4,-0x1.3ccab8ef22170p-1}}},
    {{{0x1.271f3bfe29596p-1,0x1.0597f60063435p-1},{0x1.2df2c432f3fcfp-1,-0x1.f2599a582a486p-3}}},
    {{{0x1.2d53639669d7ap-1,0x1.c89ff3d1aedabp-2},{-0x1.8750553a631f2p-2,0x1.aa7b3b4d9ac9fp-3}}},
    {{{0x1.c9dffc6b8524cp+0,0x1.e093b92b3601ep-4},{0x1.6e48655984f7ep-2,0x1.84768f27c256ap-2}}},
    {{{0x1.7cddd644d790ep-2,-0x1.2e9043e930cb3p+0},{-0x1.8f08f6d23e83dp-2,0x1.b054a7b38636ep-3}}},
    {{{0x1.00c0af7a0fd7fp+0,-0x1.adc9a1af5db9fp-2},{-0x1.8164c0377b0f3p-1,-0x1.512a69b0c863bp+0}}},
    {{{-0x1.c1fab1e6794e4p-5,0x1.30499505cedacp+0},{0x1.fef5620686e3cp-3,0x1.902c10114e795p-1}}},
    {{{-0x1.ca0ea1f1674b6p+0,0x1.b1da98be08d64p-4},{-0x1.24e171c84a5d1p-3,-0x1.5efcaec65bda9p-1}}},
    {{{-0x1.94d80a90c6598p-3,0x1.2321461b452d6p-3},{0x1.adbc45a8cce32p-1,-0x1.2233236d2f563p+0}}},
    {{{-0x1.57f4e78f8f561p-1,0x1.77cfe1e21ae0cp-1},{0x1.c8d2705bef366p-2,-0x1.c5cae91d970efp-2}}},
    {{{-0x1.4dd4cf3741e5ap-2,-0x1.2e4ec7a83bfa3p-5},{-0x1.2ca930e8c78fcp-4,-0x1.fbec1334b129ep-2}}},
    {{{0x1.52ff16d533299p-1,-0x1.486ef1ec18e36p-1},{0x1.3e5ab107e3732p-1,-0x1.6ee302331fe3ap-1}}},
    {{{-0x1.18d93b1590c4dp-5,0x1.7a3e0220ea196p-6},{0x1.148318f3aaa55p-2,0x1.b08d52660d444p-1}}},
    {{{0x1.bcab5d3aaef43p-2,0x1.0730866429b04p+0},{0x1.0a4ffdf7ed2e4p-1,0x1.0d361156b9dffp-1}}},
    {{{-0x1.ad8dce483b0acp-1,-0x1.3d7483b69a0e9p-2},{0x1.1511e2c0027b7p+0,0x1.163145289b34bp+0}}},
}};

void require(bool condition, const char* message) {
    if (!condition) throw Error(message);
}
void cancelled(std::stop_token stop) {
    if (stop.stop_requested()) throw Error("pattern operation cancelled");
}
struct NoiseTrace {
    std::array<std::uint8_t,8> input{}, output{}, key{};
    bool valid=false;
};
struct StreamCache {
    static constexpr std::size_t capacity = 512;
    Crypto key;
    StreamPurpose purpose;
    StreamDomain domain;
    std::uint64_t epoch = 0, begin = 0;
    bool valid = false;
    std::array<std::uint8_t, capacity> bytes{};
    StreamCache(const Crypto& source, StreamPurpose use, std::uint64_t time,
                StreamDomain counter_domain):key(source),purpose(use),domain(counter_domain),epoch(time) {}
    StreamCache(std::span<const std::uint8_t> seed, StreamPurpose use,
                std::uint64_t time, StreamDomain counter_domain=StreamDomain::Payload):
        StreamCache(Crypto(seed),use,time,counter_domain) {}
    ~StreamCache() { OPENSSL_cleanse(bytes.data(), bytes.size()); }
    void select_epoch(std::uint64_t time) {
        if (epoch != time) { epoch = time; valid = false; }
    }
    std::uint8_t byte(std::uint64_t offset) {
        const auto aligned = offset - offset % capacity;
        if (!valid || aligned != begin) {
            auto generated = key.stream(purpose, epoch, aligned, capacity, domain);
            std::copy(generated.begin(), generated.end(), bytes.begin());
            OPENSSL_cleanse(generated.data(), generated.size());
            begin = aligned; valid = true;
        }
        return bytes[static_cast<std::size_t>(offset-begin)];
    }
    int sign(std::uint64_t chip) {
        return ((byte(chip/8) >> (chip%8)) & 1U) ? -1 : 1;
    }
    std::complex<double> noise(std::uint64_t position, StreamCache* first = nullptr,
                               StreamCache* second = nullptr, StreamCache* third = nullptr,
                               NoiseTrace* trace = nullptr) {
        require(position<=(std::numeric_limits<std::uint64_t>::max()-7)/8,"pattern noise coordinate overflow");
        const auto uniform=[&](std::uint64_t offset) {
            std::uint32_t value=0;
            for(unsigned i=0;i<4;++i) {
                auto encoded=byte(offset+i);
                const auto index=static_cast<std::size_t>(offset+i-position*8);
                if(trace)trace->input[index]=encoded;
                if(first) {
                    const auto key_byte=first->byte(offset+i);
                    encoded^=key_byte;
                    if(trace)trace->key[index]=key_byte;
                }
                if(second)encoded^=second->byte(offset+i);
                if(third)encoded^=third->byte(offset+i);
                if(trace)trace->output[index]=encoded;
                value=(value<<8)|encoded;
            }
            return (static_cast<double>(value)+.5)/4294967296.;
        };
        // Circular Gaussian noise avoids the one-quadrature pattern structure
        // of random +/- chips. Limit its crest factor to stay inside PCM
        // headroom, normalizing E[|noise|^2] back to one.
        constexpr double maximum_radius=1.75;
        static const double normalization=std::sqrt(1-std::exp(-maximum_radius*maximum_radius));
        const auto radius=std::min(maximum_radius,std::sqrt(-std::log(uniform(position*8))));
        const auto result=std::polar(radius/normalization,tau*uniform(position*8+4));
        if(trace)trace->valid=true;
        return result;
    }
};
}

std::uint64_t pattern_chip_samples(const Config& config) {
    validate(config);
    return static_cast<std::uint64_t>(std::ceil(2. * config.sample_rate / waveform_bandwidth_hz(config)));
}
std::uint64_t pattern_chips_per_symbol(const Config& config) {
    const auto chip = pattern_chip_samples(config);
    const auto symbol = symbol_sample_count(config);
    return symbol / chip + (symbol % chip != 0);
}

struct PatternCode::Impl {
    Config config;
    std::uint64_t chip = 0, symbol = 0, chips = 0, epoch = 0;
    bool shaped = false;
    bool private_patterns = false;
    StreamCache pattern, dsss, pattern_one, dsss_one;
    std::unique_ptr<StreamCache> outer;
    struct InnerChip {
        std::uint64_t epoch=0,position=0;
        std::complex<double> value{};
        bool valid=false;
    };
    std::array<InnerChip,2> inner_chips{};
    struct Interleaved {
        static constexpr std::size_t coarse_capacity=256;
        // Include the explicit temporary seed/draw buffers in the workspace
        // bound even though setup uses them only until a map is ready.
        static constexpr std::size_t setup_bytes=sizeof(StreamCache)+32+StreamCache::capacity;
        struct SymbolState {
            std::vector<std::uint32_t> permutation;
            std::array<std::uint8_t,32> seed{};
            std::array<std::array<InnerChip,coarse_capacity>,2> coarse{};
            StreamCache rotations;
            std::uint64_t epoch=0,ordinal=0;
            bool valid=false;
            SymbolState(std::size_t count,const Crypto& key,StreamDomain domain):
                permutation(count),rotations(key,StreamPurpose::Dsss,0,domain) {}
            ~SymbolState() {
                OPENSSL_cleanse(permutation.data(),permutation.size()*sizeof(permutation[0]));
                OPENSSL_cleanse(coarse.data(),sizeof(coarse));
                OPENSSL_cleanse(seed.data(),seed.size());
            }
        };
        // Shaped payload boundaries alternate old/new symbols every sample.
        // Keeping only one map would repeatedly rebuild the whole permutation.
        std::array<SymbolState,2> symbols;
        unsigned recent=0;
        StreamDomain permutation_domain;
        Interleaved(std::size_t count,const Crypto& key,StreamDomain rotation,StreamDomain permutation):
            symbols{SymbolState(count,key,rotation),SymbolState(count,key,rotation)},
            permutation_domain(permutation) {}
        std::size_t working_bytes() const {
            return sizeof(Interleaved)+setup_bytes+(symbols[0].permutation.capacity()+symbols[1].permutation.capacity())*
                sizeof(std::uint32_t);
        }
        void invalidate() {
            for(auto& state:symbols)state.valid=false;
        }
        SymbolState& select(const SymbolStreamAddress& address,const Crypto& key,std::stop_token stop={}) {
            cancelled(stop);
            for(unsigned i=0;i<symbols.size();++i) {
                auto& state=symbols[i];
                if(state.valid && state.epoch==address.epoch && state.ordinal==address.ordinal) {
                    recent=i;return state;
                }
            }
            const auto index=!symbols[0].valid?0U:!symbols[1].valid?1U:1U-recent;
            auto& state=symbols[index];state.valid=false;
            for(auto& row:state.coarse)for(auto& entry:row)entry.valid=false;
            // Variable rejection draws never consume a neighboring symbol's
            // stream. Each canonical symbol first derives its own shuffle key
            // from a fixed, disjoint 32-byte seed address in a separate domain.
            require(address.ordinal<=(std::numeric_limits<std::uint64_t>::max()-31)/32,
                    "DSSS permutation seed address would overflow");
            auto seed=key.stream(StreamPurpose::Dsss,address.epoch,address.ordinal*32,32,permutation_domain);
            std::copy(seed.begin(),seed.end(),state.seed.begin());
            struct CleanSeed {
                Bytes& value;
                ~CleanSeed() { OPENSSL_cleanse(value.data(),value.size()); }
            } clean{seed};
            StreamCache draws(seed,StreamPurpose::Dsss,0);
            std::uint64_t offset=0;
            // Unbiased rejection sampling has an explicit failure bound, not a
            // modulo-biased fallback or an unbounded setup loop. At this N cap
            // each draw's rejection probability is below 1/4096.
            const auto maximum_draws=2*state.permutation.size()+1024;
            const auto word=[&]() {
                cancelled(stop);
                require(offset/4<maximum_draws,"DSSS permutation rejection budget exhausted");
                std::uint32_t result=0;
                for(unsigned byte=0;byte<4;++byte)result=(result<<8)|draws.byte(offset++);
                return result;
            };
            for(std::size_t i=0;i<state.permutation.size();++i) {
                if((i&255U)==0)cancelled(stop);
                state.permutation[i]=static_cast<std::uint32_t>(i);
            }
            for(auto i=state.permutation.size();i>1;--i) {
                if((i&255U)==0)cancelled(stop);
                const auto bound=static_cast<std::uint32_t>(i);
                const auto threshold=(std::uint32_t{0}-bound)%bound;
                auto selected=word();while(selected<threshold)selected=word();
                std::swap(state.permutation[i-1],state.permutation[selected%bound]);
            }
            state.epoch=address.epoch;state.ordinal=address.ordinal;
            state.rotations.select_epoch(address.epoch);
            state.valid=true;recent=index;
            return state;
        }
    };
    std::unique_ptr<Interleaved> interleaved;
    struct CachedChip {
        std::uint64_t address=0;
        std::complex<double> value{};
        bool valid=false;
    };
    std::array<CachedChip,32> shaped_chips{}, shaped_chips_one{};
    struct TraceStorage {
        std::array<NoiseTrace,TransmitTrace::byte_limit/8> symbol_starts{};
        std::array<std::uint8_t,TransmitTrace::byte_limit> rotations{},permutation{};
        std::array<bool,TransmitTrace::byte_limit> rotation_valid{};
        std::uint64_t outer_epoch=0,rotation_offset=0,permutation_offset=0;
        unsigned first_pair=0;
        bool outer_valid=false,permutation_valid=false;
        std::uint64_t first_chip=0;
        bool capture=false;
        ~TraceStorage() {
            OPENSSL_cleanse(symbol_starts.data(),sizeof(symbol_starts));
            OPENSSL_cleanse(rotations.data(),rotations.size());
            OPENSSL_cleanse(permutation.data(),permutation.size());
        }
    };
    std::unique_ptr<TraceStorage> trace;
    struct AddressCache {
        SymbolStreamAddress value{};std::uint64_t index=0,phase=0;bool valid=false;FhssPowerShape gate;
        std::unique_ptr<SchedHopSourceMap> scheduled;
        std::unique_ptr<ScheduledSymbolCalendar> packed;
        std::uint64_t packed_rank=0,packed_phase=0;
        bool packed_anchor_valid=false;
    };
    std::unique_ptr<AddressCache> address_cache;
    Impl(Config value, std::uint64_t start_epoch): config(value), epoch(start_epoch),
        private_patterns(config.spreading_mode == SpreadingMode::pattern && (config.scramble || config.dsss)),
        pattern(config.scramble ? std::span<const std::uint8_t>(config.spreading_seed) :
                                 std::span<const std::uint8_t>(public_seed),
                StreamPurpose::Scrambler, config.scramble ? start_epoch : 0,
                private_patterns ? StreamDomain::PatternZeroV2 : StreamDomain::Payload),
        dsss(config.dsss_seed, StreamPurpose::Dsss, start_epoch,
             private_patterns ? StreamDomain::PatternZeroV2 : StreamDomain::Payload),
        pattern_one(pattern.key, StreamPurpose::Scrambler, pattern.epoch, StreamDomain::PatternOneV2),
        dsss_one(dsss.key, StreamPurpose::Dsss, start_epoch, StreamDomain::PatternOneV2) {
        chip = pattern_chip_samples(config);
        symbol = symbol_sample_count(config);
        chips = symbol / chip + (symbol % chip != 0);
        const bool version2=config.dsss_factor>1 && config.outer_dsss_version==OuterDsssVersion::interleaved_v2;
        const auto complete=static_cast<std::size_t>(symbol/chip);
        const bool scheduled=scheduled_bursts(config);
        const auto fixed=sizeof(Impl)+sizeof(PatternCode)+((scheduled||deferred_symbols(config)||(config.fake_hopping&&config.fake_hopping->matched_gaps))?sizeof(AddressCache):0)+(config.dsss_factor>1?sizeof(StreamCache):0)+
            (version2?sizeof(Interleaved)+Interleaved::setup_bytes+2*complete*sizeof(std::uint32_t):0);
        require(fixed<=config.memory_limit,"pattern code exceeds memory limit");
        if(scheduled||deferred_symbols(config)||(config.fake_hopping&&config.fake_hopping->matched_gaps)) {
            address_cache=std::make_unique<AddressCache>();
            if(scheduled) {
                require(config.data_key.has_value(),"scheduled hopping requires an encryption key");
                if(packed_scheduled_symbols(config))
                    address_cache->packed=std::make_unique<ScheduledSymbolCalendar>(config);
                else address_cache->scheduled=std::make_unique<SchedHopSourceMap>(*config.scheduled_hopping,*config.data_key,config.sample_rate);
                const auto calendar_bytes=address_cache->packed?address_cache->packed->working_bytes():address_cache->scheduled->working_bytes();
                require(fixed<=config.memory_limit && calendar_bytes<=config.memory_limit-fixed,
                        "scheduled pattern calendar exceeds memory limit");
            }
            if(config.fake_hopping && config.fake_hopping->matched_gaps)
                address_cache->gate=FhssPowerShape(*config.fake_hopping,config.sample_rate,
                    pattern_pulse_padding_samples(config),symbol);
        }
        if(config.dsss_factor>1) {
            const auto domain=version2 ?
                (config.dsss_factor==10?StreamDomain::OuterDsss10RotationV2:
                 config.dsss_factor==100?StreamDomain::OuterDsss100RotationV2:StreamDomain::OuterDsss1000RotationV2) :
                (config.dsss_factor==10?StreamDomain::OuterDsss10V1:
                 config.dsss_factor==100?StreamDomain::OuterDsss100V1:StreamDomain::OuterDsss1000V1);
            outer=std::make_unique<StreamCache>(config.dsss_seed,StreamPurpose::Dsss,start_epoch,domain);
            if(version2) {
                const auto permutation=config.dsss_factor==10?StreamDomain::OuterDsss10PermutationV2:
                    config.dsss_factor==100?StreamDomain::OuterDsss100PermutationV2:StreamDomain::OuterDsss1000PermutationV2;
                interleaved=std::make_unique<Interleaved>(complete,outer->key,domain,permutation);
                require(sizeof(Impl)+sizeof(PatternCode)+(address_cache?sizeof(AddressCache):0)+sizeof(StreamCache)+interleaved->working_bytes()<=config.memory_limit,
                        "pattern code exceeds memory limit");
            }
        }
        require(config.stream_phase_samples < config.sample_rate,
                "pattern stream phase must be within its first second");
        shaped=pattern_pulse_enabled(config);
        require(sizeof(Impl) + sizeof(PatternCode) <= config.memory_limit,
                "pattern code exceeds memory limit");
    }
    ~Impl() {
        OPENSSL_cleanse(shaped_chips.data(),sizeof(shaped_chips));
        OPENSSL_cleanse(shaped_chips_one.data(),sizeof(shaped_chips_one));
        OPENSSL_cleanse(inner_chips.data(),sizeof(inner_chips));
        OPENSSL_cleanse(config.spreading_seed.data(),config.spreading_seed.size());
        OPENSSL_cleanse(config.dsss_seed.data(),config.dsss_seed.size());
    }
    SymbolStreamAddress stream_address(std::uint64_t index) {
        if(!address_cache)return symbol_stream_address(epoch,config.stream_phase_samples,index,symbol,config.sample_rate);
        auto& cache=*address_cache;
        if(!cache.valid || index!=cache.index || cache.phase!=config.stream_phase_samples) {
            if(cache.packed) {
                if(!cache.packed_anchor_valid || cache.packed_phase!=config.stream_phase_samples) {
                    cache.packed_rank=cache.packed->first_rank(epoch,config.stream_phase_samples);
                    cache.packed_phase=config.stream_phase_samples;cache.packed_anchor_valid=true;
                }
                const auto physical=cache.packed->physical_slot(symbol_schedule_detail::add(cache.packed_rank,index));
                const auto sample=static_cast<std::uint32_t>(physical%config.sample_rate);
                cache.value={physical/config.sample_rate,sample/symbol,sample};
            } else if(cache.scheduled) {
                const auto anchor=symbol_schedule_detail::add(symbol_schedule_detail::multiply(epoch,config.sample_rate),config.stream_phase_samples);
                const auto origin=cache.scheduled->locate(anchor).source;
                const auto physical=cache.scheduled->source_to_physical(origin.add_samples(
                    symbol_schedule_detail::multiply(index,symbol)));
                require(physical.fraction==0,"scheduled symbol address must have an integer sample origin");
                const auto sample=static_cast<std::uint32_t>(physical.sample%config.sample_rate);
                cache.value={physical.sample/config.sample_rate,sample/symbol,sample};
            } else cache.value=symbol_stream_address(config,epoch,config.stream_phase_samples,index);
            cache.index=index;cache.phase=config.stream_phase_samples;cache.valid=true;
        }
        return cache.value;
    }
    std::complex<double> value(std::uint64_t absolute_chip, unsigned bit, double fraction) {
        require(bit <= 1, "pattern symbol must be a zero or one bit");
        require(std::isfinite(fraction) && fraction >= 0 && fraction < 1,
                "pattern chip fraction must be within [0,1)");
        auto position = absolute_chip % chips;
        double pulse_gain=1.;
        if(address_cache && address_cache->gate.enabled) {
            const auto address=stream_address(absolute_chip/chips);
            const auto offset=position*chip;
            const auto center=static_cast<long double>(address.sample_in_second)+offset+
                .5L*std::min(chip,symbol-offset);
            const auto& gate=address_cache->gate;
            pulse_gain=fhss_utc_quiet(address.epoch,center,config.sample_rate,
                gate.coefficient_radius(),gate.lag_center)?0.:1.;
        }
        std::complex<double> outer_value{1,0};
        Interleaved::SymbolState* symbol_state=nullptr;
        if (config.scramble || config.dsss) {
            const auto address = stream_address(absolute_chip / chips);
            if(outer) {
                auto* rotations=outer.get();
                if(interleaved) {
                    symbol_state=&interleaved->select(address,outer->key);
                    rotations=&symbol_state->rotations;
                }
                rotations->select_epoch(address.epoch);
                const auto fine_position=symbol_stream_chip(address,chips,position);
                constexpr std::array<std::complex<double>,4> phases{{{1,0},{0,1},{-1,0},{0,-1}}};
                const auto rotation=rotations->byte(fine_position/4);
                outer_value=phases[(rotation>>(2*(fine_position%4)))&3U];
                if(trace && trace->capture && absolute_chip>=trace->first_chip &&
                   absolute_chip-trace->first_chip<chips) {
                    const auto first=symbol_stream_chip(address,chips,0);
                    trace->outer_valid=true;trace->outer_epoch=address.epoch;
                    trace->rotation_offset=first/4;trace->first_pair=static_cast<unsigned>(first%4);
                    const auto at=fine_position/4-first/4;
                    if(at<trace->rotations.size()) {
                        trace->rotations[at]=rotation;trace->rotation_valid[at]=true;
                    }
                    if(symbol_state && !trace->permutation_valid) {
                        trace->permutation=symbol_state->seed;trace->permutation_valid=true;
                        trace->permutation_offset=address.ordinal*32;
                    }
                }
                // The final partial chip has a different energy weight and
                // stays fixed; every complete fine-chip coefficient is merely
                // reordered before its independent unit-magnitude rotation.
                if(symbol_state && position<symbol_state->permutation.size())
                    position=symbol_state->permutation[static_cast<std::size_t>(position)];
                const auto coarse_chips=chips/config.dsss_factor+(chips%config.dsss_factor!=0);
                position=symbol_stream_chip(address,coarse_chips,position/config.dsss_factor);
            } else position = symbol_stream_chip(address, chips, position);
            if (config.scramble) { pattern.select_epoch(address.epoch); pattern_one.select_epoch(address.epoch); }
            if (config.dsss) { dsss.select_epoch(address.epoch); dsss_one.select_epoch(address.epoch); }
        }
        if (config.spreading_mode == SpreadingMode::pattern) {
            // A secret +/- sign on a real carrier disappears on squaring.
            // Each private candidate has its own versioned counter domain in
            // every enabled layer. Canonical symbol/chip addresses change the
            // entire pair at every symbol; no public mask maps one to the other.
            // Public rows use the same I/Q map and repeat at symbol boundaries
            // so acquisition needs no transmission-index hypothesis.
            NoiseTrace* captured=nullptr;
            if(trace && trace->capture && absolute_chip>=trace->first_chip) {
                const auto offset=absolute_chip-trace->first_chip;
                const auto symbol_index=offset/chips;
                if(offset%chips==0 && symbol_index<trace->symbol_starts.size()) {
                    auto& entry=trace->symbol_starts[static_cast<std::size_t>(symbol_index)];
                    if(!entry.valid)captured=&entry;
                }
            }
            const bool distinct = !legacy_public_pattern(config);
            auto& row = distinct && bit ? pattern_one : pattern;
            auto row_position=position;
            if(!private_patterns && distinct && bit) {
                require(position<=std::numeric_limits<std::uint64_t>::max()/8-public_one_first_chip,
                        "public pattern byte address would overflow");
                row_position+=public_one_first_chip;
            }
            auto* layer = config.dsss ? (private_patterns && bit ? &dsss_one : &dsss) : nullptr;
            auto& inner=symbol_state ? symbol_state->coarse[bit][position%Interleaved::coarse_capacity] : inner_chips[bit];
            // Valid only for this candidate's exact epoch and canonical coarse
            // chip address. Outer chips share the coefficient, never a pattern
            // across bit positions, keys, epochs or the zero/one alternatives.
            auto result = outer && inner.valid && inner.epoch==row.epoch &&
                inner.position==position && !captured ? inner.value :
                (!private_patterns && distinct && !captured && position<public_prefix.size() ?
                 public_prefix[static_cast<std::size_t>(position)][bit] :
                 row.noise(row_position,layer,nullptr,nullptr,captured));
            if(outer)inner={row.epoch,position,result,true};
            if (bit && !distinct) result *= bit_mask[static_cast<std::size_t>((absolute_chip % chips) % bit_mask.size())];
            return result*outer_value*pulse_gain;
        }
        const auto angle = (bit ? 1. : -1.) * std::numbers::pi / 2 *
                           (static_cast<double>(absolute_chip % 4) + fraction);
        auto result = std::polar(1., angle);
        if (config.dsss) result *= static_cast<double>(dsss.sign(position));
        return result*pulse_gain;
    }
    std::array<std::complex<double>,2> values(std::uint64_t absolute,double fraction) {
        if(config.spreading_mode!=SpreadingMode::pattern)return {value(absolute,0,fraction),value(absolute,1,fraction)};
        require(std::isfinite(fraction) && fraction>=0 && fraction<1,
                "pattern chip fraction must be within [0,1)");
        return {cached_value(absolute,0),cached_value(absolute,1)};
    }
    std::complex<double> cached_value(std::uint64_t absolute,unsigned bit) {
        const bool distinct = !legacy_public_pattern(config);
        auto& cache = distinct && bit ? shaped_chips_one : shaped_chips;
        auto& entry=cache[absolute%cache.size()];
        if(!entry.valid || entry.address!=absolute) {
            entry.value=value(absolute,distinct ? bit : 0,0);
            entry.address=absolute;entry.valid=true;
        }
        auto result=entry.value;
        if(bit && !distinct)result*=bit_mask[static_cast<std::size_t>((absolute%chips)%bit_mask.size())];
        return result;
    }
    std::array<std::complex<double>,2> shaped_values(std::uint64_t first_chip,double within) {
        require(std::isfinite(within),"pattern sample coordinate must be finite");
        if(!shaped)return {shaped_value(first_chip,0,within),shaped_value(first_chip,1,within)};
        std::array<std::complex<double>,2> result{};
        pattern_pulse_each(within,symbol,chip,[&](std::uint64_t local,double coefficient) {
            require(local<=std::numeric_limits<std::uint64_t>::max()-first_chip,"pattern chip address would overflow");
            const auto chip_values=values(first_chip+local,0);
            result[0]+=coefficient*chip_values[0];result[1]+=coefficient*chip_values[1];
        });
        return result;
    }
    std::complex<double> shaped_value(std::uint64_t first_chip,unsigned bit,double within) {
        require(bit<=1,"pattern symbol must be a zero or one bit");
        require(std::isfinite(within),"pattern sample coordinate must be finite");
        if(!shaped) {
            if(within<0 || within>=static_cast<double>(symbol))return {};
            const auto local=static_cast<std::uint64_t>(within/static_cast<double>(chip));
            require(local<=std::numeric_limits<std::uint64_t>::max()-first_chip,"pattern chip address would overflow");
            return value(first_chip+local,bit,std::clamp(within/static_cast<double>(chip)-static_cast<double>(local),0.,std::nextafter(1.,0.)));
        }
        return pattern_pulse_sum(within,symbol,chip,[&](std::uint64_t local) {
            require(local<=std::numeric_limits<std::uint64_t>::max()-first_chip,"pattern chip address would overflow");
            const auto absolute=first_chip+local;
            return cached_value(absolute,bit);
        });
    }
};

PatternCode::PatternCode(Config config, std::uint64_t epoch): impl_(std::make_unique<Impl>(config, epoch)) {}
PatternCode::~PatternCode() = default;
PatternCode::PatternCode(PatternCode&&) noexcept = default;
PatternCode& PatternCode::operator=(PatternCode&&) noexcept = default;
std::complex<double> PatternCode::value(std::uint64_t chip, unsigned bit, double fraction) {
    return impl_->value(chip, bit, fraction);
}
std::array<std::complex<double>,2> PatternCode::values(std::uint64_t chip,double fraction) {
    return impl_->values(chip,fraction);
}
std::complex<double> PatternCode::shaped_value(std::uint64_t first_chip,unsigned bit,double within) {
    return impl_->shaped_value(first_chip,bit,within);
}
std::array<std::complex<double>,2> PatternCode::shaped_values(std::uint64_t first_chip,double within) {
    return impl_->shaped_values(first_chip,within);
}
void PatternCode::prepare_symbol(std::uint64_t first_chip,std::stop_token stop) {
    cancelled(stop);
    require(first_chip%impl_->chips==0,"pattern setup requires a symbol-aligned chip address");
    if(impl_->interleaved) {
        const auto address=impl_->stream_address(first_chip/impl_->chips);
        (void)impl_->interleaved->select(address,impl_->outer->key,stop);
    }
}
void PatternCode::set_stream_phase_samples(std::uint64_t phase_samples) {
    require(phase_samples < impl_->config.sample_rate,
            "pattern stream phase must be within its first second");
    if (phase_samples == impl_->config.stream_phase_samples) return;
    impl_->config.stream_phase_samples = phase_samples;
    for (auto& entry : impl_->shaped_chips) entry.valid = false;
    for (auto& entry : impl_->shaped_chips_one) entry.valid = false;
    // The two V2 pattern states are already addressed by canonical epoch and
    // ordinal, with immutable key/version/factor/geometry. A phase change may
    // select one of those same symbols again; do not rebuild its permutation.
    // Absolute-chip shaped caches above still require invalidation. Retain the
    // conservative behavior for non-pattern modulation.
    if(impl_->interleaved && impl_->config.spreading_mode!=SpreadingMode::pattern)
        impl_->interleaved->invalidate();
}
std::uint64_t PatternCode::chip_samples() const { return impl_->chip; }
std::uint64_t PatternCode::chips_per_symbol() const { return impl_->chips; }
std::uint64_t PatternCode::symbol_samples() const { return impl_->symbol; }
std::size_t PatternCode::working_bytes() const {
    return sizeof(PatternCode) + sizeof(Impl)+(impl_->address_cache?sizeof(Impl::AddressCache):0)+
        (impl_->address_cache&&impl_->address_cache->scheduled?impl_->address_cache->scheduled->working_bytes():0)+
        (impl_->address_cache&&impl_->address_cache->packed?impl_->address_cache->packed->working_bytes():0)+(impl_->trace?sizeof(Impl::TraceStorage):0)+
        (impl_->outer?sizeof(StreamCache):0)+(impl_->interleaved?impl_->interleaved->working_bytes():0);
}
void PatternCode::enable_transmit_trace(std::uint64_t first_chip) {
    require(impl_->trace || (working_bytes()<=impl_->config.memory_limit &&
            sizeof(Impl::TraceStorage)<=impl_->config.memory_limit-working_bytes()),
            "pattern trace exceeds memory limit");
    impl_->trace=std::make_unique<Impl::TraceStorage>();impl_->trace->first_chip=first_chip;
}
void PatternCode::capture_transmit_trace(bool enabled) {
    if(impl_->trace)impl_->trace->capture=enabled;
}
void PatternCode::copy_transmit_trace(TransmitTrace& result,std::uint64_t begun_chips) const {
    result.pattern_input.clear();result.pattern_output.clear();result.pattern_key.clear();result.dsss_key.clear();
    result.outer_dsss_key.clear();result.dsss_permutation_seed.clear();
    if(!impl_->trace || impl_->config.spreading_mode!=SpreadingMode::pattern)return;
    const auto& trace=*impl_->trace;
    if(begun_chips && trace.outer_valid) {
        result.outer_dsss_epoch=trace.outer_epoch;result.outer_dsss_byte_offset=trace.rotation_offset;
        result.outer_dsss_first_pair=trace.first_pair;
        result.dsss_permutation_byte_offset=trace.permutation_offset;
        const auto count=std::min<std::uint64_t>(TransmitTrace::byte_limit,
            (std::min(begun_chips,impl_->chips)-1+trace.first_pair)/4+1);
        for(std::size_t i=0;i<count && trace.rotation_valid[i];++i)result.outer_dsss_key.push_back(trace.rotations[i]);
        if(trace.permutation_valid)result.dsss_permutation_seed.assign(trace.permutation.begin(),trace.permutation.end());
    }
    const auto begun_symbols=begun_chips?(begun_chips-1)/impl_->chips+1:0;
    const auto count=std::min<std::uint64_t>(begun_symbols,impl_->trace->symbol_starts.size());
    for(std::size_t i=0;i<count;++i) {
        const auto& entry=impl_->trace->symbol_starts[i];
        if(!entry.valid)break;
        result.pattern_input.insert(result.pattern_input.end(),entry.input.begin(),entry.input.end());
        result.pattern_output.insert(result.pattern_output.end(),entry.output.begin(),entry.output.end());
        if(impl_->config.scramble)result.pattern_key.insert(result.pattern_key.end(),entry.input.begin(),entry.input.end());
        if(impl_->config.dsss)result.dsss_key.insert(result.dsss_key.end(),entry.key.begin(),entry.key.end());
    }
}

namespace {
// Cover roots never enter the receive bank or diagnostic trace. Enabled input
// purposes contribute in NEW versioned domains; child purposes are separated
// again exactly as for an ordinary private transmission. Payload addresses and
// selected communication secrets are not used as legal cover candidates.
Config cover_config(Config config, std::uint64_t epoch, StreamDomain domain) {
    std::array<std::uint8_t,32> root{};
    struct WipeRoot { std::array<std::uint8_t,32>& value;~WipeRoot(){OPENSSL_cleanse(value.data(),value.size());} } wipe{root};
    const auto mix=[&](const Crypto& key,StreamPurpose purpose) {
        auto bytes=key.stream(purpose,epoch,0,root.size(),domain);
        for(std::size_t i=0;i<root.size();++i)root[i]^=bytes[i];
        OPENSSL_cleanse(bytes.data(),bytes.size());
    };
    if(config.data_key)mix(*config.data_key,StreamPurpose::Data);
    if(config.scramble)mix(Crypto(config.spreading_seed),StreamPurpose::Scrambler);
    if(config.dsss || config.dsss_factor>1)mix(Crypto(config.dsss_seed),StreamPurpose::Dsss);
    config.data_key=Crypto(root);OPENSSL_cleanse(root.data(),root.size());
    const auto seed=[&](StreamPurpose purpose,auto& target) {
        auto bytes=config.data_key->stream(purpose,0,0,target.size());
        std::copy(bytes.begin(),bytes.end(),target.begin());OPENSSL_cleanse(bytes.data(),bytes.size());
    };
    seed(StreamPurpose::Scrambler,config.spreading_seed);seed(StreamPurpose::Dsss,config.dsss_seed);
    config.fake_hopping.reset(); // The enclosing TX owns the ONE continuous FHSS schedule.
    config.scheduled_hopping.reset(); // Cover virtual symbols never nest the physical active/null map.
    return config;
}
double smooth_edge(double x) {
    x=std::clamp(x,0.,1.);return x*x*x*(10+x*(-15+6*x));
}
struct VirtualCover {
    Config config;
    PatternCode code;
    StreamCache data;
    std::uint64_t epoch;
    struct Entry { std::uint64_t chip=0;std::complex<double> value{};bool valid=false; };
    std::array<Entry,24> cache{};
    VirtualCover(Config value,std::uint64_t time):config(value),code(value,time),
        data(*value.data_key,StreamPurpose::Data,time,StreamDomain::Payload),epoch(time) {}
    ~VirtualCover() {
        OPENSSL_cleanse(cache.data(),sizeof(cache));
        OPENSSL_cleanse(config.spreading_seed.data(),config.spreading_seed.size());
        OPENSSL_cleanse(config.dsss_seed.data(),config.dsss_seed.size());
    }
    std::complex<double> chip(std::uint64_t symbol,std::uint64_t local,std::stop_token stop) {
        const auto first=symbol*code.chips_per_symbol(),absolute=first+local;
        auto& entry=cache[absolute%cache.size()];
        if(!entry.valid || entry.chip!=absolute) {
            code.prepare_symbol(first,stop); // FULL map, including unobserved portions of long bits.
            const auto address=symbol_stream_address(epoch,config.stream_phase_samples,symbol,
                code.symbol_samples(),config.sample_rate);
            data.select_epoch(address.epoch);
            const auto bit=(data.byte(address.ordinal/8)>>(7-address.ordinal%8))&1U;
            entry.value=code.value(absolute,bit);entry.chip=absolute;entry.valid=true;
        }
        return entry.value;
    }
    template<class Weight>
    std::complex<double> sample(long double sample,Weight weight,std::stop_token stop) {
        const auto duration=static_cast<long double>(code.symbol_samples());
        const auto support=static_cast<long double>(pattern_pulse_padding_samples(config));
        std::complex<double> result{};
        if(!support) {
            if(sample<0)return {};
            const auto symbol=static_cast<std::uint64_t>(std::floor(sample/duration));
            const auto within=sample-static_cast<long double>(symbol)*duration;
            return weight(sample)*chip(symbol,static_cast<std::uint64_t>(within/static_cast<double>(code.chip_samples())),stop);
        }
        const auto begin=std::max(0.L,std::floor((sample-support)/duration));
        const auto end=std::max(begin,std::floor((sample+support)/duration)+1);
        for(auto symbol=static_cast<std::uint64_t>(begin);symbol<static_cast<std::uint64_t>(end);++symbol)
            pattern_pulse_each(static_cast<double>(sample-static_cast<long double>(symbol)*duration),code.symbol_samples(),code.chip_samples(),
                [&](std::uint64_t local,double pulse) {
                    const auto first=local*code.chip_samples();
                    const auto length=std::min(code.chip_samples(),code.symbol_samples()-first);
                    const auto center=static_cast<long double>(symbol)*duration+first+.5L*length;
                    const auto gain=weight(center);
                    if(gain!=0)result+=pulse*gain*chip(symbol,local,stop);
                });
        return result;
    }
};
}

struct PatternTransmitter::Impl {
    Bytes bits;
    Config config;
    std::unique_ptr<FhssSchedule> hopping;
    std::unique_ptr<SchedHopSourceMap> scheduled;
    std::uint64_t physical_position=0,physical_total=0,physical_origin=0,source_origin=0;
    FhssPowerShape hop_power;
    PatternCode code;
    std::unique_ptr<VirtualCover> cover;
    bool cover_is_tail=false,private_cover=false,protected_cover=false;
    std::uint64_t cover_start=0,cover_end=0,cover_origin=0;
    struct RetainedPrefix {long double center=0;double scale=0;std::complex<double> value{};};
    using PrefixSnapshot=std::array<RetainedPrefix,32>;
    std::unique_ptr<PrefixSnapshot> retained_prefix;
    std::size_t retained_prefix_count=0;
    std::size_t cover_bytes=0;
    std::optional<std::uint64_t> stop_at;
    std::uint64_t original_total=0,original_source_total=0,useful_cover_span=0;
    std::unique_ptr<StreamCache> generated_data;
    std::unique_ptr<StreamCache> settling, settling_data, settling_pattern, settling_dsss;
    std::unique_ptr<StreamCache> suppression, suppression_data, suppression_pattern, suppression_dsss;
    std::unique_ptr<FhssSymbolCalendar> calendar;
    std::unique_ptr<ScheduledSymbolCalendar> packed_calendar;
    std::unique_ptr<VirtualCover> waiting_cover;
    std::uint64_t calendar_rank=0,calendar_tick=0;
    std::uint64_t bit_count=0, source_epoch=0, start = 0, position = 0, total = 0, training = 0, padding = 0, content_end = 0, suppression_samples = 0;
    struct SettlingChip {
        std::uint64_t address=0;
        std::complex<double> value{};
        bool valid=false,payload=false;
    };
    std::array<SettlingChip,32> settling_chips{};
    // Private/protected cover uses VirtualCover and never the diagnostic
    // suppression cache. Allocate the latter only for its actual fallback,
    // before playback, with its complete size in both workspace accounts.
    using SuppressionChips=std::array<SettlingChip,32>;
    std::unique_ptr<SuppressionChips> suppression_chips;
    Impl(Bytes input, Config value, std::uint64_t epoch, std::uint64_t start_chip, bool surrounding_noise,bool trace,
         std::uint64_t generated_count=0):
        bits(std::move(input)), config(value), code(config, epoch),
        bit_count(generated_count?generated_count:bits.size()),source_epoch(epoch),start(start_chip) {
        if(config.fake_hopping) {
            require(config.data_key.has_value(),"FHSS requires an encryption key");
            hopping=std::make_unique<FhssSchedule>(config.fake_hopping,*config.data_key,epoch);
        }
        if(trace)code.enable_transmit_trace(start);
        require(bit_count, "pattern transmission requires at least one bit");
        if(generated_count) {
            require(config.data_key.has_value(),"generated pattern bits require a Data key");
            generated_data=std::make_unique<StreamCache>(*config.data_key,StreamPurpose::Data,epoch,StreamDomain::Payload);
        }
        require(std::all_of(bits.begin(), bits.end(), [](auto bit) { return bit <= 1; }),
                "pattern input elements must be zero or one");
        require(start % code.chips_per_symbol() == 0,
                "complete pattern transmission must start at a symbol-aligned chip address");
        require(!config.fake_hopping || !config.fake_hopping->matched_gaps || start==0,
                "UTC gated transmission must rebase epoch/phase rather than use a nonzero diagnostic first_chip");
        require(bit_count <= std::numeric_limits<std::uint64_t>::max() / code.symbol_samples(),
                "pattern transmission duration would overflow");
        if(packed_scheduled_symbols(config)) {
            packed_calendar=std::make_unique<ScheduledSymbolCalendar>(config);
            // The key, epoch, phase and geometry are immutable in a TX source.
            // Do not revisit its initial group on every later PCM sample: a
            // single keyed-frame cache would thrash between anchor/current.
            scheduled=std::make_unique<SchedHopSourceMap>(*config.scheduled_hopping,*config.data_key,config.sample_rate);
            const auto anchor=symbol_schedule_detail::add(symbol_schedule_detail::multiply(epoch,config.sample_rate),config.stream_phase_samples);
            calendar_rank=packed_calendar->first_rank(epoch,config.stream_phase_samples);
            calendar_tick=scheduled->locate(anchor).source.sample;
        }
        if(deferred_symbols(config)) {
            calendar=std::make_unique<FhssSymbolCalendar>(*config.fake_hopping,config.sample_rate,pattern_pulse_padding_samples(config),code.symbol_samples());
            calendar_rank=calendar->first_rank(epoch,config.stream_phase_samples);
            calendar_tick=symbol_schedule_detail::add(symbol_schedule_detail::multiply(epoch,config.sample_rate),config.stream_phase_samples);
        }
        total = symbol_schedule_detail::add(payload_offset(bit_count-1),code.symbol_samples());
        training=surrounding_noise?training_sample_count(config):0;
        require(training<=std::numeric_limits<std::uint64_t>::max()-total,"pattern transmission duration would overflow");
        total+=training;
        padding=pattern_pulse_padding_samples(config);
        if(hopping) {
            hop_power=FhssPowerShape(*hopping->plan,config.sample_rate,padding,code.symbol_samples());
            hopping->source_origin_samples=static_cast<long double>(config.stream_phase_samples)-training-padding;
            hopping->source_rate=config.sample_rate;
        }
        require(padding<=(std::numeric_limits<std::uint64_t>::max()-total)/2,"pattern transmission duration would overflow");
        total+=2*padding;
        content_end=total;
        suppression_samples=surrounding_noise?suppression_sample_count(config):0;
        useful_cover_span=surrounding_noise?protected_cover_span_samples(config):0;
        require(suppression_samples<=std::numeric_limits<std::uint64_t>::max()-total,
                "pattern suppression duration would overflow");
        total+=suppression_samples;
        physical_total=total;
        if(scheduled_bursts(config)) {
            require(config.data_key.has_value(),"scheduled hopping requires an encryption key");
            require(start==0,"scheduled hopping requires a rebased symbol origin");
            if(!scheduled)scheduled=std::make_unique<SchedHopSourceMap>(*config.scheduled_hopping,*config.data_key,config.sample_rate);
            const auto anchor=symbol_schedule_detail::add(symbol_schedule_detail::multiply(epoch,config.sample_rate),config.stream_phase_samples);
            const auto payload=scheduled->locate(anchor);
            require(payload.active && payload.source.fraction==0,"scheduled payload must begin within an active sample cell");
            const auto prefix=symbol_schedule_detail::add(training,padding);
            require(payload.source.sample>=prefix,"scheduled prefix precedes the representable source clock");
            source_origin=payload.source.sample-prefix;
            const auto origin=scheduled->source_to_physical({source_origin});
            require(origin.fraction==0,"scheduled output origin must have an integer sample coordinate");
            physical_origin=origin.sample;
            physical_total=scheduled_end(total);
            if(hopping) {
                require(hop_power.smooth,"scheduled hopping requires the production smooth FHSS envelope");
                const auto second=symbol_schedule_detail::multiply(epoch,config.sample_rate);
                hopping->source_origin_samples=physical_origin>=second?
                    static_cast<long double>(physical_origin-second):-static_cast<long double>(second-physical_origin);
            }
        }
        original_total=physical_total;original_source_total=total;
        // Preserve the established DSSS-only diagnostic surrounding waveform:
        // it has neither Data encryption nor private Scrambler. Full-symbol
        // cover in that unsupported GUI geometry exposed an extra prefix
        // admission in the 6000Hz/32-chip regression. Do not re-seed around the
        // fixture or change RX thresholds. Encrypted GUI/message and tuning
        // paths retain full cover; broader false-accept calibration is pending.
        private_cover=private_cover_enabled(config);
        require(!hop_power.enabled||private_cover,"FHSS power ramps require private shaped patterns");
        protected_cover=private_cover && surrounding_noise;
        cover_start=content_end-padding;
        // Retain the role-local virtual stream origin. Bring the same cover
        // forward through the common shaper; do not skip to payload-length-
        // dependent cover addresses. Once any cover is emitted this is fixed.
        cover_origin=cover_start;
        cover_end=padding?total-padding-1:total-std::max<std::uint64_t>(1,config.sample_rate/100);
        // A single reusable cover map, never two full PCM symbols. Charge the
        // worst-case map plus derivation scratch even before its lazy allocation.
        if(private_cover && (training || suppression_samples))
            cover_bytes=sizeof(VirtualCover)+code.working_bytes()+sizeof(Config)+2*sizeof(Crypto)+64;
        if(calendar || packed_calendar)cover_bytes+=sizeof(VirtualCover)+code.working_bytes()+sizeof(Config)+2*sizeof(Crypto)+64;
        if(calendar)cover_bytes+=sizeof(FhssSymbolCalendar);
        if(packed_calendar)cover_bytes+=packed_calendar->working_bytes();
        const bool retain_prefix=protected_cover && training && padding;
        if(retain_prefix)cover_bytes+=sizeof(PrefixSnapshot);
        require(bit_count <= std::numeric_limits<std::uint64_t>::max() / code.chips_per_symbol(),
                "pattern transmission chip count would overflow");
        const auto chip_count = bit_count * code.chips_per_symbol();
        require(chip_count - 1 <= std::numeric_limits<std::uint64_t>::max() - start,
                "pattern transmission chip address would overflow");
        if(config.scramble || config.dsss)
            require(start+chip_count-1 <= (std::numeric_limits<std::uint64_t>::max()-7)/8,
                    "private pattern byte address would overflow");
        const auto caches=private_cover?0U:(static_cast<unsigned>(training!=0)+static_cast<unsigned>(suppression_samples!=0))*
            (1U+static_cast<unsigned>(config.data_key.has_value())+
             static_cast<unsigned>(config.scramble)+static_cast<unsigned>(config.dsss));
        const auto fixed = sizeof(Impl) + sizeof(PatternTransmitter) + code.working_bytes()+cover_bytes+
            (hopping?sizeof(FhssSchedule)+sizeof(FhssPlan)+hopping->plan->power_fallback_reason.capacity()+1+FhssSchedule::cycle_bytes+sizeof(std::array<unsigned,FhssPlan::capacity>):0)+
            (scheduled?scheduled->working_bytes():0)+(suppression_samples && !private_cover?sizeof(SuppressionChips):0)+
            (caches+static_cast<unsigned>(generated_data!=nullptr))*sizeof(StreamCache);
        require(fixed <= config.memory_limit && bits.capacity() <= config.memory_limit - fixed,
                "pattern transmitter exceeds memory limit");
        // Long symbols have no prefix. Reserve this bounded snapshot only when
        // it can be needed, but before playback so cancellation never allocates.
        if(retain_prefix)retained_prefix=std::make_unique<PrefixSnapshot>();
        if(training && !private_cover) {
            // Private layers use the same keys, purposes and epoch as the
            // payload; only the fixed CTR pad changes. Encrypt noise bytes
            // together with every enabled spreading stream before I/Q mapping.
            constexpr auto domain=StreamDomain::Preamble;
            settling=std::make_unique<StreamCache>(public_seed,StreamPurpose::Scrambler,epoch,domain);
            if(config.data_key)settling_data=std::make_unique<StreamCache>(
                *config.data_key,StreamPurpose::Data,epoch,domain);
            if(config.scramble)settling_pattern=std::make_unique<StreamCache>(
                config.spreading_seed,StreamPurpose::Scrambler,epoch,domain);
            if(config.dsss)settling_dsss=std::make_unique<StreamCache>(
                config.dsss_seed,StreamPurpose::Dsss,epoch,domain);
        }
        if(suppression_samples && !private_cover) {
            suppression_chips=std::make_unique<SuppressionChips>();
            // A third CTR domain makes the tail independent of both the
            // settling prefix and every valid payload pattern/mask position.
            constexpr auto domain=StreamDomain::Suppression;
            suppression=std::make_unique<StreamCache>(public_seed,StreamPurpose::Scrambler,epoch,domain);
            if(config.data_key)suppression_data=std::make_unique<StreamCache>(
                *config.data_key,StreamPurpose::Data,epoch,domain);
            if(config.scramble)suppression_pattern=std::make_unique<StreamCache>(
                config.spreading_seed,StreamPurpose::Scrambler,epoch,domain);
            if(config.dsss)suppression_dsss=std::make_unique<StreamCache>(
                config.dsss_seed,StreamPurpose::Dsss,epoch,domain);
        }
    }
    ~Impl() {
        if(retained_prefix)OPENSSL_cleanse(retained_prefix->data(),sizeof(*retained_prefix));
        OPENSSL_cleanse(settling_chips.data(),sizeof(settling_chips));
        if(suppression_chips)OPENSSL_cleanse(suppression_chips->data(),sizeof(*suppression_chips));
        OPENSSL_cleanse(config.spreading_seed.data(),config.spreading_seed.size());
        OPENSSL_cleanse(config.dsss_seed.data(),config.dsss_seed.size());
    }
    std::uint64_t payload_offset(std::uint64_t symbol) const {
        if(packed_calendar)return packed_calendar->source_slot(symbol_schedule_detail::add(calendar_rank,symbol))-calendar_tick;
        return calendar?calendar->slot(symbol_schedule_detail::add(calendar_rank,symbol))-calendar_tick:
            symbol_schedule_detail::multiply(symbol,code.symbol_samples());
    }
    std::uint64_t payload_lower_bound(long double position) const {
        if(position<=0)return 0;
        if(packed_calendar)return packed_calendar->first_rank_source(symbol_schedule_detail::add(calendar_tick,
            static_cast<std::uint64_t>(std::ceil(position))))-calendar_rank;
        if(!calendar)return static_cast<std::uint64_t>(std::ceil(position/code.symbol_samples()));
        return calendar->lower_bound(source_epoch,config.stream_phase_samples,static_cast<std::uint64_t>(std::ceil(position)));
    }
    // Cover is independent of every valid candidate. Its virtual symbols keep
    // full DSSS permutations; only not-yet-emitted coefficients are selected.
    std::complex<double> wait_sample(std::uint64_t cursor,std::stop_token stop) {
        if((!calendar && !packed_calendar) || cursor+padding<training+padding || cursor>=content_end)return {};
        const auto relative=static_cast<long double>(cursor)-padding-training;
        const auto previous=payload_lower_bound(relative+padding+1);
        if(previous && previous<=bit_count && relative-padding>=payload_offset(previous-1) &&
           relative+padding<payload_offset(previous-1)+code.symbol_samples())return {};
        if(!waiting_cover) {
            auto child=cover_config(config,source_epoch,StreamDomain::CoverWaitV1);
            waiting_cover=std::make_unique<VirtualCover>(child,source_epoch);
            OPENSSL_cleanse(child.spreading_seed.data(),child.spreading_seed.size());
            OPENSSL_cleanse(child.dsss_seed.data(),child.dsss_seed.size());
        }
        return waiting_cover->sample(relative,[&](long double center) {
            const auto absolute=center+padding+training;
            if(center<0 || absolute>=content_end-padding || !keep_center(absolute))return 0.;
            const auto next=payload_lower_bound(center+1);
            if(next && next<=bit_count && center>=payload_offset(next-1) && center<payload_offset(next-1)+code.symbol_samples())return 0.;
            return hop_gain(absolute);
        },stop);
    }
    unsigned bit(std::uint64_t symbol) {
        if(!generated_data)return bits[static_cast<std::size_t>(symbol)];
        // Open-ended noise visits this for every pulse sample. Reuse the
        // immutable calendar already validated for this transmitter instead of
        // reconstructing waveform geometry for each generated Data bit.
        SymbolStreamAddress address;
        if(calendar) {
            const auto total=symbol_schedule_detail::add(payload_offset(symbol),config.stream_phase_samples);
            const auto sample=static_cast<std::uint32_t>(total%config.sample_rate);
            address={symbol_schedule_detail::add(source_epoch,total/config.sample_rate),
                sample/code.symbol_samples(),sample};
        } else if(scheduled) {
            const auto physical=scheduled->source_to_physical({symbol_schedule_detail::add(source_origin,
                symbol_schedule_detail::add(training+padding,payload_offset(symbol)))});
            require(physical.fraction==0,"scheduled Data address must have an integer sample origin");
            const auto sample=static_cast<std::uint32_t>(physical.sample%config.sample_rate);
            address={physical.sample/config.sample_rate,sample/code.symbol_samples(),sample};
        } else address=symbol_stream_address(source_epoch,config.stream_phase_samples,symbol,
            code.symbol_samples(),config.sample_rate);
        generated_data->select_epoch(address.epoch);
        return (generated_data->byte(address.ordinal/8)>>(7-address.ordinal%8))&1U;
    }
    VirtualCover& virtual_cover(bool tail) {
        if(!cover || cover_is_tail!=tail) {
            cover.reset(); // release the old complete permutation before allocation
            auto child=cover_config(config,source_epoch,tail?StreamDomain::CoverSuppressionV1:StreamDomain::CoverPreambleV1);
            cover=std::make_unique<VirtualCover>(child,source_epoch);cover_is_tail=tail;
            OPENSSL_cleanse(child.spreading_seed.data(),child.spreading_seed.size());
            OPENSSL_cleanse(child.dsss_seed.data(),child.dsss_seed.size());
        }
        return *cover;
    }
    bool keep_center(long double center) const {
        return !stop_at || center<static_cast<long double>(*stop_at)+padding;
    }
    double hop_gain(long double center) {
        return hop_power.enabled&&!hop_power.smooth&&hopping->near_change(center,hop_power.coefficient_radius(),hop_power)?0.:1.;
    }
    std::complex<double> payload_symbol(std::uint64_t symbol,double within,std::stop_token stop) {
        const auto first=start+symbol*code.chips_per_symbol();
        const auto origin=static_cast<long double>(padding)+training+payload_offset(symbol);
        if(!keep_center(origin+.5L*std::min(code.chip_samples(),code.symbol_samples())))return {};
        code.prepare_symbol(first,stop);
        if(!padding) {
            if(within<0 || within>=static_cast<double>(code.symbol_samples()))return {};
            const auto local=static_cast<std::uint64_t>(within/code.chip_samples());
            const auto offset=local*code.chip_samples();
            const auto length=std::min(code.chip_samples(),code.symbol_samples()-offset);
            const auto center=origin+offset+.5L*length;
            if(!keep_center(center) || hop_gain(center)==0)return {};
            return code.shaped_value(first,bit(symbol),within);
        }
        if(!stop_at&&(!hop_power.enabled||hop_power.smooth))return code.shaped_value(first,bit(symbol),within);
        return pattern_pulse_sum(within,code.symbol_samples(),code.chip_samples(),[&](std::uint64_t local) {
            const auto offset=local*code.chip_samples();
            const auto length=std::min(code.chip_samples(),code.symbol_samples()-offset);
            if(!keep_center(origin+offset+.5L*length))return std::complex<double>{};
            if(hop_gain(origin+offset+.5L*length)==0)return std::complex<double>{};
            const auto absolute=first+local;
            auto& entry=settling_chips[absolute%settling_chips.size()];
            if(!entry.valid || !entry.payload || entry.address!=absolute) {
                entry.value=code.value(absolute,bit(symbol));entry.address=absolute;entry.valid=true;entry.payload=true;
            }
            return entry.value;
        });
    }
    void finish() {
        if(stop_at || position==total)return;
        stop_at=position;
        if(!position) { total=0;return; }
        if(protected_cover) {
            if(position>=cover_start || padding>=cover_start-position)return; // already committed to this cover; never restart it
            // A coefficient whose support touched output is immutable. Retain
            // the next support radius, then replace only untouched coefficients.
            const auto cut=predicted_cut(position);
            cover_start=std::min(cover_start,cut);
            // Keep the original symbol/partial-chip lattice, even for a
            // fractional-chip stop. The child stream remains role-local.
            cover_origin=padding+(position/code.symbol_samples())*code.symbol_samples();
            if(retained_prefix && position<training+2*padding) {
                // Keep just the prefix coefficients whose tails can survive P.
                // Enumerate support, including coefficients zero exactly at P.
                auto& prefix=virtual_cover(false);
                const auto lo=std::max(0.L,static_cast<long double>(position)-2*padding);
                const auto hi=std::min(static_cast<long double>(training),static_cast<long double>(cut)-padding);
                const auto duration=code.symbol_samples(),chip=code.chip_samples();
                for(auto symbol=static_cast<std::uint64_t>(lo/duration);symbol*duration<hi;++symbol) {
                    const auto origin=symbol*duration;
                    const auto first=lo>origin?static_cast<std::uint64_t>((lo-origin)/chip):0;
                    for(auto k=first;k<code.chips_per_symbol();++k) {
                        const auto offset=k*chip,length=std::min(chip,duration-offset);
                        const auto center=static_cast<long double>(origin)+offset+.5L*length;
                        if(center>=hi)break;
                        if(center<lo)continue;
                        require(retained_prefix_count<retained_prefix->size(),"prefix pulse support exceeds bounded retention");
                        (*retained_prefix)[retained_prefix_count++]={center+padding,std::sqrt(double(length)/chip),prefix.chip(symbol,k,{})};
                    }
                }
                cover.reset();
            }
            const auto useful=useful_cover_span;
            const auto ramp=std::max<std::uint64_t>(1,config.sample_rate/100);
            cover_end=cover_start+2*padding+useful;
            total=cover_end+(padding?padding+1:ramp);
            return;
        }
        // Diagnostic/no-surrounding path retains its original drain reference.
        const auto remaining=padding?2*padding+1:std::max<std::uint64_t>(1,config.sample_rate/100);
        total=position+std::min(remaining,total-position);
    }
    double unshaped_finish_gain(std::uint64_t cursor) const {
        if(protected_cover) {
            if(cursor<cover_end)return 1.;
            return total-cover_end>1?1-smooth_edge(double(cursor-cover_end)/double(total-cover_end-1)):0.;
        }
        if(!stop_at || cursor<*stop_at)return 1.;
        const auto duration=total-*stop_at;
        return duration>1?1-smooth_edge(static_cast<double>(cursor-*stop_at)/static_cast<double>(duration-1)):0.;
    }
    std::complex<double> protected_sample(std::uint64_t cursor,std::stop_token stop) {
        if(static_cast<long double>(cursor)+padding<cover_start)return {};
        return virtual_cover(true).sample(static_cast<long double>(cursor)-cover_origin,[&](long double center) {
            const auto absolute=center+cover_origin;
            return absolute>=cover_start && (absolute<cover_end || !padding)?hop_gain(absolute):0.;
        },stop);
    }
    std::complex<double> suppression_sample(std::uint64_t offset,std::stop_token stop) {
        require(suppression_chips && suppression,"diagnostic suppression state unavailable");
        const auto noise=[&](std::uint64_t chip) {
            if(!keep_center(static_cast<long double>(content_end)-padding+(chip+.5L)*code.chip_samples()))
                return std::complex<double>{};
            auto& entry=(*suppression_chips)[chip%suppression_chips->size()];
            if(!entry.valid || entry.address!=chip) {
                entry.value=suppression->noise(chip,suppression_data.get(),suppression_pattern.get(),suppression_dsss.get());
                entry.address=chip;entry.valid=true;
            }
            return entry.value;
        };
        // Virtual surrounding chips give shaped noise its normal power from
        // the first sample, without changing or overlapping the payload tail.
        const auto value=padding?pattern_pulse_sum(static_cast<double>(offset+padding),
            suppression_samples+2*padding,code.chip_samples(),noise):noise(offset/code.chip_samples());
        // A ten-millisecond edge taper avoids an abrupt boundary. The fixed
        // output duration includes both tapers, including for very long bits.
        const auto edge=std::min(offset,suppression_samples-1-offset);
        const auto ramp=std::max<std::uint64_t>(1,(config.sample_rate+99U)/100U);
        const auto window=edge>=ramp?1.:std::pow(std::sin(.5*std::numbers::pi*
            static_cast<double>(edge)/static_cast<double>(ramp)),2);
        return value*window;
    }
    std::complex<double> shaped_sample(std::uint64_t cursor,std::stop_token stop) {
        const auto relative=static_cast<long double>(cursor)-padding;
        std::complex<double> result{};
        if(training && private_cover && relative<=static_cast<long double>(training)+padding) {
            if(stop_at && cursor>=*stop_at && retained_prefix_count) {
                for(std::size_t i=0;i<retained_prefix_count;++i) {
                    const auto& p=(*retained_prefix)[i];
                    result+=hop_gain(p.center)*p.value*p.scale*pattern_pulse(static_cast<double>((static_cast<long double>(cursor)-p.center)/code.chip_samples()));
                }
            } else if(!stop_at || cursor<*stop_at || !protected_cover) {
                result+=virtual_cover(false).sample(relative,[&](long double center) {
                    return center<training && keep_center(static_cast<long double>(padding)+center)?hop_gain(padding+center):0.;
                },stop);
            }
        } else if(training && !private_cover) {
            result+=pattern_pulse_sum(static_cast<double>(relative),training,code.chip_samples(),[&](std::uint64_t chip) {
                const auto duration=std::min(code.chip_samples(),training-chip*code.chip_samples());
                if(!keep_center(static_cast<long double>(padding)+chip*code.chip_samples()+.5L*duration))
                    return std::complex<double>{};
                auto& entry=settling_chips[chip%settling_chips.size()];
                if(!entry.valid || entry.payload || entry.address!=chip) {
                    entry.payload=false;
                    entry.value=settling->noise(chip,settling_data.get(),settling_pattern.get(),settling_dsss.get());
                    entry.address=chip;entry.valid=true;
                }
                return entry.value;
            });
        }
        const auto payload=relative-training;
        const auto support=static_cast<long double>(padding);
        const auto duration=static_cast<long double>(code.symbol_samples());
        const auto begin=payload_lower_bound(payload-support-duration);
        const auto end=std::min(bit_count,payload_lower_bound(payload+support+1));
        for(auto symbol=static_cast<std::uint64_t>(begin);symbol<static_cast<std::uint64_t>(std::max(begin,end));++symbol) {
            const auto within=static_cast<double>(payload-payload_offset(symbol));
            if(!stop_at&&(!hop_power.enabled||hop_power.smooth||!hopping->near_change(cursor,hop_power.affected_radius(),hop_power))) {
                // Ordinary transmission keeps the existing hot path; stopping
                // must not add coefficient gates to every payload sample.
                if(config.dsss_factor>1 && config.outer_dsss_version==OuterDsssVersion::interleaved_v2)
                    code.prepare_symbol(start+symbol*code.chips_per_symbol(),stop);
                result+=code.shaped_value(start+symbol*code.chips_per_symbol(),bit(symbol),within);
            } else result+=payload_symbol(symbol,within,stop);
        }
        return result;
    }
    template<class Output, class Convert>
    std::size_t render_source(std::uint64_t& cursor, std::span<Output> output, Convert convert,
                       std::stop_token stop, const ChipObserver& observer,bool trace=false) {
        struct CaptureGuard {
            PatternCode& code;
            CaptureGuard(PatternCode& value,bool enabled):code(value) { code.capture_transmit_trace(enabled); }
            ~CaptureGuard() { code.capture_transmit_trace(false); }
        } capture(code,trace);
        cancelled(stop);
        const auto count = static_cast<std::size_t>(std::min<std::uint64_t>(output.size(), total - cursor));
        const auto angle = detail::pattern_carrier_cycles(cursor,config.sample_rate,config.carrier_hz)*tau;
        auto oscillator = std::polar(1., static_cast<double>(angle));
        const auto step = std::polar(1., tau * config.carrier_hz / config.sample_rate);
        const auto amplitude = std::sqrt(2 * nominal_signal_power)*outer_dsss_transmit_gain(config);
        if(padding || packed_calendar) {
            for(std::size_t i=0;i<count;++i,++cursor) {
                if((i&4095U)==0)cancelled(stop);
                const auto baseband=protected_cover?shaped_sample(cursor,stop)+wait_sample(cursor,stop)+protected_sample(cursor,stop):
                    (cursor<content_end?shaped_sample(cursor,stop)+wait_sample(cursor,stop):suppression_sample(cursor-content_end,stop));
                const auto gain=hop_power.smooth&&!scheduled?hop_power.gain(source_epoch,
                    hopping->source_origin_samples+cursor,config.sample_rate):1.;
                const auto finish_gain=padding?1.:unshaped_finish_gain(cursor);
                output[i]=convert(finish_gain*gain*oscillator*pattern_limit_pcm(amplitude*baseband));
                if(observer && cursor>=padding+training && cursor<content_end-padding) {
                    const auto payload=cursor-padding-training;
                    const auto next=payload_lower_bound(payload+1);
                    const auto symbol=next?next-1:0;
                    if(!next || symbol>=bit_count || payload<payload_offset(symbol) ||
                       payload>=payload_offset(symbol)+code.symbol_samples()) {oscillator*=step;continue;}
                    const auto within=payload-payload_offset(symbol);
                    if(within%code.chip_samples()==0 && keep_center(static_cast<long double>(cursor)+
                        .5L*std::min(code.chip_samples(),code.symbol_samples()-within)))
                        observer(amplitude*code.value(start+symbol*code.chips_per_symbol()+within/code.chip_samples(),
                            bit(symbol)));
                }
                oscillator*=step;
            }
            return count;
        }
        for (std::size_t i = 0; i < count;) {
            cancelled(stop);
            if(cursor>=(protected_cover?cover_start:content_end)) {
                for(;i<count;++i,++cursor) {
                    if((i&4095U)==0)cancelled(stop);
                    output[i]=convert(unshaped_finish_gain(cursor)*amplitude*oscillator*(protected_cover?protected_sample(cursor,stop):suppression_sample(cursor-content_end,stop)));
                    oscillator*=step;
                }
                continue;
            }
            if(cursor<training) {
                const auto chip=cursor/code.chip_samples();
                auto pattern=private_cover?virtual_cover(false).sample(static_cast<long double>(cursor),[](long double){return 1.;},stop):
                    settling->noise(chip,settling_data.get(),settling_pattern.get(),settling_dsss.get());
                const auto within=private_cover?cursor%code.symbol_samples():cursor;
                const auto run=static_cast<std::size_t>(std::min<std::uint64_t>({count-i,training-cursor,protected_cover?cover_start-cursor:training-cursor,
                    private_cover?code.symbol_samples()-within:training-cursor,
                    code.chip_samples()-within%code.chip_samples()}));
                for(std::size_t j=0;j<run;++j,++i,++cursor) {
                    if((i&4095U)==0)cancelled(stop);
                    output[i]=convert(unshaped_finish_gain(cursor)*amplitude*oscillator*pattern);oscillator*=step;
                }
                continue;
            }
            const auto payload=cursor-training;
            const auto symbol = payload / code.symbol_samples();
            const auto within = payload % code.symbol_samples();
            const auto chip = start + symbol * code.chips_per_symbol() + within / code.chip_samples();
            if(config.dsss_factor>1 && config.outer_dsss_version==OuterDsssVersion::interleaved_v2)
                code.prepare_symbol(start+symbol*code.chips_per_symbol(),stop);
            const auto fraction = static_cast<double>(within % code.chip_samples()) / static_cast<double>(code.chip_samples());
            const auto symbol_bit = bit(symbol);
            auto pattern = code.value(chip, symbol_bit, fraction);
            const auto pattern_step = config.spreading_mode == SpreadingMode::tone ?
                std::polar(1., (symbol_bit ? 1. : -1.) * std::numbers::pi / (2 * static_cast<double>(code.chip_samples()))) :
                std::complex<double>{1, 0};
            const auto run = static_cast<std::size_t>(std::min<std::uint64_t>({count - i,protected_cover?cover_start-cursor:content_end-cursor,
                code.symbol_samples() - within, code.chip_samples() - within % code.chip_samples()}));
            for (std::size_t j = 0; j < run; ++j, ++i, ++cursor) {
                if ((i & 4095U) == 0) cancelled(stop);
                output[i] = convert(unshaped_finish_gain(cursor)*amplitude * oscillator * pattern);
                if (j == 0 && within % code.chip_samples() == 0 && observer && unshaped_finish_gain(cursor)>0)
                    observer(amplitude * pattern);
                oscillator *= step; pattern *= pattern_step;
            }
        }
        return count;
    }
    std::uint64_t scheduled_end(std::uint64_t source_end) const {
        if(!scheduled)return source_end;
        if(!source_end)return 0;
        const auto end=scheduled->source_to_physical({symbol_schedule_detail::add(source_origin,source_end)},true);
        require(end.fraction==0 && end.sample>=physical_origin,"invalid scheduled output endpoint");
        return end.sample-physical_origin;
    }
    void finish_physical() {
        finish();
        physical_total=std::max(physical_position,scheduled_end(total));
    }
    std::uint64_t source_frontier(std::uint64_t frontier) const {
        frontier=std::max(frontier,physical_position);
        if(frontier>=physical_total)return total;
        if(!scheduled)return std::min(frontier,total);
        const auto source=scheduled->locate(symbol_schedule_detail::add(physical_origin,frontier)).source;
        require(source.sample>=source_origin,"stop forecast moved before its source origin");
        return std::min(source.sample-source_origin,total);
    }
    std::uint64_t predicted_cut(std::uint64_t at) const {
        if(padding)return symbol_schedule_detail::add(at,padding);
        if(packed_calendar && at>=training) {
            const auto relative=at-training;
            const auto next=std::min(bit_count,payload_lower_bound(static_cast<long double>(relative)+1));
            if(next) {
                const auto offset=payload_offset(next-1);
                if(relative>=offset && relative-offset<code.symbol_samples()) {
                    const auto within=relative-offset;
                    return symbol_schedule_detail::add(at,std::min(code.chip_samples()-within%code.chip_samples(),code.symbol_samples()-within));
                }
            }
            return at; // independent wait cover has no valid payload to drain
        }
        const auto within=(at>=training?at-training:at)%code.symbol_samples();
        return symbol_schedule_detail::add(at,std::min(code.chip_samples()-within%code.chip_samples(),code.symbol_samples()-within));
    }
    std::uint64_t packed_valid_support_before(std::uint64_t cut) const {
        if(!packed_calendar)return std::min(content_end,symbol_schedule_detail::add(cut,padding));
        const auto prefix=symbol_schedule_detail::add(training,padding);
        if(cut<=prefix)return 0;
        auto next=std::min(bit_count,payload_lower_bound(static_cast<long double>(cut)-prefix));
        if(!next)return 0;
        auto index=next-1;
        auto origin=symbol_schedule_detail::add(prefix,payload_offset(index));
        const auto first=.5L*std::min(code.chip_samples(),code.symbol_samples());
        if(static_cast<long double>(cut)<=origin+first) {
            if(!index)return 0;
            origin=symbol_schedule_detail::add(prefix,payload_offset(--index));
        }
        // Waiting cover, null frames and untouched future slots do not extend
        // the valid payload support budget. This is a conservative exclusive
        // pulse endpoint, never a reset of the immutable timing reference.
        const auto whole=symbol_schedule_detail::add(origin,symbol_schedule_detail::add(code.symbol_samples(),padding));
        return std::min({content_end,whole,symbol_schedule_detail::add(cut,padding)});
    }
    std::uint64_t valid_source_end() const {
        if(!total)return 0;
        const auto first=static_cast<long double>(training)+padding+payload_offset(0)+.5L*std::min(code.chip_samples(),code.symbol_samples());
        if(stop_at && static_cast<long double>(*stop_at)+padding<=first)return 0;
        if(packed_calendar && protected_cover)return packed_valid_support_before(cover_start);
        return protected_cover?std::min(content_end,cover_start+padding):std::min(content_end,total);
    }
    std::uint64_t emitted_epoch() const {
        // Leading pulse support cancels the initial payload padding exactly as
        // in the independent static exposure reference. Work on source time;
        // nulls and independent cover never expose a new payload identity.
        const auto emitted=std::min(position,valid_source_end());
        if(emitted<=training)return source_epoch;
        const auto exposed=payload_lower_bound(static_cast<long double>(emitted-training));
        if(!exposed)return source_epoch;
        const auto last=std::min(bit_count-1,exposed-1);
        if(packed_calendar)
            return packed_calendar->physical_slot(symbol_schedule_detail::add(calendar_rank,last))/config.sample_rate;
        if(scheduled) {
            const auto source=symbol_schedule_detail::add(source_origin,
                symbol_schedule_detail::add(training+padding,payload_offset(last)));
            const auto physical=scheduled->source_to_physical({source});
            require(physical.fraction==0,"exposed payload requires an integer physical address");
            return physical.sample/config.sample_rate;
        }
        if(calendar) {
            const auto elapsed=symbol_schedule_detail::add(config.stream_phase_samples,payload_offset(last));
            return symbol_schedule_detail::add(source_epoch,elapsed/config.sample_rate);
        }
        return symbol_stream_address(source_epoch,config.stream_phase_samples,last,code.symbol_samples(),config.sample_rate).epoch;
    }
    std::uint64_t predicted_valid_end(std::uint64_t frontier) const {
        const auto at=source_frontier(frontier);
        if(!at)return 0;
        if(stop_at || at>=total)return scheduled_end(valid_source_end());
        const auto first=static_cast<long double>(training)+padding+payload_offset(0)+.5L*std::min(code.chip_samples(),code.symbol_samples());
        if(static_cast<long double>(predicted_cut(at))<=first)return 0;
        if(protected_cover)return scheduled_end(packed_valid_support_before(predicted_cut(at)));
        const auto remaining=padding?2*padding+1:std::max<std::uint64_t>(1,config.sample_rate/100);
        return scheduled_end(std::min(content_end,symbol_schedule_detail::add(at,std::min(remaining,total-at))));
    }
    std::uint64_t predicted_stop_end(std::uint64_t frontier) const {
        const auto at=source_frontier(frontier);
        if(!at)return std::max(frontier,physical_position);
        if(stop_at || at>=total || (protected_cover && (at>=cover_start || padding>=cover_start-at)))return physical_total;
        if(protected_cover) {
            const auto cut=std::min(cover_start,predicted_cut(at));
            const auto ending=symbol_schedule_detail::add(symbol_schedule_detail::add(cut,2*padding),
                symbol_schedule_detail::add(useful_cover_span,padding?padding+1:std::max<std::uint64_t>(1,config.sample_rate/100)));
            return scheduled_end(ending);
        }
        const auto remaining=padding?2*padding+1:std::max<std::uint64_t>(1,config.sample_rate/100);
        return scheduled_end(symbol_schedule_detail::add(at,std::min(remaining,total-at)));
    }
    template<class Output, class Convert>
    std::size_t render(std::uint64_t& cursor, std::span<Output> output, Convert convert,
                       std::stop_token stop, const ChipObserver& observer,bool trace=false) {
        if(!scheduled) {
            const auto n=render_source(cursor,output,convert,stop,observer,trace);
            if(trace)position=cursor;
            return n;
        }
        cancelled(stop);
        const auto count=static_cast<std::size_t>(std::min<std::uint64_t>(output.size(),physical_total-cursor));
        std::size_t emitted=0;
        while(emitted<count) {
            cancelled(stop);
            const auto physical=symbol_schedule_detail::add(physical_origin,cursor);
            const auto cell=scheduled->locate(physical);
            if(!cell.active) {
                const auto next=scheduled->source_to_physical(cell.source);
                require(next.fraction==0 && next.sample>physical,"scheduled null interval failed to advance");
                const auto run=static_cast<std::size_t>(std::min<std::uint64_t>({count-emitted,next.sample-physical,4096}));
                std::fill_n(output.begin()+static_cast<std::ptrdiff_t>(emitted),run,Output{});
                emitted+=run;cursor+=run;
                continue;
            }
            require(cell.source.fraction==0 && cell.source.sample>=source_origin,
                    "scheduled source moved before its immutable origin");
            auto source=cell.source.sample-source_origin;
            require(source<total,"scheduled output exceeded its source extent");
            const auto run=static_cast<std::size_t>(std::min<std::uint64_t>({count-emitted,cell.physical_end-physical,total-source}));
            require(run!=0,"scheduled active interval failed to advance");
            // q-u is constant throughout this contiguous active run. Keep the
            // carrier on PHYSICAL sample time without a per-sample conversion.
            const auto rotation=std::polar(1.,static_cast<double>(tau*(
                detail::pattern_carrier_cycles(cursor,config.sample_rate,config.carrier_hz)-
                detail::pattern_carrier_cycles(source,config.sample_rate,config.carrier_hz))));
            std::uint64_t local=0;
            const auto written=render_source(source,output.subspan(emitted,run),[&](auto value) {
                const SchedHopCoordinate tick{physical+local};
                auto gain=SchedHopSourceMap::gain(cell,tick);
                if(hop_power.enabled)gain*=hop_power.gain(source_epoch,hopping->source_origin_samples+cursor+local,config.sample_rate);
                ++local;return convert(gain*rotation*value);
            },stop,observer,trace);
            require(written==run,"scheduled source stopped before its validated endpoint");
            if(trace)position=source;
            emitted+=written;cursor+=written;
        }
        return emitted;
    }
};

PatternTransmitter::PatternTransmitter(Bytes bits, Config config, std::uint64_t epoch, std::uint64_t start_chip, bool surrounding_noise,bool trace):
    impl_(std::make_unique<Impl>(std::move(bits), config, epoch, start_chip, surrounding_noise,trace)) {}
PatternTransmitter::PatternTransmitter(Config config,MaskedZeroBits bits,std::uint64_t epoch,bool trace):
    impl_(std::make_unique<Impl>(Bytes{},config,epoch,0,true,trace,bits.count)) {}
PatternTransmitter::~PatternTransmitter() = default;
PatternTransmitter::PatternTransmitter(PatternTransmitter&&) noexcept = default;
PatternTransmitter& PatternTransmitter::operator=(PatternTransmitter&&) noexcept = default;
std::size_t PatternTransmitter::read(std::span<float> output, std::stop_token stop, const ChipObserver& observer) {
    return impl_->render(impl_->physical_position, output, [](auto value) { return static_cast<float>(value.real()); }, stop, observer,true);
}
std::size_t PatternTransmitter::read_analytic(std::span<std::complex<double>> output, std::stop_token stop,
                                          const ChipObserver& observer) {
    return impl_->render(impl_->physical_position, output, [](auto value) { return value; }, stop, observer,true);
}
void PatternTransmitter::preview_last_analytic(std::span<std::complex<double>> output) const {
    require(output.size() <= analytic_preview_limit, "pattern preview exceeds its bounded sample limit");
    const auto count = static_cast<std::size_t>(std::min<std::uint64_t>(output.size(), impl_->physical_position));
    const auto leading = output.size() - count;
    std::fill_n(output.begin(), leading, std::complex<double>{});
    auto cursor = impl_->physical_position - count;
    impl_->render(cursor, output.subspan(leading), [](auto value) { return value; }, {}, {});
}
void PatternTransmitter::request_finish() { impl_->finish_physical(); }
std::uint64_t PatternTransmitter::original_total_samples() const { return impl_->original_total; }
std::uint64_t PatternTransmitter::original_source_total_samples() const { return impl_->original_source_total; }
bool PatternTransmitter::finished() const { return impl_->physical_position == impl_->physical_total; }
std::uint64_t PatternTransmitter::total_samples() const { return impl_->physical_total; }
std::uint64_t PatternTransmitter::samples_emitted() const { return impl_->physical_position; }
std::uint64_t PatternTransmitter::physical_output_origin() const {
    if(impl_->scheduled)return impl_->physical_origin;
    const auto anchor=symbol_schedule_detail::add(symbol_schedule_detail::multiply(impl_->source_epoch,impl_->config.sample_rate),impl_->config.stream_phase_samples);
    const auto prefix=symbol_schedule_detail::add(impl_->training,impl_->padding);
    require(anchor>=prefix,"transmit UTC origin precedes the representable sample clock");
    return anchor-prefix;
}
std::uint64_t PatternTransmitter::source_valid_payload_end() const { return impl_->valid_source_end(); }
std::uint64_t PatternTransmitter::emitted_payload_epoch() const { return impl_->emitted_epoch(); }
std::uint64_t PatternTransmitter::physical_valid_payload_end() const { return impl_->scheduled_end(source_valid_payload_end()); }
std::uint64_t PatternTransmitter::predicted_physical_valid_payload_end(std::uint64_t frontier) const { return impl_->predicted_valid_end(frontier); }
std::uint64_t PatternTransmitter::predicted_physical_stop_end(std::uint64_t frontier) const { return impl_->predicted_stop_end(frontier); }
std::uint64_t PatternTransmitter::source_total_samples() const { return impl_->total; }
std::uint64_t PatternTransmitter::source_samples_emitted() const { return impl_->position; }
double PatternTransmitter::bit_rate() const {
    if(impl_->packed_calendar)return static_cast<double>(impl_->packed_calendar->slots_per_group())/(.4*impl_->config.scheduled_hopping->time_multiplier);
    if(impl_->scheduled)return modem::bit_rate(impl_->config);
    if(impl_->calendar)return static_cast<double>(impl_->calendar->per_hop)/impl_->config.fake_hopping->dwell_seconds;
    return static_cast<double>(impl_->config.sample_rate) / static_cast<double>(impl_->code.symbol_samples());
}
std::size_t PatternTransmitter::working_bytes() const {
    return sizeof(PatternTransmitter) + sizeof(Impl) + impl_->code.working_bytes() + impl_->cover_bytes + impl_->bits.capacity()+
        (impl_->hopping?sizeof(FhssSchedule)+sizeof(FhssPlan)+impl_->hopping->plan->power_fallback_reason.capacity()+1+FhssSchedule::cycle_bytes+sizeof(std::array<unsigned,FhssPlan::capacity>):0)+
        (impl_->scheduled?impl_->scheduled->working_bytes():0)+(impl_->suppression_chips?sizeof(Impl::SuppressionChips):0)+
        ((impl_->generated_data?1U:0U)+(impl_->settling?1U:0U)+(impl_->settling_data?1U:0U)+(impl_->settling_pattern?1U:0U)+
         (impl_->settling_dsss?1U:0U)+(impl_->suppression?1U:0U)+(impl_->suppression_data?1U:0U)+
         (impl_->suppression_pattern?1U:0U)+(impl_->suppression_dsss?1U:0U))*sizeof(StreamCache);
}
void PatternTransmitter::copy_transmit_trace(TransmitTrace& result,std::uint64_t begun_chips) const {
    impl_->code.copy_transmit_trace(result,begun_chips);
    if(const auto& hopping=impl_->hopping) {
        result.fhss=true;result.fhss_available=impl_->physical_position>0;result.fhss_utc_calendar=hopping->plan->utc_schedule();
        result.fhss_epoch=hopping->plan->utc_schedule()?0:hopping->epoch;result.fhss_channels=hopping->plan->channels;
        result.fhss_dwell_seconds=hopping->plan->dwell_seconds;
        result.fhss_arrival_offset_seconds=hopping->plan->offset_seconds;
        result.fhss_arrival_available=false;
        if(result.fhss_available) {
            result.fhss_hop=hopping->ordinal(impl_->physical_position-1,impl_->config.sample_rate);
            result.fhss_channel=hopping->channel(result.fhss_hop);
            result.fhss_cycle=hopping->cycle;result.fhss_byte_offset=hopping->byte_offset;
            result.fhss_key.assign(hopping->prefix.begin(),hopping->prefix.begin()+hopping->prefix_size);
            result.fhss_shift_hz=hopping->plan->shifts[result.fhss_channel];
            if(const auto arrival=hopping->arrival_ordinal(impl_->physical_position-1,impl_->config.sample_rate)) {
                result.fhss_arrival_available=true;result.fhss_arrival_hop=*arrival;
                result.fhss_arrival_channel=hopping->channel(*arrival);
                result.fhss_arrival_shift_hz=hopping->plan->shifts[result.fhss_arrival_channel];
                (void)hopping->channel(result.fhss_hop); // retain the TX cache/trace identity
            }
        }
    }
}

struct NoiseTransmitter::Impl {
    PatternTransmitter pattern;
    Impl(const Config& config,std::uint64_t count):
        pattern(config,PatternTransmitter::MaskedZeroBits{count},config.stream_epoch,true) {}
};

Config NoiseTransmitter::prepare_config(Config config) {
    if(config.fake_hopping && (!config.fake_hopping->mandatory_ramps ||
       (config.fake_hopping->power_ramps && !config.fake_hopping->utc_schedule())))
        config.fake_hopping=resolve_fhss_power(*config.fake_hopping,config);
    // Prepare ONE temporary root before choosing its secret physical schedule.
    // It never enters saved keyfiles, receiver banks, or root-key diagnostics.
    config.data_key=Crypto::random();
    config.scramble=true;config.spreading_mode=SpreadingMode::pattern;
    const auto seed=[&](StreamPurpose purpose,auto& destination) {
        auto bytes=config.data_key->stream(purpose,0,0,destination.size());
        std::copy(bytes.begin(),bytes.end(),destination.begin());
        OPENSSL_cleanse(bytes.data(),bytes.size());
    };
    seed(StreamPurpose::Scrambler,config.spreading_seed);
    seed(StreamPurpose::Dsss,config.dsss_seed);
    return config;
}
NoiseTransmitter::NoiseTransmitter(Config config,std::uint64_t symbols):
    NoiseTransmitter([](Config value) {
        auto fresh=prepare_config(std::move(value));
        if(scheduled_bursts(fresh)) {
            const SchedHopSourceMap map(*fresh.scheduled_hopping,*fresh.data_key,fresh.sample_rate);
            const auto request=symbol_schedule_detail::add(symbol_schedule_detail::multiply(fresh.stream_epoch,fresh.sample_rate),fresh.stream_phase_samples);
            const auto output=map.source_to_physical(map.locate(request).source);
            const auto prefix=training_sample_count(fresh)+pattern_pulse_padding_samples(fresh);
            auto source=map.locate(output).source.add_samples(prefix);
            if(packed_scheduled_symbols(fresh)) {
                const ScheduledSymbolCalendar calendar(fresh);
                source={calendar.source_slot(calendar.first_rank_source(source.sample)),0};
            }
            const auto payload=map.source_to_physical(source);
            fresh.stream_epoch=payload.sample/fresh.sample_rate;fresh.stream_phase_samples=payload.sample%fresh.sample_rate;
        }
        return fresh;
    }(std::move(config)),PreparedTemporaryKeys{},symbols) {}
NoiseTransmitter::NoiseTransmitter(Config config,PreparedTemporaryKeys,std::uint64_t symbols) {
    require(config.data_key.has_value(),"prepared tuning source requires its temporary Data key");
    // Same scheduling and production checks as messages. Do not choose a new
    // root AFTER scheduling or the active/null calendar would silently change.
    struct CleanSeeds {
        Config& value;
        ~CleanSeeds() {
            OPENSSL_cleanse(value.spreading_seed.data(),value.spreading_seed.size());
            OPENSSL_cleanse(value.dsss_seed.data(),value.dsss_seed.size());
        }
    } clean{config};
    validate(config);
    require(private_cover_enabled(config),"tuning transmission cannot use diagnostic surrounding waveforms");
    const auto symbol=symbol_sample_count(config);
    const auto overhead=training_sample_count(config)+2*pattern_pulse_padding_samples(config)+suppression_sample_count(config);
    require(overhead<sample_target && symbol<=sample_target-overhead,
            "noise symbol duration exceeds the stream safety limit");
    auto maximum=(sample_target-overhead)/symbol;
    if(scheduled_bursts(config)) {
        const SchedHopSourceMap map(*config.scheduled_hopping,*config.data_key,config.sample_rate);
        const auto anchor=symbol_schedule_detail::add(symbol_schedule_detail::multiply(config.stream_epoch,config.sample_rate),config.stream_phase_samples);
        const auto payload=map.locate(anchor).source;
        const auto prefix=training_sample_count(config)+pattern_pulse_padding_samples(config);
        require(payload.sample>=prefix,"tuning prefix precedes the source clock");
        const auto source_origin=payload.sample-prefix;
        const auto physical_origin=map.source_to_physical({source_origin});
        const auto end=map.locate(physical_origin.add_samples(sample_target)).source;
        const auto available=end.sample-source_origin;
        require(available>overhead && symbol<=available-overhead,"noise symbol duration exceeds scheduled stream safety limit");
        if(packed_scheduled_symbols(config)) {
            const ScheduledSymbolCalendar calendar(config);
            maximum=calendar.lower_bound_source(config.stream_epoch,config.stream_phase_samples,available-overhead-symbol+1);
        } else maximum=(available-overhead)/symbol;
    } else if(deferred_symbols(config)) {
        const FhssSymbolCalendar calendar(*config.fake_hopping,config.sample_rate,pattern_pulse_padding_samples(config),symbol);
        maximum=calendar.lower_bound(config.stream_epoch,config.stream_phase_samples,sample_target-overhead-symbol+1);
    }
    require(symbols<=maximum,"finite noise duration exceeds the stream safety limit");
    const auto count=symbols?symbols:maximum;
    require(sizeof(NoiseTransmitter)+sizeof(Impl)<=config.memory_limit,
            "noise transmitter exceeds memory limit");
    config.memory_limit-=sizeof(NoiseTransmitter)+sizeof(Impl)-sizeof(PatternTransmitter);
    impl_=std::make_unique<Impl>(config,count);
}
NoiseTransmitter::~NoiseTransmitter()=default;
NoiseTransmitter::NoiseTransmitter(NoiseTransmitter&&) noexcept=default;
NoiseTransmitter& NoiseTransmitter::operator=(NoiseTransmitter&&) noexcept=default;
std::size_t NoiseTransmitter::read(std::span<float> output,std::stop_token stop,const ChipObserver& observer) {
    return impl_->pattern.read(output,stop,observer);
}
std::size_t NoiseTransmitter::read_analytic(std::span<std::complex<double>> output,std::stop_token stop,
                                         const ChipObserver& observer) {
    return impl_->pattern.read_analytic(output,stop,observer);
}
void NoiseTransmitter::preview_last_analytic(std::span<std::complex<double>> output) const {
    impl_->pattern.preview_last_analytic(output);
}
void NoiseTransmitter::request_finish() { impl_->pattern.request_finish(); }
std::uint64_t NoiseTransmitter::original_total_samples() const { return impl_->pattern.original_total_samples(); }
std::uint64_t NoiseTransmitter::original_source_total_samples() const { return impl_->pattern.original_source_total_samples(); }
bool NoiseTransmitter::finished() const { return impl_->pattern.finished(); }
std::uint64_t NoiseTransmitter::total_samples() const { return impl_->pattern.total_samples(); }
std::uint64_t NoiseTransmitter::samples_emitted() const { return impl_->pattern.samples_emitted(); }
std::uint64_t NoiseTransmitter::physical_output_origin() const { return impl_->pattern.physical_output_origin(); }
std::uint64_t NoiseTransmitter::physical_valid_payload_end() const { return impl_->pattern.physical_valid_payload_end(); }
std::uint64_t NoiseTransmitter::predicted_physical_valid_payload_end(std::uint64_t frontier) const { return impl_->pattern.predicted_physical_valid_payload_end(frontier); }
std::uint64_t NoiseTransmitter::predicted_physical_stop_end(std::uint64_t frontier) const { return impl_->pattern.predicted_physical_stop_end(frontier); }
std::uint64_t NoiseTransmitter::source_total_samples() const { return impl_->pattern.source_total_samples(); }
std::uint64_t NoiseTransmitter::source_samples_emitted() const { return impl_->pattern.source_samples_emitted(); }
std::uint64_t NoiseTransmitter::source_valid_payload_end() const { return impl_->pattern.source_valid_payload_end(); }
std::uint64_t NoiseTransmitter::emitted_payload_epoch() const { return impl_->pattern.emitted_payload_epoch(); }
std::size_t NoiseTransmitter::working_bytes() const {
    return sizeof(NoiseTransmitter)+sizeof(Impl)-sizeof(PatternTransmitter)+impl_->pattern.working_bytes();
}
void NoiseTransmitter::copy_transmit_trace(TransmitTrace& trace,std::uint64_t begun_chips) const {
    impl_->pattern.copy_transmit_trace(trace,begun_chips);
}

} // namespace datapump::modem
