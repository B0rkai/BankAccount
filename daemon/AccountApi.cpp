#include "AccountApi.h"
#include "Account.h"
#include "BrowseApi.h"
#include "Currency.h"
#include "ImportApi.h"
#include "ImportSession.h"
#include "Transaction.h"

using json = nlohmann::json;

namespace {

std::string Utf8(const String& s) {
	return std::string(s.utf8_str());
}

QueryApiResult Ok(const json& body) {
	return { 200, body.dump() };
}

const Account* AccountAt(const AccountManager& mgr, int64_t id) {
	return id >= 0 && id < INVALID_ID ? mgr.FindAccount(Id((Id::Type)id)) : nullptr;
}

json Owners(const AccountContext& ctx) {
	json owners = json::array();
	for (const std::string& login : ctx.logins) {
		owners.push_back(login);
	}
	return owners;
}

json AccountToJson(const AccountContext& ctx, Id::Type id) {
	const Account& acc = *ctx.mgr.FindAccount(Id(id));
	const std::string number = Utf8(acc.GetAccNumber());
	auto found = ctx.settings.find(AccountKey(number));
	const AccountSettings settings = found == ctx.settings.end() ? AccountSettings() : found->second;
	const INameResolve& names = ctx.mgr;
	json j = {
		{ "id", id },
		{ "name", Utf8(acc.GetName()) },
		{ "full_name", Utf8(names.GetName(QueryTopic::ACCOUNT, Id(id))) },
		{ "number", number },
		{ "bank", Utf8(acc.GetGroupName()) },
		{ "currency", acc.GetCurrency()->GetShortName() },
		{ "open", acc.IsOpen() },
		{ "owner", settings.owner },
		{ "transactions", acc.Size() },
		{ "first_date", acc.Size() ? json(IsoDate(acc.GetFirstRecord()->GetDate())) : json(nullptr) },
		{ "last_date", acc.Size() ? json(IsoDate(acc.GetLastRecord()->GetDate())) : json(nullptr) },
		{ "schedule", ScheduleToJson(settings.schedule, settings.runs) },
		{ "last_run", settings.runs.empty() ? json(nullptr) : RunToJson(settings.runs.back()) },
	};
	const BankConnection* conn = ConnectionOf(ctx.connections, acc.GetAccNumber());
	if (conn) {
		const std::optional<long> days = BankDaysLeft(*conn, ctx.now);
		j["connection"] = { { "id", conn->id }, { "bank", conn->bank }, { "valid_until", conn->valid_until }, { "days_left", days ? json(*days) : json(nullptr) } };
	} else {
		j["connection"] = nullptr;
	}
	const ImportSession* open = ctx.imports.FindByAccountNumber(acc.GetAccNumber());
	j["import"] = open ? json{ { "id", open->id }, { "state", open->state == ImportSession::State::Review ? "review" : "categorizing" },
		{ "open", ImportEngine::OpenRows(*open).size() }, { "rows", open->rows.size() }, { "from_bank", open->source_format == "ENABLE_BANKING" } } : json(nullptr);
	return j;
}

} // namespace

const BankConnection* ConnectionOf(const std::vector<BankConnection>& connections, const String& account_number) {
	const std::string key = AccountKey(Utf8(account_number));
	for (const BankConnection& c : connections) {
		for (const BankAccountLink& a : c.accounts) {
			if (AccountKey(a.account_number) == key) {
				return &c;
			}
		}
	}
	return nullptr;
}

QueryApiResult ListAccounts(const AccountContext& ctx) {
	json list = json::array();
	for (Id::Type id = 0; id < ctx.mgr.CountAccounts(); ++id) {
		list.push_back(AccountToJson(ctx, id));
	}
	return Ok(json{ { "accounts", list }, { "owners", Owners(ctx) } });
}

QueryApiResult AccountDetails(const AccountContext& ctx, int64_t id) {
	const Account* acc = AccountAt(ctx.mgr, id);
	if (!acc) {
		return MakeErrorResult(404, "No such account");
	}
	json j = AccountToJson(ctx, (Id::Type)id);
	json runs = json::array();
	auto found = ctx.settings.find(AccountKey(Utf8(acc->GetAccNumber())));
	if (found != ctx.settings.end()) {
		for (auto it = found->second.runs.rbegin(); it != found->second.runs.rend(); ++it) {
			runs.push_back(RunToJson(*it));
		}
	}
	j["runs"] = runs;
	j["owners"] = Owners(ctx);
	return Ok(j);
}

QueryApiResult UpdateAccountSettings(const AccountContext& ctx, int64_t id, const json& body, AccountSettingsStore& store, const std::string& actor) {
	const Account* acc = AccountAt(ctx.mgr, id);
	if (!acc) {
		return MakeErrorResult(404, "No such account");
	}
	if (!body.is_object() || (!body.contains("owner") && !body.contains("schedule"))) {
		return MakeErrorResult(400, "Expected {owner?, schedule?}");
	}
	std::optional<std::string> owner;
	if (body.contains("owner")) {
		if (!body["owner"].is_string() || (!body["owner"].get<std::string>().empty() && !ctx.logins.count(body["owner"].get<std::string>()))) {
			return MakeErrorResult(400, "The owner must be one of the logins that may use Persely");
		}
		owner = body["owner"].get<std::string>();
	}
	std::optional<std::optional<BankSchedule>> schedule;
	if (body.contains("schedule")) {
		const json& sj = body["schedule"];
		if (sj.is_null()) {
			schedule = std::optional<BankSchedule>();
		} else {
			const std::string every = sj.is_object() && sj.contains("every") && sj["every"].is_string() ? sj["every"].get<std::string>() : "";
			const int day = sj.is_object() && sj.contains("day") && sj["day"].is_number_integer() ? sj["day"].get<int>() : 0;
			if (!ValidSchedule(every, day)) {
				return MakeErrorResult(400, "A schedule is {\"every\": \"week\", \"day\": 1-7} or {\"every\": \"month\", \"day\": 1-31}");
			}
			if (!ConnectionOf(ctx.connections, acc->GetAccNumber())) {
				return MakeErrorResult(409, "Connect the account's bank first (Settings → Bank connections)");
			}
			schedule = BankSchedule{ every, day, IsoUtc(ctx.now), actor };
		}
	}
	std::string error;
	if (!store.Update(Utf8(acc->GetAccNumber()), [&](AccountSettings& s) {
			if (owner) {
				s.owner = *owner;
			}
			if (schedule) {
				s.schedule = *schedule;
			}
		}, error)) {
		return MakeErrorResult(500, "Could not save the account's settings: " + error);
	}
	return Ok(json{ { "ok", true } });
}
