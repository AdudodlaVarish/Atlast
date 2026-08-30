# Atlast

Atlast is a local, persistent full-text search tool for source code, notes,
configuration files, logs, and other text files. It recursively indexes a
directory into a local SQLite database and searches that index from the command
line.

This repository contains the minimum viable product (MVP). The MVP proves the
complete search loop:

1. Discover supported text files beneath a directory.
2. Detect new, changed, unchanged, and deleted files.
3. Store file metadata and content in SQLite.
4. Maintain an SQLite FTS5 full-text index.
5. Run ranked searches and display highlighted snippets.

Atlast does not require an account, server, network connection, hosted model,
or external search service. The indexed content remains in the database file
you choose.

New developers should read [docs/DEVELOPER_GUIDE.md](docs/DEVELOPER_GUIDE.md)
after completing the Quick start. It explains the terminology, C++ features,
database design, runtime flow, build file, and test script from first principles.

## MVP scope

The current release provides:

- Recursive directory indexing.
- A fixed allowlist of common text and source-code extensions.
- Persistent SQLite storage.
- SQLite FTS5 tokenization and full-text search.
- BM25 relevance ranking supplied by FTS5.
- Highlighted snippets around matching terms.
- Incremental indexing based on file modification time and size.
- Replacement of changed content.
- Removal of records for deleted files.
- Detection and exclusion of likely binary files.
- A configurable database path.
- A configurable search-result limit.
- One automated end-to-end test covering the complete workflow.

The MVP intentionally does not include PDF extraction, semantic embeddings,
Git history, filesystem watching, a daemon, a TUI, an HTTP API, plugins, or a
custom search index. Those features would not improve the proof that the core
index-and-search loop works.

## Requirements

Building Atlast requires:

- A C++23 compiler.
- CMake 3.20 or newer.
- SQLite 3 with FTS5 enabled.
- A build tool supported by CMake, such as MinGW Make, Ninja, or Make.

The current Windows development environment uses:

- GCC from MSYS2 UCRT64.
- CMake from MSYS2 UCRT64.
- MinGW Makefiles.
- SQLite from MSYS2 UCRT64.

SQLite has included FTS5 in standard builds for years, but distributors can
still compile SQLite without it. If FTS5 is absent, schema creation fails with
a clear SQLite error instead of creating a database that cannot search.

## Build instructions

### Windows with MSYS2 UCRT64

Install the compiler, CMake, Make, and SQLite development package if they are
not already installed:

```bash
pacman -S --needed mingw-w64-ucrt-x86_64-gcc \
  mingw-w64-ucrt-x86_64-cmake \
  mingw-w64-ucrt-x86_64-make \
  mingw-w64-ucrt-x86_64-sqlite3
```

Make sure `C:\msys64\ucrt64\bin` is on `PATH`, then configure and build from
PowerShell:

```powershell
cmake -S C:\Atlast -B C:\Atlast\build `
  -G "MinGW Makefiles" `
  -DCMAKE_BUILD_TYPE=Debug `
  -DBUILD_TESTING=ON

cmake --build C:\Atlast\build --parallel
```

The executable is produced at:

```text
C:\Atlast\build\atlast.exe
```

For an optimized build, use a separate build directory:

```powershell
cmake -S C:\Atlast -B C:\Atlast\build-release `
  -G "MinGW Makefiles" `
  -DCMAKE_BUILD_TYPE=Release `
  -DBUILD_TESTING=ON

cmake --build C:\Atlast\build-release --parallel
```

Keeping Debug and Release outputs in separate directories prevents one
configuration from silently replacing the other.

### Linux or macOS

