#include "gtest/gtest.h"
#include "DataImporter.h"
#include "Currency.h"
#include <fstream>
#include <cstdio>
#include <string>

// SAFETY NOTE: DataImporter's only public entry point, ImportFromFile(filename, ...), takes a
// real file path (not a stream) - there is no way to feed it in-memory content. As with
// ApplyRecoveryFile in AccountManagerTests.cpp, this writes one small temp file with a
// distinctive name (NOT under db\/log\) and removes it via an RAII wrapper, rather than
// exercising any of this app's own persistent file paths.
//
// The MBH format's own bank-name sentinel is matched in DataImporter.cpp via a literal
// non-ASCII string, `L"Sz\xE1mlat\xF6rt\xE9net"` (the Hungarian word for "account history",
// stored in that .cpp file as raw ISO-8859/Windows-125x bytes, not UTF-8, and not escaped) -
// confirmed via a hex dump of the source file. Per this session's established encoding-safety
// rule (never type literal non-ASCII into source, always \uXXXX in a wide literal), the sample
// CSV content written by this test spells the same word via backslash-u escapes and
// converts to genuine UTF-8 before writing to disk - matching what FileLineStreamReader::ReadLine
// actually decodes each line as (String::FromUTF8()). If this test starts failing to detect the
// MBH format on a build machine whose default ANSI code page doesn't happen to decode
// DataImporter.cpp's literal bytes back into the same characters, that would point at a real,
// environment-dependent fragility in the production literal - worth a DISABLED_ test at that
// point, not a fix here.

