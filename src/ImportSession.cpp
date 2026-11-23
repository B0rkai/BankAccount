#include <algorithm>
#include <memory>
#include "ImportSession.h"
#include "AccountManager.h"
#include "Account.h"
#include "AccountNumber.h"
#include "CategorySystem.h"
#include "DataImporter.h"
#include "INewAccount.h"
#include "Transaction.h"

bool ImportRow::Resolved() const {
	return type_id != Id(INVALID_ID) && client_id != Id(INVALID_ID) && category_id != Id(INVALID_ID);
}

size_t ImportRow::HowIndex(QueryTopic topic) {
	return topic == QueryTopic::TYPE ? 0 : topic == QueryTopic::CLIENT ? 1 : 2;
}

size_t ImportSession::FirstStoppableRow() const {
	size_t first = 0;
	while (first < rows.size() && stored_last_date && rows[first].date <= stored_last_date) {
		++first;
	}
	return first;
}

namespace {

const QueryTopic TOPICS[] = { QueryTopic::TYPE, QueryTopic::CLIENT, QueryTopic::CATEGORY };

Id& TopicId(ImportRow& row, QueryTopic topic) {
	return topic == QueryTopic::TYPE ? row.type_id : topic == QueryTopic::CLIENT ? row.client_id : row.category_id;
}
const Id& TopicId(const ImportRow& row, QueryTopic topic) {
	return TopicId(const_cast<ImportRow&>(row), topic);
}

// The first topic of `row` still unresolved, in the order the desktop asked them.
std::optional<QueryTopic> NextTopic(const ImportRow& row) {
	for (QueryTopic topic : TOPICS) {
		if (TopicId(row, topic) == Id(INVALID_ID)) {
			return topic;
		}
	}
	return std::nullopt;
}

// Supplies a new account's details on commit, in place of the desktop's dialog.
class NamedAccount : public INewAccount {
	String m_name;
public:
	explicit NamedAccount(const String& name) : m_name(name) {}
	bool NewAccountDetails(const String&, String& name, String&, CurrencyType) override {
		if (!m_name.empty()) {
			name = m_name;
		}
		return true;
	}
};

} // namespace

bool ImportEngine::Start(const AccountManager& manager, const RawImportData& data, const String& source_name, const String& source_format, ImportSession& session, std::string& error) {
	if (data.data.empty()) {
		error = "No transactions found in the file";
		return false;
	}
	session = ImportSession();
	session.account_number = data.account_number;
	session.bank = data.bank_name;
	session.currency = data.currency;
	session.source_name = source_name;
	session.source_format = source_format;
	session.export_from = data.data.front().date;
	session.export_to = data.data.back().date;

	const Account* account = nullptr;
	for (size_t i = 0; i < manager.m_accounts.size(); ++i) {
		if (manager.m_accounts[i]->CheckAccNumber(data.account_number)) {
			session.account_id = Id((Id::Type)i);
			account = manager.m_accounts[i];
			session.account_name = account->GetName();
			break;
		}
	}
	if (!account) {
		std::unique_ptr<AccountNumber> number(AccountNumber::Create(data.account_number));
		if (!number) {
			error = "The file's account number '" + std::string(data.account_number.utf8_str()) + "' isn't one Persely can read";
			return false;
		}
	}
	size_t first = 0;
	if (account && account->Size()) {
		// Account::PrepareImport(): rows before the day before the last stored day are already
		// stored; from the first row on that day or the next, the stored ones are replaced.
		const uint16_t last = account->GetLastRecord()->GetDate();
		while (first < data.data.size() && data.data[first].date + 1 < last) {
			++first;
		}
		if (first == data.data.size() || data.data[first].date > last) {
			error = "The file doesn't reach back to " + std::string(DateAsString(last).utf8_str())
				+ ", the last day already in " + std::string(account->GetName().utf8_str()) + " - download a longer period";
			return false;
		}
		session.stored_last_date = last;
		session.replace_from = data.data[first].date;
		size_t keep = account->Size();
		while (keep && account->GetTransactionAt(keep - 1).GetDate() >= session.replace_from) {
			--keep;
		}
		session.replaced_count = account->Size() - keep;
		session.skipped = first;
	}
	for (size_t i = first; i < data.data.size(); ++i) {
		const RawTransactionData& raw = data.data[i];
		ImportRow& row = session.rows.emplace_back();
		row.date = raw.date;
		row.amount = raw.amount;
		row.type = raw.type;
		row.client = raw.client;
		row.client_account = raw.client_account_number;
		row.memo = raw.memo;
		row.cat = raw.cat;
		row.description = raw.desc;
	}
	Resolve(manager, session);
	return true;
}

