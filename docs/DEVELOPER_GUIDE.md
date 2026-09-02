# Atlast developer guide

This guide is for developers who can read basic code but may be new to C++,
CMake, SQLite, full-text search, or automated testing. Its goal is to make the
entire Atlast MVP understandable without assuming prior systems-programming
experience.

Read the guide in order the first time. Later, use the glossary and function map
as references.

## What Atlast does in one sentence

Atlast reads text files, saves their paths and contents in a local SQLite
database, and asks SQLite FTS5 to find and rank matching files later.

The shortest useful mental model is:

```text
files on disk -> crawler -> SQLite documents table -> FTS5 index -> search results
```

Each arrow represents data moving to the next stage. The source files remain
unchanged. Atlast only reads them.

## Start here: important terminology

### Source code

Source code is human-readable text written in a programming language. Atlast's
source files live under `src/` and are written in C++.

### Compiler

A compiler translates C++ source code into machine instructions the computer
can execute. In the current Windows setup, the compiler is GCC's `g++` program.

Input:

```text
src/main.cpp
src/cli.cpp
src/git.cpp
src/indexer.cpp
src/search.cpp
src/database.cpp
```

Output after compilation and linking:

```text
build/atlast.exe
```

### Build

A build is the complete process of turning source files into an executable. It
usually includes compilation and linking.

### Linker and linking

Atlast calls functions implemented by SQLite, such as `sqlite3_open_v2`. Those
functions are declared in `sqlite3.h`, but their compiled implementation lives
in the SQLite library. The linker connects Atlast's compiled code to that
library.

If compilation knows a function's name but linking cannot find its
implementation, the build fails with an undefined-reference or unresolved-
external-symbol error.

### Dependency

A dependency is code the project uses but does not implement itself. SQLite is
Atlast's only runtime code dependency. CMake and the compiler are build tools,
not libraries shipped as part of Atlast's source.

### CMake

CMake is a build-system generator. It reads `CMakeLists.txt` and creates files
for another build tool, such as MinGW Make or Ninja.

CMake does not normally compile C++ source itself. The flow is:

```text
CMakeLists.txt -> CMake -> generated build files -> Make/Ninja -> compiler
```

### Configure, generate, build, and test

These words describe separate steps:

- **Configure:** CMake checks settings, the compiler, and dependencies.
- **Generate:** CMake writes build-tool files.
- **Build:** the build tool compiles and links Atlast.
- **Test:** CTest runs the already-built executable through test scenarios.

This is why changing C++ code normally requires another build but not always a
manual configure. Changing `CMakeLists.txt` requires CMake to configure again.

### Executable

An executable is a program the operating system can run. On Windows it usually
ends in `.exe`. Atlast's executable is `atlast.exe`.

### CLI

CLI means command-line interface. A CLI program receives text arguments and
prints text results in a terminal.

Example:

```powershell
.\atlast.exe search "database migration" --limit 5
```

Here:

- `atlast.exe` is the program.
- `search` is the command.
- `database migration` is the required query argument.
- `--limit` is an option.
- `5` is the value belonging to that option.

### Argument and option

An argument is a value passed to a program. An option is a named argument that
changes behavior, conventionally beginning with `-` or `--`.

Atlast's required arguments have positions. For example, the directory follows
`index`. Options may follow the required argument.

### Exit code

Every command-line program returns a small integer when it finishes. The shell
or another program can inspect this exit code.

Atlast uses:

- `0` for success.
- `1` for an operational failure.
- `2` for incorrect command usage.

Printing an error and returning an exit code solve different problems. The
message helps a person; the code helps automation.

### Filesystem

The filesystem is the operating system's model for files, paths, and
directories. C++ exposes it through the standard `<filesystem>` library.

### Recursive traversal

Traversal means visiting entries in a directory. Recursive traversal also
enters child directories, their children, and so on.

Atlast uses `std::filesystem::recursive_directory_iterator` to perform this
walk. When it encounters `.git`, `.next`, `build`, `coverage`, `dist`, or
`node_modules`, it calls `disable_recursion_pending()` so the iterator does not
enter that directory. It also checks directories for `pyvenv.cfg`, the standard
marker created by Python's `venv` module. This detects custom names such as
`my_venv` without maintaining a list of every possible environment name.

