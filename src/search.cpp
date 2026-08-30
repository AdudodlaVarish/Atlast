#include "atlast.hpp"
#include "database.hpp"

#include <sqlite3.h>

#include <iostream>
#include <string_view>

namespace atlast {

int search(std::string_view query, std::string_view database_path, int limit) {
    if (query.empty()) {
        std::cerr << "Search query cannot be empty.\n";
        return 2;
    }

    database::Connection connection = database::open(database_path);
    if (!connection || !database::ensure_schema(connection.get())) {
        return 1;
    }

    database::Statement statement{nullptr, sqlite3_finalize};
    if (!database::prepare(connection.get(), R"sql(
            SELECT path,
                   snippet(documents_fts, 1, '[', ']', ' ... ', 18)
            FROM documents_fts
            WHERE documents_fts MATCH ?
            ORDER BY bm25(documents_fts), path
            LIMIT ?
        )sql",
                           statement) ||
        !database::bind_text(statement.get(), 1, query) ||
        sqlite3_bind_int(statement.get(), 2, limit) != SQLITE_OK) {
        return 1;
    }

    int count = 0;
    int result = SQLITE_ROW;
    while ((result = sqlite3_step(statement.get())) == SQLITE_ROW) {
        ++count;
        const auto* path = sqlite3_column_text(statement.get(), 0);
        const auto* snippet = sqlite3_column_text(statement.get(), 1);
        std::cout << count << ". "
                  << (path ? reinterpret_cast<const char*>(path) : "") << '\n'
                  << "   "
                  << (snippet ? reinterpret_cast<const char*>(snippet) : "")
                  << '\n';
    }

    if (result != SQLITE_DONE) {
        std::cerr << "Search failed: " << sqlite3_errmsg(connection.get())
                  << '\n';
        return 1;
    }

    if (count == 0) {
        std::cout << "No results.\n";
    }
    return 0;
}

}  // namespace atlast
