#pragma once
#include "range.hpp"
#include <memory>
#include <vector>
#include "gnucash/gnc-engine.h"
#include "gnucash/gncCustomer.h"

struct InvoiceBuilderPrivate;

class InvoiceBuilder {
public:
	InvoiceBuilder(QofBook* book);

	// returns whether or not it was successful
	bool init();

	GncInvoice* createAndPostInvoice(GDate* date, GncCustomer* customer, const std::vector<Range>& ranges);
private:
	std::shared_ptr<InvoiceBuilderPrivate> p;
};