### Path normalization

Different path text can refer to the same file:

```text
C:\Projects\Atlast\src\main.cpp
C:\Projects\Atlast\src\.\main.cpp
```

Normalization removes avoidable differences and attempts to produce one
absolute representation. This lets a database uniqueness rule recognize the
same file on later indexing runs.

### Metadata

Metadata is information about a file rather than its content. Atlast stores:

- The normalized path.
- The root directory used to discover it.
- Modification time.
- File size.

Atlast compares modification time and size to decide whether reading the file
again is necessary.

### Database

A database stores structured data and supports queries. Atlast uses SQLite,
which keeps the entire database in a local file such as `atlast.db`.

SQLite is embedded: Atlast opens the file directly through a library. There is
no separate database server to install or run.

### Table, row, and column

A table organizes related data:

```text
documents table
+----+----------------------+----------+
| id | path                 | size     |
+----+----------------------+----------+
| 1  | C:/notes/design.md   | 1842     |
| 2  | C:/src/network.cpp   | 9204     |
+----+----------------------+----------+
```

- A **table** is the complete structure, such as `documents`.
- A **row** is one stored item, such as one indexed file.
- A **column** is one property, such as `path` or `size`.

### SQL

SQL is the language used to define and query relational databases. Atlast uses
SQL to create tables, insert documents, remove documents, and search FTS5.

### Schema

A schema describes the database's structure: tables, columns, indexes, and
triggers. The `ensure_schema` function creates Atlast's schema if it does not
already exist.

### Primary key

A primary key uniquely identifies each row. `documents.id` is an integer primary
key generated by SQLite.

### Unique constraint

A unique constraint prevents duplicate values. `documents.path` is unique, so
Atlast stores at most one row for a normalized file path.

### Database index

A database index is an additional data structure that makes particular lookups
faster. It is unrelated to a C++ array index.

Atlast has:

- A normal SQLite index on `documents.root` for fast root lookups.
- An FTS5 index for full-text search.

Indexes cost disk space and update work in exchange for faster reads.

### Full-text search

Full-text search finds documents by words and phrases instead of requiring an
exact match against an entire column.

A plain equality check asks whether the complete value equals a string. A
full-text search can find `timeout` anywhere inside a long source file.

### Search filter

A search filter removes otherwise valid matches that do not meet a predictable
condition. For example, `ext:py` keeps only Python files and `modified:7d` keeps
only files changed during the last seven days.

Atlast parses filters itself instead of asking FTS5 to interpret them. This
keeps path, extension, and date comparisons literal and deterministic.

### Token and tokenization

A token is a searchable unit, usually a word. Tokenization turns text into
tokens.

Simplified example:

```text
Input:  "Connection timed out."
Tokens: connection, timed, out
```

Atlast delegates tokenization to FTS5's `unicode61` tokenizer.

### FTS5

FTS5 is SQLite's full-text-search extension. It supplies:

- Tokenization.
- An efficient inverted index.
- Query parsing.
- BM25 ranking.
- Highlighted result snippets.

Using FTS5 is why the MVP does not need to implement its own search engine.

### Inverted index

An inverted index maps tokens to the documents that contain them. A simplified
index might look like:

```text
connection -> network.cpp, retry.md
migration  -> database.md
timeout    -> network.cpp, operations.log
```

Without this structure, every search would need to read every file again.

### BM25

BM25 is a relevance-ranking formula. It favors documents where query terms are
meaningful and occur with useful frequency while accounting for document
length and how common each term is across the collection.

Atlast asks SQLite's `bm25()` function to order matches. The MVP does not
reimplement the formula.

### Snippet

A snippet is a short piece of matching content displayed beneath a result. FTS5
chooses a nearby passage and Atlast asks it to wrap matched terms in `[` and
`]`.

### Prepared statement

A prepared statement is an SQL command compiled by SQLite before data is
supplied. Placeholder characters such as `?` mark where values belong.

```sql
SELECT path FROM documents WHERE root = ?
```

Atlast then binds a root path to the placeholder. Separating SQL structure from
data prevents file paths and search text from becoming executable SQL.

### Bind

Binding supplies a value to a prepared statement placeholder. Atlast uses
functions such as `sqlite3_bind_text` and `sqlite3_bind_int64`.

