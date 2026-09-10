#include "datapump/qr.hpp"

#include <iostream>
#include <sstream>

using namespace datapump;

namespace {
void check(bool condition, const char* message) {
    if (!condition) throw Error(message);
}
template <typename F> void rejects(F&& operation, const char* message) {
    try { operation(); } catch (const Error&) { return; }
    throw Error(message);
}
std::string matrix(const QrCode& code) {
    std::string result;
    for (int y = 0; y < code.size(); ++y) {
        for (int x = 0; x < code.size(); ++x) result += code.dark(x, y) ? '1' : '0';
        result += '\n';
    }
    return result;
}

void test_limits() {
    check(encode_qr("").size() == 21, "empty QR failed");
    check(encode_qr(std::string(500, 'x')).size() <= 77, "500 ASCII characters do not fit");
    std::string unicode;
    for (int i = 0; i < 500; ++i) unicode += "\xF0\x9F\x93\xBB";
    check(encode_qr(unicode).size() <= 177, "500 Unicode characters do not fit");
    rejects([&] { encode_qr(std::string(501, 'x')); }, "501 ASCII characters accepted");
    rejects([&] { encode_qr(unicode + "x"); }, "501 Unicode characters accepted");
    for (auto invalid : {"\x80", "\xC0\x80", "\xC1\xBF", "\xE0\x80\x80", "\xED\xA0\x80",
                        "\xF0\x80\x80\x80", "\xF4\x90\x80\x80", "\xF5\x80\x80\x80", "\xC2", "\xE2\x28\xA1"}) {
        rejects([&] { encode_qr(invalid); }, "malformed UTF-8 accepted");
    }
    check(matrix(encode_qr(std::string("a\0b", 3))) != matrix(encode_qr("a")), "embedded zero truncated");
    rejects([] { qr_svg("x", 0); }, "zero SVG scale accepted");
    rejects([] { qr_pbm("x", 65); }, "excessive PBM scale accepted");
}

void test_layout() {
    const auto qr = encode_qr("Data Pump");
    check(qr.size() == 21, "short QR should choose version 1");
    // Frozen reference from Nayuki's separate Python v1.8.0 implementation,
    // ECI 26 + byte segment, Level L, automatic version/mask, no ECC boost.
    const std::string expected =
        "111111101011101111111\n"
        "100000100011001000001\n"
        "101110101101001011101\n"
        "101110101100101011101\n"
        "101110101001001011101\n"
        "100000100111101000001\n"
        "111111101010101111111\n"
        "000000000001100000000\n"
        "111100101111110011101\n"
        "101111001111110101110\n"
        "011000110101010001000\n"
        "001111001001000111010\n"
        "111110111100110000100\n"
        "000000001111011000001\n"
        "111111100101111010100\n"
        "100000100110011110101\n"
        "101110100100111000000\n"
        "101110101010000111000\n"
        "101110101110111101000\n"
        "100000101101001010010\n"
        "111111101010010110000\n";
    check(matrix(qr) == expected, "QR reference matrix mismatch");
    check(qr.dark(0, 0) && qr.dark(6, 0) && qr.dark(0, 6) && qr.dark(3, 3), "finder pattern absent");
    check(!qr.dark(1, 1) && !qr.dark(7, 0), "finder white ring/separator absent");
    check(!qr.dark(-1, 0) && !qr.dark(0, -1) && !qr.dark(qr.size(), 0), "out-of-bounds module query failed");
    const auto svg = qr.svg(3);
    check(svg.find("viewBox=\"0 0 29 29\"") != std::string::npos, "SVG quiet zone absent");
    check(svg.find("width=\"87\"") != std::string::npos, "SVG dimensions wrong");
    check(svg.find("shape-rendering=\"crispEdges\"") != std::string::npos, "SVG lacks crisp edges");
    check(qr_svg("<script>alert('x')</script>").find("<script>") == std::string::npos, "QR payload injected SVG markup");
    std::istringstream pbm(qr.pbm(1));
    std::string magic;
    int width, height;
    pbm >> magic >> width >> height;
    check(magic == "P1" && width == 29 && height == 29, "PBM header incorrect");
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            int value = -1;
            pbm >> value;
            check(static_cast<bool>(pbm), "PBM data truncated");
            check(value == (qr.dark(x - 4, y - 4) ? 1 : 0), "PBM pixels disagree with module matrix");
        }
    }
    std::string extra;
    check(!(pbm >> extra), "PBM has trailing pixels");
}
} // namespace

int main() {
    try {
        test_limits();
        test_layout();
        std::cout << "QR tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "QR test failure: " << error.what() << '\n';
        return 1;
    }
}
