#include "atlast.hpp"
#include "database.hpp"

#include <sqlite3.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <ranges>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace fs = std::filesystem;

namespace atlast {
namespace {

constexpr std::uintmax_t max_file_size = 10 * 1024 * 1024;

constexpr std::array supported_extensions{
    ".c",    ".cc",   ".cmake", ".cpp",  ".cs",   ".css",  ".csv",
    ".cxx",  ".go",   ".h",     ".hpp",  ".htm",  ".html", ".hxx",
    ".java", ".js",   ".json",  ".log",  ".md",   ".py",   ".rs",
    ".text", ".toml", ".ts",    ".tsv",  ".txt",  ".xml",  ".yaml",
    ".yml"};

constexpr std::array ignored_directories{
    ".git", ".next", "build", "coverage", "dist", "node_modules"};

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

bool ignored_directory(const fs::path& path) {
    std::string name = path.filename().string();
    std::transform(name.begin(), name.end(), name.begin(),
                   [](unsigned char character) {
                       return static_cast<char>(std::tolower(character));
                   });
    if (std::ranges::find(ignored_directories, name) !=
        ignored_directories.end()) {
        return true;
    }

    std::error_code error;
    return fs::is_regular_file(path / "pyvenv.cfg", error) && !error;
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

bool load_existing(sqlite3* connection, std::string_view root,
                   std::unordered_map<std::string, FileState>& files) {
    database::Statement statement{nullptr, sqlite3_finalize};
    if (!database::prepare(
            connection,
            "SELECT path, modified, size FROM documents WHERE root = ?",
            statement) ||
        !database::bind_text(statement.get(), 1, root)) {
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
        std::cerr << "SQLite error: " << sqlite3_errmsg(connection) << '\n';
        return false;
    }
    return true;
}

}  // namespace

int index_directory(const fs::path& requested_root,
                    std::string_view database_path) {
    std::error_code error;
    if (!fs::is_directory(requested_root, error) || error) {
        std::cerr << "Not a readable directory: " << path_text(requested_root)
                  << '\n';
        return 1;
    }

    const fs::path root_path = normalized_path(requested_root);
    const std::string root = path_text(root_path);
    database::Connection connection = database::open(database_path);
    if (!connection || !database::ensure_schema(connection.get())) {
        return 1;
    }

    std::unordered_map<std::string, FileState> existing;
    if (!load_existing(connection.get(), root, existing) ||
        !database::execute(connection.get(), "BEGIN IMMEDIATE")) {
        return 1;
    }

    database::Statement upsert{nullptr, sqlite3_finalize};
    database::Statement remove{nullptr, sqlite3_finalize};
    database::Statement update_source{nullptr, sqlite3_finalize};
    if (!database::prepare(connection.get(), R"sql(
            INSERT INTO documents(root, path, modified, size, content)
            VALUES (?, ?, ?, ?, ?)
            ON CONFLICT(path) DO UPDATE SET
                root = excluded.root,
                modified = excluded.modified,
                size = excluded.size,
                content = excluded.content
        )sql",
                           upsert) ||
        !database::prepare(connection.get(),
                           "DELETE FROM documents WHERE path = ?", remove) ||
        !database::prepare(connection.get(), R"sql(
            INSERT INTO sources(root, last_indexed)
            VALUES (?, CAST(strftime('%s', 'now') AS INTEGER))
            ON CONFLICT(root) DO UPDATE SET
                last_indexed = excluded.last_indexed
        )sql",
                           update_source)) {
        database::execute(connection.get(), "ROLLBACK");
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
        if (!database::bind_text(remove.get(), 1, path) ||
            sqlite3_step(remove.get()) != SQLITE_DONE) {
            std::cerr << "SQLite error: " << sqlite3_errmsg(connection.get())
                      << '\n';
            return false;
        }
        existing.erase(path);
        ++stats.removed;
        return true;
    };

    try {
        for (fs::recursive_directory_iterator iterator(
                 root_path, fs::directory_options::skip_permission_denied),
             end;
             iterator != end; ++iterator) {
            const fs::directory_entry& entry = *iterator;
            std::error_code entry_error;

            if (entry.is_directory(entry_error)) {
                if (!entry_error && ignored_directory(entry.path())) {
                    iterator.disable_recursion_pending();
                }
                continue;
            }

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
                database::bind_text(upsert.get(), 1, root) &&
                database::bind_text(upsert.get(), 2, path) &&
                sqlite3_bind_int64(upsert.get(), 3, modified_count) == SQLITE_OK &&
                sqlite3_bind_int64(upsert.get(), 4,
                                   static_cast<sqlite3_int64>(size)) == SQLITE_OK &&
                database::bind_text(upsert.get(), 5, *content);

            if (!bound || sqlite3_step(upsert.get()) != SQLITE_DONE) {
                std::cerr << "Could not index " << path << ": "
                          << sqlite3_errmsg(connection.get()) << '\n';
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
                if (!database::bind_text(remove.get(), 1, path) ||
                    sqlite3_step(remove.get()) != SQLITE_DONE) {
                    std::cerr << "SQLite error: "
                              << sqlite3_errmsg(connection.get()) << '\n';
                    database_ok = false;
                    break;
                }
                ++stats.removed;
            }
        }
    }