Install a C++23 compiler, CMake, and SQLite development files using the system
package manager. Exact package names vary by distribution. Then run:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug -DBUILD_TESTING=ON
cmake --build build --parallel
```

If CMake cannot locate SQLite, confirm that both `sqlite3.h` and the SQLite
library are installed. The SQLite command-line program alone is not always
enough; the development headers and link library are also required.

## Quick start

Index a directory into the default `atlast.db` database:

```powershell
cd C:\Atlast\build
.\atlast.exe index C:\Users\you\Documents
```

Search the database:

```powershell
.\atlast.exe search "database migration"
```

Use an explicit database path when the index should live somewhere else:

```powershell
.\atlast.exe index C:\Projects --db C:\Indexes\projects.db
.\atlast.exe search "connection timeout" --db C:\Indexes\projects.db
```

The `--db` option must be used consistently. Indexing one database and then
searching another correctly produces no results because they are independent
indexes.

## Command reference

### Help

```text
atlast --help
atlast help
```

Prints command usage and exits successfully without opening a database.

### Index

```text
atlast index <directory> [--db <database>]
```

Arguments and options:

- `<directory>` is the directory to scan recursively.
- `--db <database>` selects the SQLite database file. It defaults to
  `atlast.db` in the current working directory.

Example:

```powershell
.\atlast.exe index C:\Projects\Atlast --db C:\Indexes\atlast-source.db
```

An indexing run prints these counters:

```text
Root: C:/Projects/Atlast
Scanned: 42
Indexed: 5
Unchanged: 36
Removed: 1
Skipped: 0
Failed: 0
```

Their meanings are:

- `Root`: normalized absolute directory recorded for this indexing run.
- `Scanned`: supported regular files considered by the crawler.
- `Indexed`: new or changed files written to the database.
- `Unchanged`: files skipped because stored modification time and size still
  match the filesystem.
- `Removed`: old records deleted because a file disappeared or became
  ineligible for indexing.
- `Skipped`: files rejected because they exceed the size limit or contain a
  null byte and therefore appear binary.
- `Failed`: files or directories that could not be inspected or read.

The command returns a nonzero status when any file operation fails. Successfully
processed files remain indexed, while a previously indexed file that cannot be
read keeps its older database record. This favors preserving useful data over
deleting it because of a temporary permissions or sharing error.

### Search

```text
atlast search <query> [--limit <1-100>] [--db <database>]
```

Arguments and options:

- `<query>` contains FTS5 search text and may include Atlast's `path:`, `ext:`,
  and `modified:` filters. Quote the complete query at the shell level when it
  contains spaces.
- `--limit <1-100>` controls the maximum number of results. The default is 10.
- `--db <database>` selects the database file. It defaults to `atlast.db` in
  the current working directory.

Example output:

```text
1. C:/Projects/Atlast/src/network.cpp
   The upload failed after a [connection] [timeout] and entered the retry loop.
