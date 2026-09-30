#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace media::str_san {

/** Sentinel userId for unauthenticated / anonymous (matches ref DefaultSanitizers). */
inline constexpr std::string_view k_anonymous_user_id  = "anonymous";
/** Sentinel userId for any authenticated user. */
inline constexpr std::string_view k_authenticated_user_id = "authenticated";

std::string clean_path(std::string_view raw);
std::vector<std::string> path_segments(std::string_view raw);
std::string normalise_path(std::string_view raw);

/**
 * @param err  Set if invalid: empty / invalid UUID
 * @return Normalised (lowercased) UUID, or empty if `err` set
 */
std::string clean_uuid(std::string_view raw, std::string &err);
bool        is_uuid(std::string_view raw);
std::string clean_id(std::string_view raw, std::string &err);
std::string clean_group_name(std::string_view raw, std::string &err);
std::string clean_permission(std::string_view raw, std::string &err);
std::vector<std::string> clean_permissions(const std::vector<std::string> &raw, std::string &err);

/** Reject empty/whitespace-only. */
std::string assert_non_empty_str(std::string_view value, std::string_view label, std::string &err);
bool       assert_non_empty_list(const std::vector<std::string> &values, std::string_view label, std::string &err);

} // namespace media::str_san
