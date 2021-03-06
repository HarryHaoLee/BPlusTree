#ifndef BPT_H_
#define BPT_H_

#include <assert.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <unistd.h>

#include <algorithm>
#include <cstring>

#include "./predefined.h"
using std::lower_bound;
using std::swap;
using std::upper_bound;

namespace bpt {

/* offsets */
#define OFFSET_META 0
#define OFFSET_BLOCK OFFSET_META + sizeof(meta_t)
#define SIZE_NO_CHILDREN \
  sizeof(leaf_node_t<KEY_TYPE>) - BP_ORDER * sizeof(record_t<KEY_TYPE>)

/* meta information of B+ tree */
typedef struct {
  size_t order;      /* `order` of B+ tree */
  size_t value_size; /* size of value */
  size_t key_size;   /* size of key */
  size_t num_key;
  size_t internal_node_num; /* how many internal nodes */
  size_t leaf_node_num;     /* how many leafs */
  size_t height;            /* height of tree (exclude leafs) */
  off_t slot;               /* where to store new block */
  off_t root_offset;        /* where is the root of internal nodes */
  off_t leaf_offset;        /* where is the first leaf */
} meta_t;

/* internal nodes' index segment */
template <typename KEY_TYPE>
struct index_t {
  KEY_TYPE key;
  off_t child;           /* child's offset */
  uint32_t bound[2][2];  // just for aligning
};

/***
 * internal node block
 ***/
template <typename KEY_TYPE>
struct internal_node_t {
  typedef index_t<KEY_TYPE> *child_t;
  bool node_type = 0;  // label as internal
  off_t parent;        /* parent node offset */
  off_t next;
  off_t prev;
  size_t n; /* how many children */
  index_t<KEY_TYPE> children[BP_ORDER];
};

/* the final record of value */
template <typename KEY_TYPE>
struct record_t {
  KEY_TYPE key;
  value_t value;
  uint32_t bound[2][2];  // just for aligning
};

/* leaf node block */
template <typename KEY_TYPE>
struct leaf_node_t {
  typedef record_t<KEY_TYPE> *child_t;
  bool node_type = 1;  // label as leaf
  off_t parent;        /* parent node offset */
  off_t next;
  off_t prev;
  size_t n;
  record_t<KEY_TYPE> children[BP_ORDER];
};

/* helper iterating function */
template <class T>
inline typename T::child_t begin(T &node) {
  return node.children;
}

template <class T>
inline typename T::child_t end(T &node) {
  return node.children + node.n;
}

/* the encapulated B+ tree */
template <typename KEY_TYPE>
class bplus_tree {
 public:
  explicit bplus_tree(const char *path, bool force_empty = false);
  /* abstract operations */
  int search(const KEY_TYPE &key, value_t *value) const;
  int search_single(vec4_t &key, value_t *values, size_t max, vec4_t *next_key,
                    bool *next = NULL, u_int8_t key_idx = 0) const;
  int search_range(KEY_TYPE *left, const KEY_TYPE &right, value_t *values,
                   size_t max, bool *next = NULL) const;
  int search_range_single(vec4_t *left, const vec4_t &right, value_t *values,
                          size_t max, vec4_t *next_key, bool *next = NULL,
                          u_int8_t key_idx = 0) const;
  int remove(const KEY_TYPE &key);
  int insert(const KEY_TYPE &key, value_t value);
  // useless in our setting
  int update(const KEY_TYPE &key, value_t value);
  meta_t get_meta() const { return meta; }

 public:
  char path[512];
  meta_t meta;

  /* init empty tree */
  void init_from_empty();

  /* find index */
  off_t search_index(const KEY_TYPE &key) const;

  /* find leaf */
  off_t search_leaf(off_t index, const KEY_TYPE &key) const;
  off_t search_leaf(const KEY_TYPE &key) const {
    return search_leaf(search_index(key), key);
  }

  /* remove internal node */
  void remove_from_index(off_t offset, internal_node_t<KEY_TYPE> &node,
                         const KEY_TYPE &key);

  /* borrow one key from other internal node */
  bool borrow_key(bool from_right, internal_node_t<KEY_TYPE> &borrower,
                  off_t offset);

  /* borrow one record from other leaf */
  bool borrow_key(bool from_right, leaf_node_t<KEY_TYPE> &borrower);

  /* change one's parent key to another key */
  void change_parent_child(off_t parent, const KEY_TYPE &o, const KEY_TYPE &n);

  /* merge right leaf to left leaf */
  void merge_leafs(leaf_node_t<KEY_TYPE> *left, leaf_node_t<KEY_TYPE> *right);

  void merge_keys(index_t<KEY_TYPE> *where, internal_node_t<KEY_TYPE> &left,
                  internal_node_t<KEY_TYPE> &right,
                  bool change_where_key = false);

  /* insert into leaf without split */
  void insert_record_no_split(leaf_node_t<KEY_TYPE> *leaf, const KEY_TYPE &key,
                              const value_t &value);

  /* add key to the internal node */
  void insert_key_to_index(off_t offset, const KEY_TYPE &key, off_t value,
                           off_t after);
  void insert_key_to_index_no_split(internal_node_t<KEY_TYPE> &node,
                                    const KEY_TYPE &key, off_t value);

  /* change children's parent */
  void reset_index_children_parent(index_t<KEY_TYPE> *begin,
                                   index_t<KEY_TYPE> *end, off_t parent);
  template <class T>
  void node_create(off_t offset, T *node, T *next);

