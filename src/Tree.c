#include "Tree.h"
#include "HashMap.h"
#include "path_utils.h"
#include "utils.h"
#include "err.h"

#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <stdio.h>
#include <pthread.h>

// Operations on tree which need only read-permission from hashmap are treated
// like readers in readers and writers problem. Of course operations which
// modify some things in hashmaps are treated like writers.

// Move() operation perform a lock in LCA of source and target until source
// and target are writer-locked. Then it unlocks lca lock and wait for all
// processes in subtree of source to finish (they have to finish their work,
// so there are no problems with change of path name).

// Remove() operation also waits for all processes to finish so removing of a
// vertex can be performed safely.

// When going through path from parent to son it is important to lock son
// mutex before unlocking parent mutex (if it is not done then remove() and
// move() might not see some processes correctly).

struct Tree {
    HashMap *map;
    pthread_mutex_t lock;
    pthread_cond_t readers, writers;
    // Condition used when operation need to clear entire subtree
    // before starting its work. No other process can entry this subtree
    // while someone waits on clear.
    pthread_cond_t clear;

    int rcount, wcount, rwait, wwait, change;
    bool cwait;
};


// Creates new tree of folders with one empty folder "/".
Tree *tree_new() {
    Tree *tree = safe_malloc(sizeof(Tree));

    tree->map = hmap_new();
    if (!tree->map)
        fatal("Map initialization failed.");
    tree->rcount = tree->wcount = 0;
    tree->rwait = tree->wwait = 0;
    tree->change = 0;
    tree->cwait = false;

    safe_mutex_init(&tree->lock);
    safe_cond_init(&tree->readers);
    safe_cond_init(&tree->writers);
    safe_cond_init(&tree->clear);

    return tree;
}


// Frees all memory used by given tree.
void tree_free(Tree *tree) {
    const char *key;
    void *value;
    HashMapIterator it = hmap_iterator(tree->map);
    while (hmap_next(tree->map, &it, &key, &value)) {
        tree_free(value);
    }

    hmap_free(tree->map);
    safe_cond_destroy(&tree->readers);
    safe_cond_destroy(&tree->writers);
    safe_cond_destroy(&tree->clear);
    safe_mutex_destroy(&tree->lock);

    free(tree);
}


void reader_entry_protocol(Tree *tree) {
    safe_lock(&tree->lock);
    while ((tree->wcount > 0 || tree->wwait > 0) && tree->change <= 0) {
        tree->rwait++;
        safe_wait(&tree->readers, &tree->lock);
        tree->rwait--;
    }

    tree->rcount++;
    if (tree->change > 0) {
        tree->change--;
        if (tree->change > 0)
            safe_signal(&tree->readers);
    }
    safe_unlock(&tree->lock);
}


void reader_exit_protocol(Tree *tree) {
    safe_lock(&tree->lock);
    tree->rcount--;
    if (tree->rcount == 0 && tree->wwait > 0) {
        tree->change = -1;
        safe_signal(&tree->writers);
    }
    else if (tree->cwait) {
        safe_signal(&tree->clear);
    }
    safe_unlock(&tree->lock);
}


void writer_entry_protocol(Tree *tree) {
    safe_lock(&tree->lock);
    while (tree->rcount > 0 || tree->wcount > 0 || tree->change > 0) {
        tree->wwait++;
        safe_wait(&tree->writers, &tree->lock);
        tree->wwait--;
    }
    tree->wcount++;
    tree->change = 0;
    safe_unlock(&tree->lock);
}


void writer_exit_protocol(Tree *tree) {
    safe_lock(&tree->lock);
    tree->wcount--;
    if (tree->rwait > 0) {
        tree->change = tree->rwait;
        safe_signal(&tree->readers);
    }
    else if (tree->wwait > 0) {
        tree->change = -1;
        safe_signal(&tree->writers);
    }
    else if (tree->cwait) {
        safe_signal(&tree->clear);
    }
    safe_unlock(&tree->lock);
}


// Prints content of given folder (only on first level, without recursion).
char *tree_list(Tree *tree, const char *path) {
    if (!is_path_valid(path))
        return NULL;

    char component[MAX_FOLDER_NAME_LENGTH + 1];
    char *res = NULL;
    reader_entry_protocol(tree);
    while (true) {
        path = split_path(path, component);

        bool finished = false;
        Tree *new_tree;
        if (path) {
            new_tree = hmap_get(tree->map, component);
            if (!new_tree)
                finished = true;
            else
                reader_entry_protocol(new_tree);
        }
        else {
            finished = true;
            res = make_map_contents_string(tree->map);
        }

        reader_exit_protocol(tree);

        if (finished)
            break;
        else
            tree = new_tree;
    }
    return res;
}


