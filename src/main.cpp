#include <sqlite3.h>

#include <algorithm>
#include <array>
#include <charconv>
#include <cctype>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <optional>
#include <ranges>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>

namespace fs = std::filesystem;

namespace {

constexpr std::uintmax_t max_file_size = 10 * 1024 * 1024;
constexpr int default_result_limit = 10;
constexpr int max_result_limit = 100;

constexpr std::string_view usage = R"(Atlast - local full-text search

Usage:
  atlast index <directory> [--db <database>]
  atlast search <query> [--limit <1-100>] [--db <database>]
  atlast --help
)";

constexpr std::array supported_extensions{
    ".c",    ".cc",   ".cmake", ".cpp",  ".cs",   ".css",  ".csv",
    ".cxx",  ".go",   ".h",     ".hpp",  ".htm",  ".html", ".hxx",
    ".java", ".js",   ".json",  ".log",  ".md",   ".py",   ".rs",
    ".text", ".toml", ".ts",    ".tsv",  ".txt",  ".xml",  ".yaml",
    ".yml"};

using Database = std::unique_ptr<sqlite3, decltype(&sqlite3_close)>;
using Statement = std::unique_ptr<sqlite3_stmt, decltype(&sqlite3_finalize)>;

struct Options {
    fs::path database = "atlast.db";
    int limit = default_result_limit;
};

struct FileState {
    std::int64_t modified;
    std::int64_t size;
};

struct IndexStats {
    std::size_t scanned = 0;
    std::size_t indexed = 0;
    std::size_t unchanged = 0;
    std::size_t removed = 0;
    std::size_t skipped = 0;
    std::size_t failed = 0;
};

std::string path_text(const fs::path& path) {
    const auto utf8 = path.generic_u8string();
    return {reinterpret_cast<const char*>(utf8.data()), utf8.size()};
}

fs::path normalized_path(const fs::path& path) {
    std::error_code error;
    fs::path result = fs::weakly_canonical(path, error);
    if (!error) {
        return result;
    }

    error.clear();
    result = fs::absolute(path, error);
    return error ? path.lexically_normal() : result.lexically_normal();
}

bool supported_file(const fs::path& path) {
    std::string extension = path.extension().string();
    std::transform(extension.begin(), extension.end(), extension.begin(),
                   [](unsigned char character) {
                       return static_cast<char>(std::tolower(character));
                   });
    return std::ranges::find(supported_extensions, extension) !=
           supported_extensions.end();
}

std::optional<std::string> read_file(const fs::path& path,
                                     std::uintmax_t size) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        return std::nullopt;
    }

    std::string content(static_cast<std::size_t>(size), '\0');
    input.read(content.data(), static_cast<std::streamsize>(content.size()));
    if (static_cast<std::size_t>(input.gcount()) != content.size()) {
        return std::nullopt;
    }
    return content;
}

bool run_sql(sqlite3* database, const char* sql) {
    char* error_message = nullptr;
    const int result =
        sqlite3_exec(database, sql, nullptr, nullptr, &error_message);
    if (result == SQLITE_OK) {
        return true;
    }

    std::cerr << "SQLite error: "
              << (error_message ? error_message : sqlite3_errmsg(database))
              << '\n';
    sqlite3_free(error_message);
    return false;
}

bool prepare(sqlite3* database, const char* sql, Statement& statement) {
    sqlite3_stmt* raw_statement = nullptr;
    if (sqlite3_prepare_v2(database, sql, -1, &raw_statement, nullptr) !=
        SQLITE_OK) {
        std::cerr << "SQLite error: " << sqlite3_errmsg(database) << '\n';
        return false;
    }
    statement.reset(raw_statement);
    return true;
}

bool bind_text(sqlite3_stmt* statement, int index, std::string_view value) {
    return sqlite3_bind_text(statement, index, value.data(),
                             static_cast<int>(value.size()), SQLITE_TRANSIENT) ==
           SQLITE_OK;
}

Database open_database(const fs::path& path) {
    sqlite3* raw_database = nullptr;
    const std::string filename = path_text(path);
    const int result = sqlite3_open_v2(filename.c_str(), &raw_database,
                                       SQLITE_OPEN_READWRITE |
                                           SQLITE_OPEN_CREATE,
                                       nullptr);
    if (result != SQLITE_OK) {
        std::cerr << "Could not open " << filename << ": "
                  << (raw_database ? sqlite3_errmsg(raw_database)
                                   : "out of memory")
                  << '\n';
        sqlite3_close(raw_database);
        return {nullptr, sqlite3_close};
    }

    sqlite3_busy_timeout(raw_database, 5000);
    return {raw_database, sqlite3_close};
}