  template <class T>
  void node_remove(T *prev, T *node);

  /* multi-level file open/close */
  mutable FILE *fp;
  // ensure the file is opened only once
  mutable int fp_level;
  mutable void *mem_bpt = NULL;
  mutable uintmax_t file_size = 0;

  void open_file(const char *mode = "rb+") const {
    // `rb+` will make sure we can write everywhere without truncating file
    if (fp_level == 0 && !mem_bpt) {
      fp = fopen(path, mode);
      fseek(fp, 0L, SEEK_END);
      file_size = ftell(fp);
      fseek(fp, 0L, SEEK_SET);
      // file_size = std::filesystem::file_size(path);
      file_size = get_next_size(file_size);
      // truncate to standard size
      ftruncate(fp->_fileno, file_size);
      // map to memory 100 MB
      mem_bpt = mmap(NULL, file_size, PROT_READ | PROT_WRITE, MAP_SHARED,
                     fp->_fileno, 0);
    }
    ++fp_level;
  }

  uintmax_t get_next_size(uintmax_t lower_size) const {
    uintmax_t cur = DB_SIZE * 1024 * 1024;
    while (cur < lower_size) {
      cur *= 2;
    }
    return cur;
  }
  void save() {
    msync(mem_bpt, file_size, MS_SYNC);
    // munmap(mem_bpt, file_size);
  }
  void close_file() const {
    // if (fp_level == 1) fclose(fp);

    --fp_level;
  }

  /* alloc from disk */
  off_t alloc(size_t size) {
    off_t slot = meta.slot;
    meta.slot += size;
    if ((uintmax_t)meta.slot > file_size) {
      grow_file_size();
    }
    return slot;
  }

  off_t alloc(leaf_node_t<KEY_TYPE> *leaf) {
    leaf->n = 0;
    meta.leaf_node_num++;
    return alloc(sizeof(leaf_node_t<KEY_TYPE>));
  }

  off_t alloc(internal_node_t<KEY_TYPE> *node) {
    node->n = 1;
    meta.internal_node_num++;
    return alloc(sizeof(internal_node_t<KEY_TYPE>));
  }

  void unalloc(leaf_node_t<KEY_TYPE> *leaf, off_t offset) {
    --meta.leaf_node_num;
  }

  void unalloc(internal_node_t<KEY_TYPE> *node, off_t offset) {
    --meta.internal_node_num;
  }
  void grow_file_size() const {
    ftruncate(fp->_fileno, file_size * 2);
    mem_bpt = mremap(mem_bpt, file_size, file_size * 2, MREMAP_MAYMOVE);
    assert(mem_bpt != 0);
    file_size = file_size * 2;
  }

  /* read block from disk */
  int map(void *block, off_t offset, size_t size) const {
    open_file();

    memcpy(block, (void *)((char *)mem_bpt + offset), size);
    // fseek(fp, offset, SEEK_SET);
    // size_t rd = fread(block, size, 1, fp);
    close_file();

    // return rd - 1;
    return 0;
  }

  template <class T>
  int map(T *block, off_t offset) const {
    return map(block, offset, sizeof(T));
  }

  /* write block to disk */
  int unmap(void *block, off_t offset, size_t size) const {
    open_file();
    memcpy((void *)((char *)mem_bpt + offset), block, size);
    // fseek(fp, offset, SEEK_SET);
    // size_t wd = fwrite(block, size, 1, fp);
    close_file();

    // return wd - 1;
    return 0;
  }

  template <class T>
  int unmap(T *block, off_t offset) const {
    return unmap(block, offset, sizeof(T));
  }
};

template <template <class> class NODE_TYPE, class KEY_TYPE>
bool operator<(const KEY_TYPE &l, const NODE_TYPE<KEY_TYPE> &r) {
  return keycmp(l, r.key) < 0;
}

template <template <class> class NODE_TYPE, class KEY_TYPE>
bool operator<(const NODE_TYPE<KEY_TYPE> &l, const KEY_TYPE &r) {
  return keycmp(l.key, r) < 0;
}
template <template <class> class NODE_TYPE, class KEY_TYPE>
bool operator==(const KEY_TYPE &l, const NODE_TYPE<KEY_TYPE> &r) {
  return keycmp(l, r.key) == 0;
}
template <template <class> class NODE_TYPE, class KEY_TYPE>
bool operator==(const NODE_TYPE<KEY_TYPE> &l, const KEY_TYPE &r) {
  return keycmp(l.key, r) == 0;
}
/* helper searching function */
template <typename KEY_TYPE>
inline index_t<KEY_TYPE> *find(internal_node_t<KEY_TYPE> &node,
                               const KEY_TYPE &key) {
  if (key) {
    return upper_bound(begin(node), end(node) - 1, key);
  }
  // because the end of the index range is an empty string, so if we search the
  // empty key(when merge internal nodes), we need to return the second last one
  if (node.n > 1) {
    return node.children + node.n - 2;
  }
  return begin(node);
}
template <typename KEY_TYPE>
inline record_t<KEY_TYPE> *find(leaf_node_t<KEY_TYPE> &node,
                                const KEY_TYPE &key) {
  // lower_bound
  // Returns an iterator pointing to the first element in the
  // range [first,last) which does not compare less than val.
  return lower_bound(begin(node), end(node), key);
}
}  // namespace bpt

#endif  // BPT_H_
