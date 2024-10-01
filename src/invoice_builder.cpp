#include "invoice_builder.hpp"
#include "helpers.hpp"
#include <iostream>
#include <cassert>

struct InvoiceBuilderPrivate {
	QofBook* book = nullptr;
	GncTaxTable* hst = nullptr;
	Account* memberfees = nullptr;
	Account* accountsreceivable = nullptr;
	gnc_commodity* comm = nullptr;
};

static Account* getMemberFeesAccount(QofBook* book) {
	GncGUID* mf_guid = guid_new();
	string_to_guid("37727d28d58e485899b5a82914dcd9b7", mf_guid);
	return xaccAccountLookup(mf_guid, book);
}

static Account* getAccReceiveableAccount(QofBook* book) {
	GncGUID* mf_guid = guid_new();
	string_to_guid("057795ef210a4de0bb65da7d524cc8f7", mf_guid);
	return xaccAccountLookup(mf_guid, book);
}

static gnc_commodity* getCadCommodity(QofBook* book) {
	gnc_commodity_table* commtable = gnc_commodity_table_get_table(book);
	return gnc_commodity_table_lookup(commtable, "CURRENCY", "CAD");
}

static GncTaxTable* getHSTTaxTable(QofBook* book) {
	GList* taxtables = gncBusinessGetList(book, GNC_ID_TAXTABLE, true);
	GncTaxTable* hst = nullptr;
	for (GList* t = g_list_first(taxtables); t != NULL; t = g_list_next(t)) {
		GncTaxTable* taxtable = (GncTaxTable*)t->data;
		std::string name = gncTaxTableGetName(taxtable);
		if (name == "HST (Sales)") {
			hst = taxtable;
		}
	}
	return hst;
}

InvoiceBuilder::InvoiceBuilder(QofBook* book) {
	p = std::make_shared<InvoiceBuilderPrivate>();
	p->book = book;
}

#define FIFTEEN_DAYS 1296000

//todo: use more functional programming techniques to return error information
#define IB_RETURN(line) do { \
	std::cerr << "IB_RETURN error on line " << line << std::endl; \
	return false; \
} while(0)

bool InvoiceBuilder::init() {
	p->hst = getHSTTaxTable(p->book);
	if (p->hst == nullptr) IB_RETURN(__LINE__);

	p->memberfees = getMemberFeesAccount(p->book);
	if (p->memberfees == nullptr) IB_RETURN(__LINE__);

	p->accountsreceivable = getAccReceiveableAccount(p->book);
	if (p->accountsreceivable == nullptr) IB_RETURN(__LINE__);

	p->comm = getCadCommodity(p->book);
	if (p->comm == nullptr) IB_RETURN(__LINE__);

	return true;
}

GncInvoice* InvoiceBuilder::createAndPostInvoice(GDate* date, GncCustomer* customer, const std::vector<Range>& ranges) {

	auto date_64 = gdate_to_time64(*date);
	Range monthRange = Range::createForMonth(date, -1.0);
	std::string cust_name = gncCustomerGetName(customer);

	GncInvoice* invoice = gncInvoiceCreate(p->book);
	gncInvoiceBeginEdit(invoice);

	GncOwner* owner = gncOwnerNew();
	gncOwnerInitCustomer(owner, customer);
	gncInvoiceSetOwner(invoice, owner);
	gncInvoiceSetBillTo(invoice, owner);

	char* nextID = qof_book_increment_and_format_counter(p->book, "gncInvoice");
	gncInvoiceSetID(invoice, nextID);
	gncInvoiceSetActive(invoice, true);
	gncInvoiceSetCurrency(invoice, p->comm);
	gncInvoiceSetNotes(invoice, "Automatically generated by auto_invoice");

	float coverage = 0.0f;
	for (const auto& range : ranges) {
		float overlap = range.overlapPercentage(monthRange);
		std::cout << "overlap = " << overlap << " price = " << range.getPrice() << std::endl;
		assert(overlap <= 1.0f);
		coverage += overlap;

		std::string entryName = "Monthly Dues";
		if (overlap < 1.0f) {
			auto start_str = GDateToString(range.GetStart());
			auto end_str = GDateToString(range.GetEnd());
			entryName = "Monthly Dues (" + start_str + " to " + end_str + ")";
		}

		GncEntry* entry = gncEntryCreate(p->book);
		gncEntryBeginEdit(entry);
		gncEntrySetDateGDate(entry, date);
		gncEntrySetDateEntered(entry, date_64);
		gncEntrySetDescription(entry, entryName.c_str());
		gncEntrySetQuantity(entry, double_to_gnc_numeric(overlap, 1000, GNC_HOW_DENOM_FIXED|GNC_HOW_RND_ROUND_HALF_DOWN));

		gncEntrySetInvAccount(entry, p->memberfees);
		gncEntrySetInvPrice(entry, double_to_gnc_numeric(range.getPrice(), 100, GNC_HOW_DENOM_FIXED|GNC_HOW_RND_ROUND_HALF_DOWN));
		gncEntrySetInvTaxable(entry, true);
		gncEntrySetInvTaxIncluded(entry, false);
		gncEntrySetInvTaxTable(entry, p->hst);
		gncEntryCommitEdit(entry);

		gncInvoiceAddEntry(invoice, entry);
	}
	assert(coverage <= 1.0f);

	gncInvoiceSetDateOpened(invoice, date_64);
	gncInvoiceSetIsCreditNote(invoice, false);
	gncInvoiceCommitEdit(invoice);

	std::string memo = std::string("Monthly Dues for ")+cust_name;
	auto posted_date_64 = date_64 - FIFTEEN_DAYS;
	gncInvoicePostToAccount(invoice, p->accountsreceivable, posted_date_64, date_64, memo.c_str(), true, false);

	return invoice;
}
