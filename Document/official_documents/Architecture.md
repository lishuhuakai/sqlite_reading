Architecture of SQLite

# Introduction

This document describes the architecture of the SQLite library. The information here is useful to those who want to understand or modify the inner workings of SQLite.

InterfaceSQL CommandProcessorVirtual MachineB-TreePagerOS InterfaceTokenizerParserCodeGeneratorUtilitiesTest CodeCoreBackendSQL CompilerAccessories

A nearby diagram shows the main components of SQLite and how they interoperate. The text below explains the roles of the various components.

![architecture](https://github.com/lishuhuakai/sqlite_reading/blob/main/Document/official_documents/imgs/image-20210828173741457.png)

# Overview

SQLite works by compiling SQL text into [bytecode](https://sqlite.org/opcode.html), then running that bytecode using a virtual machine.

The [sqlite3_prepare_v2()](https://sqlite.org/c3ref/prepare.html) and related interfaces act as a compiler for converting SQL text into bytecode. The [sqlite3_stmt](https://sqlite.org/c3ref/stmt.html) object is a container for a single bytecode program that implements a single SQL statement. The [sqlite3_step()](https://sqlite.org/c3ref/step.html) interface passes a bytecode program into the virtual machine, and runs the program until it either completes, or forms a row of result to be returned, or hits a fatal error, or is [interrupted](https://sqlite.org/c3ref/interrupt.html).

# Interface

Much of the [C-language Interface](https://sqlite.org/c3ref/intro.html) is found in source files [main.c](https://sqlite.org/src/file/src/main.c), [legacy.c](https://sqlite.org/src/file/src/legacy.c), and [vdbeapi.c](https://sqlite.org/src/file/src/vdbeapi.c) though some routines are scattered about in other files where they can have access to data structures with file scope. The [sqlite3_get_table()](https://sqlite.org/c3ref/free_table.html) routine is implemented in [table.c](https://sqlite.org/src/file/src/table.c). The [sqlite3_mprintf()](https://sqlite.org/c3ref/mprintf.html) routine is found in [printf.c](https://sqlite.org/src/file/src/printf.c). The [sqlite3_complete()](https://sqlite.org/c3ref/complete.html) interface is in [complete.c](https://sqlite.org/src/file/src/complete.c). The [TCL Interface](https://sqlite.org/tclsqlite.html) is implemented by [tclsqlite.c](https://sqlite.org/src/file/src/tclsqlite.c).

To avoid name collisions, all external symbols in the SQLite library begin with the prefix **sqlite3**. Those symbols that are intended for external use (in other words, those symbols which form the API for SQLite) add an underscore, and thus begin with **sqlite3_**. Extension APIs sometimes add the extension name prior to the underscore; for example: **sqlite3rbu_** or **sqlite3session_**.

# Tokenizer

When a string containing SQL statements is to be evaluated it is first sent to the tokenizer. The tokenizer breaks the SQL text into tokens and hands those tokens one by one to the parser. The tokenizer is hand-coded in the file tokenize.c.

Note that in this design, the tokenizer calls the parser. People who are familiar with YACC and BISON may be accustomed to doing things the other way around — having the parser call the tokenizer. Having the tokenizer call the parser is better, though, because it can be made threadsafe and it runs faster.

# Parser

The parser assigns meaning to tokens based on their context. The parser for SQLite is generated using the [Lemon parser generator](https://sqlite.org/lemon.html). Lemon does the same job as YACC/BISON, but it uses a different input syntax which is less error-prone. Lemon also generates a parser which is reentrant and thread-safe. And Lemon defines the concept of a non-terminal destructor so that it does not leak memory when syntax errors are encountered. The grammar file that drives Lemon and that defines the SQL language that SQLite understands is found in [parse.y](https://sqlite.org/src/file/src/parse.y).

Because Lemon is a program not normally found on development machines, the complete source code to Lemon (just one C file) is included in the SQLite distribution in the "tool" subdirectory.

# Code Generator

After the parser assembles tokens into a parse tree, the code generator runs to analyze the parse tree and generate [bytecode](https://sqlite.org/opcode.html) that performs the work of the SQL statement. The [prepared statement](https://sqlite.org/c3ref/stmt.html) object is a container for this bytecode. There are many files in the code generator, including: [attach.c](https://sqlite.org/src/file/src/attach.c), [auth.c](https://sqlite.org/src/file/src/auth.c), [build.c](https://sqlite.org/src/file/src/build.c), [delete.c](https://sqlite.org/src/file/src/delete.c), [expr.c](https://sqlite.org/src/file/src/expr.c), [insert.c](https://sqlite.org/src/file/src/insert.c), [pragma.c](https://sqlite.org/src/file/src/pragma.c), [select.c](https://sqlite.org/src/file/src/select.c), [trigger.c](https://sqlite.org/src/file/src/trigger.c), [update.c](https://sqlite.org/src/file/src/update.c), [vacuum.c](https://sqlite.org/src/file/src/vacuum.c), [where.c](https://sqlite.org/src/file/src/where.c), [wherecode.c](https://sqlite.org/src/file/src/wherecode.c), and [whereexpr.c](https://sqlite.org/src/file/src/whereexpr.c). In these files is where most of the serious magic happens. [expr.c](https://sqlite.org/src/file/src/expr.c) handles code generation for expressions. **where\*.c** handles code generation for WHERE clauses on SELECT, UPDATE and DELETE statements. The files [attach.c](https://sqlite.org/src/file/src/attach.c), [delete.c](https://sqlite.org/src/file/src/delete.c), [insert.c](https://sqlite.org/src/file/src/insert.c), [select.c](https://sqlite.org/src/file/src/select.c), [trigger.c](https://sqlite.org/src/file/src/trigger.c) [update.c](https://sqlite.org/src/file/src/update.c), and [vacuum.c](https://sqlite.org/src/file/src/vacuum.c) handle the code generation for SQL statements with the same names. (Each of these files calls routines in [expr.c](https://sqlite.org/src/file/src/expr.c) and [where.c](https://sqlite.org/src/file/src/where.c) as necessary.) All other SQL statements are coded out of [build.c](https://sqlite.org/src/file/src/build.c). The [auth.c](https://sqlite.org/src/file/src/auth.c) file implements the functionality of [sqlite3_set_authorizer()](https://sqlite.org/c3ref/set_authorizer.html).

The code generator, and especially the logic in **where\*.c** and in [select.c](https://sqlite.org/src/file/src/select.c), is sometimes called the [query planner](https://sqlite.org/optoverview.html). For any particular SQL statement, there might be hundreds, thousands, or millions of different algorithms to compute the answer. The query planner is an AI that strives to select the best algorithm from these millions of choices.

# Bytecode Engine

The [bytecode](https://sqlite.org/opcode.html) program created by the code generator is run by a virtual machine.

The virtual machine itself is entirely contained in a single source file [vdbe.c](https://sqlite.org/src/file/src/vdbe.c). The [vdbe.h](https://sqlite.org/src/file/src/vdbe.h) header file defines an interface between the virtual machine and the rest of the SQLite library and [vdbeInt.h](https://sqlite.org/src/file/src/vdbeInt.h) which defines structures and interfaces that are private the virtual machine itself. Various other **vdbe\*.c** files are helpers to the virtual machine. The [vdbeaux.c](https://sqlite.org/src/file/src/vdbeaux.c) file contains utilities used by the virtual machine and interface modules used by the rest of the library to construct VM programs. The [vdbeapi.c](https://sqlite.org/src/file/src/vdbeapi.c) file contains external interfaces to the virtual machine such as the [sqlite3_bind_int()](https://sqlite.org/c3ref/bind_blob.html) and [sqlite3_step()](https://sqlite.org/c3ref/step.html). Individual values (strings, integer, floating point numbers, and BLOBs) are stored in an internal object named "Mem" which is implemented by [vdbemem.c](https://sqlite.org/src/file/src/vdbemem.c).

SQLite implements SQL functions using callbacks to C-language routines. Even the built-in SQL functions are implemented this way. Most of the built-in SQL functions (ex: [abs()](https://sqlite.org/lang_corefunc.html#abs), [count()](https://sqlite.org/lang_aggfunc.html#count), [substr()](https://sqlite.org/lang_corefunc.html#substr), and so forth) can be found in [func.c](https://sqlite.org/src/file/src/func.c) source file. Date and time conversion functions are found in [date.c](https://sqlite.org/src/file/src/date.c). Some functions such as [coalesce()](https://sqlite.org/lang_corefunc.html#coalesce) and [typeof()](https://sqlite.org/lang_corefunc.html#typeof) are implemented as bytecode directly by the code generator.

# B-Tree

An SQLite database is maintained on disk using a B-tree implementation found in the [btree.c](https://sqlite.org/src/file/src/btree.c) source file. Separate B-trees are used for each table and each index in the database. All B-trees are stored in the same disk file. The [file format](https://sqlite.org/fileformat2.html) details are stable and well-defined and are guaranteed to be compatible moving forward.

The interface to the B-tree subsystem and the rest of the SQLite library is defined by the header file [btree.h](https://sqlite.org/src/file/src/btree.h).

# Page Cache

The B-tree module requests information from the disk in fixed-size pages. The default [page_size](https://sqlite.org/pragma.html#pragma_page_size) is 4096 bytes but can be any power of two between 512 and 65536 bytes. The page cache is responsible for reading, writing, and caching these pages. The page cache also provides the rollback and atomic commit abstraction and takes care of locking of the database file. The B-tree driver requests particular pages from the page cache and notifies the page cache when it wants to modify pages or commit or rollback changes. The page cache handles all the messy details of making sure the requests are handled quickly, safely, and efficiently.

The primary page cache implementation is in the [pager.c](https://sqlite.org/src/file/src/pager.c) file. [WAL mode](https://sqlite.org/wal.html) logic is in the separate [wal.c](https://sqlite.org/src/file/src/wal.c). In-memory caching is implemented by the [pcache.c](https://sqlite.org/src/file/src/pcache.c) and [pcache1.c](https://sqlite.org/src/file/src/pcache1.c) files. The interface between page cache subsystem and the rest of SQLite is defined by the header file [pager.h](https://sqlite.org/src/file/src/pager.h).

# OS Interface

In order to provide portability between across operating systems, SQLite uses abstract object called the [VFS](https://sqlite.org/vfs.html). Each VFS provides methods for opening, read, writing, and closing files on disk, and for other OS-specific task such as finding the current time, or obtaining randomness to initialize the built-in pseudo-random number generator. SQLite currently provides VFSes for unix (in the [os_unix.c](https://sqlite.org/src/file/src/os_unix.c) file) and Windows (in the [os_win.c](https://sqlite.org/src/file/src/os_win.c) file).

# Utilities

Memory allocation, caseless string comparison routines, portable text-to-number conversion routines, and other utilities are located in [util.c](https://sqlite.org/src/file/src/util.c). Symbol tables used by the parser are maintained by hash tables found in [hash.c](https://sqlite.org/src/file/src/hash.c). The [utf.c](https://sqlite.org/src/file/src/utf.c) source file contains Unicode conversion subroutines. SQLite has its own private implementation of [printf()](https://sqlite.org/printf.html) (with some extensions) in [printf.c](https://sqlite.org/src/file/src/printf.c) and its own pseudo-random number generator (PRNG) in [random.c](https://sqlite.org/src/file/src/random.c).

# Test Code

Files in the "src/" folder of the source tree whose names begin with **test** are for testing only and are not included in a standard build of the library.