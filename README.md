# File System

A concurrent data structure representing a directory tree, implemented in C using the dedicated version of a [pthreads](https://en.wikipedia.org/wiki/Pthreads) library to accelerate computations.

# Technical details

Paths are represented in the format `/foo/bar/baz/`. Implemented functionalities are defined in `Tree.h` and include:
* `Tree* tree_new()` - creates a new directory tree with a single empty root directory `"/"`.
* `void tree_free(Tree*)` - frees the memory allocated for the specified tree.
* `char* tree_list(Tree* tree, const char* path)` - returns the content of a directory as a string.
* `int tree_create(Tree* tree, const char* path)` - creates a new empty directory at the specified `path`.
* `int tree_remove(Tree* tree, const char* path)` - removes the directory.
* `int tree_move(Tree* tree, const char* source, const char* target)` - moves the `source` directory to the `target` path (if possible, e.g., a directory cannot be moved into one of its subdirectories).
