/* Prolog interpreter in portable C89 - the parts are inlined by tools/amalg.sh */
/* ===== p0_base.c ===== */
/* p0_base.c - types, memory (arenas), symbols, text buffers, output */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#ifdef PL_POSIX
#include <dirent.h>
#endif

#define BLKSZ 262144L
#define NSYMH 4096

typedef struct Term Term;
typedef struct Sym Sym;
typedef struct Pred Pred;
typedef struct Clause Clause;

enum { T_VAR, T_ATOM, T_QATOM, T_NUM, T_CMP, T_TV, T_CUT };

struct Term {
  int tag;
  int gnd;                 /* template compound without variables */
  union {
    struct { Term *ref; const char *base; int sep; long ctr; long born; } v;
    struct { Sym *sym; } a;
    struct { const char *txt; } n;
    struct { Sym *fn; int ar; Term **args; } c;
    struct { long idx; } x;
  } u;
};

struct Sym {
  Sym *next;
  char *text;
  Term *pt, *qt;           /* shared plain / quoted atom terms */
  int ip, pp;              /* infix / prefix priority, 0 = none */
  char ia, pa;             /* x = xfx/fx, r = xfy, l = yfx, y = fy */
  unsigned char bi[4];     /* builtin number by arity 0..3 */
  Pred *preds;
};

struct Clause {
  long id;
  Term *head;
  Term **body;
  int nbody;
  int nvars, nhead;
  char **vn;
};

struct Pred {
  Sym *fn;
  int ar;
  Pred *next;              /* chain on the symbol */
  Pred *lnext;             /* order of first assertion */
  Clause **v;
  int n, cap;
  int listed;
};

/* text buffer */
typedef struct { char *s; size_t n, cap; } Buf;

/* arena with mark / release */
typedef struct { char *p; size_t cap, used; long base; } Blk;
typedef struct { Blk *b; int n, ncap, cur; } Arena;
typedef struct { int blk; size_t off; } AMark;

static Arena g_heap, g_perm;
static Sym *g_symtab[NSYMH];

static void die(const char *msg)
{
  fflush(stdout);
  fprintf(stderr, "prolog: %s\n", msg);
  exit(2);
}

static void *xmalloc(size_t n)
{
  void *p = malloc(n ? n : 1);
  if (p == NULL) die("out of memory");
  return p;
}

static void *xrealloc(void *q, size_t n)
{
  void *p = realloc(q, n ? n : 1);
  if (p == NULL) die("out of memory");
  return p;
}

static char *xstrdup(const char *s)
{
  size_t n = strlen(s) + 1;
  char *p = (char *)xmalloc(n);
  memcpy(p, s, n);
  return p;
}

/* ---- arena ---- */
static void *aalloc(Arena *a, size_t n)
{
  Blk *b;
  void *r;
  n = (n + 7) & ~(size_t)7;
  if (a->n == 0) {
    a->ncap = 8;
    a->b = (Blk *)xmalloc(sizeof(Blk) * (size_t)a->ncap);
  }
  if (a->n == 0 || a->b[a->cur].cap - a->b[a->cur].used < n) {
    if (a->n > 0 && a->cur + 1 < a->n && a->b[a->cur + 1].cap >= n) {
      a->cur++;
      a->b[a->cur].used = 0;
    } else {
      long base = 0;
      size_t sz = n > (size_t)BLKSZ ? n : (size_t)BLKSZ;
      int i;
      if (a->n > 0) {
        for (i = a->cur + 1; i < a->n; i++) free(a->b[i].p);
        a->n = a->cur + 1;
        base = a->b[a->cur].base + (long)a->b[a->cur].cap;
      }
      if (a->n >= a->ncap) {
        a->ncap *= 2;
        a->b = (Blk *)xrealloc(a->b, sizeof(Blk) * (size_t)a->ncap);
      }
      a->b[a->n].p = (char *)xmalloc(sz);
      a->b[a->n].cap = sz;
      a->b[a->n].used = 0;
      a->b[a->n].base = base;
      a->cur = a->n;
      a->n++;
    }
  }
  b = &a->b[a->cur];
  r = b->p + b->used;
  b->used += n;
  return r;
}

static long apos(Arena *a)
{
  if (a->n == 0) return 0;
  return a->b[a->cur].base + (long)a->b[a->cur].used;
}

static AMark amark(Arena *a)
{
  AMark m;
  m.blk = a->cur;
  m.off = a->n ? a->b[a->cur].used : 0;
  return m;
}

static void arelease(Arena *a, AMark m)
{
  if (a->n == 0) return;
  a->cur = m.blk;
  a->b[a->cur].used = m.off;
}

static char *astrdup(Arena *a, const char *s)
{
  size_t n = strlen(s) + 1;
  char *p = (char *)aalloc(a, n);
  memcpy(p, s, n);
  return p;
}

/* ---- text buffer ---- */
static void bput(Buf *b, const char *s)
{
  size_t n = strlen(s);
  if (b->n + n + 1 > b->cap) {
    b->cap = (b->n + n + 1) * 2 + 64;
    b->s = (char *)xrealloc(b->s, b->cap);
  }
  memcpy(b->s + b->n, s, n + 1);
  b->n += n;
}

static void bputc(Buf *b, int c)
{
  if (b->n + 2 > b->cap) {
    b->cap = b->cap * 2 + 64;
    b->s = (char *)xrealloc(b->s, b->cap);
  }
  b->s[b->n++] = (char)c;
  b->s[b->n] = '\0';
}

static void bclear(Buf *b)
{
  b->n = 0;
  if (b->s) b->s[0] = '\0';
}

static const char *bstr(Buf *b)
{
  if (b->s == NULL) {
    b->cap = 64;
    b->s = (char *)xmalloc(b->cap);
    b->s[0] = '\0';
    b->n = 0;
  }
  return b->s;
}

/* ---- symbols ---- */
static Sym *intern(const char *s)
{
  unsigned long h = 5381;
  const char *p;
  Sym *y;
  for (p = s; *p; p++) h = h * 33 + (unsigned char)*p;
  h %= NSYMH;
  for (y = g_symtab[h]; y; y = y->next)
    if (strcmp(y->text, s) == 0) return y;
  y = (Sym *)xmalloc(sizeof(Sym));
  memset(y, 0, sizeof(Sym));
  y->text = xstrdup(s);
  y->next = g_symtab[h];
  g_symtab[h] = y;
  return y;
}

/* ---- output (everything goes to standard output, like SAY) ---- */
static void say(const char *s)
{
  fputs(s, stdout);
  fputc('\n', stdout);
}

static void charout(const char *s)
{
  fputs(s, stdout);
}

/* ---- character sets (literal sets, never code ranges) ---- */
#define S_LOWER "abcdefghijklmnopqrstuvwxyz"
#define S_UPPER "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
#define S_DIGIT "0123456789"
#define S_SYM "+-*/\\^<>=~:.?@#&$"
#define S_SOLO "()[]{}|,!;"

static int in_set(int c, const char *set)
{
  return c != 0 && strchr(set, c) != NULL;
}

static int is_digit_c(int c) { return in_set(c, S_DIGIT); }
static int is_upper_c(int c) { return in_set(c, S_UPPER); }
static int is_alnum_c(int c)
{
  return in_set(c, S_LOWER) || in_set(c, S_UPPER) || in_set(c, S_DIGIT);
}
/* IsLower of the CREXX version: alphanumeric and not upper case */
static int is_lower_c(int c) { return is_alnum_c(c) && !is_upper_c(c); }
static int is_upus_c(int c) { return c == '_' || is_upper_c(c); }
static int is_ident_c(int c) { return is_alnum_c(c) || c == '_'; }
static int is_sym_c(int c) { return in_set(c, S_SYM); }
static int is_solo_c(int c) { return in_set(c, S_SOLO); }
/* ===== p1_num.c ===== */
/* p1_num.c - numbers: the CREXX arithmetic model (double values, 15 digits) */

/* is the text a number the way IsNumber of the CREXX version sees it? */
static int is_number(const char *t)
{
  const char *p = t;
  int dig = 0;
  if (*p == '\0') return 0;
  if (*p == '-') {
    p++;
    if (*p == '+' || *p == '-') p++;
  } else if (*p == '+') {
    p++;
  }
  while (is_digit_c(*p)) { p++; dig++; }
  if (*p == '.') {
    p++;
    while (is_digit_c(*p)) { p++; dig++; }
  }
  if (dig == 0) return 0;
  if (*p == 'e' || *p == 'E') {
    p++;
    if (*p == '+' || *p == '-') p++;
    if (!is_digit_c(*p)) return 0;
    while (is_digit_c(*p)) p++;
  }
  return *p == '\0';
}

static double num_val(const char *t)
{
  return strtod(t, NULL);
}

/* digits (no trailing zeros), adjusted exponent X -> plain or e-notation */
static void fmt_digits(int neg, const char *dig, int nd, long x, char *out)
{
  char *o = out;
  long i;
  if (neg) *o++ = '-';
  if (x >= -6 && x <= 17) {
    if (x >= 0) {
      for (i = 0; i <= x; i++) *o++ = i < nd ? dig[i] : '0';
      if (nd > x + 1) {
        *o++ = '.';
        for (i = x + 1; i < nd; i++) *o++ = dig[i];
      }
    } else {
      *o++ = '0';
      *o++ = '.';
      for (i = 0; i < -x - 1; i++) *o++ = '0';
      for (i = 0; i < nd; i++) *o++ = dig[i];
    }
    *o = '\0';
  } else {
    *o++ = dig[0];
    if (nd > 1) {
      *o++ = '.';
      for (i = 1; i < nd; i++) *o++ = dig[i];
    }
    sprintf(o, "e%c%ld", x < 0 ? '-' : '+', x < 0 ? -x : x);
  }
}

/* a double as CREXX prints it: nsig (15 or 16) significant digits, the
   digits taken from the exact binary value (correct rounding) */
static void fmt_double_n(double x, int nsig, char *out)
{
  char b[96], d[24];
  int neg = 0, nd, i, carry;
  long ex;
  char *e;
  if (x != x) { strcpy(out, "nan"); return; }
  if (x - x != 0.0) { strcpy(out, x < 0 ? "-inf" : "inf"); return; }
  if (x == 0.0) { strcpy(out, "0"); return; }
  if (x < 0) { neg = 1; x = -x; }
  sprintf(b, "%.40e", x);
  e = strchr(b, 'e');
  ex = atol(e + 1);
  d[0] = b[0];
  for (i = 1; i <= nsig; i++) d[i] = b[i + 1];
  if (d[nsig] >= '5') {
    carry = 1;
    for (i = nsig - 1; i >= 0 && carry; i--) {
      if (d[i] == '9') d[i] = '0';
      else { d[i]++; carry = 0; }
    }
    if (carry) { d[0] = '1'; ex++; }
  }
  nd = nsig;
  while (nd > 1 && d[nd - 1] == '0') nd--;
  d[nd] = '\0';
  fmt_digits(neg, d, nd, ex, out);
}

static void fmt_double(double x, char *out)
{
  fmt_double_n(x, 15, out);
}

/* exact decimal digits of a number text: value = dig * 10^ex */
typedef struct { int neg; char *dig; int nd; long ex; } TD;

static void td_parse(const char *s, TD *t)
{
  const char *p = s;
  long fr = 0, ex = 0;
  int nd = 0, i;
  char *d = (char *)aalloc(&g_heap, strlen(s) + 2);
  t->neg = 0;
  if (*p == '-') {
    t->neg = 1;
    p++;
    if (*p == '+' || *p == '-') p++;
  } else if (*p == '+') {
    p++;
  }
  while (is_digit_c(*p)) {
    if (nd > 0 || *p != '0') d[nd++] = *p;
    p++;
  }
  if (*p == '.') {
    p++;
    while (is_digit_c(*p)) {
      if (nd > 0 || *p != '0') d[nd++] = *p;
      fr++;
      p++;
    }
  }
  if (*p == 'e' || *p == 'E') ex = atol(p + 1);
  /* strip trailing zeros into the exponent */
  i = nd;
  while (i > 0 && d[i - 1] == '0') { i--; ex++; }
  nd = i;
  d[nd] = '\0';
  t->dig = d;
  t->nd = nd;
  t->ex = ex - fr;
}

/* round half up to n significant digits */
static void td_round(TD *t, int n)
{
  int i, carry;
  if (t->nd <= n) return;
  t->ex += t->nd - n;
  carry = t->dig[n] >= '5';
  t->nd = n;
  t->dig[n] = '\0';
  if (carry) {
    for (i = n - 1; i >= 0 && carry; i--) {
      if (t->dig[i] == '9') t->dig[i] = '0';
      else { t->dig[i]++; carry = 0; }
    }
    if (carry) {
      /* 99..9 -> 100..0 : one more digit */
      memmove(t->dig + 1, t->dig, (size_t)n + 1);
      t->dig[0] = '1';
      t->nd = n + 1;
    }
  }
  while (t->nd > 0 && t->dig[t->nd - 1] == '0') { t->nd--; t->ex++; }
  t->dig[t->nd] = '\0';
}

static char *heap_str(size_t n)
{
  return (char *)aalloc(&g_heap, n);
}

/* integer digits of t (truncated), as plain text */
static char *td_int_text(TD *t, int neg_ok)
{
  char *o;
  long k, i;
  size_t sz = (size_t)t->nd + 8;
  int neg = neg_ok && t->neg && t->nd > 0;
  if (t->ex > 0) sz += (size_t)t->ex;
  o = heap_str(sz);
  k = 0;
  if (neg) o[k++] = '-';
  if (t->nd == 0 || -t->ex >= t->nd) {
    o[k++] = '0';
  } else if (t->ex >= 0) {
    for (i = 0; i < t->nd; i++) o[k++] = t->dig[i];
    for (i = 0; i < t->ex; i++) o[k++] = '0';
  } else {
    for (i = 0; i < t->nd + t->ex; i++) o[k++] = t->dig[i];
  }
  o[k] = '\0';
  return o;
}

/* trunc(): integer part, sign kept when the number is negative */
static char *trunc_text(const char *s)
{
  TD t;
  td_parse(s, &t);
  td_round(&t, 18);
  return td_int_text(&t, 1);
}

/* format(x,,0): round half up (away from zero) */
static char *round_text(const char *s)
{
  TD t;
  long k, i;
  char *r, *o;
  td_parse(s, &t);
  if (t.nd == 0) return astrdup(&g_heap, "0");
  if (t.ex >= 0) {
    r = td_int_text(&t, 1);
  } else {
    k = t.nd + t.ex;               /* digits before the point */
    if (k < 0 || (k == 0 && t.dig[0] < '5')) {
      return astrdup(&g_heap, "0");
    }
    o = heap_str((size_t)t.nd + 4);
    if (k == 0) {
      strcpy(o, "1");
    } else {
      int carry = t.dig[k] >= '5';
      for (i = 0; i < k; i++) o[i] = t.dig[i];
      o[k] = '\0';
      for (i = k - 1; i >= 0 && carry; i--) {
        if (o[i] == '9') o[i] = '0';
        else { o[i]++; carry = 0; }
      }
      if (carry) {
        memmove(o + 1, o, (size_t)k + 1);
        o[0] = '1';
      }
    }
    if (t.neg) {
      r = heap_str(strlen(o) + 2);
      r[0] = '-';
      strcpy(r + 1, o);
    } else {
      r = o;
    }
    return r;
  }
  /* large integers: plain up to 18 digits, else upper case e-notation */
  if (t.nd + t.ex > 18) {
    TD u;
    char *q = heap_str((size_t)t.nd + 40);
    u = t;
    fmt_digits(u.neg, u.dig, u.nd, (long)u.nd - 1 + u.ex, q);
    for (o = q; *o; o++) if (*o == 'e') *o = 'E';
    return q;
  }
  return r;
}

