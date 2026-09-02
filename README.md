# Atlast

[![Release binaries](https://github.com/AdudodlaVarish/Atlast/actions/workflows/release.yml/badge.svg)](https://github.com/AdudodlaVarish/Atlast/actions/workflows/release.yml)

Atlast is a persistent full-text search tool for developer workspaces. It
indexes source code, notes, configuration, and logs into a local SQLite
database, then returns ranked results with highlighted context.

Atlast runs locally. It does not require an account, server, hosted model, or
network connection, and it does not upload indexed content.

```console
$ atlast index ~/projects --db work.db
Root: /home/you/projects
Scanned: 1842
Indexed: 1842
Unchanged: 0
Removed: 0
Skipped: 0
Failed: 0

$ atlast search "connection timeout path:backend ext:cpp" --db work.db
1. /home/you/projects/service/backend/network.cpp
   The upload failed after a [connection] [timeout] and entered the retry loop.
```

Atlast is experimental. The current release is useful, but its file support,
performance, and interface are still deliberately small.

## Documentation

- [Installation](#installation)
- [Quick start](#quick-start)
- [Why should I use Atlast?](#why-should-i-use-atlast)
- [Why shouldn't I use Atlast?](#why-shouldnt-i-use-atlast)
- [Search syntax](#search-syntax)
- [Building](#building)
- [Developer guide](docs/DEVELOPER_GUIDE.md)

## Quick examples

Index more than one directory into the same database:

```console
$ atlast index ~/projects/api --db work.db
$ atlast index ~/projects/frontend --db work.db
$ atlast sources --db work.db
```

Search all indexed directories:

```console
$ atlast search "retry policy" --db work.db
```

Use deterministic path, extension, and modification-time filters:

```console
$ atlast search "timeout path:backend ext:py modified:30d" --db work.db
```

Use SQLite FTS5 phrases and Boolean operators:

```console
$ atlast search '"connection timeout" OR "connection reset"' --db work.db
$ atlast search "connection NOT websocket" --db work.db
$ atlast search "migrat*" --db work.db
```

Refresh every indexed directory:

```console
$ atlast refresh --db work.db
```

Search committed Git history without copying it into the database:

```console
$ atlast history ~/projects/api "old retry policy"
```

Remove one directory from the index without touching its files:

```console
$ atlast forget ~/projects/frontend --db work.db
```

## Why should I use Atlast?

- **Repeated searches are cheap.** Atlast pays the filesystem-reading cost
  during indexing and searches the persistent FTS index afterward.
- **One database can cover several workspaces.** Code, notes, and logs from
  separate directories can appear in one result list.
- **Results include context.** SQLite FTS5 supplies BM25 ranking and highlighted
  snippets instead of returning paths alone.
- **Updates are incremental.** Unchanged files are recognized by modification
  time and size, while changed and deleted files update the index.
- **The index is manageable.** `sources`, `refresh`, and `forget` make it
  clear what the database contains.
- **It stays local.** Atlast has one runtime dependency, SQLite, and no network
  feature.
- **Git history is available on demand.** The `history` command can find text
  added or removed by earlier commits.

In other words, use Atlast when you repeatedly search across several local
developer workspaces and want a persistent, inspectable index.

## Why shouldn't I use Atlast?

Atlast is not intended to replace every search tool.

- For a one-off regex search in the current repository, use
  [ripgrep](https://github.com/BurntSushi/ripgrep). Atlast requires an indexing
  step and does not support regular expressions.
- For broad desktop document search across PDF, Office, email, and archives,
  use a tool such as [Recoll](https://www.recoll.org/).
- For instant Windows filename search, use
  [Everything](https://www.voidtools.com/).
- Atlast does not parse `.gitignore`; it prunes a fixed set of common
  dependency and generated directories.
- Git-history search is not available from the native Windows executable. It
  works on Linux, macOS, and WSL.
- `watch` polls every five seconds instead of subscribing to native filesystem
  events.
- There are no published benchmarks yet.

If one of those limitations blocks your workflow, Atlast is probably not the
right tool today.

## Is Atlast faster than ripgrep?

That comparison has not been benchmarked, and the tools do different work.

ripgrep scans files for each search. Atlast reads supported files into a
persistent index and searches that index repeatedly. The first Atlast query
therefore includes an explicit indexing cost that a later query does not.

Atlast will not claim a speed advantage until its indexing throughput, database
size, memory use, and query latency have been measured on a reproducible public
corpus.

## Installation

Tagged releases are configured to publish archives for:

- Linux x86-64
- macOS ARM64
- Windows x86-64

Download an archive from the
[GitHub Releases page](https://github.com/AdudodlaVarish/Atlast/releases),
extract it, and run the executable under `atlast/bin`.

The Windows archive includes its SQLite and MinGW runtime DLLs. Linux requires
the system SQLite runtime package, commonly named `libsqlite3-0`. macOS uses
the system SQLite runtime.

Confirm the executable works:

```console
$ atlast --version
atlast 0.1.0

$ atlast --help
```

Atlast is not currently available through Homebrew, APT, Winget, Scoop, or
other package managers. Those instructions will be added only after packages
actually exist.

## Quick start

Choose one database path and use it for both indexing and searching:

```console
$ atlast index ~/projects --db ~/.atlast.db
$ atlast search "database migration" --db ~/.atlast.db
```

The database defaults to `atlast.db` in the current working directory when
`--db` is omitted.

On PowerShell:

```powershell
.\atlast.exe index C:\Projects --db C:\Indexes\work.db
.\atlast.exe search "database migration" --db C:\Indexes\work.db
```

## Usage

```text
atlast index <directory> [--db <database>]
atlast search <query> [--explain] [--limit <1-100>] [--db <database>]
atlast history <repository> <query> [--limit <1-100>]
atlast sources [--db <database>]
atlast refresh [--db <database>]
atlast watch [--db <database>]
atlast forget <directory> [--db <database>]
atlast --help
atlast --version
```

### Indexing

`index` recursively discovers supported files and prints:

```text
Root: /home/you/projects
Scanned: 1842
Indexed: 27
Unchanged: 1812
Removed: 3
Skipped: 0
Failed: 0
```

- `Scanned`: supported regular files considered.
- `Indexed`: new or changed files written.
- `Unchanged`: files whose stored modification time and size still match.
- `Removed`: stale rows deleted from the index.
- `Skipped`: oversized or likely binary files.
- `Failed`: files that could not be inspected or read.

If a changed file cannot be read, Atlast keeps its previous searchable row. If
directory traversal stops unexpectedly, Atlast does not perform stale-row
cleanup. Both rules favor preserving existing index data over deleting it after
a temporary filesystem failure.

### Searching

`search` returns at most 10 results by default:

```console
$ atlast search "shadow table" --limit 5 --db work.db
```

Results are ordered by SQLite FTS5's `bm25()` score, with path used as a
deterministic tie-breaker. `--explain` displays the row ID, source root,
stored metadata, parsed FTS query, and BM25 score.

### Indexed sources

```console
$ atlast sources --db work.db
$ atlast refresh --db work.db
$ atlast watch --db work.db
$ atlast forget ~/projects/old-service --db work.db
```

`sources` lists every indexed root, its file count, and its last index time.
`refresh` incrementally re-indexes every root. `watch` repeats that refresh
every five seconds until interrupted. `forget` deletes one root's database
rows but never deletes source files.

### Git history

```console
$ atlast history <repository> <query> [--limit <1-100>]
```

`history` delegates to the repository's installed Git command and searches
patches across all refs. It prints matching commit metadata and changed paths.
History is searched on demand and is not stored in SQLite.

## Search syntax

Atlast removes its own filters from the query and passes the remaining text to
SQLite FTS5. FTS phrases and operators therefore continue to work alongside
Atlast filters.

| Filter | Meaning |
| --- | --- |
| `path:<text>` | Case-insensitive literal substring of the normalized path |
| `ext:<extension>` | Case-insensitive final extension |
| `modified:<days>d` | Modified within the given number of days |

All filters must match the same result:

```console
$ atlast search "dcf OR money path:backend ext:py modified:30d"
```

This means:

```text
(dcf OR money)
AND path contains "backend"
AND extension is ".py"
AND modified within 30 days
```

Filter rules:

- Search text is required; `ext:py` alone is rejected.
- Each filter can appear once.
- Filter names are lowercase.
- The leading dot in `ext:.cpp` is optional.
- `modified:` accepts `1d` through `36500d`.
- Quote a path containing spaces: `path:"design notes"`.

Useful FTS5 forms:

```text
connection timeout       both tokens
"connection timeout"     exact phrase
timeout OR reset         either expression
connection NOT websocket exclude websocket
migrat*                  token prefix
content:timeout          search content, not indexed paths
```

Invalid FTS5 syntax is reported as a search error.

## Files searched

Atlast indexes regular files with these case-insensitive extensions:

```text
.c .cc .cmake .cpp .cs .css .csv .cxx .go .h .hpp .htm .html .hxx
.java .js .json .log .md .py .rs .text .toml .ts .tsv .txt .xml
.yaml .yml
```

It does not descend into:

```text
.git .next build coverage dist node_modules
```

Any directory containing `pyvenv.cfg` is also ignored, regardless of its
name. This catches Python environments named `.venv`, `venv`, `my_venv`,
or something else.

Files larger than 10 MiB are skipped. Files containing a null byte are treated
as binary and skipped. Symbolic-link directories are not followed.

The allowlist is intentionally conservative. Indexing every unknown extension
would eventually ingest databases, archives, executables, and media.

## Building

Atlast requires:

- CMake 3.20 or newer
- A C++23 compiler
- SQLite 3 with FTS5
- Make or MinGW Make
- Git only for the optional `history` command

On Linux and macOS:

```console
$ cmake --preset release
$ cmake --build --preset release --parallel
$ ctest --preset release
$ ./build/release/atlast --version
```

On Windows, run the equivalent commands from an MSYS2 UCRT64 environment:

```console
$ cmake --preset windows-release
$ cmake --build --preset windows-release --parallel
$ ctest --preset windows-release
$ ./build/windows-release/atlast.exe --version
```

The required MSYS2 packages are:

```console
$ pacman -S --needed \
    mingw-w64-ucrt-x86_64-gcc \
    mingw-w64-ucrt-x86_64-cmake \
    mingw-w64-ucrt-x86_64-make \
    mingw-w64-ucrt-x86_64-sqlite3
```

For a local install:

```console
$ cmake --install build/release --prefix package/atlast
```

## Running tests

The project has one end-to-end test instead of a test framework:

```console
$ ctest --preset debug
```

It creates disposable files and a database under the selected build directory,
runs the real executable through the complete workflow, and deletes only its
own test data.

The test covers indexing, incremental updates, ignored directories, ranked
search, snippets, filters, multiple sources, refresh, forget, provenance,
version output, and Git history when Git is available.

## How it works

Atlast uses `std::filesystem` to crawl each source and SQLite for persistence.
The normal `documents` table owns paths, metadata, and content. An
external-content FTS5 table owns the search index. SQLite triggers keep both
tables synchronized.

Indexing runs inside a transaction. Existing file metadata is loaded once,
prepared statements are reused, and unchanged files are not read. Atlast
supports one database writer at a time and waits up to five seconds for a
SQLite lock.

For the full source tour, terminology, schema, and control flow, read the
[developer guide](docs/DEVELOPER_GUIDE.md).

## Project status

Atlast currently has these deliberate limits:

- Plain-text formats only; no PDF, Office, archive, or image extraction.
- No semantic/vector search or natural-language answers.
- No regular expressions.
- No configuration file or user-defined extension list.
- One database writer.
- Complete-file reads bounded by 10 MiB.
- Modification time and size instead of content hashes.
- FTS5's default ranking parameters.
- Native Windows Git-history search is unavailable.
- Filesystem watching uses polling.

These are boundaries, not promises. Features should be added after a real
workflow or measurement justifies them.

## Contributing

Start with the [developer guide](docs/DEVELOPER_GUIDE.md), then run:

```console
$ cmake --preset debug
$ cmake --build --preset debug --parallel
$ ctest --preset debug
```

Bug reports should include the command, expected result, actual result,
operating system, compiler, and Atlast version. Please do not include private
indexed content.


