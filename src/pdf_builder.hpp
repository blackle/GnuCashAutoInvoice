#pragma once
#include "range.hpp"
#include <memory>
#include <vector>
#include "gnucash/gnc-engine.h"
#include "gnucash/gncInvoice.h"

struct PDFBuilderPrivate;

//todo: rename to PDFRenderer
class PDFBuilder {
public:
	PDFBuilder(QofBook* book);

	// returns whether or not it was successful
	bool init();

	void renderInvoice(GncInvoice* invoice, const std::string& filename);
private:
	std::shared_ptr<PDFBuilderPrivate> p;
};