### Transaction

A transaction groups database changes. It begins with `BEGIN`, ends
successfully with `COMMIT`, or is canceled with `ROLLBACK`.

Atlast indexes inside one transaction so content-table changes and FTS trigger
changes remain consistent.

### Trigger

A database trigger is SQL that runs automatically after a specified event.
Atlast defines triggers after document insertion, deletion, and update. They
apply the corresponding change to `documents_fts`.

Without those triggers, a file might be updated in `documents` while stale text
remained searchable in FTS5.

### Upsert

Upsert means "insert or update." Atlast inserts a path that is new. If the path
already exists, the unique constraint selects the update behavior instead.

### Incremental indexing

Incremental indexing processes only changes instead of rereading every file.
Atlast recognizes unchanged files by comparing stored size and modification
time.

### Stale record

A stale record describes a file that is no longer present or eligible. Atlast
removes stale records after completing a crawl.

### Unit test and end-to-end test

A unit test checks a small function in isolation. An end-to-end test runs the
real program through a complete user workflow.

Atlast currently has one end-to-end test because the MVP's important risk is the
interaction between crawling, SQLite triggers, incremental updates, and search.
The test checks those pieces together with less test code than many isolated
unit tests would require.

### CTest

CTest is CMake's test runner. CMake registers a test in `CMakeLists.txt`, and
CTest runs it with:

```powershell
ctest --test-dir C:\Atlast\build --output-on-failure
```

## Repository tour

Atlast keeps each runtime responsibility in a focused source file:

```text
CMakeLists.txt
src/atlast.hpp
src/database.hpp
src/main.cpp
src/cli.cpp
src/git.cpp
src/indexer.cpp
src/search.cpp
src/database.cpp
tests/mvp.cmake
README.md
```

The source-file responsibilities are:

- `main.cpp`: process entry point only.
- `cli.cpp`: command-line help, validation, and routing.
- `git.cpp`: on-demand Git history search through the Git CLI.
- `indexer.cpp`: crawling, filtering, file reading, and incremental updates.
- `search.cpp`: FTS5 queries and result formatting.
- `database.cpp`: SQLite connection, schema, statements, and binding.
- `atlast.hpp`: declarations shared by top-level features.
- `database.hpp`: declarations shared by SQLite consumers.

Generated files under `build/` are not source code and should not be edited
manually.

### `CMakeLists.txt`

This file tells CMake how to build and test the project.

```cmake
cmake_minimum_required(VERSION 3.20)
```

The project requires at least CMake 3.20. Requiring a known minimum avoids
silently accepting older behavior that does not understand the configuration.

```cmake
project(Atlast LANGUAGES CXX)
```

This names the project and says it uses C++ only. CMake therefore does not spend
time configuring a C compiler for this target.

```cmake
find_package(SQLite3 REQUIRED)
```

CMake locates SQLite's header and library. `REQUIRED` makes configuration stop
with an error if SQLite is unavailable. Continuing would only move the failure
to compilation or linking and make the message less clear.

```cmake
add_executable(atlast
    src/cli.cpp
    src/database.cpp
    src/git.cpp
    src/indexer.cpp
    src/main.cpp
    src/search.cpp
)
```

This creates an executable target named `atlast` from six implementation
files. A target is CMake's representation of something it can build. Headers do
not need to be listed for compilation because implementation files include them.

```cmake
target_compile_features(atlast PRIVATE cxx_std_23)
```

Atlast needs C++23. `PRIVATE` means this requirement belongs to the executable
and is not being advertised to another target that links Atlast.

```cmake
set_target_properties(atlast PROPERTIES CXX_EXTENSIONS OFF)
```

This requests standard C++23 instead of compiler-specific extensions. Code that
builds because of an accidental GCC-only language extension would be less
portable.

```cmake
target_link_libraries(atlast PRIVATE SQLite::SQLite3)
```

This links SQLite. The imported target also carries SQLite's required include
directory, which lets `#include <sqlite3.h>` work without hard-coded paths.

```cmake
include(CTest)
```

This enables CMake's standard testing support and defines the `BUILD_TESTING`
option.

```cmake
if(BUILD_TESTING)
    add_test(...)
endif()
```

