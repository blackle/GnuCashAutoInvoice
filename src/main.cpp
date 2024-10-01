#include "libguile.h"
#include "gnucash/gnc-engine.h"
#include "gnucash/gncCustomer.h"
#include "gnucash/gnc-report.h"
#include "gnucash/gnc-prefs-utils.h"
#include "gnucash/gnc-session.h"
#include "gnucash/Transaction.h"
#include "gnc-prefs.h"
#include "gnc-filepath-utils.h"
#include "gnc-environment.h"
#include "swig-runtime.h"

#include "range.hpp"
#include "invoice_builder.hpp"
#include "range_builder.hpp"
#include "pdf_builder.hpp"
#include "helpers.hpp"

#include <vector>
#include <iostream>
#include <regex>
#include <filesystem>

#define QCC_FINANCES_FILE "./QCC_Finances.gnucash"

void error_callback(gpointer data, QofBackendError errcode) {
	std::cerr << "error_callback " << errcode << std::endl;
}

#define SCM_RETURN(line) do { \
	std::cerr << "Failure line " << line << std::endl; \
	*retval = -1; \
	return; \
} while(0)

struct ProgramArgs {
	bool force = false;
	bool dry_run = false;
	int month = -1;
	int year = -1;

	// output
	int retval = 0;
};

// todo:
// - automatic emailling of the PDFs (dump to a email job file of some kind)
// - fix var names
// - move code in main.cpp into isolated functions
// - try to clean up some memleaks

struct SessionCloser {
	QofSession *m_sesh;
	SessionCloser(QofSession *sesh) : m_sesh(sesh) {}
	~SessionCloser() {
		qof_session_end(m_sesh);
		qof_session_destroy(m_sesh);
	}
};

#define FIFTEEN_DAYS 1296000

//checks that there were invoices for last month, and none for this month
bool checkForPreviousMonth(QofBook* book, const GDate* date) {
	auto prev_date = g_date_copy(date);
	g_date_subtract_months(prev_date, 1);
	auto prev_date_64 = gdate_to_time64(*prev_date) - FIFTEEN_DAYS;

	std::vector<std::string> found_invoices;
	GList* invoices = gncBusinessGetList(book, GNC_ID_INVOICE, true);
	bool found_last = false;
	for (GList* i = g_list_first(invoices); i != NULL; i = g_list_next(i)) {
		GncInvoice* invoice = (GncInvoice*)i->data;
		auto invoice_date_64 = gncInvoiceGetDatePosted(invoice);
		if (invoice_date_64 == prev_date_64) {
			found_last = true;
		}
	}
	bool failed = false;
	if (found_last == false) {
		std::cout << "WARNING! NO INVOICES FOR LAST MONTH!" << std::endl;
		std::cout << "Did you skip this month?" << std::endl;
		failed = true;
	}
	g_list_free(invoices);
	g_date_free(prev_date);
	return failed;
}

// list customers who could be invoiced
GList* getUninvoiced(QofBook* book, const GDate* date) {
	auto date_64 = gdate_to_time64(*date) - FIFTEEN_DAYS;

	std::vector<std::string> invoiced_customers;
	GList* invoices = gncBusinessGetList(book, GNC_ID_INVOICE, true);
	for (GList* i = g_list_first(invoices); i != NULL; i = g_list_next(i)) {
		GncInvoice* invoice = (GncInvoice*)i->data;
		auto invoice_date_64 = gncInvoiceGetDatePosted(invoice);
		if (invoice_date_64 == date_64) {
			auto notes = gncInvoiceGetNotes(invoice);
			if (notes == nullptr || std::string(notes).find("auto_invoice") == std::string::npos) {
				continue;
			}
			auto owner = gncInvoiceGetBillTo(invoice);
			auto customer = gncOwnerGetCustomer(owner);
			auto id = gncCustomerGetID(customer);
			if (id != nullptr) {
				invoiced_customers.push_back(id);
			}
		}
	}
	GList* uninvoiced_customers = nullptr;
	GList* customers = gncBusinessGetList(book, GNC_ID_CUSTOMER, true);
	for (GList* c = g_list_first(customers); c != NULL; c = g_list_next(c)) {
		GncCustomer* customer = (GncCustomer*)c->data;

		auto notes = gncCustomerGetNotes(customer);
		if (notes == nullptr || strlen(notes) == 0) {
			continue;
		}
		auto id = gncCustomerGetID(customer);
		if (std::find(invoiced_customers.begin(), invoiced_customers.end(), id) == invoiced_customers.end()) {
			uninvoiced_customers = g_list_append(uninvoiced_customers, customer);
		}
	}
	g_list_free(invoices);
	g_list_free(customers);
	return uninvoiced_customers;
}

bool doesTSVExist(const std::string& filename) {
	std::ifstream inputFile(filename);
	return inputFile.good();
}