bool ensure_schema(sqlite3* database) {
    return run_sql(database, R"sql(
        CREATE TABLE IF NOT EXISTS documents (
            id       INTEGER PRIMARY KEY,
            root     TEXT NOT NULL,
            path     TEXT NOT NULL UNIQUE,
            modified INTEGER NOT NULL,
            size     INTEGER NOT NULL,
            content  TEXT NOT NULL
        );

        CREATE INDEX IF NOT EXISTS documents_root ON documents(root);

        CREATE VIRTUAL TABLE IF NOT EXISTS documents_fts USING fts5(
            path,
            content,
            content = 'documents',
            content_rowid = 'id',
            tokenize = 'unicode61'
        );

        CREATE TRIGGER IF NOT EXISTS documents_insert
        AFTER INSERT ON documents BEGIN
            INSERT INTO documents_fts(rowid, path, content)
            VALUES (new.id, new.path, new.content);
        END;

        CREATE TRIGGER IF NOT EXISTS documents_delete
        AFTER DELETE ON documents BEGIN
            INSERT INTO documents_fts(documents_fts, rowid, path, content)
            VALUES ('delete', old.id, old.path, old.content);
        END;

        CREATE TRIGGER IF NOT EXISTS documents_update
        AFTER UPDATE ON documents BEGIN
            INSERT INTO documents_fts(documents_fts, rowid, path, content)
            VALUES ('delete', old.id, old.path, old.content);
            INSERT INTO documents_fts(rowid, path, content)
            VALUES (new.id, new.path, new.content);
        END;
    )sql");
}

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

bool load_existing(sqlite3* database, std::string_view root,
                   std::unordered_map<std::string, FileState>& files) {
    Statement statement{nullptr, sqlite3_finalize};
    if (!prepare(database,
                 "SELECT path, modified, size FROM documents WHERE root = ?",
                 statement) ||
        !bind_text(statement.get(), 1, root)) {
        return false;
    }

    int result = SQLITE_ROW;
    while ((result = sqlite3_step(statement.get())) == SQLITE_ROW) {
        const auto* path = sqlite3_column_text(statement.get(), 0);
        files.emplace(reinterpret_cast<const char*>(path),
                      FileState{sqlite3_column_int64(statement.get(), 1),
                                sqlite3_column_int64(statement.get(), 2)});
    }

    if (result != SQLITE_DONE) {
        std::cerr << "SQLite error: " << sqlite3_errmsg(database) << '\n';
        return false;
    }
    return true;
}

