#include <assert.h>
#include <ctype.h>
#include <inttypes.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define VERSION 6

#define MAX_ENV_SIZE 64
#define MAX_ERROR_LENGTH 1024
#define MAX_FORM_COUNT 512
#define MAX_FRAME_COUNT 64
#define MAX_FUNC_COUNT 64
#define MAX_FUNC_ARG_COUNT 8
#define MAX_FUNC_RET_COUNT 8
#define MAX_NAME_LENGTH 64
#define MAX_OP_COUNT 1024
#define MAX_POS_SOURCE_LENGTH 255
#define MAX_REG_COUNT 64
#define MAX_SCOPE_COUNT 8
#define MAX_STACK_SIZE 64
#define MAX_STATE_COUNT 64

typedef int16_t reg_t;
typedef int32_t int_t;
typedef uint16_t nrefs_t;

enum emit_res {EMIT_OK, EMIT_ERROR}; 
enum eval_res {EVAL_OK, EVAL_ERROR};
enum read_res {READ_OK, READ_NULL, READ_ERROR};

#define BASEOF(p, t, m) ({			\
      uint8_t *_p = (uint8_t *)(p);		\
      _p ? ((t *)(_p - offsetof(t, m))) : NULL;	\
    })

/*** Intrusive double-linked lists
     A trivial intrusive implementation of double-linked lists.
***/

struct ls {
  struct ls *prev, *next;
};

void ls_init(struct ls *self) {
  self->prev = self->next = self;
}

bool ls_null(const struct ls *self) {
  return self->prev == self && self->next == self;
}

void ls_ins(struct ls *self, struct ls *it) {
  it->prev = self->prev;
  self->prev->next = it;
  it->next = self;
  self->prev = it;
}

struct ls *ls_del(struct ls *self) {
  self->prev->next = self->next;
  self->next->prev = self->prev;
  return self;
}

#define _CONCAT(x, y)				\
  x##y

#define CONCAT(x, y)				\
  _CONCAT(x, y)

#define UNIQUE(x)				\
  CONCAT(x, __COUNTER__)

#define _LS_DO(in, i, _next)				\
  for (struct ls *i = (in)->next, *_next = i->next;	\
       i != (in);					\
       i = _next, _next = i->next)

#define LS_DO(in, i)				\
  _LS_DO(in, i, UNIQUE(next))

struct pos {
  char source[MAX_POS_SOURCE_LENGTH];
  uint16_t line, column;
};

struct pos *pos_init(struct pos *self, const char *source, int line, int column) {
  strcpy(self->source, source);
  self->line = line;
  self->column = column;
  return self;
}

/*** Types ***
     Types define behavior for values and are used for type-checking at compile- and runtime.
***/

struct form;
struct val;
struct vm;

struct type {
  char name[MAX_NAME_LENGTH];
  
  struct { 
    void (*dump)(struct val *val, FILE *out);
    enum emit_res (*emit)(struct val *val, struct form *form, struct ls *in, struct vm *vm);
    bool (*equal)(struct val *x, struct val *y);
    bool (*is_true)(struct val *val);
    struct val *(*lit)(struct val *val);
  } methods;
};

enum emit_res default_emit(struct val *val, struct form *form, struct ls *in, struct vm *vm);

bool default_true(struct val *val) {
  return true;
}

struct val *default_lit(struct val *val) {
  return val;
}

struct type *type_init(struct type *self, const char *name) {
  assert(strlen(name) < MAX_NAME_LENGTH);
  strcpy(self->name, name);
  self->methods.dump = NULL;
  self->methods.emit = default_emit;
  self->methods.equal = NULL;
  self->methods.is_true = default_true;
  self->methods.lit = default_lit;
  return self;
}

/*** Values ***
     Every value carries a type, each type has its own data accessor in the union.
     Methods are implemented in the type.
***/

struct func;
struct macro;
struct type;

struct val {
  struct type *type;

  union {
    bool as_bool;
    struct func *as_func;
    int_t as_int;
    struct macro *as_macro;
    struct type *as_meta;
    reg_t as_reg;
  };
};

struct val *val_init(struct val *self, struct type *type) {
  self->type = type;
  return self;
}

/*** Environments ***
     Environments are ordered sets of mappings from identifiers to values.
     Each scope gets its own compile time environment.
***/

struct env_item {
  char name[MAX_NAME_LENGTH];
  struct val val;
  struct ls order;
};

struct env {
  struct env_item items[MAX_ENV_SIZE];
  uint8_t item_count;
  struct ls order;
};

struct env *env_init(struct env *self) {
  memset(self->items, 0, sizeof(self->items));
  self->item_count = 0;
  ls_init(&self->order);
  return self;
}

struct ls *env_find(struct env *self, const char *name) {
  LS_DO(&self->order, it) {
    if (strcmp(name, BASEOF(it, struct env_item, order)->name) <= 0) {
      return it;
    }
  }