```

Results are ordered by SQLite FTS5's `bm25()` relevance score. Atlast then uses
the path as a deterministic tie-breaker. The numeric BM25 value is intentionally
not displayed because it is useful for ordering within one query, not as an
absolute quality score across unrelated queries.

## Search query syntax

Atlast separates its filters from the query, then passes the remaining search
text to SQLite FTS5 using a prepared statement. Prepared statements prevent SQL
injection, while FTS5 still interprets its own search operators. Invalid FTS5
syntax returns a search error.

Common queries include:

### All terms

```powershell
.\atlast.exe search "connection timeout"
```

Adjacent terms behave like an implicit `AND`: both terms must match, though
they do not have to be adjacent.

### Exact phrase

PowerShell must pass the double quotes through to FTS5. One convenient form is
an outer single-quoted shell string:

```powershell
.\atlast.exe search '"connection timeout"'
```

This requires the words to appear as a phrase.

### Alternatives

```powershell
.\atlast.exe search "timeout OR reset"
```

This matches either term. FTS5 operators such as `OR` are uppercase.

### Exclusion

```powershell
.\atlast.exe search "connection NOT websocket"
```

This matches `connection` while excluding results that match `websocket`.

### Prefix matching

```powershell
.\atlast.exe search "migrat*"
```

This can match tokens such as `migrate`, `migrated`, and `migration`.

### Atlast filters

Atlast recognizes three deterministic filters inside the query argument:

```text
path:<substring>
ext:<extension>
modified:<days>d
```

Filters are removed before the remaining text is sent to FTS5. All supplied
filters must match the same document.

```powershell
.\atlast.exe search "timeout path:backend ext:py modified:30d"
```

This searches for `timeout`, keeps paths containing `backend`, keeps `.py`
files, and keeps files modified during the last 30 days.

#### Path filter

```powershell
.\atlast.exe search "migration path:database"
```

`path:` performs a literal, case-insensitive substring check against the
normalized absolute path. It is not tokenized and has no wildcard syntax.

Quote a path fragment containing spaces inside the complete query:

```powershell
.\atlast.exe search 'migration path:"design notes"'
```

#### Extension filter

```powershell
.\atlast.exe search "connection ext:cpp"
.\atlast.exe search "connection ext:.CPP"
```

The leading dot is optional and matching is case-insensitive. The value must
contain only letters and digits. It matches the final extension, so `ext:ts`
also matches a filename ending in `.d.ts`.

#### Modified-date filter

```powershell
.\atlast.exe search "authentication modified:7d"
```

`modified:7d` means "modified within the last seven days." The supported range
is `1d` through `36500d`. Atlast compares against the modification timestamp
stored during the last indexing run. Re-index changed files before relying on
this filter.

#### Filter rules

- Search text is required; a filter-only query such as `ext:py` is rejected.
- Each filter may appear at most once.
- Filters may appear in any order and are combined with `AND`.
- Filter names are lowercase: `path:`, `ext:`, and `modified:`.
- Filter values are bound to prepared SQL statements.

FTS phrases and operators still work alongside filters:

```powershell
.\atlast.exe search '"connection timeout" ext:log modified:7d'
```

### Search only content

Both normalized paths and file content are indexed. The `path:` name is now an
Atlast substring filter. FTS5's `content:` column filter remains available:

```powershell
.\atlast.exe search "content:timeout"
```

This prevents a filename token from satisfying the full-text part of the query.

## Files that are indexed

Atlast currently accepts regular files with these case-insensitive extensions:

```text
.c .cc .cmake .cpp .cs .css .csv .cxx .go .h .hpp .htm .html .hxx
.java .js .json .log .md .py .rs .text .toml .ts .tsv .txt .xml
.yaml .yml
```

Additional rules:

- Directories named `.git`, `.next`, `build`, `coverage`, `dist`, or
  `node_modules` are pruned during recursive traversal. Atlast does not inspect
  their contents.
- Any directory containing `pyvenv.cfg` is recognized as a Python virtual
  environment and pruned, regardless of whether it is named `.venv`, `venv`,
  `my_venv`, or something else.
- Files larger than 10 MiB are skipped. The current implementation reads a
  complete file into memory before sending it to SQLite, so the limit bounds
  per-file memory use.
- Files containing a null byte are treated as binary and skipped.
- Symbolic-link directories are not followed by the default filesystem
  iterator behavior. This avoids accidental cycles and indexing outside the
  requested tree.
- Directories that cannot be entered because of permissions are skipped.
- Extension matching is case-insensitive.
- File paths are normalized and stored with forward slashes in UTF-8 form.

The allowlist is deliberately explicit. Indexing every unknown extension would
eventually ingest archives, databases, executables, media, and generated output
that happen not to contain an early null byte.

## Incremental indexing behavior

Before crawling a root, Atlast loads the stored path, modification time, and
size for documents attributed to that root. Each discovered file follows this
decision process:

1. Ignore unsupported files.
2. Avoid descending into ignored dependency and generated directories,
   including Python environments detected through `pyvenv.cfg`.
3. Read file metadata.
4. Skip the content read when modification time and size match the stored row.
5. Reject files over 10 MiB.
6. Read changed or new files.
7. Reject content containing a null byte.
8. Insert or update the document using its normalized path as the unique key.
9. After a complete traversal, delete stored paths no longer discovered.

After upgrading from an earlier Atlast build, run `index` again on each root.
The stale-record pass removes old rows that came from directories now ignored.

Modification time plus size is a fast change detector, not a cryptographic
guarantee. A program could theoretically rewrite a file with different content
while preserving both values. Content hashing should be added only when that
case matters in practice, because hashing every unchanged file would require
reading every file on every run and defeat the inexpensive incremental check.

If traversal stops because of a filesystem exception, Atlast does not perform
the final stale-file deletion pass. This prevents an incomplete crawl from
mistaking unvisited files for deleted files.

## Database design

The database contains a normal content table and an FTS5 external-content
table.

The normal table is conceptually:

```sql
CREATE TABLE documents (
    id       INTEGER PRIMARY KEY,
    root     TEXT NOT NULL,
    path     TEXT NOT NULL UNIQUE,
    modified INTEGER NOT NULL,
    size     INTEGER NOT NULL,
    content  TEXT NOT NULL
);
```

Column purposes:

- `id`: stable SQLite row identifier used by the FTS index.
- `root`: normalized directory responsible for the row during the most recent
  indexing run.
- `path`: normalized absolute file path and global uniqueness key.
- `modified`: filesystem modification clock value used for change detection.
- `size`: byte length used for change detection and diagnostics.
- `content`: complete file text returned to FTS5 for snippets.

The search table is conceptually:

```sql
CREATE VIRTUAL TABLE documents_fts USING fts5(
    path,
    content,
    content = 'documents',
    content_rowid = 'id',
    tokenize = 'unicode61'
);
```

`documents_fts` is an external-content FTS table. SQLite keeps the authoritative
content once in `documents`; the FTS table stores the structures needed for
search instead of another full copy of every document. Insert, delete, and
update triggers keep the search index synchronized with the content table.

All data values are bound to prepared statements. Paths, file content, roots,
queries, and limits are never concatenated into SQL commands.

## Transactions and failure handling

An index run uses `BEGIN IMMEDIATE` and commits after crawling and stale-record
cleanup. This ensures readers never observe half-written individual SQLite
changes and ensures trigger updates stay consistent with content rows.

Behavior by failure type:

- Database open or schema error: stop before crawling.
- SQL prepare, bind, or execution error: roll back the transaction.
- Changed file cannot be read: count the failure and retain its prior row.
- New file cannot be read: count the failure and create no row.
- File is now oversized or binary: remove its previous row because it is no
  longer eligible.
- Directory traversal throws: retain records for paths not visited during the
  incomplete traversal.
- Database remains locked for five seconds: SQLite returns an error. The busy
  timeout handles short overlap without waiting forever.

The database schema is created automatically by either `index` or `search`.
Searching a new database therefore succeeds and prints `No results.`

## Exit codes

Atlast uses these process exit codes:

| Code | Meaning |
| ---: | --- |
| `0` | The command completed successfully. |
| `1` | A filesystem, database, schema, indexing, or search operation failed. |
| `2` | The command line was invalid, such as a missing argument or bad limit. |

These codes make Atlast suitable for scripts and automated jobs. Console error
messages are written to standard error; normal results and statistics are
written to standard output.

## Automated tests

Run the test suite after building:

```powershell
ctest --test-dir C:\Atlast\build --output-on-failure
```

For a clean build-and-test sequence:

```powershell
cmake -S C:\Atlast -B C:\Atlast\build `
  -G "MinGW Makefiles" `
  -DCMAKE_BUILD_TYPE=Debug `
  -DBUILD_TESTING=ON
cmake --build C:\Atlast\build --parallel
ctest --test-dir C:\Atlast\build --output-on-failure
```

