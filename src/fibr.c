#include <assert.h>
#include <ctype.h>
#include <inttypes.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define _ls_do(ls, i, _next)				\
  for (struct ls *i = (ls)->next, *_next = i->next;	\
       i != (ls);					\
       i = _next, _next = i->next)

#define ls_do(ls, i)				\
  _ls_do(ls, i, a_unique(next))

struct ls {
  struct ls *prev, *next;
};

void ls_init(struct ls *self) { self->prev = self->next = self; }

bool ls_null(const struct ls *self) { return self->prev == self && self->next == self; }

void ls_insert(struct ls *self, struct ls *it) {
  it->prev = self->prev;
  self->prev->next = it;
  it->next = self;
  self->prev = it;
}

struct ls *ls_delete(struct ls *self) {
  self->prev->next = self->next;
  self->next->prev = self->prev;
  return self;
}

uint32_t ls_count(struct ls *self) {
  uint32_t n = 0;
  for (struct ls *i = self->next; i != self; i = i->next, n++);
  return n;
}

struct macro;
struct type;
struct vm;

typedef int32_t int_t;
typedef uint8_t reg_t;

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

struct val *val_init(struct val *self, struct type *type) {
  self->type = type;
  return self;
}

struct form;

const uint8_t MAX_NAME_LENGTH = 64;

enum emit_result {EMIT_OK}; 

struct type {
  char name[MAX_NAME_LENGTH];
  
  struct { 
    void (*dump)(struct val *val, FILE *out);
    enum emit_result (*emit)(struct val *val, struct form *form, struct ls *in, struct vm *vm);
    bool (*is_true)(struct val *val);
    struct val *(*literal)(struct val *val);
  } methods;
};

enum emit_result default_emit(struct val *val, struct form *form, struct ls *in, struct vm *vm);

bool default_is_true(struct val *val) {
  return true;
}

struct val *default_literal(struct val *val) {
  return val;
}

struct type *type_init(struct type *self, const char *name) {
  assert(strlen(name) < MAX_NAME_LENGTH);
  strcpy(self->name, name);
  self->methods.dump = NULL;
  self->methods.emit = default_emit;
  self->methods.is_true = default_is_true;
  self->methods.literal = default_literal;
  return self;
}

void val_dump(struct val *self, FILE *out) {
  assert(self->type->methods.dump);
  self->type->methods.dump(self, out);
}

bool val_is_true(struct val *self) {
  assert(self->type->methods.is_true);
  return self->type->methods.is_true(self);
}

struct env_item {
  char name[MAX_NAME_LENGTH];
  struct val val;
};

struct val *val_literal(struct val *self) {
  assert(self->type->methods.literal);
  return self->type->methods.literal(self);
}

const uint8_t MAX_ENV_SIZE = 64;

struct env {
  struct env_item items[MAX_ENV_SIZE];
  uint8_t item_count;
};

struct env *env_init(struct env *self) {
  memset(self->items, 0, sizeof(self->items));
  self->item_count = 0;
  return self;
}

uint8_t hash(const char *name) {
  uint8_t h = 0;
  for (const char *c = name; *c; h += *c, c++);
  return h % MAX_ENV_SIZE;
}

struct val *env_set(struct env *self, const char *name) {
  struct env_item *it = NULL;
  
  for (it = self->items+hash(name);
       it < self->items+MAX_ENV_SIZE && it->val.type;
       it++);

  assert(it < self->items+MAX_ENV_SIZE);
  strcpy(it->name, name);
  return &it->val;
}

struct val *env_get(struct env *self, const char *name) {
  for (struct env_item *it = self->items+hash(name);
       it < self->items+MAX_ENV_SIZE && it->val.type;
       it++) {
    if (strcmp(it->name, name) == 0) {
      return &it->val;
    }
  }

  return NULL;
}

struct scope {
  struct scope *parent_scope;
  struct env bindings;
  reg_t reg_count;
};

const uint8_t MAX_POS_SOURCE_LENGTH = 255;

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

struct form_id {
  char name[MAX_NAME_LENGTH];
};

struct form_literal {
  struct val val;
};

typedef uint16_t nrefs_t;

enum form_type {FORM_ID, FORM_LITERAL};

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

struct form *form_init(struct form *self, enum form_type type, struct pos pos, struct ls *out) {
  self->type = type;
  self->pos = pos;
  self->nrefs = 1;
  if (out) { ls_insert(out, &self->ls); }
  return self;
}

struct form *new_form(enum form_type type, struct pos pos, struct ls *out) {
  return form_init(malloc(sizeof(struct form)), type, pos, out);
}

struct form *form_ref(struct form *self) {
  self->nrefs++;
  return self;
}