  return &self->order;
}

struct val *env_set(struct env *self, const char *name) {
  struct ls *found = env_find(self, name);

  if (found != &self->order) {
    struct env_item *it = BASEOF(found, struct env_item, order);

    if (strcmp(it->name, name) == 0) {
      return NULL;
    }
  }
 
  struct env_item *it = self->items + self->item_count++;
  assert(it < self->items+MAX_ENV_SIZE);
  strcpy(it->name, name);
  ls_ins(found, &it->order);
  return &it->val;
}

struct val *env_get(struct env *self, const char *name) {
  struct ls *found = env_find(self, name);
  assert(found != &self->order);
  return &BASEOF(found, struct env_item, order)->val;
}

/*** Operations ***
     Operations are what the virtual machine evaluates, the end product of the translation, the most primitive kind of action.

     Each type of operation may specify it's own data-members by adding a struct and including it in struct op's union.

     The code is in eval(). 

     Please note that the order has to be consistent, and is thus kept alphabetical to simplify verification; 
     the consequences of getting it wrong are potentially undefined. 
     The reason is to support using computed goto in eval().
***/

struct op_branch {
  struct op *false_pc;
};

struct op_call {
  struct func *func;
};

struct op_drop {
  uint8_t count;
};

struct op_equal {
  struct val x, y;
};

struct op_jump {
  struct op *pc;
};

struct op_load {
  reg_t reg;
};

struct op_push {
  struct val val;
};

struct op_ret {
  struct func *func;
};

struct op_store {
  reg_t reg;
};

enum op_code {
  OP_BRANCH, OP_CALL, OP_DROP, OP_EQUAL, OP_JUMP, OP_LOAD, OP_NOP, OP_PUSH, OP_RET, OP_STORE,
  //---STOP---
  OP_STOP};

struct op {
  enum op_code code;
  struct form *form;
  
  union {
    struct op_branch as_branch;
    struct op_call as_call;
    struct op_drop as_drop;
    struct op_equal as_equal;
    struct op_jump as_jump;
    struct op_load as_load;
    struct op_push as_push;
    struct op_ret as_ret;
    struct op_store as_store;
  };
};

struct op *op_init(struct op *self, enum op_code code, struct form *form) {
  self->code = code;
  self->form = form;

  switch (code) {
  case OP_BRANCH:
    self->as_branch.false_pc = NULL;
    break;
  case OP_CALL:
    self->as_call.func = NULL;
    break;
  case OP_DROP:
    self->as_drop.count = 1;
    break;
  case OP_EQUAL:
    self->as_equal.x.type = self->as_equal.y.type = NULL;
    break;
  case OP_JUMP:
    self->as_jump.pc = NULL;
    break;
  case OP_PUSH:
    self->as_push.val.type = NULL;
    break;
  case OP_LOAD:
    self->as_load.reg = -1;
    break;
  case OP_NOP:
    break;
  case OP_RET:
    self->as_ret.func = NULL;
    break;
  case OP_STORE:
    self->as_store.reg = -1;
    break;
  default:
    break;
  }

  return self;
}

void func_dump(struct func *self, FILE *out);
void val_dump(struct val *self, FILE *out);

void op_dump(struct op *self, FILE *out) {
  switch (self->code) {
  case OP_BRANCH:
    fprintf(out, "BRANCH ");
    op_dump(self->as_branch.false_pc, out);
    break;
  case OP_CALL:
    fprintf(out, "CALL ");
    func_dump(self->as_call.func, out);
    break;
  case OP_DROP:
    fprintf(out, "DROP %" PRIu8, self->as_drop.count);
    break;
  case OP_EQUAL:
    fprintf(out, "EQUAL ");
    if (self->as_equal.x.type) { val_dump(&self->as_equal.x, out); }
    if (self->as_equal.y.type) { val_dump(&self->as_equal.y, out); }
    break;
  case OP_JUMP:
    fprintf(out, "JUMP ");
    op_dump(self->as_jump.pc, out);
    break;
  case OP_LOAD:
    fprintf(out, "LOAD %" PRId16, self->as_load.reg);
    break;
  case OP_NOP:
    fputs("NOP", out);
    break;
  case OP_PUSH:
    fputs("PUSH ", out);
    val_dump(&self->as_push.val, out);
    break;
  case OP_RET:
    fprintf(out, "RET ");
    func_dump(self->as_ret.func, out);
    break;
  case OP_STORE:
    fprintf(out, "STORE %" PRId16, self->as_store.reg);
    break;
    //---STOP---
  case OP_STOP:
    fputs("STOP", out);
    break;
  }
}

/*** Forms ***
     Code is read as forms, which are then emitted as operations.
     Each form carries its source position.
***/

struct form_group {
  struct ls items;
};

struct form_id {
  char name[MAX_NAME_LENGTH];
};

struct form_lit {
  struct val val;
};