The `atlast_mvp` end-to-end test uses only CMake's scripting language and the
built executable. It creates an isolated temporary directory under the build
tree and verifies:

1. Two supported files are indexed.
2. An unsupported `.bin` file is ignored.
3. Dependency, generated, and custom-named Python virtual-environment
   directories are not traversed or indexed.
4. A multi-term query finds the expected file.
5. A second index run reports both files as unchanged.
6. Changed content replaces old searchable content.
7. A deleted file is removed from the index.
8. New content is searchable with a result limit.
9. Path, extension, recent-modification, and combined filters narrow results.
10. Invalid filters and filter-only searches return command-line error code `2`.
11. An invalid result limit returns command-line error code `2`.

The test deletes and recreates only its own `build/test-data` directory. It does
not read or modify personal documents.

## Repository layout

```text
Atlast/
├── .vscode/
│   └── settings.json   # Disables configure-on-edit in VS Code.
├── src/
│   ├── atlast.hpp      # Shared declarations for top-level operations.
│   ├── cli.cpp         # Argument parsing and command routing.
│   ├── database.cpp    # SQLite connection, schema, and statement helpers.
│   ├── database.hpp    # Internal SQLite helper declarations.
│   ├── indexer.cpp     # Filesystem crawling and incremental indexing.
│   ├── main.cpp        # Minimal process entry point.
│   └── search.cpp      # FTS5 query execution and result output.
├── tests/
│   └── mvp.cmake       # End-to-end test with no test framework dependency.
├── CMakeLists.txt      # Build target, SQLite link, and CTest registration.
└── README.md           # User, developer, and architecture documentation.
```

