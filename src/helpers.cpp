#include "helpers.hpp"

std::string GDateToString(const GDate* date) {
	if (date == nullptr) {
		return "";
	}
	gchar formatted_date[11];
	g_date_strftime(formatted_date, 11, "%d-%m-%Y", date);
	return formatted_date;
}
