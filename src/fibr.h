#ifndef FIBR_H
#define FIBR_H

#define baseof(p, t, m) ({			\
      uint8_t *_p = (uint8_t *)(p);		\
      _p ? ((t *)(_p - offsetof(t, m))) : NULL;	\
    })

#define _CONCAT(x, y)				\
  x##y

#define CONCAT(x, y)				\
  _CONCAT(x, y)

#define UNIQUE(x)				\
  CONCAT(x, __COUNTER__)

typedef int32_t int_t;
typedef uint16_t nrefs_t;
typedef int16_t reg_t;

const uint32_t VERSION = 1;

const uint8_t MAX_ENV_SIZE = 64;
const uint16_t MAX_ERROR_LENGTH = 1024;
const uint8_t MAX_NAME_LENGTH = 64;
const uint16_t MAX_OP_COUNT = 1024;
const uint8_t MAX_POS_SOURCE_LENGTH = 255;
const reg_t MAX_REG_COUNT = 64;
const uint8_t MAX_SCOPE_COUNT = 8;
const uint8_t MAX_STACK_SIZE = 64;
const uint8_t MAX_STATE_COUNT = 64;

#define _ls_do(ls, i, _next)				\
  for (struct ls *i = (ls)->next, *_next = i->next;	\
       i != (ls);					\
       i = _next, _next = i->next)

#define ls_do(ls, i)				\
  _ls_do(ls, i, UNIQUE(next))

struct ls {
  struct ls *prev, *next;
};

enum emit_result {EMIT_OK, EMIT_ERROR}; 
enum eval_result {EVAL_OK, EVAL_ERROR};
enum read_result {READ_OK, READ_NULL, READ_ERROR};

struct pos {
  char source[MAX_POS_SOURCE_LENGTH];
  uint16_t line, column;
};

struct macro;
struct type;
struct vm;

struct val {
  struct type *type;

  union {
    bool as_bool;
    int_t as_int;
    struct macro *as_macro;
    struct type *as_meta;
    reg_t as_reg;
  };
};

struct env_item {
  char name[MAX_NAME_LENGTH];
  struct val val;
};

struct env {
  struct env_item items[MAX_ENV_SIZE];
  uint8_t item_count;
};

struct form_id {
  char name[MAX_NAME_LENGTH];
};

struct form_literal {
  struct val val;
};

enum form_type {FORM_ID, FORM_LITERAL, FORM_SEMI};

struct form {
  struct ls ls;
  enum form_type type;
  struct pos pos;
  nrefs_t nrefs;

  union {
    struct form_id as_id;
    struct form_literal as_literal;
  };
};

struct type {
  char name[MAX_NAME_LENGTH];
  
  struct { 
    void (*dump)(struct val *val, FILE *out);
    enum emit_result (*emit)(struct val *val, struct form *form, struct ls *in, struct vm *vm);
    bool (*equal)(struct val *x, struct val *y);
    bool (*is_true)(struct val *val);
    struct val *(*literal)(struct val *val);
  } methods;
};

typedef enum emit_result (*macro_body_t)(struct macro *self, struct form *form, struct ls *in, struct vm *vm);

struct macro {
  char name[MAX_NAME_LENGTH];
  uint8_t nargs;
  macro_body_t body;
};

struct op_equal {
  struct val x, y;
};

struct op_load {
  reg_t reg;
};

struct op_push {
  struct val val;
};

struct op_store {
  reg_t reg;
};

enum op_code {
  OP_EQUAL, OP_LOAD, OP_PUSH, OP_STORE,
  //---STOP---
  OP_STOP};

struct op {
  enum op_code code;
  struct form *form;
  
  union {
    struct op_equal as_equal;
    struct op_load as_load;
    struct op_push as_push;
    struct op_store as_store;
  };
};

struct scope {
  struct scope *parent_scope;
  struct env bindings;
  reg_t reg_count;
};

struct state {
  struct val regs[MAX_REG_COUNT];
  struct val stack[MAX_STACK_SIZE];
  uint8_t stack_size;
};

struct vm {
  struct type bool_type, int_type, meta_type;
  
  struct scope scopes[MAX_SCOPE_COUNT];
  uint8_t scope_count;

  struct op ops[MAX_OP_COUNT];
  uint16_t op_count;

  struct state states[MAX_STATE_COUNT];
  uint8_t state_count;

  char error[MAX_ERROR_LENGTH];
  bool debug;
};

struct val *bind_init(struct vm *vm, const char *name, struct type *type);
struct scope *push_scope(struct vm *vm);

typedef enum read_result(*reader_t)(struct vm *vm, struct pos *pos, FILE *in, struct ls *out);

enum read_result read_form(struct vm *vm, struct pos *pos, FILE *in, struct ls *out);
enum read_result read_group(struct vm *vm, struct pos *pos, FILE *in, struct ls *out);
enum read_result read_id(struct vm *vm, struct pos *pos, FILE *in, struct ls *out);
enum read_result read_int(struct vm *vm, struct pos *pos, FILE *in, struct ls *out);
enum read_result read_semi(struct vm *vm, struct pos *pos, FILE *in, struct ls *out);
enum read_result read_ws(struct vm *vm, struct pos *pos, FILE *in, struct ls *out);

#endif