enum form_type {FORM_GROUP, FORM_ID, FORM_LIT, FORM_SEMI};

struct form {
  struct ls ls;
  enum form_type type;
  struct pos pos;

  union {
    struct form_group as_group;
    struct form_id as_id;
    struct form_lit as_lit;
  };
};

struct form *form_init(struct form *self, enum form_type type, struct pos pos, struct ls *out) {
  self->type = type;
  self->pos = pos;
  if (out) { ls_ins(out, &self->ls); }

  switch (type) {
  case FORM_GROUP:
    ls_init(&self->as_group.items);
    break;
  case FORM_LIT:
    self->as_lit.val.type = NULL;
    break;
  case FORM_ID:
  case FORM_SEMI:
    break;
  }

  return self;
}

struct val *find(struct vm *vm, const char *name);
struct val *val_lit(struct val *self);

struct val *form_val(struct form *self, struct vm *vm) {
  switch (self->type) {
  case FORM_ID: {
    struct val *v = find(vm, self->as_id.name);
    if (!v) { break; }
    return val_lit(v);
  }
    
  case FORM_LIT:
    return &self->as_lit.val;

  case FORM_GROUP:
  case FORM_SEMI:
    break;
  }

  return NULL;
}

struct op *emit(struct vm *vm, enum op_code code, struct form *form);

enum emit_res default_emit(struct val *val, struct form *form, struct ls *in, struct vm *vm) {
  emit(vm, OP_PUSH, form)->as_push.val = *val;
  return EMIT_OK;
}

void error(struct vm *vm, struct pos pos, const char *fmt, ...);
enum emit_res val_emit(struct val *self, struct form *form, struct ls *in, struct vm *vm);

enum emit_res form_emit(struct form *self, struct ls *in, struct vm *vm) {
  switch (self->type) {
  case FORM_GROUP: {
    struct ls *items = &self->as_group.items;
    
    for (struct ls *it = items->next; it != items; it = items->next) {
      ls_del(it);
      struct form *f = BASEOF(it, struct form, ls);
      enum emit_res res = form_emit(f, items, vm);
      if (res != EMIT_OK) { return res; }
    }

    return EMIT_OK;
  }
    
  case FORM_ID: {
    const char *name = self->as_id.name;
    uint8_t drop_count = 0;
    
    for (const char *c = name; *c; c++, drop_count++) {
      if (*c != 'd') {
	drop_count = 0;
	break;
      }
    }

    if (drop_count) {
      emit(vm, OP_DROP, self)->as_drop.count = drop_count;
      return EMIT_OK;
    }
    
    struct val *v = find(vm, name);

    if (!v) {
      error(vm, self->pos, "Unknown id: %s", name);
      return EMIT_ERROR;
    }
    
    return val_emit(v, self, in, vm);
  }
    
  case FORM_LIT:
    emit(vm, OP_PUSH, self)->as_push.val = self->as_lit.val;
    return EMIT_OK;
  case FORM_SEMI:
    error(vm, self->pos, "Semi emit");
    break;
  }
  
  return EMIT_ERROR;
}

enum emit_res emit_forms(struct vm *vm, struct ls *in) {
  while (!ls_null(in)) {
    struct form *f = BASEOF(ls_del(in->next), struct form, ls);
    enum emit_res fr = form_emit(f, in, vm);
    if (fr != EMIT_OK) { return fr; }
  }

  return EMIT_OK;
}

/*** Macros
 ***/

typedef enum emit_res (*macro_body_t)(struct macro *self, struct form *form, struct ls *in, struct vm *vm);

struct macro {
  char name[MAX_NAME_LENGTH];
  uint8_t nargs;
  macro_body_t body;
};

struct macro *macro_init(struct macro *self, const char *name, uint8_t nargs, macro_body_t body) {
  assert(strlen(name) < MAX_NAME_LENGTH);
  strcpy(self->name, name);
  self->nargs = nargs;
  self->body = body;
  return self;
}

/*** Functions
 ***/

typedef struct op *(*func_body_t)(struct func *self, struct op *ret_pc, struct vm *vm);

struct func_arg {
  char name[MAX_NAME_LENGTH];
  struct type *type;
};

struct func {
  char name[MAX_NAME_LENGTH];
  struct func_arg args[MAX_FUNC_ARG_COUNT];
  uint8_t nargs;
  struct type *rets[MAX_FUNC_RET_COUNT];
  uint8_t nrets;
  func_body_t body;
  struct op *start_pc;
};

struct func_arg arg(const char *name, struct type *type) {
  struct func_arg arg;
  strcpy(arg.name, name);
  arg.type = type;
  return arg;
}
  
struct func *func_init(struct func *self,
		       const char *name,
		       uint8_t nargs, struct func_arg args[],
		       uint8_t nrets, struct type *rets[],
		       func_body_t body) {
  assert(strlen(name) < MAX_NAME_LENGTH);
  strcpy(self->name, name);
  self->nargs = nargs;
  memcpy(self->args, args, nargs*sizeof(struct func_arg));
  self->nrets = nrets;
  memcpy(self->rets, rets, nrets*sizeof(struct type *));
  self->body = body;
  self->start_pc = NULL;
  return self;
}