struct balanceGetterData {
	gnc_numeric* balance;
	GncOwner* owner;
	gnc_commodity* owner_currency;
};

gint balanceGetterFunc(Transaction *t, void *d) {
	balanceGetterData* data = static_cast<balanceGetterData*>(d);
	for (GList *n = xaccTransGetSplitList (t); n; n = g_list_next (n)) {
		const Split* split = static_cast<const Split*>(n->data);
		GNCLot *lot = xaccSplitGetLot(split);
		GncInvoice *invoice = gncInvoiceGetInvoiceFromLot (lot);
		GncOwner owner;

		// big if statements to check for the presence of an invoice, if that invoice belongs to this owner, and if this transaction was the posting transaction
		if (invoice && gncOwnerEqual(gncInvoiceGetOwner(invoice), data->owner) && t == gncInvoiceGetPostedTxn (invoice)) {
			gnc_numeric total = gncInvoiceGetTotal(invoice);
			*data->balance = gnc_numeric_add (*data->balance, total, gnc_commodity_get_fraction (data->owner_currency), GNC_HOW_RND_ROUND_HALF_UP);
		}
		else if (invoice || gncOwnerGetOwnerFromLot(lot, &owner)) {
			bool myOwner = invoice ? gncOwnerEqual(gncInvoiceGetOwner(invoice), data->owner) : gncOwnerEqual(&owner, data->owner);
			if (myOwner) {
				// we add the split amount because it will be negative (money moving out of acc/rev into some real account)
				*data->balance = gnc_numeric_add(*data->balance, xaccSplitGetAmount(split), gnc_commodity_get_fraction (data->owner_currency), GNC_HOW_RND_ROUND_HALF_UP);
			}
		}
	}
	return 0;
}

// todo: this is O(N^2) because we iterate over all the transactions for every customer we have, would be more efficient to loop once and aggregate
gnc_numeric getOwnerAccountBalance(QofBook* book, GncOwner* owner) {
	GList *acct_list  = gnc_account_get_descendants(gnc_book_get_root_account(book));
	GList *acct_types = gncOwnerGetAccountTypesList(owner);
	GList *acct_node = nullptr;
	gnc_numeric balance = gnc_numeric_zero();
	gnc_commodity *owner_currency = gncOwnerGetCurrency(owner);

	/* For each account */
	for (acct_node = acct_list; acct_node != nullptr; acct_node = acct_node->next) {
		Account *account = static_cast<Account*>(acct_node->data);

		/* Check if this account can have lots for the owner, otherwise skip to next */
		if (g_list_index (acct_types, (gpointer)xaccAccountGetType (account))
						== -1) continue;

		if (!gnc_commodity_equal (owner_currency, xaccAccountGetCommodity (account))) continue;

		balanceGetterData data;
		data.balance = &balance;
		data.owner = owner;
		data.owner_currency = owner_currency;
		xaccAccountForEachTransaction(account, balanceGetterFunc, &data);
	}
	g_list_free (acct_list);
	g_list_free (acct_types);
	return balance;
}