/* abs(): sign dropped, digits kept; -0 stays -0 */
static char *abs_text(const char *s)
{
  TD t;
  char *o;
  td_parse(s, &t);
  o = heap_str((size_t)t.nd + 48);
  if (t.nd == 0) {
    strcpy(o, t.neg ? "-0" : "0");
    return o;
  }
  fmt_digits(0, t.dig, t.nd, (long)t.nd - 1 + t.ex, o);
  return o;
}

/* Rexx style comparison of two texts: numeric when both are numbers */
static int rx_cmp(const char *a, const char *b)
{
  int c;
  if (is_number(a) && is_number(b)) {
    double x = num_val(a), y = num_val(b);
    return x < y ? -1 : (x > y ? 1 : 0);
  }
  c = strcmp(a, b);
  return c < 0 ? -1 : (c > 0 ? 1 : 0);
}
/* ===== p2_term.c ===== */
/* p2_term.c - terms, bindings, unification, ordering, term output */

static Sym *S_NIL, *S_DOT, *S_COMMA, *S_SEMI, *S_ARROW, *S_NECK, *S_TRUE, *S_FAIL,
           *S_FALSE, *S_BANG, *S_NOTP, *S_NOT, *S_CALL, *S_MINUS, *S_CUT, *S_QUERY,
           *S_PIPE, *S_EMPTY;

static Term **g_trail;
static long g_tn, g_tcap;
static long g_rvc;           /* RVARCOUNTER: fresh variable counter */
static long g_pvc;           /* PVARCOUNTER: parsed variable counter */
static char **g_wtvn;        /* variable names while writing a clause template */

/* ---- construction ---- */
static Term *mk_atom(Sym *y)
{
  if (y->pt == NULL) {
    y->pt = (Term *)aalloc(&g_perm, sizeof(Term));
    memset(y->pt, 0, sizeof(Term));
    y->pt->tag = T_ATOM;
    y->pt->u.a.sym = y;
  }
  return y->pt;
}

static Term *mk_qatom(Sym *y)
{
  if (y->qt == NULL) {
    y->qt = (Term *)aalloc(&g_perm, sizeof(Term));
    memset(y->qt, 0, sizeof(Term));
    y->qt->tag = T_QATOM;
    y->qt->u.a.sym = y;
  }
  return y->qt;
}

static Term *mk_qatom_s(const char *s) { return mk_qatom(intern(s)); }
static Term *mk_atom_s(const char *s) { return mk_atom(intern(s)); }

static Term *mk_var(const char *base, int sep, long ctr)
{
  long b = apos(&g_heap);
  Term *t = (Term *)aalloc(&g_heap, sizeof(Term));
  t->tag = T_VAR;
  t->gnd = 0;
  t->u.v.ref = NULL;
  t->u.v.base = base;
  t->u.v.sep = sep;
  t->u.v.ctr = ctr;
  t->u.v.born = b;
  return t;
}

static Term *mk_numa(Arena *a, const char *txt)
{
  Term *t = (Term *)aalloc(a, sizeof(Term));
  t->tag = T_NUM;
  t->gnd = 1;
  t->u.n.txt = astrdup(a, txt);
  return t;
}

static Term *mk_num(const char *txt) { return mk_numa(&g_heap, txt); }

static Term *mk_long(long v)
{
  char b[40];
  sprintf(b, "%ld", v);
  return mk_num(b);
}

static Term *mk_cmpa(Arena *a, Sym *fn, int ar)
{
  Term *t = (Term *)aalloc(a, sizeof(Term) + sizeof(Term *) * (size_t)(ar ? ar : 1));
  int i;
  t->tag = T_CMP;
  t->gnd = 0;
  t->u.c.fn = fn;
  t->u.c.ar = ar;
  t->u.c.args = (Term **)(t + 1);
  for (i = 0; i < ar; i++) t->u.c.args[i] = NULL;
  return t;
}

static Term *mk_cmp(Sym *fn, int ar) { return mk_cmpa(&g_heap, fn, ar); }

static Term *mk_cmp2(Sym *fn, Term *a, Term *b)
{
  Term *t = mk_cmp(fn, 2);
  t->u.c.args[0] = a;
  t->u.c.args[1] = b;
  return t;
}

static Term *mk_cons(Term *h, Term *t) { return mk_cmp2(S_DOT, h, t); }

/* an atom or number from a result text, as unification sees it */
static Term *mk_from_text(const char *s)
{
  if (is_number(s)) return mk_num(s);
  return mk_atom_s(s);
}

static Term *deref(Term *t)
{
  while (t->tag == T_VAR && t->u.v.ref != NULL) t = t->u.v.ref;
  return t;
}

#define IS_CMP(t) ((t)->tag == T_CMP)
#define IS_VAR(t) ((t)->tag == T_VAR)

/* ---- trail ---- */
static void trail_push(Term *v)
{
  if (g_tn >= g_tcap) {
    g_tcap = g_tcap ? g_tcap * 2 : 4096;
    g_trail = (Term **)xrealloc(g_trail, sizeof(Term *) * (size_t)g_tcap);
  }
  g_trail[g_tn++] = v;
}

static long trail_mark(void) { return g_tn; }

static void trail_undo(long mark)
{
  while (g_tn > mark) {
    g_tn--;
    g_trail[g_tn]->u.v.ref = NULL;
  }
}

static void bind(Term *v, Term *t)
{
  v->u.v.ref = t;
  trail_push(v);
}

/* ---- names and text ---- */
static void var_name(Term *v, Buf *b)
{
  char nb[40];
  bput(b, v->u.v.base);
  if (v->u.v.ctr >= 0) {
    if (v->u.v.sep) bputc(b, v->u.v.sep);
    sprintf(nb, "%ld", v->u.v.ctr);
    bput(b, nb);
  }
}

static void enc_term(Term *t, Buf *b)
{
  int i;
  switch (t->tag) {
  case T_VAR:
    bputc(b, 2);
    var_name(t, b);
    break;
  case T_QATOM: {
    const char *p;
    bputc(b, 3);
    for (p = t->u.a.sym->text; *p; p++) bputc(b, *p == ' ' ? 1 : *p);
    break;
  }
  case T_ATOM: bput(b, t->u.a.sym->text); break;
  case T_NUM: bput(b, t->u.n.txt); break;
  case T_CMP:
    bputc(b, 4);
    bput(b, t->u.c.fn->text);
    for (i = 0; i < t->u.c.ar; i++) {
      bputc(b, 6);
      enc_term(t->u.c.args[i], b);
    }
    bputc(b, 7);
    break;
  default: break;
  }
}

/* AtomText: printable text of an atom or number; the internal encoding for others */
static const char *atext(Term *t, Buf *scratch)
{
  switch (t->tag) {
  case T_ATOM:
  case T_QATOM: return t->u.a.sym->text;
  case T_NUM: return t->u.n.txt;
  default:
    bclear(scratch);
    enc_term(t, scratch);
    return bstr(scratch);
  }
}

/* the functor text of a term (atoms count as arity 0) */
static Sym *functor_of(Term *t, int *ar)
{
  Buf b;
  Sym *y;
  switch (t->tag) {
  case T_CMP: *ar = t->u.c.ar; return t->u.c.fn;
  case T_ATOM:
  case T_QATOM: *ar = 0; return t->u.a.sym;
  default:
    b.s = NULL; b.n = 0; b.cap = 0;
    y = intern(atext(t, &b));
    free(b.s);
    *ar = 0;
    return y;
  }
}

/* ---- numbers in unification and ordering ---- */
static int num_eq(Term *a, Term *b)
{
  if (strcmp(a->u.n.txt, b->u.n.txt) == 0) return 1;
  return num_val(a->u.n.txt) == num_val(b->u.n.txt);
}

static int unify(Term *a, Term *b)
{
  for (;;) {
    a = deref(a);
    b = deref(b);
    if (a->tag == T_VAR) {
      if (b == a) return 1;
      bind(a, b);
      return 1;
    }
    if (b->tag == T_VAR) {
      bind(b, a);
      return 1;
    }
    if (a->tag == T_CMP || b->tag == T_CMP) {
      int i, n;
      if (a->tag != T_CMP || b->tag != T_CMP) return 0;
      if (a->u.c.fn != b->u.c.fn || a->u.c.ar != b->u.c.ar) return 0;
      n = a->u.c.ar;
      if (n == 0) return 1;
      for (i = 0; i < n - 1; i++)
        if (!unify(a->u.c.args[i], b->u.c.args[i])) return 0;
      a = a->u.c.args[n - 1];
      b = b->u.c.args[n - 1];
      continue;
    }
    if (a == b) return 1;
    if (a->tag == T_NUM && b->tag == T_NUM) return num_eq(a, b);
    return 0;
  }
}

static int term_kind(Term *t)
{
  switch (t->tag) {
  case T_VAR: return 0;
  case T_NUM: return 1;
  case T_CMP: return 3;
  default: return 2;
  }
}

static int term_compare(Term *a, Term *b)
{
  int k1, k2, i, c;
  Buf s1, s2;
  for (;;) {
    a = deref(a);
    b = deref(b);
    k1 = term_kind(a);
    k2 = term_kind(b);
    if (k1 != k2) return k1 < k2 ? -1 : 1;
    switch (k1) {
    case 0: {
      s1.s = NULL; s1.n = s1.cap = 0;
      s2.s = NULL; s2.n = s2.cap = 0;
      if (a == b) return 0;
      var_name(a, &s1);
      var_name(b, &s2);
      c = rx_cmp(bstr(&s1), bstr(&s2));
      free(s1.s);
      free(s2.s);
      return c;
    }
    case 1: {
      double x = num_val(a->u.n.txt), y = num_val(b->u.n.txt);
      if (strcmp(a->u.n.txt, b->u.n.txt) == 0) return 0;
      return x < y ? -1 : (x > y ? 1 : 0);
    }
    case 2:
      if (a == b) return 0;
      return rx_cmp(a->u.a.sym->text, b->u.a.sym->text);
    default: {
      int n;
      if (a->u.c.ar != b->u.c.ar) return a->u.c.ar < b->u.c.ar ? -1 : 1;
      if (a->u.c.fn != b->u.c.fn) {
        c = rx_cmp(a->u.c.fn->text, b->u.c.fn->text);
        if (c != 0) return c;
      }
      n = a->u.c.ar;
      if (n == 0) return 0;
      for (i = 0; i < n - 1; i++) {
        c = term_compare(a->u.c.args[i], b->u.c.args[i]);
        if (c != 0) return c;
      }
      a = a->u.c.args[n - 1];
      b = b->u.c.args[n - 1];
      continue;
    }
    }
  }
}

/* ---- full dereferencing copy ---- */
static Term *deref_full(Term *t)
{
  Term *first = NULL, **hole = &first, *r;
  for (;;) {
    t = deref(t);
    if (t->tag != T_CMP || t->u.c.ar == 0) {
      *hole = t;
      return first;
    }
    {
      int i, n = t->u.c.ar;
      r = mk_cmp(t->u.c.fn, n);
      for (i = 0; i < n - 1; i++) r->u.c.args[i] = deref_full(t->u.c.args[i]);
      *hole = r;
      hole = &r->u.c.args[n - 1];
      t = t->u.c.args[n - 1];
    }
  }
}

/* ---- term output ---- */
static int wt_needs_quote(const char *text)
{
  const char *p;
  if (strcmp(text, "[]") == 0 || strcmp(text, "!") == 0 || strcmp(text, ";") == 0 ||
      strcmp(text, "{}") == 0) return 0;
  if (*text == '\0') return 1;
  if (is_lower_c(*text)) {
    for (p = text; *p; p++)
      if (!is_ident_c(*p)) return 1;
    return 0;
  }
  for (p = text; *p; p++)
    if (!is_sym_c(*p)) return 1;
  return 0;
}

static void wt_atom(const char *text, int quoted, Buf *b)
{
  const char *p;
  if (!quoted || !wt_needs_quote(text)) {
    bput(b, text);
    return;
  }
  bputc(b, '\'');
  for (p = text; *p; p++) {
    if (*p == '\'') bputc(b, '\'');
    bputc(b, *p);
  }
  bputc(b, '\'');
}

static void wt(Term *t, int quoted, int maxprec, Buf *b);

static void wt_list(Term *t, int quoted, Buf *b)
{
  Term *rest, *rd;
  bputc(b, '[');
  wt(t->u.c.args[0], quoted, 999, b);
  rest = t->u.c.args[1];
  for (;;) {
    rd = rest;
    if (rd->tag == T_VAR) rd = deref(rd);
    if (rd == S_NIL->pt) {
      rest = rd;
      break;
    }
    if (rd->tag == T_CMP && rd->u.c.fn == S_DOT && rd->u.c.ar == 2) {
      bputc(b, ',');
      wt(rd->u.c.args[0], quoted, 999, b);
      rest = rd->u.c.args[1];
      continue;
    }
    rest = rd;
    break;
  }
  if (rest != S_NIL->pt) {
    bputc(b, '|');
    wt(rest, quoted, 999, b);
  }
  bputc(b, ']');
}

static void wt(Term *t, int quoted, int maxprec, Buf *b)
{
  Sym *fn;
  int ar, i, prec, lp, rp;
  Buf tmp;
  switch (t->tag) {
  case T_VAR:
    bputc(b, '_');
    var_name(t, b);
    return;
  case T_TV:
    bputc(b, '_');
    bput(b, g_wtvn ? g_wtvn[t->u.x.idx] : "?");
    return;
  case T_NUM:
    bput(b, t->u.n.txt);
    return;
  case T_ATOM:
  case T_QATOM:
    wt_atom(t->u.a.sym->text, quoted, b);
    return;
  case T_CUT: {
    char nb[48];
    sprintf(nb, "$cut(%ld)", t->u.x.idx);
    bput(b, nb);
    return;
  }
  default: break;
  }
  fn = t->u.c.fn;
  ar = t->u.c.ar;
  if (fn == S_DOT && ar == 2) {
    wt_list(t, quoted, b);
    return;
  }
  if (ar == 2 && fn->ip > 0) {
    prec = fn->ip;
    lp = prec - 1;
    rp = prec - 1;
    if (fn->ia == 'r') rp = prec;
    if (fn->ia == 'l') lp = prec;
    tmp.s = NULL; tmp.n = tmp.cap = 0;
    wt(t->u.c.args[0], quoted, lp, &tmp);
    if (fn == S_COMMA) bput(&tmp, ",");
    else {
      bputc(&tmp, ' ');
      bput(&tmp, fn->text);
      bputc(&tmp, ' ');
    }
    wt(t->u.c.args[1], quoted, rp, &tmp);
    if (prec > maxprec) bputc(b, '(');
    bput(b, bstr(&tmp));
    if (prec > maxprec) bputc(b, ')');
    free(tmp.s);
    return;
  }
  if (ar == 1 && fn->pp > 0) {
    prec = fn->pp;
    rp = prec - 1;
    if (fn->pa == 'y') rp = prec;
    if (prec > maxprec) bputc(b, '(');
    bput(b, fn->text);
    bputc(b, ' ');
    wt(t->u.c.args[0], quoted, rp, b);
    if (prec > maxprec) bputc(b, ')');
    return;
  }
  wt_atom(fn->text, quoted, b);
  bputc(b, '(');
  for (i = 0; i < ar; i++) {
    if (i > 0) bputc(b, ',');
    wt(t->u.c.args[i], quoted, 999, b);
  }
  bputc(b, ')');
}

/* WriteTerm: the text lives in a static buffer until the next call */
static Buf g_wbuf;

static const char *write_term(Term *t, int quoted)
{
  bclear(&g_wbuf);
  wt(t, quoted, 1200, &g_wbuf);
  return bstr(&g_wbuf);
}
/* ===== p3_lex.c ===== */
/* p3_lex.c - operator table, tokenizer and parser */

