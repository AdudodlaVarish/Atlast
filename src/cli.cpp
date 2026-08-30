#include "atlast.hpp"

#include <charconv>
#include <iostream>
#include <string>
#include <string_view>

namespace atlast {
namespace {

constexpr int default_result_limit = 10;
constexpr int max_result_limit = 100;

constexpr std::string_view usage = R"(Atlast - local full-text search

Usage:
  atlast index <directory> [--db <database>]
  atlast search <query> [--limit <1-100>] [--db <database>]
  atlast --help
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

}  // namespace

int run(int argc, char* argv[]) {
    if (argc == 2 &&
        (std::string_view{argv[1]} == "--help" ||
         std::string_view{argv[1]} == "help")) {
        std::cout << usage;
        return 0;
    }

    if (argc < 3) {
        std::cerr << usage;
        return 2;
    }

    const std::string_view command = argv[1];
    Options options;

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
        return search(argv[2], options.database, options.limit);
    }

    std::cerr << "Unknown command: " << command << '\n' << usage;
    return 2;
}

}  // namespace atlast