void func_dump(struct func *self, FILE *out) {
  fputs(self->name, out);
}

struct op *pc(struct vm *vm);

enum emit_res func_emit(struct func *self, struct form *form, struct ls *in, struct vm *vm) {
  struct op_jump *skip = &emit(vm, OP_JUMP, form)->as_jump;
  self->start_pc = pc(vm);
  enum emit_res res = form_emit(form, in, vm);
  if (res != EMIT_OK) { return res; }
  emit(vm, OP_RET, form)->as_ret.func = self;
  skip->pc = pc(vm);
  return EMIT_OK;
}

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

struct frame {
  struct func *func;
  struct op *ret_pc;
};

/*** Virtual Machines
     The VM is the engine of the interpreter, the one struct to rule them all.
***/

struct vm {
  struct type bool_type, func_type, int_type, meta_type;

  struct func funcs[MAX_FUNC_COUNT];
  uint32_t func_count;
  
  struct form forms[MAX_FORM_COUNT];
  uint32_t form_count;
  
  struct scope scopes[MAX_SCOPE_COUNT];
  uint32_t scope_count;

  struct op ops[MAX_OP_COUNT];
  uint32_t op_count;

  struct state states[MAX_STATE_COUNT];
  uint32_t state_count;

  struct frame frames[MAX_FRAME_COUNT];
  uint32_t frame_count;

  char error[MAX_ERROR_LENGTH];
  bool debug;
};

struct val *bind_init(struct vm *vm, const char *name, struct type *type);
struct scope *push_scope(struct vm *vm);

void bool_dump(struct val *val, FILE *out) {
  fputs(val->as_bool ? "T" : "F", out);
}

bool bool_equal(struct val *x, struct val *y) {
  return x->as_bool == y->as_bool;
}

bool bool_true(struct val *val) {
  return val->as_bool;
}

void func_val_dump(struct val *val, FILE *out) {
  func_dump(val->as_func, out);
}

enum emit_res func_val_emit(struct val *val, struct form *form, struct ls *in, struct vm *vm) {
  for (uint8_t i = 0; i < val->as_func->nargs; i++) {
    struct form *f = BASEOF(ls_del(in->next), struct form, ls);
    enum emit_res res = form_emit(f, in, vm);
    if (res != EMIT_OK) { return res; }
  }
       
  emit(vm, OP_CALL, form)->as_call.func = val->as_func;
  return EMIT_OK;
}

struct val *func_val_lit(struct val *val) {
  return NULL;
}

void int_dump(struct val *val, FILE *out) {
  fprintf(out, "%" PRId32, val->as_int);
}

bool int_equal(struct val *x, struct val *y) {
  return x->as_int == y->as_int;
}

bool int_true(struct val *val) {
  return val->as_int;
}

void meta_dump(struct val *val, FILE *out) {
  fputs(val->as_meta->name, out);
}

struct vm *vm_init(struct vm *self) {
  self->scope_count = 0;

  for (struct scope *s = self->scopes, *ps = NULL; s < self->scopes+MAX_SCOPE_COUNT; ps = s, s++) {
    s->parent_scope = ps;
  }

  self->func_count = 0;
  self->frame_count = 0;
  self->op_count = 0;
  self->state_count = 0;
  self->frame_count = 0;
  *self->error = 0;
  self->debug = false;
  push_scope(self);

  type_init(&self->meta_type, "Meta");
  self->meta_type.methods.dump = meta_dump;
  bind_init(self, "Meta", &self->meta_type)->as_meta = &self->meta_type;

  type_init(&self->bool_type, "Bool");
  self->bool_type.methods.dump = bool_dump;
  self->bool_type.methods.equal = bool_equal;  
  self->bool_type.methods.is_true = bool_true;
  bind_init(self, "Bool", &self->meta_type)->as_meta = &self->bool_type;

  bind_init(self, "T", &self->bool_type)->as_bool = true;
  bind_init(self, "F", &self->bool_type)->as_bool = false;

  type_init(&self->func_type, "Func");
  self->func_type.methods.dump = func_val_dump;
  self->func_type.methods.emit = func_val_emit;
  self->func_type.methods.lit = func_val_lit;
  bind_init(self, "Func", &self->meta_type)->as_meta = &self->func_type;

  type_init(&self->int_type, "Int");
  self->int_type.methods.dump = int_dump;
  self->int_type.methods.equal = int_equal;
  self->int_type.methods.is_true = int_true;
  bind_init(self, "Int", &self->meta_type)->as_meta = &self->int_type;

  return self;
}