The test is registered only when testing is enabled. CMake passes the exact
path of the built executable and a disposable test directory to
`tests/mvp.cmake`.

## C++ foundations used in the source files

### Header files and `#include`

Header files contain declarations that let one source file use code defined
elsewhere.

```cpp
#include <sqlite3.h>
#include <filesystem>
#include <iostream>
```

Angle brackets mean the header comes from the compiler, standard library, or a
configured dependency rather than from the current source directory.

Project headers use quotes:

```cpp
#include "atlast.hpp"
#include "database.hpp"
```

`#pragma once` at the top of each project header prevents the same declarations
from being processed repeatedly within one translation unit.

### Namespace

A namespace groups names and prevents collisions.

```cpp
namespace fs = std::filesystem;
```

This creates the shorter alias `fs`, allowing `fs::path` instead of repeatedly
writing `std::filesystem::path`.

The unnamed namespace:

```cpp
namespace {
    // implementation
}
```

makes its names private to the `.cpp` file containing it. Other source files
cannot accidentally depend on those internal details.

### `constexpr`

`constexpr` marks values that can be determined at compile time:

```cpp
constexpr int default_result_limit = 10;
```

The value cannot change while Atlast runs. Naming the value also avoids hidden
"magic numbers" in command parsing.

### `std::string` and `std::string_view`

`std::string` owns character data. It allocates and manages memory for that
text.

`std::string_view` is a lightweight view into characters owned elsewhere. It
does not copy them and must not outlive the original data.

Atlast uses `string_view` for command-line arguments and SQL-bound values whose
owners remain alive during the operation. It uses `string` for file content and
normalized path text that Atlast must own.

### `std::filesystem::path`

`fs::path` represents a filesystem path using platform-appropriate rules. It is
better than manually joining strings with `/` or `\\`.

### Struct

A struct groups related values:

```cpp
struct IndexStats {
    std::size_t scanned = 0;
    std::size_t indexed = 0;
};
```

Each `IndexStats` object owns one copy of every member. Default member values
make a newly created statistics object start at zero.

### Pointer

A pointer stores the address of another object. SQLite's C API exposes database
connections as `sqlite3*` and prepared statements as `sqlite3_stmt*`.

`nullptr` means the pointer currently points to nothing.

### Resource

A resource is something that must be released after use, such as an open
database, prepared statement, file, or allocated memory.

SQLite requires:

- `sqlite3_close` for a database connection.
- `sqlite3_finalize` for a prepared statement.
- `sqlite3_free` for an error message allocated by SQLite.

### RAII

RAII means Resource Acquisition Is Initialization. The idea is to connect a
resource's lifetime to a normal C++ object's lifetime, so cleanup occurs
automatically when the object leaves scope.

Atlast defines:

```cpp
using Connection = std::unique_ptr<sqlite3, decltype(&sqlite3_close)>;
using Statement = std::unique_ptr<sqlite3_stmt, decltype(&sqlite3_finalize)>;
```

These standard-library smart pointers call the matching SQLite cleanup function
automatically. This prevents leaks on early returns without creating custom
wrapper classes.

### Scope

Scope is the region where a name exists. Variables inside a function disappear
when the function returns. RAII uses this rule: when a `Statement` disappears,
its destructor finalizes the SQLite statement.

### Function

A function is a named operation with inputs and an output:

```cpp
bool supported_file(const fs::path& path)
```

- `bool` is the return type.
- `supported_file` is the function name.
- `const fs::path& path` is the parameter.

`const` promises not to modify the path. `&` passes a reference instead of
copying the path object.

### `std::optional`

`std::optional<T>` holds either a value of type `T` or no value.

`read_file` returns `std::optional<std::string>`:

- A string means the read succeeded.
- `std::nullopt` means it failed.

This prevents an empty file from being confused with a failed read because an
empty string is a valid file content value.

### Container

A container stores a collection of values. Atlast uses:

- `std::array` for the compile-time extension allowlist.
- `std::unordered_map` to map paths to stored metadata.
- `std::unordered_set` to remember paths discovered during a crawl.

"Unordered" means iteration order is not meaningful, but lookup by key is
usually fast.

### Lambda

