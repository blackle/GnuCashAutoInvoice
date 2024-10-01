#pragma once
#include <glib.h>
#include <optional>
#include <string>

class Range {
public:
	// null means ±infinity (- for start, + for end)
	Range(GDate* start, GDate* end, float price);
	Range(const Range& other);
	~Range();

	// Calculate intersection and return a new Range with the current price
	std::optional<Range> intersect(const Range& range) const;

	//-1 represents infinity here
	int numDays() const;

	//how much does this overlap with the passed range?
	float overlapPercentage(const Range& range) const;

	// Create a Range for a specific month
	static Range createForMonth(GDate* firstDayOfMonth, float price);

	// Create a Range from start and end GDate pointers
	static Range createFromStartAndEnd(GDate* start, GDate* end, float price);

	void setPrice(float price);
	float getPrice() const;

	GDate* GetStart() const { return m_start; }
	GDate* GetEnd() const { return m_end; }

	std::string toString() const;

private:
	GDate* m_start = nullptr;
	GDate* m_end = nullptr;
	float m_price;
};