// Traverse tree via given path. If it doesn't encounter error holds writer
// entry permission to last vertex on path.
int find_node(Tree **tree, const char *path, size_t to_grandparent) {
    char component[MAX_FOLDER_NAME_LENGTH + 1];
    if (to_grandparent == 0)
        writer_entry_protocol(*tree);
    else
        reader_entry_protocol(*tree);
    while (true) {
        path = split_path(path, component);
        if (!path)
            break;
        Tree *old_tree = *tree;
        *tree = hmap_get((*tree)->map, component);
        if (!(*tree)) {
            reader_exit_protocol(old_tree);
            return ENOENT;
        }
        to_grandparent--;
        if (to_grandparent == 0)
            writer_entry_protocol(*tree);
        else
            reader_entry_protocol(*tree);
        reader_exit_protocol(old_tree);
    }
    return 0;
}


// Creates new subfolder.
int tree_create(Tree *tree, const char *path) {
    if (!is_path_valid(path))
        return EINVAL;
    if (is_root(path))
        return EEXIST;

    char child_name[MAX_FOLDER_NAME_LENGTH + 1];
    char *parent_path = make_path_to_parent(path, child_name);
    path = parent_path;

    int err = find_node(&tree, path, count_slashes(parent_path) - 1);
    if (err != 0) {
        free(parent_path);
        return err;
    }

    if (hmap_get(tree->map, child_name)) {
        err = EEXIST;
    }
    else {
        Tree *son = tree_new();
        hmap_insert(tree->map, child_name, son);
    }

    writer_exit_protocol(tree);

    free(parent_path);
    return err;
}


// Removes folder if it is empty.
int tree_remove(Tree *tree, const char *path) {
    if (!is_path_valid(path))
        return EINVAL;
    if (is_root(path))
        return EBUSY;

    char child_name[MAX_FOLDER_NAME_LENGTH + 1];
    char *parent_path = make_path_to_parent(path, child_name);
    path = parent_path;

    int err = find_node(&tree, path, count_slashes(parent_path) - 1);
    if (err != 0) {
        free(parent_path);
        return err;
    }

    Tree *son = hmap_get(tree->map, child_name);
    bool son_destroy = false;
    if (!son) {
        err = ENOENT;
    }
    else {
        safe_lock(&son->lock);
        while (son->rcount > 0 || son->wcount > 0 || son->rwait > 0
        || son->wwait > 0) {
            son->cwait = true;
            safe_wait(&son->clear, &son->lock);
            son->cwait = false;
        }
        if (hmap_size(son->map) != 0)
            err = ENOTEMPTY;
        else
            son_destroy = true;
        safe_unlock(&son->lock);
    }

    if (son_destroy) {
        hmap_remove(tree->map, child_name);
        tree_free(son);
    }

    writer_exit_protocol(tree);

    free(parent_path);
    return err;
}


// Waiting for finish of all ongoing processes in the tree.
void bfs_clear(Tree *tree) {
    safe_lock(&tree->lock);
    while (tree->rcount > 0 || tree->wcount > 0 || tree->rwait > 0
    || tree->wwait > 0) {
        tree->cwait = true;
        safe_wait(&tree->clear, &tree->lock);
        tree->cwait = false;
    }

    const char *key;
    void *value;
    HashMapIterator it = hmap_iterator(tree->map);
    while (hmap_next(tree->map, &it, &key, &value))
        bfs_clear(value);

    safe_unlock(&tree->lock);
}


// Going down paths below lca in move().
int move_dfs(Tree **tree, const char *path, char *component,
             size_t to_grandparent) {
    int err = 0;
    bool in_lca = true;
    while (true) {
        path = split_path(path, component);
        if (!path)
            break;
        Tree *old_tree = *tree;
        *tree = hmap_get((*tree)->map, component);
        if (!(*tree)) {
            if (!in_lca)
                reader_exit_protocol(old_tree);
            err = ENOENT;
            break;
        }
        to_grandparent--;
        if (to_grandparent == 0)
            writer_entry_protocol(*tree);
        else
            reader_entry_protocol(*tree);
        if (in_lca)
            in_lca = false;
        else
            reader_exit_protocol(old_tree);
    }
    return err;
}