A lambda is a small function written where it is needed. `remove_file` is a
lambda because it is used only inside `index_directory` and needs direct access
to that function's statements, statistics, and metadata map.

### Error code and exception

C++ libraries can report failures in different ways.

- `std::error_code` stores an error without throwing.
- An exception interrupts normal control flow and can be caught.

Atlast uses error-code overloads for expected individual file failures. It also
catches `std::filesystem::filesystem_error` around recursive traversal so an
unexpected traversal failure cannot terminate the process.

### Cast

A cast explicitly converts one type to another. Atlast uses casts where C and
C++ APIs represent compatible data with different types, such as SQLite's
unsigned-byte text pointer and C++'s `char` text.

Casts deserve attention during review because an incorrect cast can hide a type
mistake.

## Source and function map

Read these files and functions in execution order:

1. `main.cpp`: `main`
2. `cli.cpp`: `run`, `parse_options`, then `parse_search_request`
3. `indexer.cpp`: `index_directory`, `list_sources`, `refresh_sources`,
   `watch_sources`, `forget_directory`, and their local helpers
4. `search.cpp`: `search`
5. `git.cpp`: `search_git_history`
6. `database.cpp`: `open`, `ensure_schema`, `prepare`, `bind_text`, and
   `execute`

This order follows the program's decisions from user input toward lower-level
details.

### `main`

`main` is the operating system's entry point. Atlast receives:

```cpp
int main(int argc, char* argv[])
```

- `argc` is the number of command-line strings.
- `argv` is the array of those strings.
- `argv[0]` is the executable name.
- `argv[1]` is normally `index`, `search`, `history`, `sources`, `refresh`,
  `watch`, `forget`, or `--help`.
- `argv[2]` is the required directory or query for commands that need one.

`main` performs one handoff:

```cpp
return atlast::run(argc, argv);
```

The operating-system entry point therefore contains no application logic.

### `run`

`run`, in `cli.cpp`, handles help, validates argument counts, parses options,
and routes to the matching top-level function. It does not crawl files, invoke
Git, or write SQL itself. This keeps command routing visible in one place.

### `parse_options`

`parse_options` walks arguments after the required command value.

When it finds `--db`, it requires another argument and stores that path. When a
search command finds `--limit`, `std::from_chars` converts the text to an integer
without exceptions. The function rejects values outside 1 through 100.

The `allow_limit` and `allow_explain` parameters prevent commands from silently
accepting options they do not support.

### `parse_search_request`

`parse_search_request` separates Atlast filters from FTS5 search text. Given:

```text
timeout path:backend ext:py modified:30d
```

it produces a `SearchRequest` containing:

```text
text          = timeout
path          = backend
extension     = py
modified_days = 30
```

The parser keeps quoted FTS phrases together, accepts a quoted path containing
spaces, normalizes extensions to lowercase, validates day ranges, rejects
duplicate filters, and requires at least one full-text term.

`SearchRequest` is a plain struct rather than a class hierarchy because it only
transports four validated values from `cli.cpp` to `search.cpp`.

### `path_text`

`path_text` converts a filesystem path into UTF-8 bytes with forward slashes.
SQLite stores text, so Atlast needs a stable string form for paths.

The `reinterpret_cast` views the `char8_t` UTF-8 bytes as ordinary `char` bytes.
It does not change the bytes.

### `normalized_path`

This helper first tries `weakly_canonical`, which resolves as much of a path as
possible. If that fails, it tries `absolute`. Its final fallback performs only
lexical normalization.

The fallbacks matter because error reporting should still include a useful path
even when part of that path cannot be resolved.

### `supported_file`

This extracts the extension, converts it to lowercase, and searches the fixed
allowlist. The lowercase transformation makes `.CPP` and `.cpp` equivalent.

The lambda receives `unsigned char` because passing a negative signed `char` to
`std::tolower` is invalid for non-ASCII byte values.

### `read_file`

This opens a file in binary mode, allocates exactly the expected number of
bytes, reads them, and confirms the byte count.

Binary mode prevents platform newline translation from making the metadata size
and bytes-read count disagree.

The caller checks the 10 MiB limit before calling this function, so the
allocation is bounded.

### `database::execute`

`database::execute` runs SQL that contains no user-provided values, such as
schema creation and transaction commands. It prints SQLite's allocated error
message and frees that message afterward.

