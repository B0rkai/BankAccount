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

// Builds one valid MBH Bank CSV export: a title line containing the MBH sentinel word (see
// BuildValidMbhCsvContent()'s header variable below for the escaped spelling), three arbitrary
// non-empty filler lines (rows 1-3, never inspected by ImportFromCSV
// beyond needing to exist and be non-empty - CSVParser stops at the first empty line), and one
// real MBH_Column_* - shaped transaction row at index 4, matching ImportColumnsMBH's 24 columns
// (src/DataImporter.cpp) exactly - built via JoinSemicolon rather than a hand-typed string so the
// column count can't silently drift from MBH_Column_SIZE.
String BuildValidMbhCsvContent() {
    String header = L"Sz\u00E1mlat\u00F6rt\u00E9net"; // Szamlatortenet, escaped to avoid a literal non-ASCII byte in source
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
        "",                 // 19 Kiegeszitoinformacio
        "",                 // 20 Megbizasazonositoja
        "",                 // 21 SWIFTreferenciaszam
        "Test Shop",        // 22 Tranzakciohelye (client name, since column 6 is empty)
        "2024.03.14",       // 23 Konyvelesidatum (booking date - only used if 11 is empty)
    };
    EXPECT_EQ(row.size(), 24u); // == MBH_Column_SIZE - see the comment above (ASSERT_EQ can't be
                                 // used here: it expands to a `return;`, which doesn't typecheck
                                 // in a function returning String)

    String content;
    content.append(header).append("\n");
    content.append("filler line 1\n");
    content.append("filler line 2\n");
    content.append("filler line 3\n");
    content.append(JoinSemicolon(row)).append("\n");
    return content;
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

}
