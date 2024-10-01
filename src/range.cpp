#include "range.hpp"
#include <cassert>
#include <algorithm>

template<typename T>
GDate* safe_comp(T func, const GDate* a, const GDate* b) {
	if (a == nullptr || b == nullptr) {
		const GDate* leftover = (a == nullptr) ? b : a;
		if (leftover == nullptr) {
			return nullptr;
		}
		return g_date_copy(leftover);
	}
	return g_date_new_julian(func(g_date_get_julian(a), g_date_get_julian(b)));
}

int int_max(int a, int b) { return std::max(a, b); }
int int_min(int a, int b) { return std::min(a, b); }

Range::Range(GDate* start, GDate* end, float price) : m_price(price) {
	if (start != nullptr) m_start = g_date_copy(start);
	if (end != nullptr) m_end = g_date_copy(end);
}

Range::Range(const Range& other) {
	if (other.m_start != nullptr) m_start = g_date_copy(other.m_start);
	if (other.m_end != nullptr) m_end = g_date_copy(other.m_end)	;
	m_price = other.m_price;
}

Range::~Range() {
	if (m_start != nullptr) g_date_free(m_start);
	if (m_end != nullptr) g_date_free(m_end);
}

std::optional<Range> Range::intersect(const Range& range) const {
	GDate* intersection_start = safe_comp(int_max, m_start, range.m_start);
	GDate* intersection_end = safe_comp(int_min, m_end, range.m_end);

	if (intersection_start != nullptr && intersection_end != nullptr) {
		if (g_date_compare(intersection_start, intersection_end) >= 0) {
			// No intersection
			g_date_free(intersection_start);
			g_date_free(intersection_end);
			return std::nullopt;
		}
	}

	float intersection_price = m_price;
	return Range(intersection_start, intersection_end, intersection_price);
}

int Range::numDays() const {
	if (m_start == nullptr || m_end == nullptr) {
		return -1;
	}
	return g_date_get_julian(m_end) - g_date_get_julian(m_start) + 1;
}

float Range::overlapPercentage(const Range& range) const {
	assert(range.numDays() != -1);
	auto overlap = intersect(range);
	if (!overlap.has_value()) {
		return 0;
	}
	float range_days = range.numDays();
	float overlap_days = overlap.value().numDays();
	return overlap_days/range_days;
}

Range Range::createForMonth(GDate* firstDayOfMonth, float price) {
	GDate* lastDayOfMonth = g_date_new_julian(
		g_date_get_julian(firstDayOfMonth) + g_date_get_days_in_month(g_date_get_month(firstDayOfMonth), g_date_get_year(firstDayOfMonth)) - 1
	);

	return Range(firstDayOfMonth, lastDayOfMonth, price);
}

Range Range::createFromStartAndEnd(GDate* start, GDate* end, float price) {
	return Range(start, end, price);
}

void Range::setPrice(float price) {
	m_price = price;
}

float Range::getPrice() const {
	return m_price;
}

static std::string dateToString(const GDate* date, int side) {
	if (date == nullptr) {
		return side < 0 ? "-infty" : "infty";
	}
	gchar buffer[0xFF];
	g_date_strftime(buffer, sizeof(buffer), "%d-%m-%Y", date);
	return std::string(buffer);
}

std::string Range::toString() const {
	return dateToString(m_start, -1) + " <-> " + dateToString(m_end, 1);
}