### `database::prepare` and `database::bind_text`

`prepare` compiles SQL into a reusable SQLite statement and puts the raw pointer
inside the RAII `Statement` type.

`bind_text` supplies text to one `?` placeholder. `SQLITE_TRANSIENT` tells
SQLite to copy the bytes before the C++ value can disappear or change.

### `database::open`

This converts the path to text and opens SQLite with read, write, and create
permissions. SQLite creates the file if necessary.

The five-second busy timeout lets a short-lived competing database operation
finish before Atlast reports that the database is locked.

The function returns an empty `Connection` smart pointer on failure and a managed
connection on success.

### `database::ensure_schema`

This creates every required database object with `IF NOT EXISTS`, making it safe
to run whenever Atlast opens a database.

The function creates:

1. `documents`, the authoritative rows.
2. `sources`, one row per indexed root and its last index time.
3. `documents_root`, a normal lookup index.
4. `documents_fts`, the FTS5 search index.
5. An insert trigger.
6. A delete trigger.
7. An update trigger.

It also adds `sources` rows with an unknown timestamp for roots found in a
database created by an older Atlast version.

The FTS table uses `content = 'documents'`, called external-content mode. The
normal table owns the original text while FTS5 owns its search structures.

### `load_existing`

This loads metadata for files belonging to one indexed root into an
`unordered_map`:

```text
path -> {modified time, size}
```

That map makes each later unchanged-file check fast and avoids querying SQLite
once per file.

### `index_directory`

This is the largest function because it owns one complete workflow. Its phases
are:

1. Validate that the requested root is a directory.
2. Normalize the root path.
3. Open the database and ensure its schema.
4. Load previously indexed metadata for the root.
5. Begin a database transaction.
6. Prepare one reusable upsert statement and one reusable delete statement.
7. Recursively visit supported files.
8. Prune dependency and generated directories before entering them, including
   custom-named Python environments detected by `pyvenv.cfg`.
9. Compare metadata and avoid rereading unchanged files.
10. Read and validate changed files.
11. Upsert eligible content.
12. Remove database rows whose files disappeared.
13. Record the root's last index time.
14. Commit or roll back database changes.
15. Print statistics and return an exit code.

Prepared statements are reset and rebound inside the loop rather than prepared
again for every file. Preparing once reduces repeated SQL parsing work.

The `discovered` set answers a specific question after traversal: "Which old
paths were not seen this time?" Those are stale and can be deleted only after a
complete traversal.

When a read fails, the path is still discovered. Therefore, a temporary read
failure does not cause the stale cleanup to delete its older searchable row.

### `list_sources`, `refresh_sources`, `watch_sources`, and `forget_directory`

`list_sources` joins `sources` to `documents`, counts current files per root,
and formats the stored UTC index time with SQLite. `refresh_sources` reads every
stored root, closes that listing connection, then calls `index_directory` for
each root. It continues after a source fails and returns a failure status at the
end. `watch_sources` reruns that same refresh every five seconds until Ctrl+C.
`forget_directory` normalizes the requested root, then deletes its document and
source rows in one transaction. Existing delete triggers remove the
corresponding FTS entries; none of these functions modifies the original files.

### `search`

The search flow is:

1. Receive a validated `SearchRequest` from the CLI.
2. Open the database and ensure its schema.
3. Start with the fixed FTS query.
4. Append trusted SQL clauses only for filters that are present.
5. Bind search text, filter values, timestamp threshold, and result limit.
6. Step through matching rows.
7. Print each normalized path and highlighted snippet.
8. With `--explain`, print row, root, metadata version, query, and BM25 score.
9. Print `No results.` when no rows match.

The SQL text is assembled only from fixed strings written in `search.cpp`.
User-provided values always occupy `?` placeholders and are bound separately.
The path check uses a case-insensitive literal substring comparison, extension
matching checks the filename ending, and the modified filter compares stored
filesystem-clock values with a threshold calculated from the current time.

SQLite returns one row each time `sqlite3_step` returns `SQLITE_ROW`. It returns
`SQLITE_DONE` when iteration is complete. Any other result indicates an error.

The SQL orders first by `bm25(documents_fts)` and then by path. The path order
makes ties deterministic rather than dependent on internal database order.