bool form_deref(struct form *self) {
  assert(self->nrefs);
  if (--self->nrefs) { return false; }
  free(self);
  return true;
}

struct val *find(struct vm *vm, const char *name);

struct val *form_val(struct form *self, struct vm *vm) {
  switch (self->type) {
  case FORM_ID: {
    struct val *v = find(vm, self->as_id.name);
    if (!v) { break; }
    return val_literal(v);
  }
    
  case FORM_LITERAL:
    return &self->as_literal.val;
  }

  return NULL;
}

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
  OP_LOAD, OP_PUSH, OP_STORE,
  //---STOP---
  OP_STOP};

struct op {
  enum op_code code;
  struct form *form;
  
  union {
    struct op_load as_load;
    struct op_push as_push;
    struct op_store as_store;
  };
};

void op_dump(struct op *self, FILE *out) {
  switch (self->code) {
  case OP_LOAD:
    fprintf(out, "LOAD %" PRIu8, self->as_load.reg);
    break;
  case OP_PUSH:
    fputs("PUSH ", out);
    val_dump(&self->as_push.val, out);
    break;
  case OP_STORE:
    fprintf(out, "STORE %" PRIu8, self->as_store.reg);
    break;
    //---STOP---
  case OP_STOP:
    fputs("STOP", out);
    break;
  }
}

const reg_t MAX_REG_COUNT = 64;
const uint8_t MAX_STACK_SIZE = 64;

struct state {
  struct val regs[MAX_REG_COUNT];
  struct val stack[MAX_STACK_SIZE];
  uint8_t stack_size;
};

struct state *state_init(struct state *self) {
  memset(self->regs, 0, sizeof(self->regs));
  self->stack_size = 0;
  return self;
}

const uint8_t MAX_SCOPE_COUNT = 8;
const uint16_t MAX_OP_COUNT = 1024;
const uint8_t MAX_STATE_COUNT = 64;
const uint16_t MAX_ERROR_LENGTH = 1024;

struct vm {  
  struct scope scopes[MAX_SCOPE_COUNT];
  uint8_t scope_count;

  struct op ops[MAX_OP_COUNT];
  uint16_t op_count;

  struct state states[MAX_STATE_COUNT];
  uint8_t state_count;

  char error[MAX_ERROR_LENGTH];
  bool debug;
};

struct vm *vm_init(struct vm *self) {
  self->scope_count = 0;

  for (struct scope *s = self->scopes, *ps = NULL; s < self->scopes+MAX_SCOPE_COUNT; ps = s, s++) {
    s->parent_scope = ps;
  }
  
  self->op_count = 0;
  self->state_count = 0;
  *self->error = 0;
  self->debug = true;
  return self;
}