std::vector<ImportEngine::Recallable> ImportEngine::Replaced(const AccountManager& manager, const ImportSession& session) {
	// The stored transactions this import replaces, categorized ones only, newest first - the
	// order Account::PrepareImport() stashed them in, so duplicates are recalled the same way.
	std::vector<Recallable> stored;
	if ((Id::Type)session.account_id >= manager.m_accounts.size() || !session.replaced_count) {
		return stored;
	}
	const Account& account = *manager.m_accounts[session.account_id];
	if (account.Size() < session.replaced_count) {
		return stored;
	}
	for (size_t i = account.Size(); i-- > account.Size() - session.replaced_count;) {
		const Transaction& tr = account.GetTransactionAt(i);
		if (tr.GetCategoryId() != Id(UNCATEGORIZED)) {
			stored.push_back({ tr.GetDate(), tr.GetAmount(), tr.GetClientId(), tr.GetCategoryId() });
		}
	}
	return stored;
}

bool ImportEngine::Resolve(const AccountManager& manager, ImportSession& session) {
	if (session.state == ImportSession::State::Committed) {
		return false;
	}
	const ImportSession before = session;
	std::vector<Recallable> recallable = Replaced(manager, session);
	// In file order, as the desktop went: a row's category reuses a replaced transaction only
	// once, and topics are tried type, client, category - each needs the one before.
	for (ImportRow& row : session.rows) {
		for (QueryTopic topic : TOPICS) {
			const size_t h = ImportRow::HowIndex(topic);
			if (row.how[h] != 'm') { // answers stay; everything else is worked out again
				TopicId(row, topic) = Id(INVALID_ID);
				row.how[h] = '-';
			}
		}
		for (QueryTopic topic : TOPICS) {
			if (row.how[ImportRow::HowIndex(topic)] != 'm' && !TryAuto(manager, row, topic, recallable)) {
				break;
			}
		}
	}
	UpdateProgress(session);
	if (session.state != before.state || session.cursor != before.cursor) {
		return true;
	}
	for (size_t i = 0; i < session.rows.size(); ++i) {
		const ImportRow& a = session.rows[i];
		const ImportRow& b = before.rows[i];
		if (a.how != b.how || a.type_id != b.type_id || a.client_id != b.client_id || a.category_id != b.category_id) {
			return true;
		}
	}
	return false;
}

void ImportEngine::UpdateProgress(ImportSession& session) {
	if (session.state == ImportSession::State::Committed) {
		return;
	}
	const std::vector<size_t> open = OpenRows(session);
	session.cursor = open.empty() ? session.End() : open.front();
	session.state = open.empty() ? ImportSession::State::Review : ImportSession::State::Categorizing;
}

std::vector<size_t> ImportEngine::OpenRows(const ImportSession& session) {
	std::vector<size_t> open;
	for (size_t i = 0; i < session.End(); ++i) {
		if (NextTopic(session.rows[i])) {
			open.push_back(i);
		}
	}
	return open;
}

std::optional<ImportQuestion> ImportEngine::Question(const AccountManager& manager, const ImportSession& session, std::optional<size_t> row) {
	const size_t index = row.value_or(session.cursor);
	if (session.state == ImportSession::State::Committed || index >= session.End()) {
		return std::nullopt;
	}
	std::optional<QueryTopic> topic = NextTopic(session.rows[index]);
	if (!topic) {
		return std::nullopt;
	}
	return Ask(manager, session, index, *topic);
}

bool ImportEngine::TryAuto(const AccountManager& manager, ImportRow& row, QueryTopic topic, std::vector<Recallable>& recallable) {
	if (topic == QueryTopic::TYPE || topic == QueryTopic::CLIENT) {
		// ProcessOneTopic(): only a single definitive keyword match resolves by itself.
		IdSet ids = manager.SearchIds(topic, topic == QueryTopic::TYPE ? row.type : row.client, false);
		if (ids.size() != 1) {
			return false;
		}
		TopicId(row, topic) = *ids.begin();
		row.how[ImportRow::HowIndex(topic)] = 'a';
		return true;
	}
	// CATEGORY - ProcessOneTransaction()'s order: the file's own category, the category of the
	// stored transaction this row replaces, keywords on the raw texts, keywords on the names.
	if (!row.cat.empty()) {
		row.category_id = manager.m_category_system.GetId(row.cat);
		row.how[2] = 'a';
		return true;
	}
	auto recalled = std::find_if(recallable.begin(), recallable.end(), [&](const Recallable& r) {
		return r.date == row.date && r.amount == row.amount && r.client == row.client_id;
	});
	if (recalled != recallable.end()) {
		row.category_id = recalled->category;
		row.how[2] = 'r';
		recallable.erase(recalled);
		return true;
	}
	// Categorize() only reads (and logs); it just isn't declared const in its interface.
	CategorySystem& categories = const_cast<CategorySystem&>(manager.m_category_system);
	Id category = categories.Categorize({ row.type, row.client, row.memo });
	if ((Id::Type)category == 0) {
		category = categories.Categorize(StringVector{ manager.GetTransactionType(row.type_id), manager.GetClientName(row.client_id) });
	}
	if ((Id::Type)category == 0) {
		return false;
	}
	row.category_id = category;
	row.how[2] = 'a';
	return true;
}