### `search_git_history`

This function validates the repository path, escapes the query as a literal Git
regular expression, safely quotes shell arguments on POSIX systems, and reads
`git log -G` output through a pipe. Git remains the owner of commit and blob
storage; Atlast does not duplicate history in SQLite. The command currently
requires Linux, macOS, or WSL.

## Understanding the test

`tests/mvp.cmake` is a CMake script, not a second C++ program. It uses CMake's
built-in file and process commands to test the real executable.

### Test isolation

The script receives `TEST_DIRECTORY` inside the build directory. It deletes and
recreates only that directory, then writes small sample files there.

Isolation means the test does not depend on or modify a developer's documents.
It also means every run begins from the same state.

### `run_atlast`

The helper function runs the executable, captures standard output, standard
error, and exit code, then compares the actual exit code with the expected one.

If they differ, `message(FATAL_ERROR ...)` fails the CTest test and prints the
captured diagnostics.

### Assertions

An assertion checks that observed behavior matches an expectation. The CMake
script uses `if` conditions and regular-expression matching as assertions.

Examples include checking that:

- The first run reports two indexed files.
- Search output names `network.txt`.
- Explained output includes provenance and BM25 details.
- The unsupported `.bin` file does not appear.
- Files inside ignored dependency and generated directories do not appear.
- Files inside a custom-named Python virtual environment do not appear.
- Path and extension filters both accept and reject the expected documents.
- A recent-modification filter finds a newly created fixture.
- Combined filters apply all conditions together.
- Invalid and filter-only searches return exit code `2`.
- The next run reports unchanged files.
- Old content disappears after replacement.
- New content becomes searchable.
- Refresh updates both registered source directories.
- A deleted file contributes to the removal count.
- An invalid limit returns exit code `2`.
- Git history search finds a committed test marker when Git is installed.

This test targets observable behavior instead of internal function details. A
future refactor can reorganize source files without rewriting the test if CLI
behavior remains correct.

## Follow one file through the system

Suppose this file exists:

```text
C:\Notes\network.txt
```

with content:

```text
The upload failed because of a connection timeout.
```

Running:

```powershell
.\atlast.exe index C:\Notes
```

causes this sequence:

1. `main` recognizes the `index` command.
2. `parse_options` keeps the default database path unless `--db` is present.
3. `index_directory` validates and normalizes `C:\Notes`.
4. `database::open` opens or creates `atlast.db`.
5. `database::ensure_schema` creates missing tables, FTS structures, and
   triggers.
6. The recursive iterator reaches `network.txt`.
7. `supported_file` accepts `.txt`.
8. Atlast reads metadata and sees whether the stored row is current.
9. `read_file` loads the bytes because the file is new or changed.
10. The null-byte check accepts it as text.
11. The upsert writes the `documents` row.
12. SQLite's insert or update trigger changes `documents_fts`.
13. The transaction commits.

Later, running:

```powershell
.\atlast.exe search "connection timeout"
```

causes:

1. `main` recognizes `search`.
2. `search` binds `connection timeout` to the FTS `MATCH` placeholder.
3. FTS5 consults its inverted index instead of reopening source files.
4. BM25 determines result order.
5. `snippet` selects matching context and adds brackets.
6. Atlast prints the path and snippet.

This is the complete MVP data lifecycle.

## How to make a safe change

Use this loop for every code change:

1. State the behavior you want in one sentence.
2. Find the smallest function responsible for that behavior.
3. Add or adjust an end-to-end assertion first when practical.
4. Make the smallest source change.
5. Build.
6. Run CTest with failure output enabled.
7. Manually run the changed command once if output formatting matters.
8. Update documentation when the command, limit, schema, or behavior changed.

Commands:

```powershell
cmake --build C:\Atlast\build --parallel
ctest --test-dir C:\Atlast\build --output-on-failure
```

Do not edit files under `build/`. They are generated and can be deleted at any
time.

## Good first exercises

These exercises help a new developer learn the code without requiring a new
architecture.

### Exercise 1: add a supported extension

Add one text extension to `supported_extensions`, add a sample file with that
extension to `tests/mvp.cmake`, and verify it is indexed.

This teaches arrays, extension filtering, and end-to-end tests.

