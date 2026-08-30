#pragma once

#include <sqlite3.h>

#include <memory>
#include <string_view>

namespace atlast::database {

using Connection = std::unique_ptr<sqlite3, decltype(&sqlite3_close)>;
using Statement = std::unique_ptr<sqlite3_stmt, decltype(&sqlite3_finalize)>;

Connection open(std::string_view path);
bool ensure_schema(sqlite3* connection);
bool execute(sqlite3* connection, const char* sql);
bool prepare(sqlite3* connection, const char* sql, Statement& statement);
bool bind_text(sqlite3_stmt* statement, int index, std::string_view value);

}  // namespace atlast::database
