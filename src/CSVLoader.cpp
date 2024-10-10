#include <fstream>
#include "CSVLoader.h"
#include "CSVReader.h"
#include "IDataBase.h"

bool CSVLoader::LoadFileToDB(IDataBase& dbif, const std::string& filename) {
    std::ifstream filestr(filename);
    CSVRow row(filestr);
    int date_column = row.WhichColumn("Date");
    int type_column = row.WhichColumn("Type");
    int amount_column = row.WhichColumn("Amount");
    int client_name_column = row.WhichColumn("Client name");
    int client_acc_num_column = row.WhichColumn("Client account number");
    int memo_column = row.WhichColumn("Memo");
    int description_column = row.WhichColumn("Description");
    int category_column = row.WhichColumn("Subcategory");
    int account_num_column = row.WhichColumn("Bank account number");
    int account_name_column = row.WhichColumn("Account name");
    int bank_name_column = row.WhichColumn("Bank");
    uint8_t account_id = 0xffu;
    std::string prev_acc_number;
    while (filestr >> row) {
        const char* acc_number = row[account_num_column];
        if (strcmp(prev_acc_number.data(), acc_number) != 0) {
            account_id = dbif.GetAccountId(row[bank_name_column], row[account_num_column], HUF, row[account_name_column]);
            prev_acc_number = acc_number;
        }
        uint8_t type_id = dbif.GetTransactionTypeId(row[type_column]);
        uint16_t client_id = dbif.GetClientId(row[client_name_column], row[client_acc_num_column]);
        dbif.AddTransaction(account_id, std::stol(row[date_column]), type_id, std::stoll(row[amount_column]), client_id, row[category_column], row[memo_column], row[description_column]);
    }
    return false;
}
