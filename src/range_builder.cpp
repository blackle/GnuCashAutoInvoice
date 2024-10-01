#include "range_builder.hpp"
#include <iostream>
#include <regex>
#include <cassert>

#include "json.hpp"
using json = nlohmann::json;

RangeBuilder::RangeBuilder(QofBook* book) : m_book(book) {}

static GDate* dateFromDMY(const std::string& dmy) {
	std::regex pattern(R"((\d+)-(\d+)-(\d+))");
	std::smatch matches;
	if (std::regex_match(dmy, matches, pattern)) {
		int day = std::stoi(matches[1].str());
		int month = std::stoi(matches[2].str());
		int year = std::stoi(matches[3].str());
		return g_date_new_dmy(day, (GDateMonth)month, year);
	}
	//todo: better error handling
	assert(dmy.length() == 0);
	return nullptr;
}

//todo const?
std::optional<std::vector<Range>> RangeBuilder::createRanges(GDate* date, GncCustomer* customer) {

	std::string cust_name = gncCustomerGetName(customer);
	std::string cust_notes = gncCustomerGetNotes(customer);

	Range monthRange = Range::createForMonth(date, -1.0);

	json customer_data;
	try {
		customer_data = json::parse(cust_notes);
	} catch (const json::exception& e) {
		std::cerr << "WARNING, COULDN'T PARSE JSON DATA!" << std::endl;
		return std::nullopt;
	}

	std::vector<Range> ranges;
	for (const auto& dat : customer_data) {
		std::string dues = dat["dues"];
		Range range(dateFromDMY(dat["start"]), dateFromDMY(dat["end"]), std::stof(dues));
		auto intersected_range = range.intersect(monthRange);
		if (intersected_range.has_value()) {
			ranges.push_back(intersected_range.value());
		}
	}
	return ranges;
}
