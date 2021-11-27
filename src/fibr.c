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

#include "fibr.h"

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

struct val *val_init(struct val *self, struct type *type) {
  self->type = type;
  return self;
}

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

enum emit_result val_emit(struct val *self, struct form *form, struct ls *in, struct vm *vm) {
  assert(self->type->methods.emit);
  return self->type->methods.emit(self, form, in, vm);
}

bool val_is_true(struct val *self) {
  assert(self->type->methods.is_true);
  return self->type->methods.is_true(self);
}

struct val *val_literal(struct val *self) {
  assert(self->type->methods.literal);
  return self->type->methods.literal(self);
}

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

struct pos *pos_init(struct pos *self, const char *source, int line, int column) {
  strcpy(self->source, source);
  self->line = line;
  self->column = column;
  return self;
}

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

  case FORM_SEMI:
    break;
  }

  return NULL;
}

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

struct state *state_init(struct state *self) {
  memset(self->regs, 0, sizeof(self->regs));
  self->stack_size = 0;
  return self;
}

void int_dump(struct val *val, FILE *out) {
  fprintf(out, "%" PRId32, val->as_int);
}

bool int_is_true(struct val *val) {
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
  
  self->op_count = 0;
  self->state_count = 0;
  *self->error = 0;
  self->debug = false;
  push_scope(self);

  type_init(&self->meta_type, "Meta");
  self->meta_type.methods.dump = meta_dump;
  bind_init(self, "Meta", &self->meta_type)->as_meta = &self->meta_type;

  type_init(&self->int_type, "Int");
  self->int_type.methods.dump = int_dump;
  self->int_type.methods.is_true = int_is_true;
  bind_init(self, "Int", &self->meta_type)->as_meta = &self->int_type;

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
  struct scope *scope = peek_scope(vm);
  if (env_get(&scope->bindings, name)) { return NULL; }
  return env_set(&scope->bindings, name);
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

enum emit_result form_emit(struct form *self, struct ls *in, struct vm *vm) {
  switch (self->type) {
  case FORM_ID: {
    const char *name = self->as_id.name;
    struct val *v = find(vm, name);

    if (!v) {
      error(vm, self->pos, "Unknown id: %s", name);
      return EMIT_ERROR;
    }
    
    return val_emit(v, self, in, vm);
  }
    
  case FORM_LITERAL:
    emit(vm, OP_PUSH, self)->as_push.val = self->as_literal.val;
    return EMIT_OK;
  case FORM_SEMI:
    error(vm, self->pos, "Semi emit");
    break;
  }
  
  return EMIT_ERROR;
}

enum emit_result emit_forms(struct vm *vm, struct ls *in) {
  while (!ls_null(in)) {
    struct form *f = baseof(ls_delete(in->next), struct form, ls);
    enum emit_result fr = form_emit(f, in, vm);
    if (fr != EMIT_OK) { return fr; }
  }

  return EMIT_OK;
}

struct macro *macro_init(struct macro *self, const char *name, uint8_t nargs, macro_body_t body) {
  assert(strlen(name) < MAX_NAME_LENGTH);
  strcpy(self->name, name);
  self->nargs = nargs;
  self->body = body;
  return self;
}
  
enum read_result read_form(struct vm *vm, struct pos *pos, FILE *in, struct ls *out) {
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

enum read_result read_group(struct vm *vm, struct pos *pos, FILE *in, struct ls *out) {
  return READ_NULL;
}

enum read_result read_id(struct vm *vm, struct pos *pos, FILE *in, struct ls *out) {
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
  struct form *f = new_form(FORM_ID, fpos, out);
  strcpy(f->as_id.name, name);
  return READ_OK;
}

enum read_result read_int(struct vm *vm, struct pos *pos, FILE *in, struct ls *out) {
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
   struct form *f = new_form(FORM_LITERAL, fpos, out);
   val_init(&f->as_literal.val, &vm->int_type)->as_int = neg ? -v : v;
   return READ_OK;
}

enum read_result read_semi(struct vm *vm, struct pos *pos, FILE *in, struct ls *out) {
  struct pos fpos = *pos;
  char c = 0;
  
  if (!(c = fgetc(in))) { return READ_NULL; }
  
  if (c != ';') {
    ungetc(c, in);
    return READ_NULL;
  }
  
    pos->column++;
    new_form(FORM_SEMI, fpos, out);
    return READ_OK;
}
    
enum read_result read_ws(struct vm *vm, struct pos *pos, FILE *in, struct ls *out) {
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

enum emit_result macro_emit(struct val *val, struct form *form, struct ls *in, struct vm *vm) {
  struct macro *self = val->as_macro;
  struct ls *a = in->next;

  for (uint8_t i = 0; i < self->nargs; i++, a = a->next) {
    if (a == in) {
      error(vm, form->pos, "Missing macro arguments");
      return EMIT_ERROR;
    }
  }

  return self->body(self, form, in, vm);
}

struct val *macro_literal(struct val *val) {
  return NULL;
}

enum emit_result debug_body(struct macro *self, struct form *form, struct ls *in, struct vm *vm) {
  vm->debug = !vm->debug;
  printf("%s\n", vm->debug ? "T" : "F");
  return EMIT_OK;
}

const int VERSION = 1;

int main () {
  printf("fibr %d\n\n", VERSION);
  
  struct vm vm;
  vm_init(&vm);
  push_state(&vm);


  struct type macro_type;
  type_init(&macro_type, "Macro");
  macro_type.methods.dump = macro_dump;
  macro_type.methods.emit = macro_emit;
  macro_type.methods.literal = macro_literal;
  bind_init(&vm, "Macro", &vm.meta_type)->as_meta = &macro_type;
  
  struct macro debug_macro;
  macro_init(&debug_macro, "debug", 0, debug_body);
  bind_init(&vm, "debug", &macro_type)->as_macro = &debug_macro;
  struct pos pos;
  pos_init(&pos, "repl", 0, 0);

  while (!feof(stdin)) {
    struct ls forms;
    ls_init(&forms);

    while (read_form(&vm, &pos, stdin, &forms) == READ_OK) {
      struct form *f = baseof(forms.prev, struct form, ls);

      if (f->type == FORM_SEMI) {
	ls_delete(forms.prev);
	break;
      }
    }
    
    struct op *start = peek_op(&vm);

    if (emit_forms(&vm, &forms) != EMIT_OK) {
      printf("%s\n", vm.error);
      continue;
    }
  
    emit(&vm, OP_STOP, NULL);
    
    if (eval(&vm, start) != EVAL_OK) {
      printf("%s\n", vm.error);
      continue;
    }

    dump_stack(&vm, stdout);
    fputc('\n', stdout);
  }
  
  return 0;
}