enum { K_BARE, K_VAR, K_QATOM, K_STR, K_END, K_FOPEN, K_QEND };

typedef struct { int kind; char *s; int isnum; } Tok;

static Tok *g_tok;
static long g_ntok, g_tokcap, g_ppos;
static char g_lastterm = '.';

typedef struct { char *name; Term *var; } VEnt;
typedef struct { VEnt *e; int n, cap; } VMap;

static VMap g_pvarmap;       /* PVARMAP: variables of the clause being read */
static VMap g_qvarmap;       /* QUERYVARMAP: variables of the current query */

static void set_infix(const char *s, int prec, int assoc)
{
  Sym *y = intern(s);
  y->ip = prec;
  y->ia = (char)assoc;
}

static void set_prefix(const char *s, int prec, int assoc)
{
  Sym *y = intern(s);
  y->pp = prec;
  y->pa = (char)assoc;
}

static void init_ops(void)
{
  static const char *x700[] = {"=", "\\=", "==", "\\==", "@<", "@>", "@=<", "@>=", "is",
    "<", ">", "=<", ">=", "=:=", "=\\=", "=..", NULL};
  int i;
  set_infix(":-", 1200, 'x');
  set_infix("-->", 1200, 'x');
  set_infix(";", 1100, 'r');
  set_infix("|", 1100, 'r');
  set_infix("->", 1050, 'r');
  set_infix(",", 1000, 'r');
  for (i = 0; x700[i]; i++) set_infix(x700[i], 700, 'x');
  set_infix("+", 500, 'l');
  set_infix("-", 500, 'l');
  set_infix("/\\", 500, 'l');
  set_infix("\\/", 500, 'l');
  set_infix("xor", 500, 'l');
  set_infix("*", 400, 'l');
  set_infix("/", 400, 'l');
  set_infix("//", 400, 'l');
  set_infix("mod", 400, 'l');
  set_infix("rem", 400, 'l');
  set_infix("<<", 400, 'l');
  set_infix(">>", 400, 'l');
  set_infix("^", 200, 'r');
  set_infix("**", 200, 'x');
  set_prefix(":-", 1200, 'x');
  set_prefix("?-", 1200, 'x');
  set_prefix("\\+", 900, 'y');
  set_prefix("-", 200, 'y');
  set_prefix("\\", 200, 'y');
}

static void init_syms(void)
{
  S_NIL = intern("[]");
  S_DOT = intern(".");
  S_COMMA = intern(",");
  S_SEMI = intern(";");
  S_ARROW = intern("->");
  S_NECK = intern(":-");
  S_TRUE = intern("true");
  S_FAIL = intern("fail");
  S_FALSE = intern("false");
  S_BANG = intern("!");
  S_NOTP = intern("\\+");
  S_NOT = intern("not");
  S_CALL = intern("call");
  S_MINUS = intern("-");
  S_CUT = intern("$cut");
  S_QUERY = intern("?-");
  S_PIPE = intern("|");
  S_EMPTY = intern("");
  mk_atom(S_NIL);
}

/* ---- variable maps ---- */
static void vmap_clear(VMap *m)
{
  int i;
  for (i = 0; i < m->n; i++) free(m->e[i].name);
  m->n = 0;
}

static Term *vmap_get(VMap *m, const char *name)
{
  int i;
  for (i = 0; i < m->n; i++)
    if (strcmp(m->e[i].name, name) == 0) return m->e[i].var;
  return NULL;
}

static void vmap_put(VMap *m, const char *name, Term *v)
{
  if (m->n >= m->cap) {
    m->cap = m->cap ? m->cap * 2 : 16;
    m->e = (VEnt *)xrealloc(m->e, sizeof(VEnt) * (size_t)m->cap);
  }
  m->e[m->n].name = xstrdup(name);
  m->e[m->n].var = v;
  m->n++;
}

static void vmap_copy(VMap *dst, VMap *src)
{
  int i;
  vmap_clear(dst);
  for (i = 0; i < src->n; i++) vmap_put(dst, src->e[i].name, src->e[i].var);
}

/* ---- tokenizer ---- */
static void tok_clear(void)
{
  long i;
  for (i = 0; i < g_ntok; i++) free(g_tok[i].s);
  g_ntok = 0;
  g_ppos = 0;
}

static void tok_add(int kind, const char *s, size_t n)
{
  Tok *t;
  if (g_ntok >= g_tokcap) {
    g_tokcap = g_tokcap ? g_tokcap * 2 : 256;
    g_tok = (Tok *)xrealloc(g_tok, sizeof(Tok) * (size_t)g_tokcap);
  }
  t = &g_tok[g_ntok++];
  t->kind = kind;
  t->s = (char *)xmalloc(n + 1);
  memcpy(t->s, s, n);
  t->s[n] = '\0';
  t->isnum = kind == K_BARE && is_number(t->s);
}

static int last_tok_atom_shaped(void)
{
  Tok *l;
  const char *s;
  if (g_ntok == 0) return 0;
  l = &g_tok[g_ntok - 1];
  switch (l->kind) {
  case K_QATOM: return 1;
  case K_VAR: case K_STR: case K_END: case K_FOPEN: return 0;
  case K_QEND: return 1;
  default: break;
  }
  if (l->isnum) return 0;
  s = l->s;
  if (s[1] == '\0' && strchr("()[]{},|", s[0])) return 0;
  return 1;
}