The C++ code is split by runtime responsibility. `main.cpp` delegates to the
CLI, while indexing, searching, and SQLite details remain in focused translation
units. The project deliberately avoids storage interfaces, factories, plugin
APIs, and one-class-per-file scaffolding because each current responsibility has
one concrete implementation.

## Troubleshooting

### CMake cannot find SQLite

Typical error:

```text
Could NOT find SQLite3
```

Install the SQLite development package, not only the SQLite executable. Confirm
that the compiler environment can find both `sqlite3.h` and the SQLite library.
Using the compiler, CMake, and SQLite packages from the same MSYS2 environment
avoids mixing incompatible runtimes.

### `sqlite3.dll` is missing on Windows

Ensure `C:\msys64\ucrt64\bin` is on `PATH`, or place the matching DLL beside
`atlast.exe`. Do not use a DLL from a different MSYS2 runtime or architecture.

### `no such module: fts5`

The SQLite library was compiled without FTS5. Install a standard SQLite build
with FTS5 enabled and rebuild Atlast against that library. Atlast depends on
FTS5 for indexing, BM25, and snippets; there is intentionally no second search
implementation.

### Search reports a syntax error

The query is parsed by FTS5. Check unmatched quotes, misplaced operators, and
shell quoting. Start with a single word, then add phrase or Boolean syntax.

### Search returns no results after indexing

Check that:

- The file extension is supported.
- The file is no larger than 10 MiB.
- The index command reported no read failures.
- The search command uses the same `--db` path as the index command.
- The query terms appear as tokens in the content or path.

### An edited file is reported unchanged

Atlast compares modification time and size. Some tools can deliberately
preserve timestamps, and a replacement can coincidentally keep the same size.
Delete the database and re-index to force a rebuild. Content hashing is a later
upgrade if this case becomes common.

### The database is locked

Atlast waits up to five seconds for a SQLite lock. Let the other index operation
finish and retry. The MVP is designed for one writer at a time.

## Cleaning generated files

Build outputs and test data live under the selected build directory. The
default database lives wherever the command is run unless `--db` is supplied.

PowerShell examples:

```powershell
Remove-Item -Recurse -Force C:\Atlast\build
Remove-Item C:\Path\To\atlast.db
```

Deleting the SQLite database removes the index only. It never removes source
documents. The index can always be rebuilt from the original files.

## Known limitations

- Current files only; no Git history or time-travel search.
- Plain-text formats only; no PDF, Office, archive, or image extraction.
- No filesystem watcher; users rerun `index` to observe changes.
- No semantic or vector search.
- No natural-language answer generation.
- No Boolean expressions, negation, or ranges for Atlast filters; one `path:`,
  `ext:`, and `modified:Nd` value may be combined with full-text search.
- No per-root deletion command.
- One database writer at a time.
- Files are read completely into memory, bounded by the 10 MiB limit.
- Modification time and size are used instead of content hashes.
- Narrow command-line arguments can limit some Unicode path handling on
  Windows terminals depending on the active code page.
- Search ranking uses FTS5 defaults and is not user-configurable.

These are deliberate MVP boundaries, not partially implemented features.

## Recommended roadmap

The next milestones should preserve the working CLI and add one measurable
capability at a time:

1. **Git history:** invoke the Git CLI and index commit, path, and blob content.
   Reuse Git's object store instead of creating another content-addressed store.
2. **Provenance:** show the row, file version, query terms, and scoring factors
   responsible for each result.
3. **Filesystem watching:** add live updates only after repeated manual indexing
   is proven inconvenient.
4. **Document extraction:** add one format at a time behind real sample files
   and tests.
5. **Semantic retrieval:** benchmark lexical search first, then add embeddings
   only for queries that lexical search demonstrably misses.

The product's intended differentiator is eventually time-aware, explainable
developer search. The current MVP supplies the deterministic local-search
foundation that those later capabilities need.