namespace {

class TempImportFile {
    String m_path;
public:
    TempImportFile(const String& path, const std::string& utf8_content) : m_path(path) {
        std::ofstream out(std::string(path.utf8_str()), std::ios::binary);
        out << utf8_content;
    }
    ~TempImportFile() { std::remove(std::string(m_path.utf8_str()).c_str()); }
    const String& Path() const { return m_path; }
};

String JoinSemicolon(const std::vector<String>& fields) {
    String result;
    for (size_t i = 0; i < fields.size(); ++i) {
        if (i) {
            result.append(";");
        }
        result.append(fields[i]);
    }
    return result;
}

// The MBH sentinel word itself, escaped per the note above (Szamlatortenet).
const String kMbhHeader = L"Sz\u00E1mlat\u00F6rt\u00E9net";

// Mirrors two entries of ImportColumnsMBH (src/DataImporter.cpp) - that enum has internal linkage
// so tests can't reference it directly, but the row layout is part of the file format this test
// exercises, and BuildValidMbhRow() above documents the full column-by-column mapping.
constexpr size_t kMbhColumnKozlemeny = 10;       // Kozlemeny (memo)
constexpr size_t kMbhColumnTranzakciohelye = 29; // Tranzakciohelye (client name)

// One valid MBH_Column_* - shaped transaction row, matching ImportColumnsMBH's 31 columns
// (src/DataImporter.cpp) exactly - returned as a vector (rather than a hand-typed string) so
// individual tests can override one field without the column count silently drifting from
// MBH_Column_SIZE.
std::vector<String> BuildValidMbhRow() {
    std::vector<String> row = {
        "1234567890123456", // 0 Szamla (account number)
        "Vasarlas",         // 1 Megbizastipusa (type)
        "-5000",            // 2 Osszeg (amount)
        "HUF",              // 3 Devizanem (currency)
        "-5000",            // 4 Eredetiosszeg (unused)
        "",                 // 5 Ellenoldaliszamlatulajdonosa
        "",                 // 6 Ellenoldaliszamlaszama (empty -> client comes from Tranzakciohelye)
        "",                 // 7 Ellenoldalimasodlagosazonosito
        "",                 // 8 Ellenoldalimasodlagosazonositotipusa
        "",                 // 9 Megbizobankja
        "Test purchase memo", // 10 Kozlemeny (memo)
        "2024.03.15",       // 11 Tranzakciodatuma (transaction date - takes priority)
        "",                 // 12 Kartya
        "",                 // 13 Koltsegviselesmodja
        "",                 // 14 Bankkoltseg1
        "",                 // 15 Bankkoltseg2
        "",                 // 16 Bankkoltseg3
        "",                 // 17 Bankkoltseg4
        "",                 // 18 Bankkoltseg5
        "",                 // 19 Ugyfelazonosito
        "",                 // 20 KedvezmenyezettTranzakcioazonositoja
        "",                 // 21 SzamlaAzonosito
        "",                 // 22 TorzsvasarloAzonosito
        "",                 // 23 KereskedoiEszkozAzonosito
        "",                 // 24 NavEllenorzoKod
        "",                 // 25 BoltAzonosito
        "",                 // 26 Kiegeszitoinformacio
        "",                 // 27 Megbizasazonositoja
        "",                 // 28 SWIFTreferenciaszam
        "Test Shop",        // 29 Tranzakciohelye (client name, since column 6 is empty)
        "2024.03.14",       // 30 Konyvelesidatum (booking date - only used if 11 is empty)
    };
    EXPECT_EQ(row.size(), 31u); // == MBH_Column_SIZE - ASSERT_EQ can't be used here: it expands
                                 // to a `return;`, which doesn't typecheck in a function
                                 // returning a vector.
    return row;
}

// Assembles a title line containing the MBH sentinel word, three arbitrary non-empty filler
// lines (rows 1-3, never inspected by ImportFromCSV beyond needing to exist and be non-empty -
// CSVParser stops at the first empty line), and the given transaction row, joined with
// line_ending throughout (so callers can exercise CRLF line endings too). No line_ending is
// appended after the final row - callers that want a trailing newline at end-of-file pass it as
// part of assembling their own content, or rely on line_ending being appended between lines only.
String BuildMbhCsvContent(const std::vector<String>& row, const char* line_ending = "\n", bool trailing_line_ending = true) {
    String content;
    content.append(kMbhHeader).append(line_ending);
    content.append("filler line 1").append(line_ending);
    content.append("filler line 2").append(line_ending);
    content.append("filler line 3").append(line_ending);
    content.append(JoinSemicolon(row));
    if (trailing_line_ending) {
        content.append(line_ending);
    }
    return content;
}

String BuildValidMbhCsvContent() {
    return BuildMbhCsvContent(BuildValidMbhRow());
}

TEST(DataImporterTest, ImportFromFileDetectsAndParsesAnMbhCsvTransaction) {
    TempImportFile file("test_dataimporter_mbh.csv", std::string(BuildValidMbhCsvContent().utf8_str()));

    RawImportData data;
    ImportFromFile(file.Path(), data);

    EXPECT_EQ(data.bank_name, "MBH Bank");
    EXPECT_EQ(data.account_number, "1234567890123456");
    EXPECT_EQ(data.currency, HUF);
    ASSERT_EQ(data.data.size(), 1u);

    const RawTransactionData& tr = data.data[0];
    EXPECT_EQ(tr.date, DMYToExcelSerialDate(15, 3, 2024)); // Tranzakciodatuma, not Konyvelesidatum
    EXPECT_EQ(tr.type, "Vasarlas");
    EXPECT_EQ(tr.amount.GetValue(), -5000);
    EXPECT_EQ(tr.client, "Test Shop"); // from Tranzakciohelye, since Ellenoldaliszamlaszama was empty
    EXPECT_EQ(tr.client_account_number, "");
    EXPECT_EQ(tr.memo, "Test purchase memo");
}

TEST(DataImporterTest, ImportFromFileIgnoresAnUnrecognizedExtension) {
    TempImportFile file("test_dataimporter_unknown.txt", "irrelevant content");

    RawImportData data;
    ImportFromFile(file.Path(), data); // neither .xml nor .csv - the "Not supported" branch

    EXPECT_TRUE(data.bank_name.empty());
    EXPECT_TRUE(data.data.empty());
}

TEST(DataImporterTest, ImportFromFileLeavesDataEmptyForACsvMissingTheMbhSentinel) {
    // A .csv file that doesn't start with the MBH sentinel line - ImportFromCSV() returns early
    // without setting bank_name/account_number or extracting any transactions.
    String content = "Some Other Bank Export\nfiller\nfiller\nfiller\na;b;c\n";
    TempImportFile file("test_dataimporter_notmbh.csv", std::string(String(content).utf8_str()));

    RawImportData data;
    ImportFromFile(file.Path(), data);

    EXPECT_TRUE(data.bank_name.empty());
    EXPECT_TRUE(data.data.empty());
}

TEST(DataImporterTest, ImportFromFileSkipsALeadingUtf8Bom) {
    // FileLineStreamReader peeks the first 3 bytes and, only if they match the UTF-8 BOM
    // (EF BB BF), consumes them before reading the first real line; otherwise it seeks back to
    // 0 so no content is lost. A BOM-prefixed export must parse identically to a BOM-less one.
    std::string utf8_content = std::string(BuildValidMbhCsvContent().utf8_str());
    std::string with_bom = "\xEF\xBB\xBF" + utf8_content;
    TempImportFile file("test_dataimporter_mbh_bom.csv", with_bom);

    RawImportData data;
    ImportFromFile(file.Path(), data);

    EXPECT_EQ(data.bank_name, "MBH Bank");
    EXPECT_EQ(data.account_number, "1234567890123456");
    ASSERT_EQ(data.data.size(), 1u);
    EXPECT_EQ(data.data[0].memo, "Test purchase memo");
}

TEST(DataImporterTest, ImportFromFileHandlesCrlfLineEndings) {
    // Real bank exports are typically CRLF-terminated. FileLineStreamReader reads through a
    // non-binary std::ifstream, which text-mode-translates "\r\n" to "\n" on Windows, so no
    // stray '\r' should leak into any parsed field (in particular the last column of the last
    // row, Konyvelesidatum, which would otherwise fail date parsing).
    String content = BuildMbhCsvContent(BuildValidMbhRow(), "\r\n");
    TempImportFile file("test_dataimporter_mbh_crlf.csv", std::string(content.utf8_str()));

    RawImportData data;
    ImportFromFile(file.Path(), data);

    EXPECT_EQ(data.bank_name, "MBH Bank");
    ASSERT_EQ(data.data.size(), 1u);
    const RawTransactionData& tr = data.data[0];
    EXPECT_EQ(tr.date, DMYToExcelSerialDate(15, 3, 2024));
    EXPECT_EQ(tr.memo, "Test purchase memo");
}

TEST(DataImporterTest, ImportFromFileRoundtripsNonAsciiCharactersInADataField) {
    // Every line is decoded via String::FromUTF8() (FileLineStreamReader::ReadLine), not just the
    // MBH sentinel line - a client/memo field with accented characters must survive the same way.
    std::vector<String> row = BuildValidMbhRow();
    row[kMbhColumnTranzakciohelye] = L"V\u00E1s\u00E1rl\u00E1s Kft."; // Vasarlas Kft., escaped to avoid a literal non-ASCII byte in source
    String content = BuildMbhCsvContent(row);
    TempImportFile file("test_dataimporter_mbh_nonascii.csv", std::string(content.utf8_str()));

    RawImportData data;
    ImportFromFile(file.Path(), data);

    ASSERT_EQ(data.data.size(), 1u);
    EXPECT_EQ(data.data[0].client, L"V\u00E1s\u00E1rl\u00E1s Kft.");
}

TEST(DataImporterTest, ImportFromFileStripsTheWrappingQuotesOfAQuotedField) {
    // A field's wrapping quotes are CSV-generator syntax marking "this field's content may
    // contain special characters", not part of the value - ParseMultiValueString strips them.
    // CSVParser separately uses a running quote count to decide whether a field continues onto
    // another physical line, and ParseMultiValueString uses matched quote pairs to protect an
    // embedded ';' from being treated as a column boundary.
    std::vector<String> row = BuildValidMbhRow();
    row[kMbhColumnKozlemeny] = "\"Quoted memo\"";
    String content = BuildMbhCsvContent(row);
    TempImportFile file("test_dataimporter_mbh_quoted.csv", std::string(content.utf8_str()));

    RawImportData data;
    ImportFromFile(file.Path(), data);

    ASSERT_EQ(data.data.size(), 1u);
    EXPECT_EQ(data.data[0].memo, "Quoted memo");
}

TEST(DataImporterTest, ImportFromFileMergesAQuotedFieldThatSpansMultiplePhysicalLines) {
    // A field opening with a quote but not closing it on the same physical line leaves an odd
    // running quote count, so CSVParser keeps appending subsequent physical lines (joined with a
    // single literal space, not the original line break) until the count goes even again. This
    // is how a bank export with an embedded newline inside a quoted memo gets reassembled into
    // one logical CSV row.
    std::vector<String> row = BuildValidMbhRow();
    row[kMbhColumnKozlemeny] = "\"Test purchase\nmemo continued\""; // one embedded newline, quotes balance across the two physical lines
    String content = BuildMbhCsvContent(row);
    TempImportFile file("test_dataimporter_mbh_multiline.csv", std::string(content.utf8_str()));

    RawImportData data;
    ImportFromFile(file.Path(), data);

    EXPECT_EQ(data.bank_name, "MBH Bank"); // detection still works despite the row spanning 2 physical lines
    ASSERT_EQ(data.data.size(), 1u);
    const RawTransactionData& tr = data.data[0];
    EXPECT_EQ(tr.memo, "Test purchase memo continued"); // line break replaced by a single space, wrapping quotes stripped
    EXPECT_EQ(tr.date, DMYToExcelSerialDate(15, 3, 2024)); // columns after the merged field still align correctly
}

TEST(DataImporterTest, ImportFromFileDoesNotHangOnAQuoteLeftUnterminatedThroughEndOfFile) {
    // If the running quote count is still odd when the stream hits real EOF, CSVParser's
    // "!eof && odd count" loop condition must stop the continuation attempts anyway (there is no
    // further line to append) rather than looping forever. This file's last row's memo field
    // opens a quote and never closes it, and the file ends immediately after that row with no
    // trailing line ending.
    std::vector<String> row = BuildValidMbhRow();
    row[kMbhColumnKozlemeny] = "\"Unterminated across\nlines"; // opens with a quote, never closes it
    String content = BuildMbhCsvContent(row, "\n", /*trailing_line_ending=*/false);
    TempImportFile file("test_dataimporter_mbh_unterminated.csv", std::string(content.utf8_str()));

    RawImportData data;
    ImportFromFile(file.Path(), data); // must return promptly, not hang

    // ParseMultiValueString now treats every ';' after the unclosed quote as protected content
    // rather than a column boundary, so the row collapses to far fewer than MBH_Column_SIZE
    // fields (columns 10 through 23 fold into one trailing field). ImportFromCSV's own
    // "data[4].size() < MBH_Column_SIZE" guard catches that and rejects the whole file, rather
    // than extracting a transaction from misaligned columns.
    EXPECT_TRUE(data.bank_name.empty());
    EXPECT_TRUE(data.data.empty());
}

TEST(DataImporterTest, ImportFromFileKeepsAnEmbeddedSemicolonInsideAQuotedFieldAsOneColumn) {
    // The fix that made CommonTypes.cpp's ParseMultiValueString quote-aware: a ';' between a
    // matched pair of '"' must stay part of the same field instead of splitting it in two, so
    // the columns after it in the row stay correctly aligned.
    std::vector<String> row = BuildValidMbhRow();
    row[kMbhColumnKozlemeny] = "\"Contains; a semicolon\"";
    String content = BuildMbhCsvContent(row);
    TempImportFile file("test_dataimporter_mbh_embedded_semicolon.csv", std::string(content.utf8_str()));

    RawImportData data;
    ImportFromFile(file.Path(), data);

    EXPECT_EQ(data.bank_name, "MBH Bank");
    ASSERT_EQ(data.data.size(), 1u);
    const RawTransactionData& tr = data.data[0];
    EXPECT_EQ(tr.memo, "Contains; a semicolon"); // embedded ';' kept, wrapping quotes stripped
    EXPECT_EQ(tr.date, DMYToExcelSerialDate(15, 3, 2024)); // later columns still align correctly
}

TEST(DataImporterTest, ImportFromFileUnescapesADoubledQuoteInsideAQuotedFieldToOneLiteralQuote) {
    // Standard CSV escaping: a bank export that itself contains a '"' inside a quoted field (e.g.
    // a business name like Kft. "Teszt") writes it as a doubled '""'. ParseMultiValueString must
    // collapse each such pair to a single literal '"' in the parsed value, and still strip the
    // field's own (non-doubled) wrapping quotes as usual.
    std::vector<String> row = BuildValidMbhRow();
    row[kMbhColumnKozlemeny] = "\"Contains \"\"quoted\"\" text\"";
    String content = BuildMbhCsvContent(row);
    TempImportFile file("test_dataimporter_mbh_doubled_quote.csv", std::string(content.utf8_str()));

    RawImportData data;
    ImportFromFile(file.Path(), data);

    EXPECT_EQ(data.bank_name, "MBH Bank");
    ASSERT_EQ(data.data.size(), 1u);
    const RawTransactionData& tr = data.data[0];
    EXPECT_EQ(tr.memo, "Contains \"quoted\" text"); // doubled quotes collapsed, outer wrapping quotes stripped
    EXPECT_EQ(tr.date, DMYToExcelSerialDate(15, 3, 2024)); // later columns still align correctly
}

}
