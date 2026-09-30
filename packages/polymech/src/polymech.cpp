#include "polymech/polymech.h"
#include "logger/logger.h"
#include "postgres/postgres.h"


namespace polymech {

std::string fetch_pages() { return fetch_pages("*"); }

std::string fetch_pages(const std::string &select, const std::string &filter,
                        int limit) {
  logger::debug("polymech::fetch_pages → select=" + select +
                " filter=" + filter + " limit=" + std::to_string(limit));
  return postgres::query("pages", select, filter, limit);
}

} // namespace polymech
