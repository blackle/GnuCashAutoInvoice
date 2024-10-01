#pragma once
#include "range.hpp"
#include <memory>
#include <vector>
#include <optional>
#include "gnucash/gnc-engine.h"
#include "gnucash/gncCustomer.h"

class RangeBuilder {
public:
	RangeBuilder(QofBook* book);

	std::optional<std::vector<Range>> createRanges(GDate* date, GncCustomer* customer);
private:
	QofBook* m_book = nullptr;
};