### Exercise 2: change the default result limit

Change `default_result_limit`, update or extend the test so more documents match
one query, and verify the count.

This teaches constants, option defaults, SQL limits, and behavior tests.

### Exercise 3: add a `--version` command

Handle `--version` beside `--help`, print a compile-time version string, and add
a CTest assertion.

This teaches command routing and exit codes without touching storage.

### Exercise 4: improve one error message

Choose an operational error, include the relevant path, and manually trigger
the condition. Keep the same exit-code meaning.

This teaches trust-boundary diagnostics and the difference between human and
machine error reporting.

## Changes that are not beginner exercises

Avoid these until the current flow is comfortable:

- Changing triggers or external-content FTS behavior.
- Adding concurrency inside the index transaction.
- Following symbolic links.
- Replacing prepared statements with generated SQL.
- Introducing plugin interfaces.
- Adding embeddings or approximate-nearest-neighbor libraries.
- Splitting every helper into a separate class or source file.

These changes affect correctness, data consistency, security, or architecture
and deserve design discussion plus stronger tests.

## Code review checklist

Before accepting a change, ask:

- Does it preserve source files and avoid writing outside the selected database?
- Are all user values bound to SQL placeholders?
- Can every opened SQLite resource be cleaned up on early return?
- Does an indexing failure preserve older useful data when appropriate?
- Can incomplete traversal accidentally delete unvisited records?
- Is a file-size or allocation boundary still enforced?
- Does the CLI return `2` for usage errors and `1` for operational errors?
- Is the new behavior covered by the end-to-end test?
- Does the README or this guide need an update?
- Is the new abstraction or dependency solving a current, demonstrated need?

## Glossary quick reference

| Term | Simple meaning |
| --- | --- |
| Argument | A value passed to a program. |
| Bind | Supply data to an SQL placeholder. |
| BM25 | A formula used to rank text-search results. |
| Build | Turn source code into an executable. |
| CMake | Tool that generates files for a build tool. |
| CLI | Text interface used from a terminal. |
| Column | One property stored for every table row. |
| Commit | Finish and save a database transaction. |
| Compiler | Program that translates C++ into machine code. |
| CTest | CMake's test runner. |
| Database | Structured data stored and queried together. |
| Dependency | External code used by the project. |
| Exit code | Number reporting command success or failure. |
| Filter | Condition that narrows otherwise valid search results. |
| FTS5 | SQLite's full-text-search extension. |
| Index | Data structure that speeds up lookups. |
| Incremental | Processing changes rather than everything again. |
| Linker | Tool that connects compiled code to libraries. |
| Metadata | File information such as path, time, and size. |
| MVP | Smallest complete product proving the core workflow. |
| Normalize | Convert equivalent paths to a consistent form. |
| Prepared statement | SQL compiled separately from its data values. |
| Primary key | Value uniquely identifying a table row. |
| Query | Request for matching database information. |
| RAII | C++ technique for automatic resource cleanup. |
| Recursive | Repeating through nested directories. |
| Rollback | Cancel changes in a database transaction. |
| Row | One stored record in a table. |
| Schema | Definition of a database's structure. |
| Snippet | Short matching passage shown in a result. |
| SQL | Language used to define and query relational data. |
| Table | Named collection of rows and columns. |
| Token | Searchable unit, usually a word. |
| Transaction | Group of database changes handled together. |
| Trigger | SQL run automatically after a table event. |
| Upsert | Insert a new row or update the existing one. |

## When you understand the codebase

You are ready to contribute independently when you can explain:

1. How CMake finds and links SQLite.
2. How `main` chooses between indexing and searching.
3. Why paths are normalized before storage.
4. How Atlast detects unchanged files.
5. Why unreadable old files are retained rather than deleted.
6. How `documents` and `documents_fts` differ.
7. Why triggers are necessary.
8. How prepared statements protect SQL structure.
9. How BM25 and snippets are delegated to FTS5.
10. What the end-to-end test proves.
11. Which failures return exit code `1` versus `2`.
12. Which current limitations are deliberate MVP boundaries.

If any answer is unclear, follow one sample file through the lifecycle again,
then read the corresponding function. That path connects every major part of
the system without requiring you to memorize the code.
