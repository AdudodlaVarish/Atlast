Working Ideas: Use XZ Compression for lossless compression. 

Ignore venv and certain dependency folders (may not be applicable when working on normal folders)

______________________________________________________________________________________________

Idea 1: FTS Multithreading for Preprocessing and just one writer for the Sqlite Database

Idea 2: FTS Multithreading for Preprocessing and then creating X number of Sqlite Databases to parallelize writing and then potentially concat together

Idea 3: Same as Idea 2 except instead of concatinating X databases, run a multithread search over all of them. 

