#include "database.hpp"

#include <iostream>
#include <string>

namespace atlast::database {

bool execute(sqlite3* connection, const char* sql) {
    char* error_message = nullptr;
    const int result =
        sqlite3_exec(connection, sql, nullptr, nullptr, &error_message);
    if (result == SQLITE_OK) {
        return true;
    }

    std::cerr << "SQLite error: "
              << (error_message ? error_message : sqlite3_errmsg(connection))
              << '\n';
    sqlite3_free(error_message);
    return false;
}

bool prepare(sqlite3* connection, const char* sql, Statement& statement) {
    sqlite3_stmt* raw_statement = nullptr;
    if (sqlite3_prepare_v2(connection, sql, -1, &raw_statement, nullptr) !=
        SQLITE_OK) {
        std::cerr << "SQLite error: " << sqlite3_errmsg(connection) << '\n';
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

Connection open(std::string_view path) {
    sqlite3* raw_connection = nullptr;
    const std::string filename{path};
    const int result = sqlite3_open_v2(filename.c_str(), &raw_connection,
                                       SQLITE_OPEN_READWRITE |
                                           SQLITE_OPEN_CREATE,
                                       nullptr);
    if (result != SQLITE_OK) {
        std::cerr << "Could not open " << filename << ": "
                  << (raw_connection ? sqlite3_errmsg(raw_connection)
                                     : "out of memory")
                  << '\n';
        sqlite3_close(raw_connection);
        return {nullptr, sqlite3_close};
    }

    sqlite3_busy_timeout(raw_connection, 5000);
    return {raw_connection, sqlite3_close};
}

bool ensure_schema(sqlite3* connection) {
    return execute(connection, R"sql(
        CREATE TABLE IF NOT EXISTS documents (
            id       INTEGER PRIMARY KEY,
            root     TEXT NOT NULL,
            path     TEXT NOT NULL UNIQUE,
            modified INTEGER NOT NULL,
            size     INTEGER NOT NULL,
            content  TEXT NOT NULL
        );

        CREATE TABLE IF NOT EXISTS sources (
            root         TEXT PRIMARY KEY,
            last_indexed INTEGER NOT NULL
        );

        INSERT OR IGNORE INTO sources(root, last_indexed)
        SELECT DISTINCT root, 0 FROM documents;

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

}  // namespace atlast::database