void verror(struct vm *vm, struct pos pos, const char *fmt, va_list args) {
  int n = snprintf(vm->error, MAX_ERROR_LENGTH, "Error in %s, line %" PRIu16 " column %" PRIu16 ": ", pos.source, pos.line, pos.column);
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
  struct scope *scope = peek_scope(vm);
  if (env_get(&scope->bindings, name)) { return NULL; }
  return env_set(&scope->bindings, name);
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

struct op *emit(struct vm *vm, enum op_code code, struct form *form) {
  assert(vm->op_count < MAX_OP_COUNT);
  struct op *op = vm->ops + vm->op_count++;
  op->code = code;
  op->form = form;
  return op;
}

struct op *peek_op(struct vm *vm) {
  assert(vm->op_count < MAX_OP_COUNT);
  return vm->ops+vm->op_count;
}

struct val *reg(struct vm *vm, reg_t reg) {
  assert(reg < MAX_REG_COUNT);
  return peek_state(vm)->regs+reg;
}

struct val *push(struct vm *vm) {
  struct state *s = peek_state(vm);
  return s->stack+s->stack_size++;
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

enum eval_result {EVAL_OK};
  
enum eval_result eval(struct vm *vm, struct op *op) {
  static const void* dispatch[] = {
    &&LOAD, &&PUSH, &&STORE,
    //---STOP---
    &&STOP};
  
  DISPATCH(op);

 LOAD: {
    *reg(vm, op->as_load.reg) = *pop(vm);
    DISPATCH(op+1);
  }
  
 PUSH: {
    *push(vm) = op->as_push.val;
    DISPATCH(op+1);
  }

 STORE: {
    *push(vm) = *reg(vm, op->as_store.reg);
    DISPATCH(op+1);    
  }
  
 STOP: {}

  return EVAL_OK;
}

enum emit_result default_emit(struct val *val, struct form *form, struct ls *in, struct vm *vm) {
  emit(vm, OP_PUSH, form)->as_push.val = *val;
  return EMIT_OK;
}

typedef void (*macro_body_t)(struct form *self, struct ls *in, struct vm *vm);

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
			 
typedef struct form *(*reader_t)(struct vm *vm, struct pos *pos, FILE *in, struct ls *out);

struct form *read_group(struct vm *vm, struct pos *pos, FILE *in, struct ls *out);
struct form *read_id(struct vm *vm, struct pos *pos, FILE *in, struct ls *out);
struct form *read_int(struct vm *vm, struct pos *pos, FILE *in, struct ls *out);
struct form *read_ws(struct vm *vm, struct pos *pos, FILE *in, struct ls *out);

struct form *read_form(struct vm *vm, struct pos *pos, FILE *in, struct ls *out) {
  static const int COUNT = 4;
  static const reader_t readers[COUNT] = {read_ws, read_int, read_group, read_id};

  for (int i=0; i < COUNT; i++) {
    struct form *f = readers[i](vm, pos, in, out);
    if (f) { return f; }
  }

  return NULL;
}

struct form *read_group(struct vm *vm, struct pos *pos, FILE *in, struct ls *out) {
  return NULL;
}

struct form *read_id(struct vm *vm, struct pos *pos, FILE *in, struct ls *out) {
  struct pos fpos = *pos;
  char name[MAX_NAME_LENGTH], *p = name, c = 0;

  while ((c = fgetc(in))) {
    assert(p < name + MAX_NAME_LENGTH);
    
    if (isspace(c) || c == '(' || c == ')') {
      ungetc(c, in);
      break;
    }

    *p++ = c;
    pos->column++;
  }

  if (p == name) { return NULL; }
  
  *p = 0;
  struct form *f = new_form(FORM_ID, fpos, out);
  strcpy(f->as_id.name, name);
  return f;
}

struct form *read_int(struct vm *vm, struct pos *pos, FILE *in, struct ls *out) {
  return NULL;
}

struct form *read_ws(struct vm *vm, struct pos *pos, FILE *in, struct ls *out) {
  char c = 0, pc = 0;
  
  while ((c = fgetc(in))) {
    switch (c) {
    case ' ':
    case '\t':
      pos->column++;
      break;
    case '\n':
      if (pc == '\n') {
	ungetc(c, in);
	return NULL;
      }
      
      pos->line++;
      pos->column = 0;
      break;
    default:
      ungetc(c, in);
      return NULL;
    }

    pc = c;
  }
  
  return NULL;
}


void int_dump(struct val *val, FILE *out) {
  fprintf(out, "%" PRId32, val->as_int);
}

bool int_is_true(struct val *val) {
  return val->as_int;
}

void macro_dump(struct val *val, FILE *out) {
  fprintf(out, "Macro(%s)", val->as_macro->name);
}

struct val *macro_literal(struct val *val) {
  return NULL;
}

void meta_dump(struct val *val, FILE *out) {
  fputs(val->as_meta->name, out);
}

void debug_macro_body(struct form *self, struct ls *in, struct vm *vm) {
  vm->debug = !vm->debug;
  printf("debug %s", vm->debug ? "on" : "off");
}

const int VERSION = 1;

int main () {
  printf("fibr %d\n\n", VERSION);
  
  struct vm vm;
  vm_init(&vm);
  push_scope(&vm);
  push_state(&vm);

  struct type meta_type;
  type_init(&meta_type, "Meta");
  meta_type.methods.dump = meta_dump;
  val_init(bind(&vm, "Meta"), &meta_type)->as_meta = &meta_type;

  struct type macro_type;
  type_init(&macro_type, "Macro");
  macro_type.methods.dump = macro_dump;
  macro_type.methods.literal = macro_literal;
  val_init(bind(&vm, "Macro"), &meta_type)->as_meta = &macro_type;
  
  struct type int_type;
  type_init(&int_type, "Int");
  int_type.methods.dump = int_dump;
  int_type.methods.is_true = int_is_true;
  val_init(bind(&vm, "Int"), &meta_type)->as_meta = &int_type;

  struct macro debug_macro;
  macro_init(&debug_macro, "debug", 0, debug_macro_body);
  val_init(bind(&vm, "debug"), &macro_type)->as_macro = &debug_macro;
    
  struct pos pos;
  pos_init(&pos, "repl", 0, 0);
  
  struct ls forms;
  ls_init(&forms);
  while (read_form(&vm, &pos, stdin, &forms));

  struct op *start = peek_op(&vm);
  
  emit(&vm, OP_STOP, NULL);
  eval(&vm, start);

  dump_stack(&vm, stdout);
  fputc('\n', stdout);
  return 0;
}