struct form *new_form(struct vm *vm, enum form_type type, struct pos pos, struct ls *out) {
  struct form *self = vm->forms + vm->form_count++;
  return form_init(self, type, pos, out);
}

void val_dump(struct val *self, FILE *out) {
  assert(self->type->methods.dump);
  self->type->methods.dump(self, out);
}

enum emit_res val_emit(struct val *self, struct form *form, struct ls *in, struct vm *vm) {
  assert(self->type->methods.emit);
  return self->type->methods.emit(self, form, in, vm);
}

bool val_equal(struct val *self, struct val *other) {
  assert(self->type->methods.equal);
  return self->type->methods.equal(self, other);
}

bool val_true(struct val *self) {
  assert(self->type->methods.is_true);
  return self->type->methods.is_true(self);
}

struct val *val_lit(struct val *self) {
  assert(self->type->methods.lit);
  return self->type->methods.lit(self);
}

struct state *state_init(struct state *self) {
  memset(self->regs, 0, sizeof(self->regs));
  self->stack_size = 0;
  return self;
}

struct frame *frame_init(struct frame *self, struct func *func, struct op *ret_pc) {
  self->func = func;
  self->ret_pc = ret_pc;
  return self;
}

void verror(struct vm *vm, struct pos pos, const char *fmt, va_list args) {
  int n = snprintf(vm->error, MAX_ERROR_LENGTH,
		   "Error in %s, line %" PRIu16 " column %" PRIu16 ": ",
		   pos.source, pos.line, pos.column);
  
  assert(vsnprintf(vm->error+n, MAX_ERROR_LENGTH-n, fmt, args) > 0);
}

void error(struct vm *vm, struct pos pos, const char *fmt, ...) {
  va_list args;
  va_start(args, fmt);
  verror(vm, pos, fmt, args);
  va_end(args);
}

struct scope *scope_init(struct scope *self, struct vm *vm);

struct scope *push_scope(struct vm *vm) {
  assert(vm->scope_count < MAX_SCOPE_COUNT);
  return scope_init(vm->scopes+vm->scope_count++, vm);
}

struct scope *peek_scope(struct vm *vm) {
  assert(vm->scope_count);
  return vm->scopes+vm->scope_count-1;
}

struct val *bind(struct vm *vm, const char *name) {
  return env_set(&peek_scope(vm)->bindings, name);
}

struct val *bind_init(struct vm *vm, const char *name, struct type *type) {
  return val_init(bind(vm, name), type);
}

struct val *find(struct vm *vm, const char *name) {
  struct scope *scope = peek_scope(vm);
  return env_get(&scope->bindings, name);
}

struct state *push_state(struct vm *vm) {
  assert(vm->state_count < MAX_STATE_COUNT);
  return state_init(vm->states+vm->state_count++);
}
			 
struct state *peek_state(struct vm *vm) {
  assert(vm->state_count);
  return vm->states+vm->state_count-1;
}

struct state *pop_state(struct vm *vm) {
  assert(vm->state_count);
  return vm->states + --vm->state_count;
}

struct frame *push_frame(struct vm *vm, struct func *func, struct op *ret_pc) {
  push_state(vm);
  assert(vm->frame_count < MAX_FRAME_COUNT);
  return frame_init(vm->frames+vm->frame_count++, func, ret_pc);
}
			 
struct frame *peek_frame(struct vm *vm) {
  assert(vm->frame_count);
  return vm->frames+vm->frame_count-1;
}

struct frame *pop_frame(struct vm *vm) {
  assert(vm->frame_count);
  pop_state(vm);
  return vm->frames + --vm->frame_count;
}

struct op *emit(struct vm *vm, enum op_code code, struct form *form) {
  assert(vm->op_count < MAX_OP_COUNT);
  struct op *op = op_init(vm->ops + vm->op_count++, code, form);
  return op;
}

struct op *pc(struct vm *vm) {
  return vm->ops + vm->op_count;
}

struct val *reg(struct vm *vm, reg_t reg) {
  assert(reg < MAX_REG_COUNT);
  return peek_state(vm)->regs+reg;
}

struct val *push(struct vm *vm) {
  struct state *s = peek_state(vm);
  return s->stack+s->stack_size++;
}

struct val *push_init(struct vm *vm, struct type *type) {
  struct state *s = peek_state(vm);
  return val_init(s->stack+s->stack_size++, type);
}

struct val *peek(struct vm *vm) {
  struct state *s = peek_state(vm);
  assert(s->stack_size);
  return s->stack+s->stack_size-1;
}

struct val *pop(struct vm *vm) {
  struct state *s = peek_state(vm);
  assert(s->stack_size);
  return s->stack + --s->stack_size;
}

void dump_stack(struct vm *vm, FILE *out) {
  fputc('[', out);
  struct state *s = peek_state(vm);
  
  for (struct val *v = s->stack; v < s->stack + s->stack_size; v++) {
    if (v > s->stack) { fputc(' ', out); }
    val_dump(v, out);
  }

  fputc(']', out);
}