static void scm_main (void *data, [[maybe_unused]] int argc, [[maybe_unused]] char **argv) {
	ProgramArgs* args = (ProgramArgs*)data;
	int* retval = &args->retval;

	const std::string tsv_filename = args->dry_run ? "/tmp/email_jobs.tsv" : "email_jobs.tsv";
	if (doesTSVExist(tsv_filename) && !args->dry_run) {
		std::cerr << "Error: File '" << tsv_filename << "' already exists. Aborting." << std::endl;
		SCM_RETURN(__LINE__);
	}

	std::ofstream tsv_file(tsv_filename);
	// Check if the file is opened successfully
	if (!tsv_file.is_open()) {
		std::cerr << "Error opening the email jobs file!" << std::endl;
		SCM_RETURN(__LINE__);
	}

	scm_c_eval_string("(debug-set! stack 200000)");
	scm_c_use_module ("gnucash utilities");
	scm_c_use_module ("gnucash app-utils");
	scm_c_use_module ("gnucash reports");

	gnc_report_init();
	gnc_prefs_init();

	//for some reason this doesn't get loaded from the prefs...
	gnc_prefs_set_file_save_compressed(false);

	auto current_date = g_date_new_dmy(1, (GDateMonth)args->month, args->year);
	auto current_date_64 = gdate_to_time64(*current_date);
	auto current_date_string = GDateToString(current_date);

	auto pdf_path = std::string("./Invoices/") + current_date_string;
	if (!std::filesystem::exists(pdf_path)) {
		std::filesystem::create_directory(pdf_path);
	}

	QofSession *sesh = gnc_get_current_session();
	SessionCloser closer(sesh);

	QofBook* book = qof_session_get_book(sesh);
	if (sesh == nullptr || book == nullptr) SCM_RETURN(__LINE__);

	qof_session_begin(sesh, QCC_FINANCES_FILE, SESSION_NORMAL_OPEN);
	if (qof_session_get_error(sesh) != ERR_BACKEND_NO_ERR) SCM_RETURN(__LINE__);

	qof_session_load(sesh, nullptr);
	if (qof_session_get_error(sesh) != ERR_BACKEND_NO_ERR) SCM_RETURN(__LINE__);

	if (checkForPreviousMonth(book, current_date) && !args->force) {
		return;
	}

	InvoiceBuilder invoice_builder(book);
	if (invoice_builder.init() == false) SCM_RETURN(__LINE__);

	RangeBuilder range_builder(book);

	PDFBuilder pdf_builder(book);
	if (pdf_builder.init() == false) SCM_RETURN(__LINE__);

	GList* customers = getUninvoiced(book, current_date);
	for (GList* c = g_list_first(customers); c != NULL; c = g_list_next(c)) {

		GncCustomer* customer = (GncCustomer*)c->data;

		GncOwner owner;
		owner.type = GNC_OWNER_CUSTOMER;
		owner.owner.customer = customer;
		owner.qof_temp = nullptr;
		gnc_numeric balance = getOwnerAccountBalance(book, &owner);
		double balanced = gnc_numeric_to_double(balance);

		bool active = gncCustomerGetActive(customer);
		if (!active) {
			continue;
		}
		GncAddress* addr = gncCustomerGetAddr(customer);
		std::string cust_name = gncCustomerGetName(customer);
		std::string cust_email = gncAddressGetEmail(addr);

		std::cout << "--- generating invoices for " << gncCustomerGetName(customer) << std::endl;

		auto ranges_opt = range_builder.createRanges(current_date, customer);
		if (!ranges_opt.has_value()) {
			std::cerr << "some kind of error in createRanges" << std::endl;
			SCM_RETURN(__LINE__);
		}
		auto ranges = ranges_opt.value();
		if (ranges.size() == 0) {
			std::cout << "no ranges for this member" << std::endl;
			continue;
		}

		GncInvoice* invoice = invoice_builder.createAndPostInvoice(current_date, customer, ranges);

		if (!args->dry_run) {
			std::string filename = pdf_path + "/" + cust_name + " Invoice.pdf";
			pdf_builder.renderInvoice(invoice, filename);

			// add email+filename for invoice sending
			gchar* balance_str = gnc_numeric_to_string(balance);
			tsv_file << cust_email << "\t" << filename << "\t" << balance_str << std::endl;
			g_free(balance_str);
		}
		std::cout << "balance for " << cust_name << " = " << balanced << std::endl;
	}
	g_list_free(customers);

	if (!args->dry_run) {
		qof_session_save(sesh, nullptr);
	}
	if (qof_session_get_error(sesh) != ERR_BACKEND_NO_ERR) SCM_RETURN(__LINE__);

	std::cout << "Invoice generation successful!" << std::endl;
}


//this is all boilerplate for gnucash. you want to look at the function "scm_main"
#define ARGS_STATUS_MISSING 1
#define ARGS_STATUS_BAD 2
#define ARGS_STATUS_OK 0

bool parseArguments(ProgramArgs& args, int argc, char* argv[]) {
	int status = ARGS_STATUS_MISSING;
	for (int i = 1; i < argc; ++i) {
		std::string arg(argv[i]);
		if (arg == "--force" || arg == "-f") {
			args.force = true;
		} else if (arg == "--dry-run" || arg == "-d") {
			args.dry_run = true;
		} else if (i+1 == argc) {
			//parse out the month-year
			std::regex pattern(R"((\d+)-(\d+))");
			std::smatch matches;
			if (std::regex_match(arg, matches, pattern)) {
				args.month = std::stoi(matches[1].str());
				args.year = std::stoi(matches[2].str());
				status = ARGS_STATUS_OK;
			} else {
				status = ARGS_STATUS_BAD; break;
			}
		} else {
			status = ARGS_STATUS_BAD; break;
		}
	}
	return status == ARGS_STATUS_OK;
}

int main(int argc, char* argv[]) {
	g_setenv("GUILE_WARN_DEPRECATED", "no", true);

	ProgramArgs args;
	if (parseArguments(args, argc, argv) == false) {
		std::cerr << "bad arguments" << std::endl;
		std::cerr << argv[0] << " [-f|--force] [-d|--dry-run] month-year" << std::endl;
		return -1;
	}
	if (args.dry_run) {
		std::cerr << "WARNING: THIS IS A DRY RUN!!!!" << std::endl;
	}
	gnc_environment_setup();
	gnc_filepath_init();

	gnc_engine_add_commit_error_callback(error_callback, nullptr);
	gnc_engine_init(0, nullptr);
	if (gnc_engine_is_initialized() == false) {
		return -1;
	}

	scm_boot_guile(0, nullptr, scm_main, &args);
	return args.retval;
}