int index_directory(const fs::path& requested_root,
                    const fs::path& database_path) {
    std::error_code error;
    if (!fs::is_directory(requested_root, error) || error) {
        std::cerr << "Not a readable directory: " << path_text(requested_root)
                  << '\n';
        return 1;
    }

    const fs::path root_path = normalized_path(requested_root);
    const std::string root = path_text(root_path);
    Database database = open_database(database_path);
    if (!database || !ensure_schema(database.get())) {
        return 1;
    }

    std::unordered_map<std::string, FileState> existing;
    if (!load_existing(database.get(), root, existing) ||
        !run_sql(database.get(), "BEGIN IMMEDIATE")) {
        return 1;
    }

    Statement upsert{nullptr, sqlite3_finalize};
    Statement remove{nullptr, sqlite3_finalize};
    if (!prepare(database.get(), R"sql(
            INSERT INTO documents(root, path, modified, size, content)
            VALUES (?, ?, ?, ?, ?)
            ON CONFLICT(path) DO UPDATE SET
                root = excluded.root,
                modified = excluded.modified,
                size = excluded.size,
                content = excluded.content
        )sql",
                 upsert) ||
        !prepare(database.get(), "DELETE FROM documents WHERE path = ?",
                 remove)) {
        run_sql(database.get(), "ROLLBACK");
        return 1;
    }

    IndexStats stats;
    std::unordered_set<std::string> discovered;
    bool traversal_complete = true;
    bool database_ok = true;

    const auto remove_file = [&](const std::string& path) {
        if (!existing.contains(path)) {
            return true;
        }
        sqlite3_reset(remove.get());
        sqlite3_clear_bindings(remove.get());
        if (!bind_text(remove.get(), 1, path) ||
            sqlite3_step(remove.get()) != SQLITE_DONE) {
            std::cerr << "SQLite error: " << sqlite3_errmsg(database.get())
                      << '\n';
            return false;
        }
        existing.erase(path);
        ++stats.removed;
        return true;
    };

    try {
        for (const fs::directory_entry& entry : fs::recursive_directory_iterator(
                 root_path, fs::directory_options::skip_permission_denied)) {
            std::error_code entry_error;
            if (!entry.is_regular_file(entry_error) || entry_error ||
                !supported_file(entry.path())) {
                continue;
            }

            ++stats.scanned;
            const std::string path = path_text(normalized_path(entry.path()));
            discovered.insert(path);

            const std::uintmax_t size = entry.file_size(entry_error);
            if (entry_error) {
                std::cerr << "Could not inspect: " << path << '\n';
                ++stats.failed;
                continue;
            }

            const fs::file_time_type modified = entry.last_write_time(entry_error);
            if (entry_error) {
                std::cerr << "Could not inspect: " << path << '\n';
                ++stats.failed;
                continue;
            }

            if (size > max_file_size) {
                ++stats.skipped;
                database_ok = remove_file(path);
                if (!database_ok) {
                    break;
                }
                continue;
            }

            const auto modified_count = static_cast<std::int64_t>(
                modified.time_since_epoch().count());
            const auto found = existing.find(path);
            if (found != existing.end() &&
                found->second.modified == modified_count &&
                found->second.size == static_cast<std::int64_t>(size)) {
                ++stats.unchanged;
                continue;
            }

            std::optional<std::string> content = read_file(entry.path(), size);
            if (!content) {
                std::cerr << "Could not read: " << path << '\n';
                ++stats.failed;
                continue;
            }

            if (content->find('\0') != std::string::npos) {
                ++stats.skipped;
                database_ok = remove_file(path);
                if (!database_ok) {
                    break;
                }
                continue;
            }

            sqlite3_reset(upsert.get());
            sqlite3_clear_bindings(upsert.get());
            const bool bound =
                bind_text(upsert.get(), 1, root) &&
                bind_text(upsert.get(), 2, path) &&
                sqlite3_bind_int64(upsert.get(), 3, modified_count) == SQLITE_OK &&
                sqlite3_bind_int64(upsert.get(), 4,
                                   static_cast<sqlite3_int64>(size)) == SQLITE_OK &&
                bind_text(upsert.get(), 5, *content);

            if (!bound || sqlite3_step(upsert.get()) != SQLITE_DONE) {
                std::cerr << "Could not index " << path << ": "
                          << sqlite3_errmsg(database.get()) << '\n';
                database_ok = false;
                break;
            }

            existing[path] = {modified_count, static_cast<std::int64_t>(size)};
            ++stats.indexed;
        }
    } catch (const fs::filesystem_error& exception) {
        std::cerr << "Directory traversal stopped: " << exception.what() << '\n';
        traversal_complete = false;
        ++stats.failed;
    }

    if (database_ok && traversal_complete) {
        for (const auto& [path, state] : existing) {
            static_cast<void>(state);
            if (!discovered.contains(path)) {
                sqlite3_reset(remove.get());
                sqlite3_clear_bindings(remove.get());
                if (!bind_text(remove.get(), 1, path) ||
                    sqlite3_step(remove.get()) != SQLITE_DONE) {
                    std::cerr << "SQLite error: "
                              << sqlite3_errmsg(database.get()) << '\n';
                    database_ok = false;
                    break;
                }
                ++stats.removed;
            }
        }
    }

    if (!database_ok) {
        run_sql(database.get(), "ROLLBACK");
        return 1;
    }

    if (!run_sql(database.get(), "COMMIT")) {
        run_sql(database.get(), "ROLLBACK");
        return 1;
    }

    std::cout << "Root: " << root << '\n'
              << "Scanned: " << stats.scanned << '\n'
              << "Indexed: " << stats.indexed << '\n'
              << "Unchanged: " << stats.unchanged << '\n'
              << "Removed: " << stats.removed << '\n'
              << "Skipped: " << stats.skipped << '\n'
              << "Failed: " << stats.failed << '\n';
    return stats.failed == 0 ? 0 : 1;
}

int search(std::string_view query, const Options& options) {
    if (query.empty()) {
        std::cerr << "Search query cannot be empty.\n";
        return 2;
    }

    Database database = open_database(options.database);
    if (!database || !ensure_schema(database.get())) {
        return 1;
    }

    Statement statement{nullptr, sqlite3_finalize};
    if (!prepare(database.get(), R"sql(
            SELECT path,
                   snippet(documents_fts, 1, '[', ']', ' ... ', 18)
            FROM documents_fts
            WHERE documents_fts MATCH ?
            ORDER BY bm25(documents_fts), path
            LIMIT ?
        )sql",
                 statement) ||
        !bind_text(statement.get(), 1, query) ||
        sqlite3_bind_int(statement.get(), 2, options.limit) != SQLITE_OK) {
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
        std::cerr << "Search failed: " << sqlite3_errmsg(database.get()) << '\n';
        return 1;
    }

    if (count == 0) {
        std::cout << "No results.\n";
    }
    return 0;
}

}  // namespace

int main(int argc, char* argv[]) {
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
        return search(argv[2], options);
    }

    std::cerr << "Unknown command: " << command << '\n' << usage;
    return 2;
}