struct scope *scope_init(struct scope *self, struct vm *vm) {
  env_init(&self->bindings);
  self->reg_count = self->parent_scope ? self->parent_scope->reg_count : 0;
  return self;
}

#define DISPATCH(next_op)						\
  if (vm->debug) { op_dump(next_op, stdout); fputc('\n', stdout); }	\
  goto *dispatch[(op = next_op)->code]
  
enum eval_res eval(struct vm *vm, struct op *start_pc) {
  static const void* dispatch[] = {
    &&BRANCH, &&CALL, &&DROP, &&EQUAL, &&JUMP, &&LOAD, &&NOP, &&PUSH, &&RET, &&STORE,
    //---STOP---
    &&STOP};

  struct op *op = start_pc;
  DISPATCH(op);

 BRANCH: {
    struct op_branch *branch = &op->as_branch;
    DISPATCH(val_true(pop(vm)) ? op+1 : branch->false_pc);
  }

 CALL: {
    struct op_call *call = &op->as_call;
    DISPATCH(call->func->body(call->func, op+1, vm));
  }
  
 DROP: {
    struct op_drop *drop = &op->as_drop;
    struct state *state = peek_state(vm);
    if (state->stack_size < drop->count) {
      error(vm, op->form->pos, "Not enough values");
      return EVAL_ERROR;
    }
    
    state->stack_size -= drop->count; 
    DISPATCH(op+1);
  }
  
 EQUAL: {
    struct op_equal *equal = &op->as_equal;
    struct val x = equal->x, y = equal->y;
    if (!y.type) { y = *pop(vm); }
    if (!x.type) { x = *pop(vm); }
    push_init(vm, &vm->bool_type)->as_bool = val_equal(&x, &y);
    DISPATCH(op+1);
  }

 JUMP: {
    struct op_jump *jump = &op->as_jump;
    DISPATCH(jump->pc);
  }

 LOAD: {
    *reg(vm, op->as_load.reg) = *pop(vm);
    DISPATCH(op+1);
  }

 NOP: {
    while ((op+1)->code == OP_NOP) { op++; }
    DISPATCH(op);
  }
  
 PUSH: {
    *push(vm) = op->as_push.val;
    DISPATCH(op+1);
  }

 RET: {
    struct frame *f = pop_frame(vm);
    DISPATCH(f->ret_pc);
  }
  
 STORE: {
    *push(vm) = *reg(vm, op->as_store.reg);
    DISPATCH(op+1);    
  }
  
 STOP: {}

  return EVAL_OK;
}

/*** Readers
     Readers transform code into forms.
     New readers must be added in the right order to read_form().
***/

typedef enum read_res(*reader_t)(struct vm *vm, struct pos *pos, FILE *in, struct ls *out);

enum read_res read_group(struct vm *vm, struct pos *pos, FILE *in, struct ls *out);
enum read_res read_id(struct vm *vm, struct pos *pos, FILE *in, struct ls *out);
enum read_res read_int(struct vm *vm, struct pos *pos, FILE *in, struct ls *out);
enum read_res read_semi(struct vm *vm, struct pos *pos, FILE *in, struct ls *out);
enum read_res read_ws(struct vm *vm, struct pos *pos, FILE *in, struct ls *out);

enum read_res read_form(struct vm *vm, struct pos *pos, FILE *in, struct ls *out) {
  static const int COUNT = 5;
  static const reader_t readers[COUNT] = {read_ws, read_int, read_semi, read_group, read_id};

  for (int i=0; i < COUNT; i++) {
    switch (readers[i](vm, pos, in, out)) {
    case READ_OK:
      return READ_OK;
    case READ_NULL:
      break;
    case READ_ERROR:
      return READ_ERROR;
    }
  }

  return READ_NULL;
}

enum read_res read_group(struct vm *vm, struct pos *pos, FILE *in, struct ls *out) {
  struct pos fpos = *pos;  
  char c = fgetc(in);
  
  if (c != '(') {
    ungetc(c, in);
    return READ_NULL;
  }

  pos->column++;
  struct form *f = new_form(vm, FORM_GROUP, fpos, out);

  for (;;) {
    c = fgetc(in);

    if (!c || c == ')') {
      break;
    }

    ungetc(c, in);

    enum read_res res = read_form(vm, pos, in, &f->as_group.items);

    switch (res) {
    case READ_NULL:
      c = 0;
      break;
    case READ_ERROR:
      return res;
    case READ_OK:
      break;
    }
  }

  if (!c) {
    error(vm, fpos, "Open group");
    return READ_ERROR;
  }

  pos->column++;
  return READ_OK;
}

enum read_res read_id(struct vm *vm, struct pos *pos, FILE *in, struct ls *out) {
  struct pos fpos = *pos;
  char name[MAX_NAME_LENGTH], *p = name, c = 0;
  
