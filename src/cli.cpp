#include "atlast.hpp"

#include <algorithm>
#include <charconv>
#include <cctype>
#include <iostream>
#include <ranges>
#include <string>
#include <string_view>
#include <vector>

namespace atlast {
namespace {

constexpr int default_result_limit = 10;
constexpr int max_result_limit = 100;

constexpr std::string_view usage = R"(Atlast - local full-text search

Usage:
  atlast index <directory> [--db <database>]
  atlast search <query> [--limit <1-100>] [--db <database>]
  atlast sources [--db <database>]
  atlast forget <directory> [--db <database>]
  atlast --help

Search filters:
  path:<substring>  ext:<extension>  modified:<days>d
)";

struct Options {
    std::string database = "atlast.db";
    int limit = default_result_limit;
};

bool parse_options(int argc, char* argv[], int start, bool allow_limit,
                   Options& options) {
    for (int index = start; index < argc; ++index) {
        const std::string_view option = argv[index];

        if (option == "--db") {
            if (++index == argc) {
                std::cerr << "--db requires a database path.\n";
                return false;
            }
            options.database = argv[index];
            continue;
        }

        if (option == "--limit" && allow_limit) {
            if (++index == argc) {
                std::cerr << "--limit requires a number.\n";
                return false;
            }

            const std::string_view value = argv[index];
            int limit = 0;
            const auto [end, error] =
                std::from_chars(value.data(), value.data() + value.size(), limit);
            if (error != std::errc{} || end != value.data() + value.size() ||
                limit < 1 || limit > max_result_limit) {
                std::cerr << "--limit must be between 1 and "
                          << max_result_limit << ".\n";
                return false;
            }
            options.limit = limit;
            continue;
        }

        std::cerr << "Unknown option: " << option << '\n';
        return false;
    }
    return true;
}

std::vector<std::string> query_tokens(std::string_view input) {
    std::vector<std::string> tokens;
    std::string token;
    bool quoted = false;

    for (const char character : input) {
        if (character == '"') {
            quoted = !quoted;
            token += character;
        } else if (std::isspace(static_cast<unsigned char>(character)) &&
                   !quoted) {
            if (!token.empty()) {
                tokens.push_back(std::move(token));
                token.clear();
            }
        } else {
            token += character;
        }
    }

    if (!token.empty()) {
        tokens.push_back(std::move(token));
    }
    return tokens;
}

std::string filter_value(std::string_view value) {
    if (value.size() >= 2 && value.front() == '"' && value.back() == '"') {
        value.remove_prefix(1);
        value.remove_suffix(1);
    }
    return std::string{value};
}

bool parse_search_request(std::string_view input, SearchRequest& request) {
    bool path_seen = false;
    bool extension_seen = false;

    for (const std::string& token : query_tokens(input)) {
        if (token.starts_with("path:")) {
            if (path_seen ||
                (request.path = filter_value(
                     std::string_view{token}.substr(5))).empty()) {
                std::cerr << "path: requires one non-empty value.\n";
                return false;
            }
            path_seen = true;
            continue;
        }

        if (token.starts_with("ext:")) {
            if (extension_seen) {
                std::cerr << "ext: may only be specified once.\n";
                return false;
            }

            request.extension =
                filter_value(std::string_view{token}.substr(4));
            if (request.extension.starts_with('.')) {
                request.extension.erase(0, 1);
            }
            if (request.extension.empty() ||
                !std::ranges::all_of(
                    request.extension, [](unsigned char character) {
                        return std::isalnum(character);
                    })) {
                std::cerr << "ext: requires a file extension such as py.\n";
                return false;
            }
            std::ranges::transform(
                request.extension, request.extension.begin(),
                [](unsigned char character) {
                    return static_cast<char>(std::tolower(character));
                });
            extension_seen = true;
            continue;
        }

        if (token.starts_with("modified:")) {
            if (request.modified_days) {
                std::cerr << "modified: may only be specified once.\n";
                return false;
            }

            const std::string value =
                filter_value(std::string_view{token}.substr(9));
            int days = 0;
            if (value.size() < 2 ||
                std::tolower(static_cast<unsigned char>(value.back())) != 'd') {
                std::cerr << "modified: requires a duration such as 30d.\n";
                return false;
            }
            const auto [end, error] = std::from_chars(
                value.data(), value.data() + value.size() - 1, days);
            if (error != std::errc{} ||
                end != value.data() + value.size() - 1 || days < 1 ||
                days > 36500) {
                std::cerr
                    << "modified: days must be between 1d and 36500d.\n";
                return false;
            }
            request.modified_days = days;
            continue;
        }

        if (!request.text.empty()) {
            request.text += ' ';
        }
        request.text += token;
    }

    if (request.text.empty()) {
        std::cerr << "Search requires text in addition to filters.\n";
        return false;
    }
    return true;
}

}  // namespace

int run(int argc, char* argv[]) {
    if (argc < 2) {
        std::cerr << usage;
        return 2;
    }

    const std::string_view command = argv[1];
    if (argc == 2 && (command == "--help" || command == "help")) {
        std::cout << usage;
        return 0;
    }

    Options options;
    if (command == "sources") {
        if (!parse_options(argc, argv, 2, false, options)) {
            return 2;
        }
        return list_sources(options.database);
    }

    if (argc < 3) {
        std::cerr << usage;
        return 2;
    }

    if (command == "index") {
        if (!parse_options(argc, argv, 3, false, options)) {
            return 2;
        }
        return index_directory(argv[2], options.database);
    }

    if (command == "search") {
        if (!parse_options(argc, argv, 3, true, options)) {
            return 2;
        }
        SearchRequest request;
        if (!parse_search_request(argv[2], request)) {
            return 2;
        }
        return search(request, options.database, options.limit);
    }

    if (command == "forget") {
        if (!parse_options(argc, argv, 3, false, options)) {
            return 2;
        }
        return forget_directory(argv[2], options.database);
    }

    std::cerr << "Unknown command: " << command << '\n' << usage;
    return 2;
}

}  // namespace atlast