    if (!database_ok) {
        database::execute(connection.get(), "ROLLBACK");
        return 1;
    }

    if (!database::bind_text(update_source.get(), 1, root) ||
        sqlite3_step(update_source.get()) != SQLITE_DONE) {
        std::cerr << "SQLite error: " << sqlite3_errmsg(connection.get())
                  << '\n';
        database::execute(connection.get(), "ROLLBACK");
        return 1;
    }

    if (!database::execute(connection.get(), "COMMIT")) {
        database::execute(connection.get(), "ROLLBACK");
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

int list_sources(std::string_view database_path) {
    database::Connection connection = database::open(database_path);
    if (!connection || !database::ensure_schema(connection.get())) {
        return 1;
    }

    database::Statement statement{nullptr, sqlite3_finalize};
    if (!database::prepare(connection.get(), R"sql(
            SELECT s.root,
                   count(d.id),
                   CASE WHEN s.last_indexed = 0 THEN 'unknown'
                        ELSE strftime('%Y-%m-%d %H:%M:%S UTC',
                                      s.last_indexed, 'unixepoch')
                   END
            FROM sources AS s
            LEFT JOIN documents AS d ON d.root = s.root
            GROUP BY s.root, s.last_indexed
            ORDER BY s.root
        )sql",
                           statement)) {
        return 1;
    }

    bool found = false;
    int result = SQLITE_ROW;
    while ((result = sqlite3_step(statement.get())) == SQLITE_ROW) {
        found = true;
        std::cout << "Root: " << sqlite3_column_text(statement.get(), 0)
                  << '\n'
                  << "Files: " << sqlite3_column_int64(statement.get(), 1)
                  << '\n'
                  << "Last indexed: " << sqlite3_column_text(statement.get(), 2)
                  << "\n\n";
    }

    if (result != SQLITE_DONE) {
        std::cerr << "SQLite error: " << sqlite3_errmsg(connection.get())
                  << '\n';
        return 1;
    }
    if (!found) {
        std::cout << "No indexed sources.\n";
    }
    return 0;
}

int refresh_sources(std::string_view database_path) {
    std::vector<std::string> roots;
    {
        database::Connection connection = database::open(database_path);
        if (!connection || !database::ensure_schema(connection.get())) {
            return 1;
        }

        database::Statement statement{nullptr, sqlite3_finalize};
        if (!database::prepare(connection.get(),
                               "SELECT root FROM sources ORDER BY root",
                               statement)) {
            return 1;
        }

        int result = SQLITE_ROW;
        while ((result = sqlite3_step(statement.get())) == SQLITE_ROW) {
            const auto* root = sqlite3_column_text(statement.get(), 0);
            roots.emplace_back(reinterpret_cast<const char*>(root));
        }
        if (result != SQLITE_DONE) {
            std::cerr << "SQLite error: " << sqlite3_errmsg(connection.get())
                      << '\n';
            return 1;
        }
    }

    if (roots.empty()) {
        std::cout << "No indexed sources.\n";
        return 0;
    }

    int status = 0;
    for (std::size_t index = 0; index < roots.size(); ++index) {
        if (index != 0) {
            std::cout << '\n';
        }
        if (index_directory(roots[index], database_path) != 0) {
            status = 1;
        }
    }
    return status;
}

int forget_directory(const fs::path& requested_root,
                     std::string_view database_path) {
    const std::string root = path_text(normalized_path(requested_root));
    database::Connection connection = database::open(database_path);
    if (!connection || !database::ensure_schema(connection.get()) ||
        !database::execute(connection.get(), "BEGIN IMMEDIATE")) {
        return 1;
    }

    database::Statement remove_documents{nullptr, sqlite3_finalize};
    database::Statement remove_source{nullptr, sqlite3_finalize};
    if (!database::prepare(connection.get(),
                           "DELETE FROM documents WHERE root = ?",
                           remove_documents) ||
        !database::prepare(connection.get(),
                           "DELETE FROM sources WHERE root = ?", remove_source) ||
        !database::bind_text(remove_documents.get(), 1, root) ||
        sqlite3_step(remove_documents.get()) != SQLITE_DONE) {
        std::cerr << "SQLite error: " << sqlite3_errmsg(connection.get())
                  << '\n';
        database::execute(connection.get(), "ROLLBACK");
        return 1;
    }
    const int removed = sqlite3_changes(connection.get());

    if (!database::bind_text(remove_source.get(), 1, root) ||
        sqlite3_step(remove_source.get()) != SQLITE_DONE) {
        std::cerr << "SQLite error: " << sqlite3_errmsg(connection.get())
                  << '\n';
        database::execute(connection.get(), "ROLLBACK");
        return 1;
    }
    if (sqlite3_changes(connection.get()) == 0) {
        database::execute(connection.get(), "ROLLBACK");
        std::cerr << "Source not indexed: " << root << '\n';
        return 1;
    }
    if (!database::execute(connection.get(), "COMMIT")) {
        database::execute(connection.get(), "ROLLBACK");
        return 1;
    }

    std::cout << "Forgot: " << root << '\n'
              << "Removed: " << removed << '\n';
    return 0;
}

}  // namespace atlast