  while ((c = fgetc(in))) {
    assert(p < name + MAX_NAME_LENGTH);

    if (isspace(c) || c == '(' || c == ')' || c == ';') {
      ungetc(c, in);
      break;
    }
    
    *p++ = c;
    pos->column++;
  }

  if (p == name) { return READ_NULL; }
  
  *p = 0;
  struct form *f = new_form(vm, FORM_ID, fpos, out);
  strcpy(f->as_id.name, name);
  return READ_OK;
}

enum read_res read_int(struct vm *vm, struct pos *pos, FILE *in, struct ls *out) {
  int_t v = 0;
  bool neg = false;
  struct pos fpos = *pos;
  
  char c = fgetc(in);
  if (!c) { return READ_NULL; }

  if (c == '-') {
    c = fgetc(in);

    if (isdigit(c)) {
      neg = true;
      pos->column++;
      ungetc(c, in);
    } else {
      ungetc(c, in);
      ungetc('-', in);
      return READ_NULL;
    }
  } else {
    ungetc(c, in);
  }

  while ((c = fgetc(in))) {
    if (!isdigit(c)) {
      ungetc(c, in);
      break;
    }
    
    v *= 10;
    v += c - '0';
    pos->column++;
  }

  if (pos->column == fpos.column) { return READ_NULL; }
  struct form *f = new_form(vm, FORM_LIT, fpos, out);
  val_init(&f->as_lit.val, &vm->int_type)->as_int = neg ? -v : v;
  return READ_OK;
}

enum read_res read_semi(struct vm *vm, struct pos *pos, FILE *in, struct ls *out) {
  struct pos fpos = *pos;
  char c = 0;
  
  if (!(c = fgetc(in))) { return READ_NULL; }
  
  if (c != ';') {
    ungetc(c, in);
    return READ_NULL;
  }
  
  pos->column++;
  new_form(vm, FORM_SEMI, fpos, out);
  return READ_OK;
}
    
enum read_res read_ws(struct vm *vm, struct pos *pos, FILE *in, struct ls *out) {
  char c = 0;

  while ((c = fgetc(in))) {
    switch (c) {
    case ' ':
    case '\t':
      pos->column++;
      break;
    case '\n':
      pos->line++;
      pos->column = 0;
      break;
    default:
      ungetc(c, in);
      return READ_NULL;
    }
  }
  
  return READ_NULL;
}

void macro_dump(struct val *val, FILE *out) {
  fprintf(out, "Macro(%s)", val->as_macro->name);
}

enum emit_res macro_emit(struct val *val, struct form *form, struct ls *in, struct vm *vm) {
  struct macro *self = val->as_macro;
  struct ls *a = in->next;

  for (uint8_t i = 0; i < self->nargs; i++, a = a->next) {
    if (a == in) {
      error(vm, form->pos, "Missing macro arguments: %s %" PRIu8, self->name, i);
      return EMIT_ERROR;
    }
  }

  return self->body(self, form, in, vm);
}

struct val *macro_lit(struct val *val) {
  return NULL;
}

struct op *add_body(struct func *self, struct op *ret_pc, struct vm *vm) {
  struct val y = *pop(vm);
  struct val *x = peek(vm);
  x->as_int += y.as_int;
  return ret_pc;
}

struct op *debug_body(struct func *self, struct op *ret_pc, struct vm *vm) {
  vm->debug = !vm->debug;
  push_init(vm, &vm->bool_type)->as_bool = vm->debug;
  return ret_pc;
}

enum emit_res equal_body(struct macro *self, struct form *form, struct ls *in, struct vm *vm) {
  struct op_equal *op = &emit(vm, OP_EQUAL, form)->as_equal;

  struct form *x = BASEOF(ls_del(in->next), struct form, ls);
  struct val *xv = form_val(x, vm);
  
  if (xv) {
    op->x = *xv;
  } else {
    enum emit_res fr = form_emit(x, in, vm);
    if (fr != EMIT_OK) { return fr; }
  }

  struct form *y = BASEOF(ls_del(in->next), struct form, ls);
  struct val *yv = form_val(y, vm);

  if (yv) {
    op->y = *yv;
  } else {
    enum emit_res fr = form_emit(y, in, vm);
    if (fr != EMIT_OK) { return fr; }
  }

  return EMIT_OK;
}

struct op *__func_body(struct func *self, struct op *ret_pc, struct vm *vm) {
  push_frame(vm, self, ret_pc);
  return self->start_pc;
}

enum emit_res func_body(struct macro *self, struct form *form, struct ls *in, struct vm *vm) {
  struct form *name_form = BASEOF(ls_del(in->next), struct form, ls);
  const char *name = name_form->as_id.name;
  
  struct form *args_form = BASEOF(ls_del(in->next), struct form, ls);
  struct func_arg args[MAX_FUNC_ARG_COUNT];
  uint8_t nargs = 0;
  