static int is_layout(int c)
{
  return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

/* the character after a backslash; -1 for a line continuation */
static int read_escape(int c)
{
  switch (c) {
  case 'n': return '\n';
  case 't': return '\t';
  case 'r': return '\r';
  case 'a': return '\a';
  case 'b': return '\b';
  case 'f': return '\f';
  case 'v': return '\v';
  case '\n': return -1;
  default: return c;
  }
}

static void tokenize(const char *src)
{
  size_t len = strlen(src), i = 0, j, k;
  int ch, nc;
  Buf cb;
  cb.s = NULL; cb.n = cb.cap = 0;
  tok_clear();
  while (i < len) {
    ch = (unsigned char)src[i];
    if (is_layout(ch)) { i++; continue; }
    if (ch == '%') {
      while (i < len && src[i] != '\n') i++;
      continue;
    }
    if (ch == '/' && src[i + 1] == '*') {
      i += 2;
      while (i < len && !(src[i] == '*' && src[i + 1] == '/')) i++;
      i += 2;
      continue;
    }
    if (ch == '\'' || ch == '"') {
      int q = ch;
      j = i + 1;
      bclear(&cb);
      for (;;) {
        int c2;
        if (j >= len) break;
        c2 = (unsigned char)src[j];
        if (c2 == q) {
          if (src[j + 1] == q) { bputc(&cb, q); j += 2; continue; }
          j++;
          break;
        }
        if (c2 == '\\') {
          int e = read_escape((unsigned char)src[j + 1]);
          if (src[j + 1] != '\0' && e >= 0) bputc(&cb, e);
          j += 2;
          continue;
        }
        bputc(&cb, c2);
        j++;
      }
      tok_add(q == '\'' ? K_QATOM : K_STR, bstr(&cb), cb.n);
      i = j;
      continue;
    }
    if (is_digit_c(ch)) {
      j = i;
      while (j < len && is_digit_c(src[j])) j++;
      if (src[j] == '.' && is_digit_c(src[j + 1])) {
        j++;
        while (j < len && is_digit_c(src[j])) j++;
      }
      if (src[j] == 'e' || src[j] == 'E') {
        k = j + 1;
        if (src[k] == '+' || src[k] == '-') k++;
        if (is_digit_c(src[k])) {
          j = k;
          while (j < len && is_digit_c(src[j])) j++;
        }
      }
      tok_add(K_BARE, src + i, j - i);
      i = j;
      continue;
    }
    if (ch == '.') {
      nc = (unsigned char)src[i + 1];
      if (nc == '\0' || is_layout(nc) || nc == '%') {
        tok_add(K_END, "", 0);
        i++;
        continue;
      }
    }
    if (ch == '?') {
      nc = (unsigned char)src[i + 1];
      if (nc == '\0' || is_layout(nc) || nc == '%') {
        tok_add(K_QEND, "", 0);
        i++;
        continue;
      }
    }
    if (is_upus_c(ch)) {
      j = i;
      while (j < len && is_ident_c(src[j])) j++;
      tok_add(K_VAR, src + i, j - i);
      i = j;
      continue;
    }
    if (is_lower_c(ch)) {
      j = i;
      while (j < len && is_ident_c(src[j])) j++;
      tok_add(K_BARE, src + i, j - i);
      i = j;
      continue;
    }
    if (ch == '(') {
      int isf = 0;
      if (i > 0 && !is_layout((unsigned char)src[i - 1]) && last_tok_atom_shaped()) isf = 1;
      if (isf) tok_add(K_FOPEN, "", 0);
      else tok_add(K_BARE, "(", 1);
      i++;
      continue;
    }
    if (is_solo_c(ch)) {
      tok_add(K_BARE, src + i, 1);
      i++;
      continue;
    }
    if (is_sym_c(ch)) {
      j = i;
      while (j < len && is_sym_c(src[j])) j++;
      tok_add(K_BARE, src + i, j - i);
      i = j;
      continue;
    }
    i++;
  }
  free(cb.s);
}

/* ---- parser ---- */
static Term *p_term(int maxprec);

static const char *peek_text(Buf *b)
{
  Tok *t;
  bclear(b);
  if (g_ppos >= g_ntok) return "<end of input>";
  t = &g_tok[g_ppos];
  switch (t->kind) {
  case K_VAR: return t->s;
  case K_QATOM:
    bputc(b, '\''); bput(b, t->s); bputc(b, '\'');
    return bstr(b);
  case K_STR:
    bputc(b, '"'); bput(b, t->s); bputc(b, '"');
    return bstr(b);
  case K_END: return ".";
  case K_QEND: return "?";
  case K_FOPEN: return "(";
  default: return t->s;
  }
}

static void syntax_error_near(const char *msg)
{
  Buf b;
  b.s = NULL; b.n = b.cap = 0;
  charout("prolog: syntax error -- ");
  charout(msg);
  charout(" ");
  charout(peek_text(&b));
  charout("\n");
  free(b.s);
}

static Term *p_resolve_var(const char *name)
{
  Term *v;
  if (strcmp(name, "_") == 0) {
    g_pvc++;
    return mk_var("G", 0, g_pvc);
  }
  v = vmap_get(&g_pvarmap, name);
  if (v != NULL) return v;
  g_pvc++;
  v = mk_var(intern(name)->text, '_', g_pvc);
  vmap_put(&g_pvarmap, name, v);
  return v;
}

static Term *p_codes(const char *s)
{
  size_t n = strlen(s), i;
  Term *r = S_NIL->pt;
  for (i = n; i > 0; i--) r = mk_cons(mk_long((long)(unsigned char)s[i - 1]), r);
  return r;
}

static int tok_is_num(long i)
{
  return i < g_ntok && g_tok[i].isnum;
}

static int looks_like_term_start(long i)
{
  Tok *t;
  Sym *y;
  if (i >= g_ntok) return 0;
  t = &g_tok[i];
  switch (t->kind) {
  case K_VAR: case K_QATOM: case K_STR: return 1;
  case K_BARE: break;
  default: return 1;
  }
  if (t->s[1] == '\0' && strchr(")]},|", t->s[0])) return 0;
  if (strcmp(t->s, "(") == 0 || strcmp(t->s, "[") == 0) return 1;
  if (t->isnum) return 1;
  y = intern(t->s);
  if (y->ip > 0 && y->pp == 0) return 0;
  return 1;
}

static Term *p_compound_args(const char *fname)
{
  Term *args[64], **av = args, *a, *r;
  int n = 0, cap = 64, i;
  g_ppos++;                                    /* the functor paren */
  if (g_ppos < g_ntok && g_tok[g_ppos].kind == K_BARE && strcmp(g_tok[g_ppos].s, ")") == 0) {
    g_ppos++;
    return mk_cmp(intern(fname), 0);
  }
  for (;;) {
    a = p_term(999);
    if (a == NULL) { if (av != args) free(av); return NULL; }
    if (n >= cap) {
      Term **nv = (Term **)xmalloc(sizeof(Term *) * (size_t)cap * 2);
      memcpy(nv, av, sizeof(Term *) * (size_t)n);
      if (av != args) free(av);
      av = nv;
      cap *= 2;
    }
    av[n++] = a;
    if (g_ppos >= g_ntok) {
      say("prolog: syntax error -- expected , or ) in argument list");
      if (av != args) free(av);
      return NULL;
    }
    if (g_tok[g_ppos].kind == K_BARE && strcmp(g_tok[g_ppos].s, ",") == 0) { g_ppos++; continue; }
    if (g_tok[g_ppos].kind == K_BARE && strcmp(g_tok[g_ppos].s, ")") == 0) { g_ppos++; break; }
    syntax_error_near("expected , or ) near");
    if (av != args) free(av);
    return NULL;
  }
  r = mk_cmp(intern(fname), n);
  for (i = 0; i < n; i++) r->u.c.args[i] = av[i];
  if (av != args) free(av);
  return r;
}

static Term *p_list(void)
{
  Term **items = NULL, *a, *tail, *r;
  int n = 0, cap = 0, i;
  g_ppos++;
  if (g_ppos < g_ntok && g_tok[g_ppos].kind == K_BARE && strcmp(g_tok[g_ppos].s, "]") == 0) {
    g_ppos++;
    return S_NIL->pt;
  }
  for (;;) {
    a = p_term(999);
    if (a == NULL) { free(items); return NULL; }
    if (n >= cap) {
      cap = cap ? cap * 2 : 16;
      items = (Term **)xrealloc(items, sizeof(Term *) * (size_t)cap);
    }
    items[n++] = a;
    if (g_ppos >= g_ntok) {
      say("prolog: syntax error -- unterminated list");
      free(items);
      return NULL;
    }
    if (g_tok[g_ppos].kind == K_BARE && strcmp(g_tok[g_ppos].s, ",") == 0) { g_ppos++; continue; }
    break;
  }
  tail = S_NIL->pt;
  if (g_ppos < g_ntok && g_tok[g_ppos].kind == K_BARE && strcmp(g_tok[g_ppos].s, "|") == 0) {
    g_ppos++;
    tail = p_term(999);
    if (tail == NULL) { free(items); return NULL; }
  }
  if (g_ppos < g_ntok && g_tok[g_ppos].kind == K_BARE && strcmp(g_tok[g_ppos].s, "]") == 0) {
    g_ppos++;
  } else {
    syntax_error_near("expected ] near");
    free(items);
    return NULL;
  }
  r = tail;
  for (i = n - 1; i >= 0; i--) r = mk_cons(items[i], r);
  free(items);
  return r;
}

static Term *p_primary(void)
{
  Tok *t;
  const char *s;
  Sym *y;
  if (g_ppos >= g_ntok || g_tok[g_ppos].kind == K_END) {
    say("prolog: syntax error -- unexpected end of clause");
    return NULL;
  }
  t = &g_tok[g_ppos];
  if (t->kind == K_BARE && strcmp(t->s, "(") == 0) {
    Term *inner;
    g_ppos++;
    inner = p_term(1200);
    if (inner == NULL) return NULL;
    if (g_ppos < g_ntok && g_tok[g_ppos].kind == K_BARE && strcmp(g_tok[g_ppos].s, ")") == 0) {
      g_ppos++;
    } else {
      syntax_error_near("expected ) near");
      return NULL;
    }
    return inner;
  }
  if (t->kind == K_BARE && strcmp(t->s, "[") == 0) return p_list();
  if (t->kind == K_VAR) {
    g_ppos++;
    return p_resolve_var(t->s);
  }
  if (t->isnum) {
    g_ppos++;
    return mk_num(t->s);
  }
  if (t->kind == K_STR) {
    g_ppos++;
    return p_codes(t->s);
  }
  if (t->kind == K_QATOM) {
    Term *nm = mk_qatom_s(t->s);
    g_ppos++;
    if (g_ppos < g_ntok && g_tok[g_ppos].kind == K_FOPEN) return p_compound_args(nm->u.a.sym->text);
    return nm;
  }
  s = t->s;
  if (t->kind == K_BARE) {
    y = intern(s);
    if (y->pp > 0 && !(g_ppos + 1 < g_ntok && g_tok[g_ppos + 1].kind == K_FOPEN)) {
      if (strcmp(s, "-") == 0 && tok_is_num(g_ppos + 1)) {
        Term *r;
        char *nb = (char *)xmalloc(strlen(g_tok[g_ppos + 1].s) + 2);
        nb[0] = '-';
        strcpy(nb + 1, g_tok[g_ppos + 1].s);
        r = mk_num(nb);
        free(nb);
        g_ppos += 2;
        return r;
      }
      if (looks_like_term_start(g_ppos + 1)) {
        Term *a1, *r;
        g_ppos++;
        a1 = p_term(y->pp);
        if (a1 == NULL) return NULL;
        r = mk_cmp(y, 1);
        r->u.c.args[0] = a1;
        return r;
      }
    }
  }
  /* plain atom (reserved punctuation excluded) */
  if (t->kind == K_BARE && t->s[1] == '\0' && strchr(")]},", t->s[0]) == NULL) s = t->s;
  else if (t->kind == K_BARE && t->s[1] == '\0' && strchr(")]},", t->s[0]) != NULL) s = NULL;
  else if (t->kind == K_FOPEN) s = "\024";
  else if (t->kind == K_QEND) s = "\025";
  if (s != NULL) {
    g_ppos++;
    if (g_ppos < g_ntok && g_tok[g_ppos].kind == K_FOPEN) return p_compound_args(s);
    return mk_atom_s(s);
  }
  syntax_error_near("unexpected token");
  return NULL;
}

static Term *p_term(int maxprec)
{
  Term *left = p_primary(), *right;
  Tok *t;
  Sym *y;
  int prec, rightmax;
  if (left == NULL) return NULL;
  for (;;) {
    if (g_ppos >= g_ntok) break;
    t = &g_tok[g_ppos];
    if (t->kind != K_BARE) break;
    if (t->s[1] == '\0' && strchr("()[]{}", t->s[0])) break;
    y = intern(t->s);
    if (y->ip == 0) break;
    prec = y->ip;
    if (prec > maxprec) break;
    rightmax = prec - 1;
    if (y->ia == 'r') rightmax = prec;
    g_ppos++;
    right = p_term(rightmax);
    if (right == NULL) return NULL;
    left = mk_cmp2(y, left, right);
  }
  return left;
}

/* ParseClauseTerm: one clause, query or directive; NULL after an error */
static Term *parse_clause(void)
{
  Term *t;
  Buf b;
  vmap_clear(&g_pvarmap);
  g_lastterm = '.';
  if (g_ppos >= g_ntok) return NULL;
  t = p_term(1200);
  if (t == NULL) return NULL;
  if (g_ppos < g_ntok && g_tok[g_ppos].kind == K_END) {
    g_ppos++;
    g_lastterm = '.';
    return t;
  }
  if (g_ppos < g_ntok && g_tok[g_ppos].kind == K_QEND) {
    g_ppos++;
    g_lastterm = '?';
    return t;
  }
  b.s = NULL; b.n = b.cap = 0;
  charout("prolog: syntax error -- expected end of clause (. or ?) near token ");
  charout(peek_text(&b));
  charout("\n");
  free(b.s);
  return NULL;
}
/* ===== p4_db.c ===== */
/* p4_db.c - clause database, clause instantiation, listing, consult */

typedef struct Goal { Term *t; struct Goal *next; } Goal;

static Pred *g_plhead, *g_pltail;
static long g_clcount, g_bootmax;

static Pred *pred_find(Sym *fn, int ar, int create)
{
  Pred *p;
  for (p = fn->preds; p; p = p->next)
    if (p->ar == ar) return p;
  if (!create) return NULL;
  p = (Pred *)xmalloc(sizeof(Pred));
  memset(p, 0, sizeof(Pred));
  p->fn = fn;
  p->ar = ar;
  p->next = fn->preds;
  fn->preds = p;
  return p;
}

static void pred_note_listed(Pred *p)
{
  if (p->listed) return;
  p->listed = 1;
  p->lnext = NULL;
  if (g_pltail) g_pltail->lnext = p;
  else g_plhead = p;
  g_pltail = p;
}

/* a snapshot of the clause list stays valid while clauses are added */
static void pred_add(Pred *p, Clause *c, int atz)
{
  pred_note_listed(p);
  if (atz) {
    if (p->n < p->cap) {
      p->v[p->n++] = c;
    } else {
      Clause **nv;
      int ncap = p->cap ? p->cap * 2 : 4;
      nv = (Clause **)xmalloc(sizeof(Clause *) * (size_t)ncap);
      if (p->n) memcpy(nv, p->v, sizeof(Clause *) * (size_t)p->n);
      nv[p->n++] = c;
      p->v = nv;
      p->cap = ncap;
    }
  } else {
    Clause **nv = (Clause **)xmalloc(sizeof(Clause *) * (size_t)(p->n + 4));
    if (p->n) memcpy(nv + 1, p->v, sizeof(Clause *) * (size_t)p->n);
    nv[0] = c;
    p->v = nv;
    p->n++;
    p->cap = p->n + 3;
  }
}

static void pred_set(Pred *p, Clause **keep, int n)
{
  Clause **nv = (Clause **)xmalloc(sizeof(Clause *) * (size_t)(n + 4));
  if (n) memcpy(nv, keep, sizeof(Clause *) * (size_t)n);
  p->v = nv;
  p->n = n;
  p->cap = n + 4;
}

/* ---- templates ---- */
typedef struct { Term **v; int n, cap; } VList;

static int vl_index(VList *l, Term *v)
{
  int i;
  for (i = 0; i < l->n; i++)
    if (l->v[i] == v) return i;
  if (l->n >= l->cap) {
    l->cap = l->cap ? l->cap * 2 : 16;
    l->v = (Term **)xrealloc(l->v, sizeof(Term *) * (size_t)l->cap);
  }
  l->v[l->n] = v;
  return l->n++;
}

static int is_gnd(Term *t)
{
  if (t->tag == T_CMP) return t->gnd;
  return t->tag == T_ATOM || t->tag == T_QATOM || t->tag == T_NUM;
}

static Term *tmpl_build(Term *t, VList *vl)
{
  t = deref(t);
  switch (t->tag) {
  case T_VAR: {
    Term *x = (Term *)aalloc(&g_perm, sizeof(Term));
    x->tag = T_TV;
    x->gnd = 0;
    x->u.x.idx = vl_index(vl, t);
    return x;
  }
  case T_NUM: return mk_numa(&g_perm, t->u.n.txt);
  case T_CMP: {
    int i, n = t->u.c.ar, g = 1;
    Term *r = mk_cmpa(&g_perm, t->u.c.fn, n);
    for (i = 0; i < n; i++) {
      r->u.c.args[i] = tmpl_build(t->u.c.args[i], vl);
      if (!is_gnd(r->u.c.args[i])) g = 0;
    }
    r->gnd = g;
    return r;
  }
  default: return t;
  }
}

static void flatten_conj(Term *t, Term ***out, int *n, int *cap)
{
  while (t->tag == T_CMP && t->u.c.fn == S_COMMA && t->u.c.ar == 2) {
    flatten_conj(t->u.c.args[0], out, n, cap);
    t = t->u.c.args[1];
  }
  if (*n >= *cap) {
    *cap = *cap ? *cap * 2 : 8;
    *out = (Term **)xrealloc(*out, sizeof(Term *) * (size_t)*cap);
  }
  (*out)[(*n)++] = t;
}

/* AssertClause: term has been dereferenced fully; atz = 1 appends */
static void assert_clause(Term *term, int atz)
{
  Term *head = term, **goals = NULL;
  int ng = 0, gcap = 0, i, ar;
  VList vl;
  Clause *c;
  Sym *fn;
  Pred *p;
  Buf nb;
  if (term->tag == T_CMP && term->u.c.fn == S_NECK && term->u.c.ar == 2) {
    head = term->u.c.args[0];
    flatten_conj(term->u.c.args[1], &goals, &ng, &gcap);
  }
  fn = functor_of(head, &ar);
  p = pred_find(fn, ar, 1);
  g_clcount++;
  c = (Clause *)xmalloc(sizeof(Clause));
  c->id = g_clcount;
  vl.v = NULL; vl.n = vl.cap = 0;
  c->head = tmpl_build(head, &vl);
  c->nhead = vl.n;
  c->nbody = ng;
  c->body = (Term **)aalloc(&g_perm, sizeof(Term *) * (size_t)(ng ? ng : 1));
  for (i = 0; i < ng; i++) c->body[i] = tmpl_build(goals[i], &vl);
  c->nvars = vl.n;
  c->vn = (char **)aalloc(&g_perm, sizeof(char *) * (size_t)(vl.n ? vl.n : 1));
  nb.s = NULL; nb.n = nb.cap = 0;
  for (i = 0; i < vl.n; i++) {
    bclear(&nb);
    var_name(vl.v[i], &nb);
    c->vn[i] = astrdup(&g_perm, bstr(&nb));
  }
  free(nb.s);
  free(vl.v);
  free(goals);
  pred_add(p, c, atz);
}

/* ---- instantiation ---- */
static Term *inst(Term *t, Term **fr, Clause *c, long cbase)
{
  int i, n;
  Term *r;
  switch (t->tag) {
  case T_TV:
    i = (int)t->u.x.idx;
    if (fr[i] == NULL) fr[i] = mk_var(c->vn[i], '#', cbase + i + 1);
    return fr[i];
  case T_CMP:
    if (t->gnd) return t;
    n = t->u.c.ar;
    r = mk_cmp(t->u.c.fn, n);
    for (i = 0; i < n; i++) r->u.c.args[i] = inst(t->u.c.args[i], fr, c, cbase);
    return r;
  default: return t;
  }
}

static Term *inst_goal(Term *t, Term **fr, Clause *c, long cbase, long barrier)
{
  if (t->tag == T_ATOM && t->u.a.sym == S_BANG) {
    Term *x = (Term *)aalloc(&g_heap, sizeof(Term));
    x->tag = T_CUT;
    x->gnd = 0;
    x->u.x.idx = barrier;
    return x;
  }
  if (t->tag == T_CMP && t->u.c.ar == 2 &&
      (t->u.c.fn == S_COMMA || t->u.c.fn == S_SEMI || t->u.c.fn == S_ARROW)) {
    Term *r = mk_cmp(t->u.c.fn, 2);
    r->u.c.args[0] = inst_goal(t->u.c.args[0], fr, c, cbase, barrier);
    r->u.c.args[1] = inst_goal(t->u.c.args[1], fr, c, cbase, barrier);
    return r;
  }
  return inst(t, fr, c, cbase);
}

/* unify a goal argument with a head template */
static int unify_head(Term *tm, Term *t, Term **fr, Clause *c, long cbase)
{
  Term *d;
  int i, n;
  switch (tm->tag) {
  case T_TV:
    i = (int)tm->u.x.idx;
    if (fr[i] == NULL) {
      d = deref(t);
      if (d->tag == T_VAR) {
        fr[i] = mk_var(c->vn[i], '#', cbase + i + 1);
        bind(d, fr[i]);
      } else {
        fr[i] = d;
      }
      return 1;
    }
    return unify(t, fr[i]);
  case T_CMP:
    d = deref(t);
    if (d->tag == T_VAR) {
      bind(d, inst(tm, fr, c, cbase));
      return 1;
    }
    if (d->tag != T_CMP || d->u.c.fn != tm->u.c.fn || d->u.c.ar != tm->u.c.ar) return 0;
    n = tm->u.c.ar;
    for (i = 0; i < n; i++)
      if (!unify_head(tm->u.c.args[i], d->u.c.args[i], fr, c, cbase)) return 0;
    return 1;
  default:
    d = deref(t);
    if (d->tag == T_VAR) {
      bind(d, tm);
      return 1;
    }
    return unify(d, tm);
  }
}

static Term *conj_term(Term **goals, int n)
{
  Term *r;
  int i;
  if (n == 0) return mk_atom(S_TRUE);
  r = goals[n - 1];
  for (i = n - 2; i >= 0; i--) r = mk_cmp2(S_COMMA, goals[i], r);
  return r;
}

/* retract/1: first clause whose head and body unify */
static int retract_clause(Term *headpat, Term *bodypat)
{
  int ar, i, j;
  Sym *fn = functor_of(headpat, &ar);
  Pred *p = pred_find(fn, ar, 0);
  Clause **v, *c;
  int n;
  if (p == NULL) return 0;
  v = p->v;
  n = p->n;
  for (i = 0; i < n; i++) {
    long tm = trail_mark();
    AMark hm = amark(&g_heap);
    Term **fr, *rh, *rb, **gl;
    c = v[i];
    fr = (Term **)aalloc(&g_heap, sizeof(Term *) * (size_t)(c->nvars + 1));
    for (j = 0; j < c->nvars; j++) fr[j] = NULL;
    rh = inst(c->head, fr, c, g_rvc);
    gl = (Term **)aalloc(&g_heap, sizeof(Term *) * (size_t)(c->nbody + 1));
    for (j = 0; j < c->nbody; j++) gl[j] = inst(c->body[j], fr, c, g_rvc);
    g_rvc += c->nvars;
    rb = conj_term(gl, c->nbody);
    if (unify(headpat, rh) && unify(bodypat, rb)) {
      Clause **keep = (Clause **)xmalloc(sizeof(Clause *) * (size_t)(n + 1));
      int k = 0;
      for (j = 0; j < n; j++) if (v[j] != c) keep[k++] = v[j];
      pred_set(p, keep, k);
      free(keep);
      return 1;
    }
    trail_undo(tm);
    arelease(&g_heap, hm);
  }
  return 0;
}

static void retract_all(Term *pat)
{
  int ar, i, j, n, k = 0;
  Sym *fn = functor_of(pat, &ar);
  Pred *p = pred_find(fn, ar, 1);
  Clause **keep, **v;
  v = p->v;
  n = p->n;
  keep = (Clause **)xmalloc(sizeof(Clause *) * (size_t)(n + 1));
  for (i = 0; i < n; i++) {
    long tm = trail_mark();
    AMark hm = amark(&g_heap);
    Term **fr, *rh;
    Clause *c = v[i];
    fr = (Term **)aalloc(&g_heap, sizeof(Term *) * (size_t)(c->nvars + 1));
    for (j = 0; j < c->nvars; j++) fr[j] = NULL;
    rh = inst(c->head, fr, c, g_rvc);
    g_rvc += c->nhead;
    if (!unify(pat, rh)) keep[k++] = c;
    trail_undo(tm);
    arelease(&g_heap, hm);
  }
  pred_set(p, keep, k);
  free(keep);
}

/* ---- listing and saving ---- */
static void clause_text(Clause *c, Buf *b)
{
  g_wtvn = c->vn;
  wt(c->head, 1, 1200, b);
  if (c->nbody == 0) {
    bputc(b, '.');
  } else {
    AMark hm = amark(&g_heap);
    bput(b, " :-\n    ");
    wt(conj_term(c->body, c->nbody), 1, 1200, b);
    bputc(b, '.');
    arelease(&g_heap, hm);
  }
  g_wtvn = NULL;
}

static void listing_all(int user_only)
{
  Pred *p;
  Buf b;
  int i, shown;
  b.s = NULL; b.n = b.cap = 0;
  for (p = g_plhead; p; p = p->lnext) {
    if (p->n == 0) continue;
    shown = 0;
    for (i = 0; i < p->n; i++) {
      if (user_only && p->v[i]->id <= g_bootmax) continue;
      bclear(&b);
      clause_text(p->v[i], &b);
      say(bstr(&b));
      shown = 1;
    }
    if (shown) say("");
  }
  free(b.s);
}

static void listing_one(const char *name)
{
  Pred *p;
  Buf b, k;
  int i;
  const char *sl;
  b.s = NULL; b.n = b.cap = 0;
  k.s = NULL; k.n = k.cap = 0;
  for (p = g_plhead; p; p = p->lnext) {
    /* the key is name/arity; its name part ends at the first slash */
    bclear(&k);
    sl = strchr(p->fn->text, '/');
    if (sl == NULL) bput(&k, p->fn->text);
    else {
      const char *q;
      for (q = p->fn->text; q < sl; q++) bputc(&k, *q);
    }
    if (rx_cmp(bstr(&k), name) != 0) continue;
    for (i = 0; i < p->n; i++) {
      bclear(&b);
      clause_text(p->v[i], &b);
      say(bstr(&b));
    }
    say("");
  }
  free(b.s);
  free(k.s);
}

static void save_workspace(const char *fname, int user_only)
{
  FILE *f = fopen(fname, "w");
  Pred *p;
  Buf b;
  int i;
  long count = 0;
  char nb[48];
  b.s = NULL; b.n = b.cap = 0;
  if (f == NULL) {
    charout("prolog: cannot write ");
    say(fname);
    return;
  }
  for (p = g_plhead; p; p = p->lnext) {
    for (i = 0; i < p->n; i++) {
      if (user_only && p->v[i]->id <= g_bootmax) continue;
      bclear(&b);
      clause_text(p->v[i], &b);
      fputs(bstr(&b), f);
      fputs("\n\n", f);
      count++;
    }
  }
  fclose(f);
  free(b.s);
  if (count == 0 && user_only) {
    charout("prolog: nothing added this session -- saved an empty file to ");
    say(fname);
  } else {
    sprintf(nb, "%ld", count);
    charout("prolog: saved ");
    charout(nb);
    charout(" clause(s) to ");
    say(fname);
  }
}
/* ===== p5_solve.c ===== */
/* p5_solve.c - arithmetic and the resolution engine */

/* ---- arithmetic values: a double plus the text CREXX would hold ---- */
typedef struct { double d; const char *lit; Term *src; char buf[48]; } AV;

static void av_make(AV *r, double x)
{
  fmt_double(x, r->buf);
  r->lit = NULL;
  r->src = NULL;
  if (x != x || x - x != 0.0) r->d = x;
  else r->d = strtod(r->buf, NULL);
}

static const char *av_text(const AV *a)
{
  return a->lit ? a->lit : a->buf;
}

static void av_text_in(AV *r, const char *txt)
{
  r->lit = txt;
  r->src = NULL;
  r->d = num_val(txt);
  r->buf[0] = '\0';
}

static int arith_err(const char *msg, const char *arg)
{
  charout("prolog: arithmetic: ");
  charout(msg);
  if (arg) {
    charout(" ");
    charout(arg);
  }
  charout("\n");
  return 0;
}

static int arith_eval(Term *t, AV *r);

static int arith_fn1(Sym *fn, AV *a, AV *r)
{
  const char *f = fn->text;
  char *tt;
  if (strcmp(f, "-") == 0) { av_make(r, 0.0 - a->d); return 1; }
  if (strcmp(f, "+") == 0) { *r = *a; return 1; }
  if (strcmp(f, "abs") == 0) { av_text_in(r, abs_text(av_text(a))); return 1; }
  if (strcmp(f, "sign") == 0) {
    av_make(r, a->d > 0 ? 1.0 : (a->d < 0 ? -1.0 : 0.0));
    return 1;
  }
  if (strcmp(f, "sqrt") == 0) {
    if (a->d < 0) return arith_err("sqrt of a negative number", NULL);
    av_make(r, sqrt(a->d));
    return 1;
  }
  if (strcmp(f, "truncate") == 0 || strcmp(f, "integer") == 0) {
    av_text_in(r, trunc_text(av_text(a)));
    return 1;
  }
  if (strcmp(f, "float") == 0) { *r = *a; return 1; }
  if (strcmp(f, "round") == 0) { av_text_in(r, round_text(av_text(a))); return 1; }
  if (strcmp(f, "floor") == 0) {
    double neg = 0.0;
    tt = trunc_text(av_text(a));
    if (a->d < 0 && a->d != num_val(tt)) neg = 1.0;
    av_make(r, num_val(tt) - neg);
    return 1;
  }
  if (strcmp(f, "ceiling") == 0) {
    double pos = 0.0;
    tt = trunc_text(av_text(a));
    if (a->d > 0 && a->d != num_val(tt)) pos = 1.0;
    av_make(r, num_val(tt) + pos);
    return 1;
  }
  {
    Buf b;
    char nb[24];
    b.s = NULL; b.n = b.cap = 0;
    bput(&b, f);
    sprintf(nb, "/%d", 1);
    bput(&b, nb);
    arith_err("unknown function", bstr(&b));
    free(b.s);
  }
  return 0;
}

static int arith_fn2(Sym *fn, AV *a, AV *b, AV *r)
{
  const char *f = fn->text;
  if (strcmp(f, "+") == 0) { av_make(r, a->d + b->d); return 1; }
  if (strcmp(f, "-") == 0) { av_make(r, a->d - b->d); return 1; }
  if (strcmp(f, "*") == 0) { av_make(r, a->d * b->d); return 1; }
  if (strcmp(f, "/") == 0) {
    if (b->d == 0.0) return arith_err("division by zero", NULL);
    av_make(r, a->d / b->d);
    return 1;
  }
  if (strcmp(f, "//") == 0) {
    char qb[64];
    if (b->d == 0.0) return arith_err("division by zero", NULL);
    fmt_double_n(a->d / b->d, 16, qb);
    av_text_in(r, trunc_text(qb));
    return 1;
  }
  if (strcmp(f, "mod") == 0) {
    if (b->d == 0.0) return arith_err("division by zero", NULL);
    av_make(r, fmod(a->d, b->d));
    if (r->d != 0.0) {
      int mneg = r->d < 0, bneg = b->d < 0;
      if (mneg != bneg) av_make(r, r->d + b->d);
    }
    return 1;
  }
  if (strcmp(f, "rem") == 0) {
    if (b->d == 0.0) return arith_err("division by zero", NULL);
    av_make(r, fmod(a->d, b->d));
    return 1;
  }
  if (strcmp(f, "**") == 0) { av_make(r, pow(a->d, b->d)); return 1; }
  if (strcmp(f, "min") == 0) { *r = a->d < b->d ? *a : *b; return 1; }
  if (strcmp(f, "max") == 0) { *r = a->d > b->d ? *a : *b; return 1; }
  {
    Buf bb;
    bb.s = NULL; bb.n = bb.cap = 0;
    bput(&bb, f);
    bput(&bb, "/2");
    arith_err("unknown function", bstr(&bb));
    free(bb.s);
  }
  return 0;
}

static int arith_eval(Term *t, AV *r)
{
  AV a, b;
  t = deref(t);
  if (t->tag == T_VAR) return arith_err("unbound variable", NULL);
  if (t->tag == T_NUM) {
    r->lit = t->u.n.txt;
    r->src = t;
    r->d = num_val(t->u.n.txt);
    r->buf[0] = '\0';
    return 1;
  }
  if (t->tag != T_CMP) {
    Buf sb;
    const char *nm;
    sb.s = NULL; sb.n = sb.cap = 0;
    nm = atext(t, &sb);
    if (strcmp(nm, "pi") == 0) av_text_in(r, "3.14159265358979");
    else if (strcmp(nm, "e") == 0) av_text_in(r, "2.71828182845905");
    else {
      arith_err("not evaluable:", nm);
      free(sb.s);
      return 0;
    }
    free(sb.s);
    return 1;
  }
  if (t->u.c.ar == 1) {
    if (!arith_eval(t->u.c.args[0], &a)) return 0;
    return arith_fn1(t->u.c.fn, &a, r);
  }
  if (t->u.c.ar == 2) {
    if (!arith_eval(t->u.c.args[0], &a)) return 0;
    if (!arith_eval(t->u.c.args[1], &b)) return 0;
    return arith_fn2(t->u.c.fn, &a, &b, r);
  }
  charout("prolog: arithmetic: not evaluable: ");
  charout(write_term(t, 1));
  charout("\n");
  return 0;
}

static Term *av_term(AV *r)
{
  if (r->src != NULL) return r->src;
  return mk_from_text(av_text(r));
}

/* ---- the engine ---- */
enum { M_FIRST, M_FINDALL, M_INTERACT };
enum { F_CUT, F_DISJ, F_ITE, F_ITE0, F_NEG, F_USER, F_FA };

typedef struct FAcc {
  Arena ar;
  Term **res;
  long n, cap;
  Term **omap, **nmap;      /* heap variable -> findall copy */
  long nmapn, nmapcap;
} FAcc;

typedef struct Frame {
  int kind;
  int savedmode;
  long tmark;
  AMark hmark;
  Goal *rest;
  Term *a, *b;
  Clause **v;
  int n, i;
  long barrier;
  FAcc *fa, *savedfa;
  Term *tmpl, *savedtmpl;
  long fapos;
} Frame;

static Frame *g_stk;
static long g_sp, g_stkcap;
static int g_mode = M_FIRST;
static long g_cutsig, g_callcount;
static FAcc *g_fa;
static Term *g_fatmpl;
static long g_fapos;

static int bi_exec(int id, Term *goal);
static int on_solution(void);
#define BI_FINDALL_ID 1

static Goal *new_goal(Term *t, Goal *next)
{
  Goal *g = (Goal *)aalloc(&g_heap, sizeof(Goal));
  g->t = t;
  g->next = next;
  return g;
}

static Frame *push_frame(int kind)
{
  Frame *f;
  if (g_sp >= g_stkcap) {
    g_stkcap = g_stkcap ? g_stkcap * 2 : 1024;
    g_stk = (Frame *)xrealloc(g_stk, sizeof(Frame) * (size_t)g_stkcap);
  }
  f = &g_stk[g_sp++];
  f->kind = kind;
  f->tmark = trail_mark();
  f->hmark = amark(&g_heap);
  f->savedmode = g_mode;
  return f;
}

static void arena_free(Arena *a)
{
  int i;
  for (i = 0; i < a->n; i++) free(a->b[i].p);
  free(a->b);
  a->b = NULL;
  a->n = a->ncap = a->cur = 0;
}

/* copy a solution out of the heap: numbers and new variables are duplicated */
static Term *fa_out(FAcc *fa, Term *t)
{
  Term *first = NULL, **hole = &first, *r;
  for (;;) {
    t = deref(t);
    if (t->tag == T_VAR) {
      long i;
      Term *c;
      if (t->u.v.born < g_fapos) { *hole = t; return first; }
      for (i = 0; i < fa->nmapn; i++)
        if (fa->omap[i] == t) { *hole = fa->nmap[i]; return first; }
      c = (Term *)aalloc(&fa->ar, sizeof(Term));
      c->tag = T_VAR;
      c->gnd = 0;
      c->u.v.ref = NULL;
      c->u.v.base = astrdup(&fa->ar, t->u.v.base);
      c->u.v.sep = t->u.v.sep;
      c->u.v.ctr = t->u.v.ctr;
      c->u.v.born = -1;
      if (fa->nmapn >= fa->nmapcap) {
        fa->nmapcap = fa->nmapcap ? fa->nmapcap * 2 : 16;
        fa->omap = (Term **)xrealloc(fa->omap, sizeof(Term *) * (size_t)fa->nmapcap);
        fa->nmap = (Term **)xrealloc(fa->nmap, sizeof(Term *) * (size_t)fa->nmapcap);
      }
      fa->omap[fa->nmapn] = t;
      fa->nmap[fa->nmapn] = c;
      fa->nmapn++;
      *hole = c;
      return first;
    }
    if (t->tag == T_NUM) { *hole = mk_numa(&fa->ar, t->u.n.txt); return first; }
    if (t->tag != T_CMP || t->u.c.ar == 0) {
      if (t->tag == T_CMP) {
        *hole = mk_cmpa(&fa->ar, t->u.c.fn, 0);
      } else {
        *hole = t;
      }
      return first;
    }
    {
      int i, n = t->u.c.ar;
      r = mk_cmpa(&fa->ar, t->u.c.fn, n);
      for (i = 0; i < n - 1; i++) r->u.c.args[i] = fa_out(fa, t->u.c.args[i]);
      *hole = r;
      hole = &r->u.c.args[n - 1];
      t = t->u.c.args[n - 1];
    }
  }
}

typedef struct { Term **o, **n; long cnt, cap; } InMap;

static Term *fa_in(InMap *m, Term *t)
{
  Term *first = NULL, **hole = &first, *r;
  for (;;) {
    if (t->tag == T_VAR && t->u.v.born == -1) {
      long i;
      Term *c;
      for (i = 0; i < m->cnt; i++)
        if (m->o[i] == t) { *hole = m->n[i]; return first; }
      c = mk_var(astrdup(&g_heap, t->u.v.base), t->u.v.sep, t->u.v.ctr);
      if (m->cnt >= m->cap) {
        m->cap = m->cap ? m->cap * 2 : 16;
        m->o = (Term **)xrealloc(m->o, sizeof(Term *) * (size_t)m->cap);
        m->n = (Term **)xrealloc(m->n, sizeof(Term *) * (size_t)m->cap);
      }
      m->o[m->cnt] = t;
      m->n[m->cnt] = c;
      m->cnt++;
      *hole = c;
      return first;
    }
    if (t->tag == T_NUM) { *hole = mk_num(t->u.n.txt); return first; }
    if (t->tag != T_CMP || t->u.c.ar == 0) {
      if (t->tag == T_CMP) *hole = mk_cmp(t->u.c.fn, 0);
      else *hole = t;
      return first;
    }
    {
      int i, n = t->u.c.ar;
      r = mk_cmp(t->u.c.fn, n);
      for (i = 0; i < n - 1; i++) r->u.c.args[i] = fa_in(m, t->u.c.args[i]);
      *hole = r;
      hole = &r->u.c.args[n - 1];
      t = t->u.c.args[n - 1];
    }
  }
}

static Term *fa_list(FAcc *fa)
{
  InMap m;
  Term *r = S_NIL->pt;
  long i;
  m.o = m.n = NULL;
  m.cnt = m.cap = 0;
  for (i = fa->n - 1; i >= 0; i--) {
    Term *e = fa_in(&m, fa->res[i]);
    r = mk_cons(e, r);
  }
  free(m.o);
  free(m.n);
  return r;
}

static void fa_free(FAcc *fa)
{
  arena_free(&fa->ar);
  free(fa->res);
  free(fa->omap);
  free(fa->nmap);
  free(fa);
}

static int run(Goal *goals)
{
  long base = g_sp;
  int v = 0, ar, ar2, i, k, ok;
  Term *goal, *g0, *left, **fr;
  Goal *rest;
  Sym *fn;
  Frame *fp;
  Clause *cl;
  Pred *pr;
  long cbase;

eval:
  if (goals == NULL) {
    v = on_solution();
    goto ret;
  }
  goal = deref(goals->t);
  rest = goals->next;
  if (goal->tag == T_VAR) {
    say("prolog: instantiation error (unbound goal)");
    v = 0;
    goto ret;
  }
  if (goal->tag == T_CUT) {
    fp = push_frame(F_CUT);
    fp->barrier = goal->u.x.idx;
    goals = rest;
    goto eval;
  }
  if (goal->tag == T_CMP) { fn = goal->u.c.fn; ar = goal->u.c.ar; }
  else fn = functor_of(goal, &ar);

  if (fn == S_COMMA && ar == 2) {
    goals = new_goal(goal->u.c.args[0], new_goal(goal->u.c.args[1], rest));
    goto eval;
  }
  if (fn == S_TRUE && ar == 0) { goals = rest; goto eval; }
  if ((fn == S_FAIL || fn == S_FALSE) && ar == 0) { v = 0; goto ret; }
  if (fn == S_BANG && ar == 0) { goals = rest; goto eval; }
  if (fn == S_SEMI && ar == 2) {
    left = goal->u.c.args[0];
    if (left->tag == T_CMP && left->u.c.fn == S_ARROW && left->u.c.ar == 2) {
      fp = push_frame(F_ITE);
      fp->a = left->u.c.args[1];
      fp->b = goal->u.c.args[1];
      fp->rest = rest;
      g_mode = M_FIRST;
      goals = new_goal(left->u.c.args[0], NULL);
      goto eval;
    }
    fp = push_frame(F_DISJ);
    fp->a = goal->u.c.args[1];
    fp->rest = rest;
    goals = new_goal(left, rest);
    goto eval;
  }
  if (fn == S_ARROW && ar == 2) {
    fp = push_frame(F_ITE0);
    fp->a = goal->u.c.args[1];
    fp->rest = rest;
    g_mode = M_FIRST;
    goals = new_goal(goal->u.c.args[0], NULL);
    goto eval;
  }
  if ((fn == S_NOTP || fn == S_NOT) && ar == 1) {
    fp = push_frame(F_NEG);
    fp->rest = rest;
    g_mode = M_FIRST;
    goals = new_goal(goal->u.c.args[0], NULL);
    goto eval;
  }
  if (fn == S_CALL && ar >= 1) {
    g0 = deref(goal->u.c.args[0]);
    if (ar == 1) {
      goals = new_goal(g0, rest);
    } else {
      Sym *f2 = functor_of(g0, &ar2);
      Term *ng = mk_cmp(f2, ar2 + ar - 1);
      for (i = 0; i < ar2; i++) ng->u.c.args[i] = g0->u.c.args[i];
      for (i = 1; i < ar; i++) ng->u.c.args[ar2 + i - 1] = goal->u.c.args[i];
      goals = new_goal(ng, rest);
    }
    goto eval;
  }
  if (ar < 4 && fn->bi[ar]) {
    if (fn->bi[ar] == BI_FINDALL_ID) {
      FAcc *fa = (FAcc *)xmalloc(sizeof(FAcc));
      memset(fa, 0, sizeof(FAcc));
      fp = push_frame(F_FA);
      fp->fa = fa;
      fp->savedfa = g_fa;
      fp->savedtmpl = g_fatmpl;
      fp->fapos = g_fapos;
      fp->b = goal->u.c.args[2];
      fp->rest = rest;
      g_mode = M_FINDALL;
      g_fa = fa;
      g_fatmpl = goal->u.c.args[0];
      g_fapos = apos(&g_heap);
      goals = new_goal(goal->u.c.args[1], NULL);
      goto eval;
    }
    if (bi_exec(fn->bi[ar], goal)) goals = rest;
    else { v = 0; goto ret; }
    goto eval;
  }
  /* a user predicate */
  pr = pred_find(fn, ar, 0);
  if (pr == NULL || pr->n == 0) {
    if (pr == NULL || !pr->listed) {
      char num[24];
      charout("prolog: unknown procedure ");
      charout(fn->text);
      sprintf(num, "/%d", ar);
      say(num);
    }
    v = 0;
    goto ret;
  }
  fp = push_frame(F_USER);
  fp->a = goal;
  fp->rest = rest;
  fp->v = pr->v;
  fp->n = pr->n;
  fp->i = 0;
  fp->barrier = ++g_callcount;

try_clause:
  fp = &g_stk[g_sp - 1];
  if (fp->i >= fp->n) {
    g_sp--;
    v = 0;
    goto ret;
  }
  fp->tmark = trail_mark();
  fp->hmark = amark(&g_heap);
  cl = fp->v[fp->i];
  fr = (Term **)aalloc(&g_heap, sizeof(Term *) * (size_t)(cl->nvars + 1));
  for (k = 0; k < cl->nvars; k++) fr[k] = NULL;
  cbase = g_rvc;
  g_rvc += cl->nhead;
  ok = unify_head(cl->head, fp->a, fr, cl, cbase);
  if (ok) {
    Goal *g = fp->rest;
    long barrier = fp->barrier;
    g_rvc += cl->nvars - cl->nhead;
    for (k = cl->nbody - 1; k >= 0; k--)
      g = new_goal(inst_goal(cl->body[k], fr, cl, cbase, barrier), g);
    goals = g;
    goto eval;
  }
  trail_undo(fp->tmark);
  arelease(&g_heap, fp->hmark);
  if (g_cutsig != 0) {
    if (g_cutsig == fp->barrier) g_cutsig = 0;
    g_sp--;
    v = 0;
    goto ret;
  }
  fp->i++;
  goto try_clause;

ret:
  if (g_sp == base) return v;
  fp = &g_stk[g_sp - 1];
  switch (fp->kind) {
  case F_CUT: {
    long b = fp->barrier;
    g_sp--;
    if (!v) g_cutsig = b;
    goto ret;
  }
  case F_DISJ:
    g_sp--;
    if (v) goto ret;
    trail_undo(fp->tmark);
    arelease(&g_heap, fp->hmark);
    goals = new_goal(fp->a, fp->rest);
    goto eval;
  case F_ITE:
    g_sp--;
    g_mode = fp->savedmode;
    if (v) {
      goals = new_goal(fp->a, fp->rest);
      goto eval;
    }
    trail_undo(fp->tmark);
    arelease(&g_heap, fp->hmark);
    goals = new_goal(fp->b, fp->rest);
    goto eval;
  case F_ITE0:
    g_sp--;
    g_mode = fp->savedmode;
    if (v) {
      goals = new_goal(fp->a, fp->rest);
      goto eval;
    }
    trail_undo(fp->tmark);
    arelease(&g_heap, fp->hmark);
    v = 0;
    goto ret;
  case F_NEG:
    g_sp--;
    g_mode = fp->savedmode;
    trail_undo(fp->tmark);
    arelease(&g_heap, fp->hmark);
    if (v) { v = 0; goto ret; }
    goals = fp->rest;
    goto eval;
  case F_USER:
    if (v) { g_sp--; goto ret; }
    trail_undo(fp->tmark);
    arelease(&g_heap, fp->hmark);
    if (g_cutsig != 0) {
      if (g_cutsig == fp->barrier) g_cutsig = 0;
      g_sp--;
      v = 0;
      goto ret;
    }
    fp->i++;
    goto try_clause;
  default: {                          /* F_FA */
    FAcc *fa = fp->fa;
    Term *lst, *res;
    Goal *rs = fp->rest;
    g_sp--;
    g_mode = fp->savedmode;
    g_fa = fp->savedfa;
    g_fatmpl = fp->savedtmpl;
    g_fapos = fp->fapos;
    res = fp->b;
    trail_undo(fp->tmark);
    arelease(&g_heap, fp->hmark);
    lst = fa_list(fa);
    fa_free(fa);
    if (unify(res, lst)) {
      goals = rs;
      goto eval;
    }
    v = 0;
    goto ret;
  }
  }
}

/* a solution of the goal list: what happens depends on the mode */
static int interact_on_solution(void);

static int on_solution(void)
{
  switch (g_mode) {
  case M_FIRST: return 1;
  case M_FINDALL: {
    FAcc *fa = g_fa;
    if (fa->n >= fa->cap) {
      fa->cap = fa->cap ? fa->cap * 2 : 16;
      fa->res = (Term **)xrealloc(fa->res, sizeof(Term *) * (size_t)fa->cap);
    }
    fa->res[fa->n++] = fa_out(fa, g_fatmpl);
    return 0;
  }
  default: return interact_on_solution();
  }
}

static int solve_once(Term *goal)
{
  int saved = g_mode, found;
  g_mode = M_FIRST;
  found = run(new_goal(goal, NULL));
  g_mode = saved;
  return found;
}
/* ===== p6_bi.c ===== */
/* p6_bi.c - built-in predicates */

enum {
  BI_NONE, BI_FINDALL, BI_UNIFY, BI_NOTUNIFY, BI_EQ, BI_NEQ, BI_LT_T, BI_GT_T, BI_LE_T,
  BI_GE_T, BI_COMPARE, BI_IS, BI_LT, BI_GT, BI_LE, BI_GE, BI_AEQ, BI_ANE, BI_VAR,
  BI_NONVAR, BI_ATOM, BI_ATOMIC, BI_NUMBER, BI_INTEGER, BI_FLOAT, BI_COMPOUND,
  BI_CALLABLE, BI_ISLIST, BI_GROUND, BI_FUNCTOR, BI_ARG, BI_UNIV, BI_COPY,
  BI_ATOM_CODES, BI_ATOM_CHARS, BI_NUM_CODES, BI_NUM_CHARS, BI_ATOM_LENGTH,
  BI_CHAR_CODE, BI_ATOM_CONCAT, BI_UPCASE, BI_DOWNCASE, BI_ASSERT, BI_ASSERTA,
  BI_ASSERTZ, BI_RETRACT, BI_RETRACTALL, BI_WRITE, BI_WRITELN, BI_NL, BI_PRINT,
  BI_TAB, BI_CONSULT, BI_LISTING0, BI_LISTINGALL, BI_LISTING1, BI_SAVE, BI_SAVEALL,
  BI_LOAD, BI_CXCALL2, BI_CXCALL3, BI_HALT0, BI_HALT1
};

static void reg_bi(const char *name, int ar, int id)
{
  intern(name)->bi[ar] = (unsigned char)id;
}

static void init_builtins(void)
{
  reg_bi("=", 2, BI_UNIFY);
  reg_bi("\\=", 2, BI_NOTUNIFY);
  reg_bi("==", 2, BI_EQ);
  reg_bi("\\==", 2, BI_NEQ);
  reg_bi("@<", 2, BI_LT_T);
  reg_bi("@>", 2, BI_GT_T);
  reg_bi("@=<", 2, BI_LE_T);
  reg_bi("@>=", 2, BI_GE_T);
  reg_bi("compare", 3, BI_COMPARE);
  reg_bi("is", 2, BI_IS);
  reg_bi("<", 2, BI_LT);
  reg_bi(">", 2, BI_GT);
  reg_bi("=<", 2, BI_LE);
  reg_bi(">=", 2, BI_GE);
  reg_bi("=:=", 2, BI_AEQ);
  reg_bi("=\\=", 2, BI_ANE);
  reg_bi("var", 1, BI_VAR);
  reg_bi("nonvar", 1, BI_NONVAR);
  reg_bi("atom", 1, BI_ATOM);
  reg_bi("atomic", 1, BI_ATOMIC);
  reg_bi("number", 1, BI_NUMBER);
  reg_bi("integer", 1, BI_INTEGER);
  reg_bi("float", 1, BI_FLOAT);
  reg_bi("compound", 1, BI_COMPOUND);
  reg_bi("callable", 1, BI_CALLABLE);
  reg_bi("is_list", 1, BI_ISLIST);
  reg_bi("ground", 1, BI_GROUND);
  reg_bi("functor", 3, BI_FUNCTOR);
  reg_bi("arg", 3, BI_ARG);
  reg_bi("=..", 2, BI_UNIV);
  reg_bi("copy_term", 2, BI_COPY);
  reg_bi("atom_codes", 2, BI_ATOM_CODES);
  reg_bi("atom_chars", 2, BI_ATOM_CHARS);
  reg_bi("number_codes", 2, BI_NUM_CODES);
  reg_bi("number_chars", 2, BI_NUM_CHARS);
  reg_bi("atom_length", 2, BI_ATOM_LENGTH);
  reg_bi("char_code", 2, BI_CHAR_CODE);
  reg_bi("atom_concat", 3, BI_ATOM_CONCAT);
  reg_bi("upcase_atom", 2, BI_UPCASE);
  reg_bi("downcase_atom", 2, BI_DOWNCASE);
  reg_bi("assert", 1, BI_ASSERT);
  reg_bi("asserta", 1, BI_ASSERTA);
  reg_bi("assertz", 1, BI_ASSERTZ);
  reg_bi("retract", 1, BI_RETRACT);
  reg_bi("retractall", 1, BI_RETRACTALL);
  reg_bi("findall", 3, BI_FINDALL);
  reg_bi("write", 1, BI_WRITE);
  reg_bi("writeln", 1, BI_WRITELN);
  reg_bi("nl", 0, BI_NL);
  reg_bi("print", 1, BI_PRINT);
  reg_bi("tab", 1, BI_TAB);
  reg_bi("consult", 1, BI_CONSULT);
  reg_bi("listing", 0, BI_LISTING0);
  reg_bi("listing_all", 0, BI_LISTINGALL);
  reg_bi("listing", 1, BI_LISTING1);
  reg_bi("save", 1, BI_SAVE);
  reg_bi("save_all", 1, BI_SAVEALL);
  reg_bi("load", 1, BI_LOAD);
  reg_bi("crexx_call", 2, BI_CXCALL2);
  reg_bi("crexx_call", 3, BI_CXCALL3);
  reg_bi("halt", 0, BI_HALT0);
  reg_bi("halt", 1, BI_HALT1);
}

static int is_builtin(Sym *fn, int ar)
{
  return ar >= 0 && ar < 4 && fn->bi[ar] != 0;
}

/* ---- list helpers ---- */
static int is_proper_list(Term *t)
{
  t = deref(t);
  while (t->tag == T_CMP && t->u.c.fn == S_DOT && t->u.c.ar == 2) t = deref(t->u.c.args[1]);
  return t == S_NIL->pt;
}

static int is_ground(Term *t)
{
  int i, n;
  for (;;) {
    t = deref(t);
    if (t->tag == T_VAR) return 0;
    if (t->tag != T_CMP) return 1;
    n = t->u.c.ar;
    if (n == 0) return 1;
    for (i = 0; i < n - 1; i++)
      if (!is_ground(t->u.c.args[i])) return 0;
    t = t->u.c.args[n - 1];
  }
}

/* the elements of a proper-looking list (stops at the first non-cell) */
static int list_items(Term *t, Term ***out)
{
  int n = 0, cap = 8;
  Term **v = (Term **)aalloc(&g_heap, sizeof(Term *) * (size_t)cap);
  t = deref(t);
  while (t->tag == T_CMP && t->u.c.fn == S_DOT && t->u.c.ar == 2) {
    if (n >= cap) {
      Term **nv = (Term **)aalloc(&g_heap, sizeof(Term *) * (size_t)cap * 2);
      memcpy(nv, v, sizeof(Term *) * (size_t)n);
      v = nv;
      cap *= 2;
    }
    v[n++] = t->u.c.args[0];
    t = deref(t->u.c.args[1]);
  }
  *out = v;
  return n;
}

static Term *codes_list(const char *s)
{
  size_t n = strlen(s), i;
  Term *r = S_NIL->pt;
  for (i = n; i > 0; i--) r = mk_cons(mk_long((long)(unsigned char)s[i - 1]), r);
  return r;
}

static Term *chars_list(const char *s)
{
  size_t n = strlen(s), i;
  Term *r = S_NIL->pt;
  char cb[2];
  cb[1] = '\0';
  for (i = n; i > 0; i--) {
    cb[0] = s[i - 1];
    r = mk_cons(mk_qatom_s(cb), r);
  }
  return r;
}

/* d2c: the characters whose big-endian code is the number */
static void d2c_text(unsigned long v, Buf *b)
{
  char tmp[16];
  int n = 0;
  if (v == 0) return;
  while (v > 0 && n < 16) {
    tmp[n++] = (char)(v & 255UL);
    v >>= 8;
  }
  while (n > 0) bputc(b, (unsigned char)tmp[--n]);
}

static void codes_to_text(Term *l, Buf *b)
{
  Term **it;
  int n = list_items(l, &it), i;
  for (i = 0; i < n; i++) {
    Term *e = deref(it[i]);
    if (e->tag == T_NUM) d2c_text((unsigned long)num_val(e->u.n.txt), b);
  }
}

static void chars_to_text(Term *l, Buf *b)
{
  Term **it;
  int n = list_items(l, &it), i;
  Buf s;
  s.s = NULL; s.n = s.cap = 0;
  for (i = 0; i < n; i++) bput(b, atext(deref(it[i]), &s));
  free(s.s);
}

static unsigned long c2d_text(const char *s)
{
  unsigned long v = 0;
  const char *p;
  for (p = s; *p; p++) v = v * 256UL + (unsigned long)(unsigned char)*p;
  return v;
}

static const char *translate_case(const char *s, int up, Buf *b)
{
  const char *p;
  const char *k;
  bclear(b);
  for (p = s; *p; p++) {
    k = strchr(up ? S_LOWER : S_UPPER, *p);
    if (*p != '\0' && k != NULL)
      bputc(b, (up ? S_UPPER : S_LOWER)[k - (up ? S_LOWER : S_UPPER)]);
    else bputc(b, *p);
  }
  return bstr(b);
}

/* ---- consulting ---- */
static void consult_string(const char *text)
{
  tokenize(text);
  while (g_ppos < g_ntok) {
    AMark hm = amark(&g_heap);
    Term *t = parse_clause();
    if (t == NULL) break;
    if (t->tag == T_CMP && t->u.c.fn == S_NECK && t->u.c.ar == 1) {
      long mark = trail_mark();
      int ok = solve_once(t->u.c.args[0]);
      if (!ok) {
        charout("prolog: directive failed: ");
        say(write_term(t, 1));
      }
      trail_undo(mark);
    } else {
      assert_clause(t, 1);
    }
    arelease(&g_heap, hm);
  }
}

static int consult_file(const char *fname)
{
  FILE *f = fopen(fname, "r");
  Buf b;
  char chunk[1024];
  size_t n;
  if (f == NULL) return 0;
  b.s = NULL; b.n = b.cap = 0;
  while ((n = fread(chunk, 1, sizeof chunk - 1, f)) > 0) {
    chunk[n] = '\0';
    bput(&b, chunk);
  }
  fclose(f);
  if (b.n > 0 && b.s[b.n - 1] != '\n') bputc(&b, '\n');
  consult_string(b.n ? b.s : "");
  free(b.s);
  return 1;
}

static int bi_consult(Term *a)
{
  Buf s;
  const char *name;
  int ok;
  s.s = NULL; s.n = s.cap = 0;
  name = atext(deref(a), &s);
  ok = consult_file(name);
  if (!ok) {
    charout("prolog: cannot open file: ");
    say(name);
  }
  free(s.s);
  return ok;
}

/* ---- functor/3, arg/3, =../2 ---- */
static int bi_functor(Term *goal)
{
  Term *t = deref(goal->u.c.args[0]), *nm, *fa;
  Buf s;
  int r, ar;
  if (t->tag != T_VAR) {
    Sym *fn;
    Term *ft;
    fn = functor_of(t, &ar);
    if (t->tag == T_NUM) ft = t;
    else if (t->tag != T_CMP) {
      s.s = NULL; s.n = s.cap = 0;
      ft = mk_qatom_s(atext(t, &s));
      free(s.s);
    } else ft = mk_qatom(fn);
    if (!unify(goal->u.c.args[1], ft)) return 0;
    return unify(goal->u.c.args[2], mk_long(ar));
  }
  nm = deref(goal->u.c.args[1]);
  fa = deref(goal->u.c.args[2]);
  if (fa->tag != T_NUM) return 0;
  if (num_val(fa->u.n.txt) == 0.0) return unify(t, nm);
  {
    long n = (long)num_val(fa->u.n.txt), i;
    Term *c;
    s.s = NULL; s.n = s.cap = 0;
    c = mk_cmp(intern(atext(nm, &s)), n > 0 ? (int)n : 0);
    free(s.s);
    for (i = 0; i < n; i++) {
      g_rvc++;
      c->u.c.args[i] = mk_var("FA", 0, g_rvc);
    }
    r = unify(t, c);
  }
  return r;
}

static int bi_arg(Term *goal)
{
  Term *n = deref(goal->u.c.args[0]), *t = deref(goal->u.c.args[1]);
  double d;
  long k;
  if (t->tag != T_CMP) return 0;
  if (n->tag != T_NUM) return 0;
  d = num_val(n->u.n.txt);
  if (d != (double)(long)d) return 0;
  k = (long)d;
  if (k < 1 || k > t->u.c.ar) return 0;
  return unify(goal->u.c.args[2], t->u.c.args[k - 1]);
}

static int bi_univ(Term *goal)
{
  Term *t = deref(goal->u.c.args[0]), *lst, **it;
  Buf s;
  int n, i;
  if (t->tag != T_VAR) {
    Term *r = S_NIL->pt;
    if (t->tag == T_CMP) {
      for (i = t->u.c.ar - 1; i >= 0; i--) r = mk_cons(t->u.c.args[i], r);
      r = mk_cons(mk_qatom(t->u.c.fn), r);
    } else if (t->tag == T_NUM) {
      r = mk_cons(t, r);
    } else {
      s.s = NULL; s.n = s.cap = 0;
      r = mk_cons(mk_qatom_s(atext(t, &s)), r);
      free(s.s);
    }
    return unify(goal->u.c.args[1], r);
  }
  lst = deref(goal->u.c.args[1]);
  n = list_items(lst, &it);
  if (n == 0) return 0;
  if (n == 1) return unify(t, it[0]);
  {
    Term *c;
    int ok;
    s.s = NULL; s.n = s.cap = 0;
    c = mk_cmp(intern(atext(deref(it[0]), &s)), n - 1);
    free(s.s);
    for (i = 1; i < n; i++) c->u.c.args[i - 1] = it[i];
    ok = unify(t, c);
    return ok;
  }
}

/* copy_term: fresh names in order of first occurrence */
typedef struct { Term **o, **n; int cnt, cap; } CMap;

static Term *copy_rec(Term *t, CMap *m)
{
  Term *first = NULL, **hole = &first, *r;
  for (;;) {
    t = deref(t);
    if (t->tag == T_VAR) {
      int i;
      Term *c;
      Buf nb;
      for (i = 0; i < m->cnt; i++)
        if (m->o[i] == t) { *hole = m->n[i]; return first; }
      nb.s = NULL; nb.n = nb.cap = 0;
      var_name(t, &nb);
      g_rvc++;
      c = mk_var(astrdup(&g_heap, bstr(&nb)), '#', g_rvc);
      free(nb.s);
      if (m->cnt >= m->cap) {
        m->cap = m->cap ? m->cap * 2 : 16;
        m->o = (Term **)xrealloc(m->o, sizeof(Term *) * (size_t)m->cap);
        m->n = (Term **)xrealloc(m->n, sizeof(Term *) * (size_t)m->cap);
      }
      m->o[m->cnt] = t;
      m->n[m->cnt] = c;
      m->cnt++;
      *hole = c;
      return first;
    }
    if (t->tag != T_CMP || t->u.c.ar == 0) { *hole = t; return first; }
    {
      int i, n = t->u.c.ar;
      r = mk_cmp(t->u.c.fn, n);
      for (i = 0; i < n - 1; i++) r->u.c.args[i] = copy_rec(t->u.c.args[i], m);
      *hole = r;
      hole = &r->u.c.args[n - 1];
      t = t->u.c.args[n - 1];
    }
  }
}

static int arith_cmp(Term *goal, int op)
{
  AV a, b;
  if (!arith_eval(goal->u.c.args[0], &a)) return 0;
  if (!arith_eval(goal->u.c.args[1], &b)) return 0;
  switch (op) {
  case BI_LT: return a.d < b.d;
  case BI_GT: return a.d > b.d;
  case BI_LE: return a.d <= b.d;
  case BI_GE: return a.d >= b.d;
  case BI_AEQ: return a.d == b.d;
  default: return a.d != b.d;
  }
}

static int int_valued(Term *v)
{
  return v->tag == T_NUM && num_val(v->u.n.txt) == num_val(trunc_text(v->u.n.txt));
}

static int bi_exec(int id, Term *goal)
{
  Term **A = goal->tag == T_CMP ? goal->u.c.args : NULL;
  Term *v, *t;
  Buf s;
  int r;
  switch (id) {
  case BI_UNIFY: return unify(A[0], A[1]);
  case BI_NOTUNIFY: {
    long mark = trail_mark();
    r = unify(A[0], A[1]);
    trail_undo(mark);
    return !r;
  }
  case BI_EQ: return term_compare(A[0], A[1]) == 0;
  case BI_NEQ: return term_compare(A[0], A[1]) != 0;
  case BI_LT_T: return term_compare(A[0], A[1]) < 0;
  case BI_GT_T: return term_compare(A[0], A[1]) > 0;
  case BI_LE_T: return term_compare(A[0], A[1]) <= 0;
  case BI_GE_T: return term_compare(A[0], A[1]) >= 0;
  case BI_COMPARE: {
    int c = term_compare(A[1], A[2]);
    return unify(A[0], mk_atom_s(c < 0 ? "<" : (c > 0 ? ">" : "=")));
  }
  case BI_IS: {
    AV a;
    if (!arith_eval(A[1], &a)) return 0;
    return unify(A[0], av_term(&a));
  }
  case BI_LT: case BI_GT: case BI_LE: case BI_GE: case BI_AEQ: case BI_ANE:
    return arith_cmp(goal, id);
  case BI_VAR: return deref(A[0])->tag == T_VAR;
  case BI_NONVAR: return deref(A[0])->tag != T_VAR;
  case BI_ATOM:
    v = deref(A[0]);
    return v->tag == T_ATOM || v->tag == T_QATOM;
  case BI_ATOMIC:
    v = deref(A[0]);
    return v->tag != T_VAR && v->tag != T_CMP;
  case BI_NUMBER: return deref(A[0])->tag == T_NUM;
  case BI_INTEGER:
    v = deref(A[0]);
    return int_valued(v);
  case BI_FLOAT:
    v = deref(A[0]);
    return v->tag == T_NUM && !int_valued(v);
  case BI_COMPOUND: return deref(A[0])->tag == T_CMP;
  case BI_CALLABLE:
    v = deref(A[0]);
    return v->tag == T_ATOM || v->tag == T_QATOM || v->tag == T_CMP;
  case BI_ISLIST: return is_proper_list(A[0]);
  case BI_GROUND: return is_ground(A[0]);
  case BI_FUNCTOR: return bi_functor(goal);
  case BI_ARG: return bi_arg(goal);
  case BI_UNIV: return bi_univ(goal);
  case BI_COPY: {
    CMap m;
    Term *c;
    m.o = m.n = NULL;
    m.cnt = m.cap = 0;
    c = copy_rec(A[0], &m);
    free(m.o);
    free(m.n);
    return unify(A[1], c);
  }
  case BI_ATOM_CODES:
  case BI_ATOM_CHARS: {
    v = deref(A[0]);
    s.s = NULL; s.n = s.cap = 0;
    if (v->tag != T_VAR) {
      const char *txt = atext(v, &s);
      r = unify(A[1], id == BI_ATOM_CODES ? codes_list(txt) : chars_list(txt));
    } else {
      Buf o;
      o.s = NULL; o.n = o.cap = 0;
      if (id == BI_ATOM_CODES) codes_to_text(deref(A[1]), &o);
      else chars_to_text(deref(A[1]), &o);
      r = unify(v, mk_qatom_s(bstr(&o)));
      free(o.s);
    }
    free(s.s);
    return r;
  }
  case BI_NUM_CODES:
  case BI_NUM_CHARS: {
    v = deref(A[0]);
    s.s = NULL; s.n = s.cap = 0;
    if (v->tag != T_VAR) {
      const char *txt = atext(v, &s);
      r = unify(A[1], id == BI_NUM_CODES ? codes_list(txt) : chars_list(txt));
    } else {
      Buf o;
      o.s = NULL; o.n = o.cap = 0;
      if (id == BI_NUM_CODES) codes_to_text(deref(A[1]), &o);
      else chars_to_text(deref(A[1]), &o);
      if (!is_number(bstr(&o))) {
        charout(id == BI_NUM_CODES ? "prolog: number_codes: not a number: "
                                   : "prolog: number_chars: not a number: ");
        say(bstr(&o));
        r = 0;
      } else {
        r = unify(v, mk_num(bstr(&o)));
      }
      free(o.s);
    }
    free(s.s);
    return r;
  }
  case BI_ATOM_LENGTH: {
    v = deref(A[0]);
    s.s = NULL; s.n = s.cap = 0;
    r = unify(A[1], mk_long((long)strlen(atext(v, &s))));
    free(s.s);
    return r;
  }
  case BI_CHAR_CODE: {
    v = deref(A[0]);
    s.s = NULL; s.n = s.cap = 0;
    if (v->tag != T_VAR) {
      r = unify(A[1], mk_long((long)c2d_text(atext(v, &s))));
    } else {
      Buf o;
      t = deref(A[1]);
      o.s = NULL; o.n = o.cap = 0;
      if (t->tag != T_NUM) { free(s.s); return 0; }
      d2c_text((unsigned long)num_val(t->u.n.txt), &o);
      r = unify(v, mk_qatom_s(bstr(&o)));
      free(o.s);
    }
    free(s.s);
    return r;
  }
  case BI_ATOM_CONCAT: {
    Term *a = deref(A[0]), *b = deref(A[1]);
    Buf o, s2;
    if (a->tag != T_VAR && b->tag != T_VAR) {
      s.s = NULL; s.n = s.cap = 0;
      s2.s = NULL; s2.n = s2.cap = 0;
      o.s = NULL; o.n = o.cap = 0;
      bput(&o, atext(a, &s));
      bput(&o, atext(b, &s2));
      r = unify(A[2], mk_qatom_s(bstr(&o)));
      free(s.s); free(s2.s); free(o.s);
      return r;
    }
    say("prolog: atom_concat/3: first two arguments must both be bound (split "
        "mode is not supported)");
    return 0;
  }
  case BI_UPCASE:
  case BI_DOWNCASE: {
    Buf o;
    v = deref(A[0]);
    s.s = NULL; s.n = s.cap = 0;
    o.s = NULL; o.n = o.cap = 0;
    r = unify(A[1], mk_qatom_s(translate_case(atext(v, &s), id == BI_UPCASE, &o)));
    free(s.s); free(o.s);
    return r;
  }
  case BI_ASSERT:
  case BI_ASSERTZ:
    assert_clause(deref_full(A[0]), 1);
    return 1;
  case BI_ASSERTA:
    assert_clause(deref_full(A[0]), 0);
    return 1;
  case BI_RETRACT: {
    Term *hp, *bp;
    t = deref_full(A[0]);
    hp = t;
    bp = mk_atom(S_TRUE);
    if (t->tag == T_CMP && t->u.c.fn == S_NECK && t->u.c.ar == 2) {
      hp = t->u.c.args[0];
      bp = t->u.c.args[1];
    }
    return retract_clause(hp, bp);
  }
  case BI_RETRACTALL:
    retract_all(deref_full(A[0]));
    return 1;
  case BI_WRITE:
    charout(write_term(deref_full(A[0]), 0));
    return 1;
  case BI_PRINT:
    charout(write_term(deref_full(A[0]), 1));
    return 1;
  case BI_WRITELN:
    say(write_term(deref_full(A[0]), 0));
    return 1;
  case BI_NL:
    say("");
    return 1;
  case BI_TAB: {
    AV a;
    long n;
    if (!arith_eval(A[0], &a)) return 0;
    for (n = (long)a.d; n > 0; n--) fputc(' ', stdout);
    return 1;
  }
  case BI_CONSULT:
  case BI_LOAD:
    return bi_consult(A[0]);
  case BI_LISTING0: listing_all(1); return 1;
  case BI_LISTINGALL: listing_all(0); return 1;
  case BI_LISTING1: {
    s.s = NULL; s.n = s.cap = 0;
    listing_one(atext(deref(A[0]), &s));
    free(s.s);
    return 1;
  }
  case BI_SAVE:
  case BI_SAVEALL: {
    s.s = NULL; s.n = s.cap = 0;
    save_workspace(atext(deref(A[0]), &s), id == BI_SAVE);
    free(s.s);
    return 1;
  }
  case BI_CXCALL2:
  case BI_CXCALL3: {
    Buf nm, o;
    const char *arg = "";
    const char *res = NULL;
    AV a;
    nm.s = NULL; nm.n = nm.cap = 0;
    o.s = NULL; o.n = o.cap = 0;
    bput(&nm, atext(deref(A[0]), &o));
    bclear(&o);
    if (id == BI_CXCALL3) arg = write_term(deref_full(A[1]), 0);
    if (strcmp(bstr(&nm), "hello") == 0) res = "hello from CREXX";
    else if (strcmp(bstr(&nm), "double") == 0 && is_number(arg)) {
      av_make(&a, num_val(arg) + num_val(arg));
      res = av_text(&a);
    }
    if (res == NULL) {
      charout("prolog: crexx_call: no CREXX callback registered for ");
      charout(bstr(&nm));
      say(" -- see crexxcallback.crexx in this package");
      r = 0;
    } else {
      r = unify(A[id == BI_CXCALL3 ? 2 : 1], is_number(res) ? mk_num(res) : mk_qatom_s(res));
    }
    free(nm.s);
    free(o.s);
    return r;
  }
  case BI_HALT0:
    say("prolog: halt");
    fflush(stdout);
    exit(0);
  case BI_HALT1: {
    AV a;
    if (!arith_eval(A[0], &a)) return 0;
    fflush(stdout);
    exit((int)a.d);
  }
  default: break;
  }
  say("prolog: internal error -- unimplemented builtin");
  return 0;
}
/* ===== p7_main.c ===== */
/* p7_main.c - start-up, session commands, top level, main */

static const char *const g_boot[] = {
  "append([], L, L).",
  "append([H|T], L, [H|R]) :- append(T, L, R).",
  "member(X, [X|_]).",
  "member(X, [_|T]) :- member(X, T).",
  "length([], 0).",
  "length([_|T], N) :- length(T, N0), N is N0 + 1.",
  "reverse(L, R) :- reverse_(L, [], R).",
  "reverse_([], A, A).",
  "reverse_([H|T], A, R) :- reverse_(T, [H|A], R).",
  "last([X], X) :- !.",
  "last([_|T], X) :- last(T, X).",
  "nth0(0, [X|_], X) :- !.",
  "nth0(N, [_|T], X) :- integer(N), N > 0, N1 is N - 1, nth0(N1, T, X).",
  "nth1(1, [X|_], X) :- !.",
  "nth1(N, [_|T], X) :- integer(N), N > 1, N1 is N - 1, nth1(N1, T, X).",
  "sum_list([], 0).",
  "sum_list([H|T], S) :- sum_list(T, S0), S is S0 + H.",
  "max_list([X], X) :- !.",
  "max_list([H|T], M) :- max_list(T, M0), (H >= M0 -> M = H ; M = M0).",
  "min_list([X], X) :- !.",
  "min_list([H|T], M) :- min_list(T, M0), (H =< M0 -> M = H ; M = M0).",
  "between(L, H, L) :- L =< H.",
  "between(L, H, X) :- L < H, L1 is L + 1, between(L1, H, X).",
  "forall(Cond, Action) :- \\+ (Cond, \\+ Action).",
  "select(X, [X|T], T).",
  "select(X, [H|T], [H|R]) :- select(X, T, R).",
  "selectchk(X, L, R) :- select(X, L, R), !.",
  "permutation([], []).",
  "permutation(L, [H|T]) :- select(H, L, R), permutation(R, T).",
  "delete([], _, []).",
  "delete([X|T], X, R) :- !, delete(T, X, R).",
  "delete([H|T], X, [H|R]) :- delete(T, X, R).",
  "exclude(_, [], []).",
  "exclude(P, [H|T], R) :- (call(P, H) -> exclude(P, T, R) ; R = [H|R1], exclude(P, T, R1)).",
  "include(_, [], []).",
  "include(P, [H|T], R) :- (call(P, H) -> R = [H|R1], include(P, T, R1) ; include(P, T, R)).",
  "maplist(_, []).",
  "maplist(P, [H|T]) :- call(P, H), maplist(P, T).",
  "maplist(_, [], []).",
  "maplist(P, [H|T], [H2|T2]) :- call(P, H, H2), maplist(P, T, T2).",
  "maplist(_, [], [], []).",
  "maplist(P, [H|T], [H2|T2], [H3|T3]) :- call(P, H, H2, H3), maplist(P, T, T2, T3).",
  "msort_insert(X, [], [X]).",
  "msort_insert(X, [H|T], [X,H|T]) :- X @=< H, !.",
  "msort_insert(X, [H|T], [H|R]) :- msort_insert(X, T, R).",
  "msort([], []).",
  "msort([H|T], S) :- msort(T, S0), msort_insert(H, S0, S).",
  "sort(L, S) :- msort(L, S0), dedupe_(S0, S).",
  "dedupe_([], []).",
  "dedupe_([X], [X]) :- !.",
  "dedupe_([X,Y|T], R) :- X == Y, !, dedupe_([Y|T], R).",
  "dedupe_([X,Y|T], [X|R]) :- dedupe_([Y|T], R).",
  "concat_atom([], '').",
  "concat_atom([A], A) :- !.",
  "concat_atom([A|T], R) :- concat_atom(T, R0), atom_concat(A, R0, R).",
  NULL
};

static void init_engine(void)
{
  Pred *p;
  int i;
  Buf b;
  /* a fresh session: tokens, variables, trail, clause database */
  tok_clear();
  vmap_clear(&g_qvarmap);
  vmap_clear(&g_pvarmap);
  g_lastterm = '.';
  g_pvc = 0;
  g_tn = 0;
  g_sp = 0;
  g_mode = M_FIRST;
  g_cutsig = 0;
  g_callcount = 0;
  g_fa = NULL;
  g_fatmpl = NULL;
  if (g_heap.n > 0) {
    AMark z;
    z.blk = 0;
    z.off = 0;
    arelease(&g_heap, z);
  }
  for (i = 0; i < NSYMH; i++) {
    Sym *y;
    for (y = g_symtab[i]; y; y = y->next)
      for (p = y->preds; p; p = p->next) {
        p->n = 0;
        p->listed = 0;
        p->lnext = NULL;
      }
  }
  g_plhead = g_pltail = NULL;
  g_clcount = 0;
  b.s = NULL; b.n = b.cap = 0;
  for (i = 0; g_boot[i]; i++) {
    if (i > 0) bputc(&b, '\n');
    bput(&b, g_boot[i]);
  }
  consult_string(bstr(&b));
  free(b.s);
  g_bootmax = g_clcount;
}

/* ---- standard input, one line at a time ---- */
static int stdin_empty(void)
{
  int c = getc(stdin);
  if (c == EOF) return 1;
  ungetc(c, stdin);
  return 0;
}

static char *read_line(void)
{
  Buf b;
  int c;
  b.s = NULL; b.n = b.cap = 0;
  bputc(&b, 'x');
  b.n = 0;
  b.s[0] = '\0';
  while ((c = getc(stdin)) != EOF && c != '\n') bputc(&b, c);
  return b.s;
}

/* STRIP of Rexx: blanks only */
static char *strip_blanks(char *s)
{
  size_t n;
  while (*s == ' ') s++;
  n = strlen(s);
  while (n > 0 && s[n - 1] == ' ') s[--n] = '\0';
  return s;
}

static void sys_help(void)
{
  static const char *const h[] = {
    "session commands:",
    "  )listing      list what YOU added this session (not the",
    "                built-in library -- see listing_all below)",
    "  )lib [dir]    list .pl files available to consult (current",
    "                directory, or [dir] if given)",
    "  )clear        wipe the database back to a fresh session",
    "                (the built-in library is reloaded, your own",
    "                facts/rules/assertions are gone)",
    "  )help  or )?  show this message",
    "  )quit )exit   end the session",
    "",
    "the terminator says what you mean:",
    "  end a QUERY with ?   -- asks whether it is true / for a value:",
    "    female(ann)?",
    "    parent(tom, X)?",
    "  end a FACT or RULE with .  -- adds it to the database:",
    "    female(ann).",
    "    grandparent(X,Z) :- parent(X,Y), parent(Y,Z).",
    "  ?- goal.  and  :- goal.  (SWI/file-directive style, ending in .) "
    "also work and are treated as queries, for anything pasted in from "
    "elsewhere.",
    "",
    "exporting/reloading what you built this session:",
    "  save(File)?          save what YOU added (not the built-in library) "
    "as loadable Prolog source",
    "  save_all(File)?      save EVERYTHING, built-in library included -- "
    "a fully self-contained file",
    "  consult(File)?       load clauses/directives from a file (also: load(File)?)",
    "",
    "other goals, typed as a query:",
    "  listing?             list what you added this session",
    "  listing_all?         list the whole database, library included",
    "  listing(Name)?       list clauses for Name (any arity), library or not",
    "  halt?                end the session (or just: halt.)",
    NULL
  };
  int i;
  for (i = 0; h[i]; i++) say(h[i]);
}

static void sys_lib(const char *dir)
{
#ifdef PL_POSIX
  DIR *d;
  struct dirent *e;
  int found = 0;
  const char *lookin = *dir ? dir : ".";
  d = opendir(lookin);
  if (d == NULL) {
    charout("prolog: )lib -- cannot read directory: ");
    say(lookin);
    return;
  }
  while ((e = readdir(d)) != NULL) {
    size_t n = strlen(e->d_name);
    if (n > 3 && strcmp(e->d_name + n - 3, ".pl") == 0) {
      charout("   ");
      say(e->d_name);
      found++;
    }
  }
  closedir(d);
  if (!found) {
    charout("prolog: )lib -- no .pl files in ");
    say(lookin);
  }
#else
  charout("prolog: )lib -- cannot read directory: ");
  say(*dir ? dir : ".");
#endif
}

/* returns 1 for )quit */
static int handle_syscmd(char *ln)
{
  char *cmd = ln, *rest;
  size_t i;
  char up[32];
  while (*ln && *ln != ' ') ln++;
  if (*ln) {
    *ln++ = '\0';
    while (*ln == ' ') ln++;
  }
  rest = ln;
  for (i = 0; cmd[i] && i < sizeof up - 1; i++)
    up[i] = (char)(strchr(S_LOWER, cmd[i]) ? S_UPPER[strchr(S_LOWER, cmd[i]) - S_LOWER] : cmd[i]);
  up[i] = '\0';
  if (strcmp(up, ")LISTING") == 0) listing_all(1);
  else if (strcmp(up, ")CLEAR") == 0) {
    init_engine();
    say("prolog: workspace cleared.");
  } else if (strcmp(up, ")LIB") == 0) sys_lib(strip_blanks(rest));
  else if (strcmp(up, ")HELP") == 0 || strcmp(up, ")?") == 0) sys_help();
  else if (strcmp(up, ")QUIT") == 0 || strcmp(up, ")EXIT") == 0) return 1;
  else {
    charout("prolog: unknown session command: ");
    charout(cmd);
    say(" (try )help)");
  }
  return 0;
}

/* ---- top level ---- */
static int interact_on_solution(void)
{
  Buf line;
  int i;
  char *ans, *a;
  if (g_qvarmap.n == 0) {
    say("true.");
  } else {
    line.s = NULL; line.n = line.cap = 0;
    for (i = 0; i < g_qvarmap.n; i++) {
      if (i > 0) bput(&line, ", ");
      bput(&line, g_qvarmap.e[i].name);
      bput(&line, " = ");
      bput(&line, write_term(deref_full(g_qvarmap.e[i].var), 1));
    }
    say(bstr(&line));
    free(line.s);
  }
  charout("     more (;)? ");
  fflush(stdout);
  if (stdin_empty()) return 1;
  ans = read_line();
  a = strip_blanks(ans);
  i = strcmp(a, ";") == 0;
  free(ans);
  return i ? 0 : 1;
}

static void solve_interact_show(Term *g)
{
  int ok;
  long mark;
  vmap_copy(&g_qvarmap, &g_pvarmap);
  g_mode = M_INTERACT;
  mark = trail_mark();
  ok = run(new_goal(g, NULL));
  g_mode = M_FIRST;
  if (!ok) say("false.");
  trail_undo(mark);
}

static int query_prefixed(Term *t)
{
  return t->tag == T_CMP && t->u.c.ar == 1 &&
         (t->u.c.fn == S_QUERY || t->u.c.fn == S_NECK);
}

static int reserved_name(Term *t)
{
  int ar;
  Sym *fn = functor_of(t, &ar);
  if (t->tag != T_CMP) {
    if (fn == S_TRUE || fn == S_FAIL || fn == S_FALSE || fn == S_BANG) return 1;
    return is_builtin(fn, 0);
  }
  return is_builtin(fn, ar);
}

static void run_top_level(void)
{
  char *raw, *ln;
  say("Prolog-in-CREXX -- type )help for session commands, halt. to quit");
  say("(end a QUERY with ? -- e.g. female(ann)?  or parent(tom,X)? -- end a "
      "FACT or RULE with . to add it -- e.g. female(ann). or foo(X):-bar(X).)");
  for (;;) {
    AMark hm;
    say("?-");
    if (stdin_empty()) {
      say("");
      say("goodbye.");
      return;
    }
    raw = read_line();
    ln = strip_blanks(raw);
    if (*ln == '\0') { free(raw); continue; }
    if (*ln == ')') {
      int q = handle_syscmd(ln);
      free(raw);
      if (q) {
        say("goodbye.");
        return;
      }
      continue;
    }
    hm = amark(&g_heap);
    tokenize(ln);
    while (g_ppos < g_ntok) {
      Term *goal = parse_clause();
      if (goal == NULL) break;
      if (g_lastterm == '?') {
        solve_interact_show(query_prefixed(goal) ? goal->u.c.args[0] : goal);
      } else if (query_prefixed(goal)) {
        solve_interact_show(goal->u.c.args[0]);
      } else if (reserved_name(goal)) {
        solve_interact_show(goal);
      } else {
        assert_clause(goal, 1);
        charout("%");
        charout("  added: ");
        say(write_term(goal, 1));
      }
    }
    free(raw);
    arelease(&g_heap, hm);
  }
}

int main(int argc, char **argv)
{
  init_syms();
  init_ops();
  init_builtins();
  init_engine();
  if (argc > 1 && argv[1][0] != '\0') {
    FILE *f = fopen(argv[1], "r");
    if (f == NULL) {
      charout("prolog: cannot open source file: ");
      say(argv[1]);
      return 1;
    }
    fclose(f);
    consult_file(argv[1]);
  }
  run_top_level();
  fflush(stdout);
  return 0;
}
