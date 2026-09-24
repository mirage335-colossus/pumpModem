#include "binary_editor.hpp"
#include <iostream>
#include <stdexcept>

using namespace datapump;
using namespace datapump::gui;

namespace {
void check(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
Bytes bytes(std::string_view text) { return {text.begin(), text.end()}; }
template<class Operation>
void rejects_unchanged(BinaryEditor& editor, Operation operation, const char* message) {
    const auto original_bytes = editor.bytes();
    const auto original_text = editor.text();
    const auto original_binary = editor.binary();
    const auto original_escaped = editor.escaped();
    const auto original_raw = editor.raw_bits();
    bool rejected = false;
    try { operation(); } catch (const Error&) { rejected = true; }
    check(rejected, message);
    check(editor.bytes() == original_bytes && editor.text() == original_text &&
          editor.binary() == original_binary && editor.escaped() == original_escaped && editor.raw_bits() == original_raw,
          "Rejected edit changed the committed message");
}

void synchronized_prefix() {
    BinaryEditor editor;
    check(editor.bytes().empty() && editor.text().empty() && editor.binary().empty() && !editor.escaped(),
          "Empty editor did not start synchronized");
    editor.edit_text("ABC");
    check(editor.binary() == "01000001 01000010\n01000011", "Binary rendering lost bit order or byte grouping");
    editor.edit_binary(" 00000000\t00000001\r\n01111111\f10000000\v11111111 ");
    check(editor.bytes() == Bytes({0, 1, 127, 128, 255}), "Binary edit lost leading zeros or full byte values");
    check(editor.escaped() && editor.text() == "\\x00\\x01\\x7F\\x80\\xFF", "Non-text bytes were not shown losslessly");
    editor.edit_binary("01001000 01101001");
    check(editor.text() == "Hi" && !editor.escaped(), "Printable bytes did not restore ordinary text editing");
    editor.edit_text(R"(C:\notes\x41)");
    check(editor.bytes() == bytes(R"(C:\notes\x41)") && !editor.escaped(), "Ordinary text decoded literal path escapes");
    editor.edit_binary(" \n\t");
    check(editor.bytes().empty() && editor.text().empty() && editor.binary().empty(), "Empty binary input did not clear a short message");
}

void preserved_suffix() {
    BinaryEditor editor(bytes("0123456789abcdefSUFFIX"));
    check(editor.binary().size() == 143, "Binary field did not stop at 16 bytes");
    editor.edit_binary("01011000");
    check(editor.text() == "XSUFFIX" && !editor.raw_bits(), "Shorter binary replacement damaged bytes beyond the old prefix");
    editor.edit_text("0123456789abcdefSUFFIX");
    editor.edit_binary("");
    check(editor.text() == "SUFFIX", "Clearing binary prefix removed the message suffix");

    // The 16-byte boundary deliberately falls inside the UTF-8 euro sign.
    const std::string original = std::string(15, 'a') + "\xe2\x82\xac" + "tail";
    editor.edit_text(original);
    const auto unchanged = editor.binary();
    editor.edit_binary(unchanged);
    check(editor.text() == original && !editor.escaped(), "An unchanged byte prefix split a UTF-8 character");
    editor.edit_binary(std::string(128, '0'));
    check(editor.bytes().size() == original.size() &&
          std::equal(editor.bytes().begin() + 16, editor.bytes().end(), bytes(original).begin() + 16),
          "Binary edit re-encoded a multibyte suffix at the 16-byte boundary");
    check(editor.escaped() && editor.text().ends_with("\\x82\\xACtail"), "Split UTF-8 suffix was not escaped losslessly");
    const auto snapshot = editor.bytes();
    editor.edit_text(editor.text());
    check(editor.bytes() == snapshot, "Escaped UTF-8 suffix failed to round-trip");
}

void escaped_roundtrip() {
    Bytes all;
    for (unsigned value = 0; value != 256; ++value) all.push_back(static_cast<std::uint8_t>(value));
    BinaryEditor editor(all);
    check(editor.escaped(), "Arbitrary byte data did not select escaped mode");
    check(valid_clipboard_text(bytes(editor.text())), "Escaped display cannot cross a native editor boundary");
    editor.edit_text(editor.text());
    check(editor.bytes() == all, "Some byte values were lost in an escaped text round-trip");

    editor = BinaryEditor(Bytes{0, '\\', 'x', '4', '1', '\n', '\t', 0xc3, 0xa9});
    check(editor.text() == "\\x00\\\\x41\n\t\xc3\xa9", "Escaping changed literal backslashes or displayable UTF-8");
    editor.edit_text("\\x41\\x42");
    check(editor.text() == "AB" && !editor.escaped(), "Escaped printable replacements did not return to normal mode");
    editor = BinaryEditor(Bytes{0});
    editor.edit_text("\\xFf\\x0a\\\\");
    check(editor.bytes() == Bytes({255, 10, '\\'}), "Escaped mode did not decode mixed-case hex and escaped backslashes");
}

void invalid_drafts() {
    BinaryEditor editor(bytes("message with a preserved suffix"));
    for (const auto invalid : {"2", "0000000x", "1", "00000000 0", "0b00000000"})
        rejects_unchanged(editor, [&] { editor.edit_binary(invalid); }, "Invalid or partial binary byte was accepted");
    rejects_unchanged(editor, [&] { editor.edit_binary(std::string(129, '1')); }, "More than 128 meaningful bits were accepted");
    rejects_unchanged(editor, [&] { editor.edit_text(std::string("x\0y", 3)); }, "Literal embedded zero reached the text display");
    rejects_unchanged(editor, [&] { editor.edit_text("\xc0\xaf"); }, "Invalid literal UTF-8 reached the text display");
    editor = BinaryEditor(Bytes{0});
    for (const auto invalid : {"\\", "\\n", "\\x", "\\x0", "\\xGG", "\\X00"})
        rejects_unchanged(editor, [&] { editor.edit_text(invalid); }, "Invalid escaped text was accepted");
}

void payload_limit() {
    BinaryEditor editor(Bytes(BinaryEditor::payload_limit, 0));
    check(editor.text().size() == 4 * BinaryEditor::payload_limit, "Escaped display was limited by its rendered length");
    editor.edit_text(editor.text());
    check(editor.bytes().size() == BinaryEditor::payload_limit, "Maximum-size escaped data could not round-trip");
    rejects_unchanged(editor, [&] { editor.edit_text(editor.text() + "\\x00"); }, "Escaped text exceeded the decoded byte limit");
    editor = BinaryEditor(bytes("valid"));
    rejects_unchanged(editor, [&] { editor.edit_text(std::string(BinaryEditor::payload_limit + 1, 'a')); }, "Ordinary text exceeded the byte limit");
}
void exact_raw_drafts() {
    BinaryEditor editor(bytes("A"));
    editor.edit_binary("0 0\n1");
    check(editor.raw_bits() == std::optional<Bytes>(Bytes{0,0,1}) && editor.binary() == "001",
          "Three-bit draft lost leading zeros or gained padding");
    check(editor.bytes() == bytes("A") && editor.text() == "A",
          "Partial byte replaced the separate text interpretation with padded data");
    rejects_unchanged(editor, [&] { editor.edit_binary("0012"); }, "Invalid raw bit accepted");
    editor.edit_binary("00000001");
    check(editor.raw_bits() == std::optional<Bytes>(Bytes{0,0,0,0,0,0,0,1}) &&
          editor.bytes() == Bytes{1} && editor.text() == "\\x01",
          "Byte-aligned raw draft changed mode or failed to update the byte view");
    editor.edit_text(editor.text());
    check(!editor.raw_bits() && editor.bytes() == Bytes{1}, "Text edit did not select byte/text transmission");
    for(std::size_t length=1;length<=128;++length) {
        editor.edit_binary(std::string(length,'0'));
        check(editor.raw_bits() && *editor.raw_bits() == Bytes(length,0), "Exact raw draft length was rounded");
        const auto binary=editor.binary();
        check(static_cast<std::size_t>(std::count(binary.begin(),binary.end(),'0')) == length,
              "Raw binary display lost meaningful zero bits");
    }
    rejects_unchanged(editor, [&] { editor.edit_binary(std::string(129,'0')); }, "Raw draft exceeded its bit limit");
    editor.edit_binary("");
    check(!editor.raw_bits() && editor.bytes().empty() && editor.binary().empty(), "Clearing raw input retained its bits or text");
}
}

void bounded_dictionary_selection() {
    BinaryEditor editor(Bytes(16,'A'));const auto preview=editor.text();
    Bytes bits(208,1);bits.front()=0;
    editor.select_raw_bits(bits,208);
    check(editor.raw_bits()==bits && editor.text()==preview && editor.bytes()==Bytes(16,'A'),
          "Extended raw selection changed its independent dictionary preview or padded bits");
    rejects_unchanged(editor,[&]{editor.select_raw_bits(Bytes(209,0),208);},"Raw selection exceeded its declared bound");
    rejects_unchanged(editor,[&]{editor.select_raw_bits(Bytes{0,2,1},208);},"Nonbinary raw selection was accepted");
    rejects_unchanged(editor,[&]{editor.select_raw_bits({},208);},"Empty raw selection was accepted");
    rejects_unchanged(editor,[&]{editor.edit_binary(std::string(129,'0'));},"Console prefix editing exceeded its own 128-bit bound");
}

void received_binary_edits() {
    constexpr std::string_view allowed="abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789,.@ -_/=\n";
    for(bool shellcode:{false,true})for(unsigned first=0;first<256;first+=16) {
        std::string bits,expected;
        Bytes raw;
        for(unsigned value=first;value<first+16;++value) {
            const bool permitted=shellcode?value=='\n'||(value>=32&&value<=126):
                allowed.find(static_cast<char>(value))!=allowed.npos;
            expected+=permitted?static_cast<char>(value):'_';
            for(unsigned n=8;n;--n) {bits+=(value>>(n-1))&1?'1':'0';raw.push_back((value>>(n-1))&1);}
        }
        BinaryEditor editor;editor.edit_binary(bits,shellcode);
        check(editor.text()==expected&&editor.bytes()==bytes(expected)&&!editor.escaped()&&editor.raw_bits()==raw,
              "Received binary editing must preserve exact bits with only permitted ASCII and LF in the text view");
    }
}
int main() {
    try {
        received_binary_edits();
        synchronized_prefix();
        preserved_suffix();
        escaped_roundtrip();
        invalid_drafts();
        payload_limit();
        exact_raw_drafts();bounded_dictionary_selection();
        std::cout << "Binary editor synchronization and lossless byte editing passed\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