ImportQuestion ImportEngine::Ask(const AccountManager& manager, const ImportSession& session, size_t index, QueryTopic topic) {
	const ImportRow& row = session.rows[index];
	ImportQuestion q;
	q.row = index;
	q.topic = topic;
	if (topic == QueryTopic::CATEGORY) {
		// Offered: this client's past categories, the most used one pre-selected.
		q.optional = true;
		q.preselect = Id(UNCATEGORIZED);
		if ((Id::Type)row.client_id != 0) {
			// CategoriesUsedByClient() as the desktop's import saw it: the rows this import
			// replaces were already gone, the ones before this row already added.
			std::vector<Id> used;
			auto add_earlier_rows = [&]() {
				for (size_t i = 0; i < index; ++i) {
					const ImportRow& earlier = session.rows[i];
					if (earlier.client_id == row.client_id && earlier.category_id != Id(INVALID_ID)) {
						used.push_back(earlier.category_id);
					}
				}
			};
			for (size_t a = 0; a < manager.m_accounts.size(); ++a) {
				const Account& account = *manager.m_accounts[a];
				const bool importing = Id((Id::Type)a) == session.account_id;
				const size_t end = importing && account.Size() >= session.replaced_count ? account.Size() - session.replaced_count : account.Size();
				for (size_t i = 0; i < end; ++i) {
					const Transaction& tr = account.GetTransactionAt(i);
					if (tr.GetClientId() == row.client_id) {
						used.push_back(tr.GetCategoryId());
					}
				}
				if (importing) {
					add_earlier_rows();
				}
			}
			if (session.account_id == Id(INVALID_ID)) { // a new account, after all the others
				add_earlier_rows();
			}
			Id most_frequent(INVALID_ID);
			q.choices = AccountManager::DistinctCategories(used, most_frequent);
			if (!q.choices.empty()) {
				q.preselect = most_frequent;
			}
		}
		return q;
	}
	// ProcessOneTopic(): several definitive matches to choose from; otherwise suggestion-tier
	// matches, then low-confidence ones, and with none a "create new" pre-filled with the raw text.
	const String& name = topic == QueryTopic::TYPE ? row.type : row.client;
	q.optional = topic == QueryTopic::CLIENT;
	q.exact_value = name;
	q.choices = manager.SearchIds(topic, name, false);
	if (q.choices.size() > 1) {
		return q;
	}
	q.choices = manager.SearchIdsSuggested(topic, name);
	if (q.choices.empty()) {
		q.choices = manager.SearchIds(topic, name, true);
		if (q.choices.empty()) {
			q.create_suggestion = name;
		}
	}
	if (q.choices.size() == 1) {
		q.preselect = *q.choices.begin();
	}
	return q;
}

bool ImportEngine::Answer(AccountManager& manager, ImportSession& session, const ImportAnswer& answer, std::string& error) {
	const std::optional<ImportQuestion> q = Question(manager, session, answer.row);
	if (!q || q->topic != answer.topic) {
		error = "This question was already answered";
		return false;
	}
	Id id(INVALID_ID);
	switch (answer.kind) {
	case ImportAnswer::Kind::Select:
		if (!manager.IsValidId(answer.topic, answer.id)) {
			error = "No such choice";
			return false;
		}
		id = answer.id;
		break;
	case ImportAnswer::Kind::Default:
		if (!q->optional) {
			error = "This one needs an answer";
			return false;
		}
		id = Id(0);
		break;
	case ImportAnswer::Kind::CreateNew: {
		String name = answer.new_name;
		name.Trim(true).Trim(false);
		if (name.empty()) {
			error = "The new name is empty";
			return false;
		}
		id = manager.CreateId(answer.topic, name);
		if (id == Id(INVALID_ID)) {
			error = "More than one is called that already - pick one instead";
			return false;
		}
		break;
	}
	}
	String keyword = answer.keyword;
	keyword.Trim(true).Trim(false);
	if (!keyword.empty()) {
		if (answer.kind == ImportAnswer::Kind::Default) {
			error = "A keyword needs a client or category to point to";
			return false;
		}
		manager.AddKeyword(answer.topic, id, keyword, answer.keyword_definitive);
	}
	ImportRow& row = session.rows[q->row];
	if (answer.description) {
		row.description = answer.description->Strip(wxString::both);
	}
	TopicId(row, answer.topic) = id;
	row.how[ImportRow::HowIndex(answer.topic)] = 'm';
	Resolve(manager, session); // a new keyword or client may settle other rows too
	return true;
}