// Traversing to find source in move().
int source_dfs(Tree *lca_tree, Tree *target_tree, const char *source,
               char *source_child, char *target_child, char *component,
               size_t to_grandparent) {
    Tree *source_tree = lca_tree;
    int err = move_dfs(&source_tree, source, component, to_grandparent);
    if (err != 0) {
        if (lca_tree != target_tree)
            writer_exit_protocol(target_tree);
        writer_exit_protocol(lca_tree);
        return err;
    }

    Tree *to_be_moved = hmap_get(source_tree->map, source_child);
    if (!to_be_moved) {
        err = ENOENT;
        if (source_tree != lca_tree)
            writer_exit_protocol(source_tree);
        if (target_tree != lca_tree)
            writer_exit_protocol(target_tree);
        writer_exit_protocol(lca_tree);
        return err;
    }

    // Releasing lca lock, because we are already locked in source and target.
    if (lca_tree != source_tree && lca_tree != target_tree)
        writer_exit_protocol(lca_tree);

    bfs_clear(to_be_moved);
    hmap_remove(source_tree->map, source_child);
    hmap_insert(target_tree->map, target_child, to_be_moved);
    if (source_tree != target_tree)
        writer_exit_protocol(source_tree);
    writer_exit_protocol(target_tree);
    return err;
}


// Traversing to find target in move().
int target_dfs(Tree *lca_tree, const char *target, char *target_child,
               const char *source, char *source_child, char *component,
               size_t to_grandparent, size_t to_grandparent_source) {
    Tree *target_tree = lca_tree;
    int err = move_dfs(&target_tree, target, component, to_grandparent);
    if (err != 0) {
        writer_exit_protocol(lca_tree);
        return err;
    }

    // We hold writer permission to target and start going down to source.
    // Also, we move critical section of lca node to source_dfs().
    if (hmap_get(target_tree->map, target_child)) {
        if (target_tree != lca_tree)
            writer_exit_protocol(target_tree);
        writer_exit_protocol(lca_tree);
        err = EEXIST;
    }
    else {
        err = source_dfs(lca_tree, target_tree, source, source_child,
                         target_child, component, to_grandparent_source);
    }
    return err;
}


// Moves folder source with its content to target.
int tree_move(Tree *tree, const char *source, const char *target) {
    if (!is_path_valid(source) || !is_path_valid(target))
        return EINVAL;
    if (is_root(source))
        return EBUSY;
    if (is_root(target))
        return EEXIST;
    // Custom error when source is ancestor of target.
    size_t size = strlen(source);
    if (size < strlen(target) && strncmp(source, target, size) == 0)
        return -1;

    char source_child_name[MAX_FOLDER_NAME_LENGTH + 1];
    char *source_parent_path = make_path_to_parent(source, source_child_name);
    source = source_parent_path;

    char target_child_name[MAX_FOLDER_NAME_LENGTH + 1];
    char *target_parent_path = make_path_to_parent(target, target_child_name);
    target = target_parent_path;

    Tree *source_tree = tree;
    Tree *target_tree = tree;

    size_t common = common_files(source_parent_path, target_parent_path) - 1;\
    size_t source_size = count_slashes(source_parent_path) - 1 - common;
    size_t target_size = count_slashes(target_parent_path) - 1 - common;
    int err = 0;
    char component[MAX_FOLDER_NAME_LENGTH + 1];
    if (common == 0)
        writer_entry_protocol(source_tree);
    else
        reader_entry_protocol(source_tree);

    while (common > 0) {
        source = split_path(source, component);
        target = split_path(target, component);
        if (!source)
            break;
        Tree *old_tree = source_tree;
        source_tree = hmap_get(source_tree->map, component);
        target_tree = source_tree;
        if (!source_tree) {
            reader_exit_protocol(old_tree);
            err = ENOENT;
            break;
        }
        common--;
        if (common == 0)
            writer_entry_protocol(source_tree);
        else
            reader_entry_protocol(source_tree);
        reader_exit_protocol(old_tree);
    }
    if (err != 0) {
        free(source_parent_path);
        free(target_parent_path);
        return err;
    }

    // Now we do have writer permission in LCA of source and target.
    err = target_dfs(target_tree, target, target_child_name, source,
                     source_child_name, component, target_size, source_size);

    free(source_parent_path);
    free(target_parent_path);
    return err;
}