  struct form *rets_form = BASEOF(ls_del(in->next), struct form, ls);
  struct type *rets[MAX_FUNC_RET_COUNT];
  uint8_t nrets = 0;

  struct func *func = func_init(vm->funcs + vm->func_count++, name, nargs, args, nrets, rets, __func_body);
  struct form *body = BASEOF(ls_del(in->next), struct form, ls);
  enum emit_res res = func_emit(func, body, in, vm);
  if (res != EMIT_OK) { return res; }
  
  if (strcmp(name, "_") == 0) {
    push_init(vm, &vm->func_type)->as_func = func;
  } else {
    bind_init(vm, name, &vm->func_type)->as_func = func; 
  }

  return EMIT_OK;
}

enum emit_res if_body(struct macro *self, struct form *form, struct ls *in, struct vm *vm) {
  struct form *cf = BASEOF(ls_del(in->next), struct form, ls);
  enum emit_res fr = form_emit(cf, in, vm);
  if (fr != EMIT_OK) { return fr; }
  struct op_branch *b = &emit(vm, OP_BRANCH, form)->as_branch;

  struct form *tf = BASEOF(ls_del(in->next), struct form, ls);
  fr = form_emit(tf, in, vm);
  if (fr != EMIT_OK) { return fr; }

  struct op_jump *j = &emit(vm, OP_JUMP, form)->as_jump;
  b->false_pc = pc(vm);
  struct form *ff = BASEOF(ls_del(in->next), struct form, ls);
  fr = form_emit(ff, in, vm);
  if (fr != EMIT_OK) { return fr; }
  j->pc = pc(vm);
  
  return EMIT_OK;
}

enum emit_res nop_body(struct macro *self, struct form *form, struct ls *in, struct vm *vm) {
  return EMIT_OK;
}

struct op *sub_body(struct func *self, struct op *ret_pc, struct vm *vm) {
  struct val y = *pop(vm);
  struct val *x = peek(vm);
  x->as_int -= y.as_int;
  return ret_pc;
}

int main () {
  printf("fibr %d\n\n", VERSION);
  
  struct vm vm;
  vm_init(&vm);
  push_state(&vm);

  struct type macro_type;
  type_init(&macro_type, "Macro");
  macro_type.methods.dump = macro_dump;
  macro_type.methods.emit = macro_emit;
  macro_type.methods.lit = macro_lit;
  bind_init(&vm, "Macro", &vm.meta_type)->as_meta = &macro_type;
  
  struct func add_func;
  func_init(&add_func, "+",
	    2, (struct func_arg[]){arg("x", &vm.int_type), arg("y", &vm.int_type)},
	    1, (struct type *[]){&vm.int_type},
	    add_body);
  bind_init(&vm, "+", &vm.func_type)->as_func = &add_func;

  struct func debug_func;
  func_init(&debug_func, "debug",
	    0, (struct func_arg[]){},
	    1, (struct type *[]){&vm.bool_type},
	    debug_body);
  bind_init(&vm, "debug", &vm.func_type)->as_func = &debug_func;

  struct macro equal_macro;
  macro_init(&equal_macro, "=", 2, equal_body);
  bind_init(&vm, "=", &macro_type)->as_macro = &equal_macro;

  struct macro func_macro;
  macro_init(&func_macro, "func", 4, func_body);
  bind_init(&vm, "func", &macro_type)->as_macro = &func_macro;

  struct macro if_macro;
  macro_init(&if_macro, "if", 3, if_body);
  bind_init(&vm, "if", &macro_type)->as_macro = &if_macro;

  struct macro nop_macro;
  macro_init(&nop_macro, "_", 0, nop_body);
  bind_init(&vm, "_", &macro_type)->as_macro = &nop_macro;

  struct func sub_func;
  func_init(&sub_func, "-",
	    2, (struct func_arg[]){arg("x", &vm.int_type), arg("y", &vm.int_type)},
	    1, (struct type *[]){&vm.int_type},
	    sub_body);
  bind_init(&vm, "-", &vm.func_type)->as_func = &sub_func;

  struct pos pos;
  pos_init(&pos, "repl", 0, 0);

  while (!feof(stdin)) {
    struct ls forms;
    ls_init(&forms);

    while (read_form(&vm, &pos, stdin, &forms) == READ_OK) {
      struct form *f = BASEOF(forms.prev, struct form, ls);

      if (f->type == FORM_SEMI) {
	ls_del(forms.prev);
	break;
      }
    }
    
    struct op *start_pc = pc(&vm);

    if (emit_forms(&vm, &forms) != EMIT_OK) {
      printf("%s\n", vm.error);
      continue;
    }
  
    emit(&vm, OP_STOP, NULL);
    
    if (eval(&vm, start_pc) != EVAL_OK) {
      printf("%s\n", vm.error);
      continue;
    }

    dump_stack(&vm, stdout);
    fputc('\n', stdout);
  }
  
  return 0;
}
