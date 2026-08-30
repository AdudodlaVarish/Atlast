#include "atlast.hpp"
#include "database.hpp"

#include <sqlite3.h>

#include <chrono>
#include <filesystem>
#include <iostream>
#include <string>
#include <string_view>

namespace atlast {

int search(const SearchRequest& request, std::string_view database_path,
           int limit) {
    database::Connection connection = database::open(database_path);
    if (!connection || !database::ensure_schema(connection.get())) {
        return 1;
    }

    std::string sql = R"sql(
        SELECT d.path,
               snippet(documents_fts, 1, '[', ']', ' ... ', 18)
        FROM documents_fts
        JOIN documents AS d ON d.id = documents_fts.rowid
        WHERE documents_fts MATCH ?
    )sql";
    if (!request.path.empty()) {
        sql += " AND instr(lower(d.path), lower(?)) > 0";
    }
    if (!request.extension.empty()) {
        sql += " AND lower(d.path) LIKE ?";
    }
    if (request.modified_days) {
        sql += " AND d.modified >= ?";
    }
    sql += " ORDER BY bm25(documents_fts), d.path LIMIT ?";

    database::Statement statement{nullptr, sqlite3_finalize};
    if (!database::prepare(connection.get(), sql.c_str(), statement)) {
        return 1;
    }

    int parameter = 1;
    bool bound = database::bind_text(statement.get(), parameter++, request.text);
    if (!request.path.empty()) {
        bound = bound &&
                database::bind_text(statement.get(), parameter++, request.path);
    }
    const std::string extension_pattern = "%." + request.extension;
    if (!request.extension.empty()) {
        bound = bound && database::bind_text(statement.get(), parameter++,
                                              extension_pattern);
    }
    if (request.modified_days) {
        const auto threshold =
            std::filesystem::file_time_type::clock::now() -
            std::chrono::days{*request.modified_days};
        bound =
            bound &&
            sqlite3_bind_int64(
                statement.get(), parameter++,
                static_cast<sqlite3_int64>(threshold.time_since_epoch().count())) ==
                SQLITE_OK;
    }
    bound = bound &&
            sqlite3_bind_int(statement.get(), parameter, limit) == SQLITE_OK;
    if (!bound) {
        std::cerr << "SQLite error: " << sqlite3_errmsg(connection.get())
                  << '\n';
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