bool ImportEngine::StopAt(ImportSession& session, std::optional<size_t> row, std::string& error) {
	if (session.state == ImportSession::State::Committed) {
		error = "Already saved";
		return false;
	}
	if (row && *row >= session.rows.size()) {
		row.reset(); // after the last row: keep them all
	}
	if (row && *row < session.FirstStoppableRow()) {
		error = "Rows up to " + std::string(DateAsString(session.stored_last_date).utf8_str())
			+ " replace transactions already saved, so they can't be dropped - discard the import instead";
		return false;
	}
	session.stop_at = row;
	UpdateProgress(session);
	return true;
}

bool ImportEngine::EditRow(const AccountManager& manager, ImportSession& session, size_t index, std::optional<Id> category_id, std::optional<Id> client_id, std::optional<String> description, std::string& error) {
	if (session.state == ImportSession::State::Committed || index >= session.rows.size()) {
		error = "No such row";
		return false;
	}
	if ((category_id && !manager.IsValidId(QueryTopic::CATEGORY, *category_id)) || (client_id && !manager.IsValidId(QueryTopic::CLIENT, *client_id))) {
		error = "No such client or category";
		return false;
	}
	ImportRow& row = session.rows[index];
	if (category_id) {
		row.category_id = *category_id;
		row.how[2] = 'm';
	}
	if (client_id) {
		row.client_id = *client_id;
		row.how[1] = 'm';
	}
	if (description) {
		row.description = description->Strip(wxString::both);
	}
	Resolve(manager, session);
	return true;
}

bool ImportEngine::Commit(AccountManager& manager, ImportSession& session, std::string& error) {
	if (session.state == ImportSession::State::Committed) {
		error = "Already saved";
		return false;
	}
	Resolve(manager, session);
	if (session.state != ImportSession::State::Review) {
		error = std::to_string(OpenRows(session).size()) + " row(s) still need an answer";
		return false;
	}
	const size_t end = session.End();
	for (size_t i = 0; i < end; ++i) {
		if (!session.rows[i].Resolved()) {
			error = "Row " + std::to_string(i + 1) + " isn't resolved";
			return false;
		}
	}
	Account* account = nullptr;
	if (session.account_id == Id(INVALID_ID)) {
		NamedAccount details(session.account_name);
		session.account_id = manager.CreateOrGetAccountId(session.account_number, session.bank, session.currency, &details);
		account = manager.m_accounts[session.account_id];
		if (account->Size()) {
			error = "An account with this number was created since the upload - discard and upload again";
			return false; // the caller rolls back
		}
	} else {
		if ((Id::Type)session.account_id >= manager.m_accounts.size()) {
			error = "The account is gone";
			return false;
		}
		account = manager.m_accounts[session.account_id];
		// Unchanged since the upload: the same last day, and the same rows to replace.
		const uint16_t last = account->Size() ? account->GetLastRecord()->GetDate() : 0;
		size_t keep = account->Size();
		while (session.replaced_count && keep && account->GetTransactionAt(keep - 1).GetDate() >= session.replace_from) {
			--keep;
		}
		if (last != session.stored_last_date || account->Size() - keep != session.replaced_count) {
			error = "The account's transactions changed since the upload - discard and upload again";
			return false;
		}
	}
	if (session.replaced_count) {
		account->PruneLastTransactions(session.replaced_count);
	}
	for (size_t i = 0; i < end; ++i) {
		const ImportRow& row = session.rows[i];
		// The client's bank account number, as ProcessOneTransaction() remembered it (not for
		// "no client") - only now, since until Save a row's client can still change.
		if ((Id::Type)row.client_id != 0 && manager.m_client_man.AddAccountNumber(row.client_id, row.client_account)) {
			manager.m_journal.AppendClientAccountNumber(row.client_id, row.client_account);
		}
		account->AddImportedTransaction(row.date, row.type_id, row.amount, row.client_id, row.memo, row.category_id, row.description);
	}
	manager.Modified();
	session.state = ImportSession::State::Committed;
	return true;
}

Id ImportEngine::MergedId(Id id, const IdSet& from, Id to) {
	if (id == Id(INVALID_ID)) {
		return id;
	}
	const Id target = from.count(id) ? to : id;
	const auto shift = std::count_if(from.begin(), from.end(), [&](const Id& f) { return f < target; });
	return Id((Id::Type)((Id::Type)target - shift));
}

void ImportEngine::RemapAfterMerge(ImportSession& session, QueryTopic topic, const IdSet& from, Id to) {
	for (ImportRow& row : session.rows) {
		Id& id = TopicId(row, topic);
		id = MergedId(id, from, to);
	}
}
