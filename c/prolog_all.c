/* Prolog interpreter in portable C89 - the parts are inlined by tools/amalg.sh */
/* ===== p0_base.c ===== */
/* p0_base.c - types, memory (arenas), symbols, text buffers, output */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>
#ifdef PL_POSIX
#include <dirent.h>
#endif

#define BLKSZ 262144L
#define NSYMH 4096

typedef struct Term Term;
typedef struct Sym Sym;
typedef struct Pred Pred;
typedef struct Clause Clause;
typedef struct PIdx PIdx;

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
  char *mem;               /* one block: this struct, the templates, body and names */
  size_t msz;
  int zmark;               /* retracted clause still in use (garbage collector) */
};

struct Pred {
  Sym *fn;
  int ar;
  Pred *next;              /* chain on the symbol */
  Pred *lnext;             /* order of first assertion */
  Clause **v;
  int n, cap;
  int listed;
  PIdx *idx;               /* first-argument index, built on demand (p4_db.c) */
};

/* text buffer */
typedef struct { char *s; size_t n, cap; } Buf;

/* arena with mark / release */
typedef struct { char *p; size_t cap, used; long base; } Blk;
typedef struct { Blk *b; int n, ncap, cur; int hdr; } Arena;
typedef struct { int blk; size_t off; } AMark;

/* Every object of the heap (hdr = 1) starts with a header word: its size in bytes
   (header included, a multiple of 8), the kind (bits 0-1) and the collector's mark
   (bit 2).  The garbage collector (p5_gc.c) walks the heap with it. */
#define HDRSZ 8
enum { HK_RAW, HK_TERM, HK_GOAL };
#define HK_KIND 3UL
#define HK_MARK 4UL

static Arena g_heap = { NULL, 0, 0, 0, 1 }, g_perm;
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
static void *aalloc_k(Arena *a, size_t n, int kind)
{
  Blk *b;
  char *r;
  if (a->hdr && n < sizeof(void *)) n = sizeof(void *);   /* room for a forwarding pointer */
  n = (n + 7) & ~(size_t)7;
  if (a->hdr) n += HDRSZ;
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
  if (a->hdr) {
    *(unsigned long *)r = (unsigned long)n | (unsigned long)kind;
    r += HDRSZ;
  }
  return r;
}

static void *aalloc(Arena *a, size_t n) { return aalloc_k(a, n, HK_RAW); }

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
           *S_PIPE, *S_EMPTY, *S_MSORT, *S_SORT;

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
  Term *t = (Term *)aalloc_k(&g_heap, sizeof(Term), HK_TERM);
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
  Term *t = (Term *)aalloc_k(a, sizeof(Term), HK_TERM);
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
  Term *t = (Term *)aalloc_k(a, sizeof(Term) + sizeof(Term *) * (size_t)(ar ? ar : 1), HK_TERM);
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
  S_MSORT = intern("msort");
  S_SORT = intern("sort");
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

/* ---- first-argument index ----------------------------------------------
   For a predicate with IDX_MIN or more clauses, a call whose first argument is
   an atom, a number or a compound is resolved through a hash table from the
   key (atom symbol / numeric value / functor and arity) to the clauses that can
   match it: those with the same key and those with a variable first argument,
   in clause order.  A bucket is an array of clauses plus their positions in the
   predicate; a running call keeps the pointer and the count it started with, so
   buckets (like the clause arrays) only grow at the end and are never freed.
   pre[i] is the sum of nhead over clauses 0..i-1: clauses skipped by the index
   still advance the variable renaming counter g_rvc exactly as trying them
   would have done (see try_clause in p5_solve.c), so answers and variable
   names are unchanged.
   The index describes one snapshot (v, n) of the predicate; assertz keeps it
   up to date, asserta / retract / reload drop it and it is rebuilt on demand. */
#ifndef IDX_MIN
#define IDX_MIN 8
#endif
#define IDX_MAXWILD 64

typedef struct { Clause **c; int *pos; int n, cap; } IBkt;
typedef struct { int kind; const void *p; double d; int ar; IBkt b; } IEnt;  /* kind 0 = free */
struct PIdx {
  Clause **v;
  int n;
  long *pre;
  int precap;
  IEnt *tab;
  int tcap, tcnt;
  IBkt wild;
  int bad;                 /* too many variable-first-argument clauses: do not use */
};

/* ---- storage no longer reachable from the database --------------------------
   Retracted clauses, clause arrays replaced by assert / retract and the arrays of
   dropped first-argument indexes can still be in use: a running call holds the
   array and walks its clauses, and terms made from a clause share its ground
   sub-terms, numbers and variable names.  They wait on these lists and the garbage
   collector (p5_gc.c) frees them once nothing refers to them any more. */
typedef struct { void *p; size_t sz; int mark; } ZArr;
static Clause **g_zcl;
static long g_zcln, g_zclcap;
static ZArr *g_zar;
static long g_zarn, g_zarcap;
static unsigned long g_zbytes;      /* bytes waiting on the two lists */

static void zombie_arr(void *p, size_t sz)
{
  if (p == NULL) return;
  if (g_zarn >= g_zarcap) {
    g_zarcap = g_zarcap ? g_zarcap * 2 : 64;
    g_zar = (ZArr *)xrealloc(g_zar, sizeof(ZArr) * (size_t)g_zarcap);
  }
  g_zar[g_zarn].p = p;
  g_zar[g_zarn].sz = sz;
  g_zar[g_zarn].mark = 0;
  g_zarn++;
  g_zbytes += (unsigned long)sz;
}

static void zombie_clause(Clause *c)
{
  if (g_zcln >= g_zclcap) {
    g_zclcap = g_zclcap ? g_zclcap * 2 : 64;
    g_zcl = (Clause **)xrealloc(g_zcl, sizeof(Clause *) * (size_t)g_zclcap);
  }
  c->zmark = 0;
  g_zcl[g_zcln++] = c;
  g_zbytes += (unsigned long)c->msz;
}

static void zombie_idx(PIdx *x)
{
  int i;
  if (x == NULL) return;
  zombie_arr(x->pre, sizeof(long) * (size_t)x->precap);
  zombie_arr(x->wild.c, sizeof(Clause *) * (size_t)x->wild.cap);
  zombie_arr(x->wild.pos, sizeof(int) * (size_t)x->wild.cap);
  for (i = 0; i < x->tcap; i++) {
    if (!x->tab[i].kind) continue;
    zombie_arr(x->tab[i].b.c, sizeof(Clause *) * (size_t)x->tab[i].b.cap);
    zombie_arr(x->tab[i].b.pos, sizeof(int) * (size_t)x->tab[i].b.cap);
  }
  free(x->tab);            /* running calls hold bucket arrays, never the table */
  free(x);
}

/* key of a (dereferenced, non-variable) first argument; 0 = no key */
static int idx_key(Term *t, const void **p, double *d, int *ar)
{
  *p = NULL;
  *d = 0.0;
  *ar = 0;
  switch (t->tag) {
  case T_ATOM: case T_QATOM: *p = t->u.a.sym; return 1;
  case T_NUM:
    *d = num_val(t->u.n.txt);
    if (*d == 0.0) *d = 0.0;
    return 2;
  case T_CMP: *p = t->u.c.fn; *ar = t->u.c.ar; return 3;
  default: return 0;
  }
}

static unsigned long idx_hash(int kind, const void *p, double d, int ar)
{
  unsigned long h;
  int e;
  double m;
  if (kind == 2) {
    m = frexp(d, &e);
    h = (unsigned long)(long)(m * 2147483648.0) * 31UL + (unsigned long)(long)(e + 2000);
  } else {
    h = (unsigned long)p;
    h = (h >> 3) * 2654435761UL + (unsigned long)ar * 40503UL;
  }
  return (h ^ (h >> 15)) * 2246822519UL + (unsigned long)kind;
}

static int idx_first_key(Clause *c, const void **p, double *d, int *ar)
{
  Term *a;
  if (c->head->tag != T_CMP || c->head->u.c.ar < 1) return 0;
  a = c->head->u.c.args[0];
  return idx_key(a, p, d, ar);   /* T_TV (variable) gives 0 */
}

static void ibkt_add(IBkt *b, Clause *c, int pos)
{
  if (b->n >= b->cap) {
    int ncap = b->cap ? b->cap * 2 : 2;
    Clause **nc = (Clause **)xmalloc(sizeof(Clause *) * (size_t)ncap);
    int *np = (int *)xmalloc(sizeof(int) * (size_t)ncap);
    if (b->n) {
      memcpy(nc, b->c, sizeof(Clause *) * (size_t)b->n);
      memcpy(np, b->pos, sizeof(int) * (size_t)b->n);
    }
    zombie_arr(b->c, sizeof(Clause *) * (size_t)b->cap);
    zombie_arr(b->pos, sizeof(int) * (size_t)b->cap);
    b->c = nc;
    b->pos = np;
    b->cap = ncap;
  }
  b->c[b->n] = c;
  b->pos[b->n] = pos;
  b->n++;
}

static IEnt *idx_find(PIdx *x, int kind, const void *p, double d, int ar, int create)
{
  unsigned long h;
  int i;
  if (x->tcap == 0 || (create && (x->tcnt + 1) * 2 > x->tcap)) {
    int ncap = x->tcap ? x->tcap * 2 : 16, j;
    IEnt *nt = (IEnt *)xmalloc(sizeof(IEnt) * (size_t)ncap);
    memset(nt, 0, sizeof(IEnt) * (size_t)ncap);
    for (j = 0; j < x->tcap; j++) {
      if (x->tab[j].kind) {
        h = idx_hash(x->tab[j].kind, x->tab[j].p, x->tab[j].d, x->tab[j].ar);
        i = (int)(h & (unsigned long)(ncap - 1));
        while (nt[i].kind) i = (i + 1) & (ncap - 1);
        nt[i] = x->tab[j];
      }
    }
    free(x->tab);   /* entries are copied by value; running calls hold bucket arrays only */
    x->tab = nt;
    x->tcap = ncap;
  }
  h = idx_hash(kind, p, d, ar);
  i = (int)(h & (unsigned long)(x->tcap - 1));
  while (x->tab[i].kind) {
    IEnt *e = &x->tab[i];
    if (e->kind == kind && e->p == p && e->ar == ar && (kind != 2 || e->d == d)) return e;
    i = (i + 1) & (x->tcap - 1);
  }
  if (!create) return NULL;
  x->tab[i].kind = kind;
  x->tab[i].p = p;
  x->tab[i].d = d;
  x->tab[i].ar = ar;
  x->tab[i].b.c = NULL;
  x->tab[i].b.pos = NULL;
  x->tab[i].b.n = x->tab[i].b.cap = 0;
  x->tcnt++;
  return &x->tab[i];
}

/* a new bucket starts as a copy of the variable-first-argument clauses before it */
static void idx_new_bucket(PIdx *x, IEnt *e)
{
  int i;
  for (i = 0; i < x->wild.n; i++) ibkt_add(&e->b, x->wild.c[i], x->wild.pos[i]);
}

/* add clause c at position pos; returns 0 when the index must be dropped */
static int idx_append(PIdx *x, Clause *c, int pos)
{
  const void *p;
  double d;
  int ar, kind, i;
  if (pos + 2 > x->precap) {
    int ncap = x->precap ? x->precap * 2 : 16;
    long *np = (long *)xmalloc(sizeof(long) * (size_t)ncap);
    if (x->precap) memcpy(np, x->pre, sizeof(long) * (size_t)x->precap);
    zombie_arr(x->pre, sizeof(long) * (size_t)x->precap);
    x->pre = np;
    x->precap = ncap;
  }
  if (pos == 0) x->pre[0] = 0;
  x->pre[pos + 1] = x->pre[pos] + c->nhead;
  kind = idx_first_key(c, &p, &d, &ar);
  if (kind == 0) {
    if (x->wild.n >= IDX_MAXWILD || (long)x->tcnt * (x->wild.n + 1) > 4096L) return 0;
    ibkt_add(&x->wild, c, pos);
    for (i = 0; i < x->tcap; i++)
      if (x->tab[i].kind) ibkt_add(&x->tab[i].b, c, pos);
  } else {
    IEnt *e = idx_find(x, kind, p, d, ar, 0);
    if (e == NULL) {
      e = idx_find(x, kind, p, d, ar, 1);
      idx_new_bucket(x, e);
    }
    ibkt_add(&e->b, c, pos);
  }
  x->n = pos + 1;
  return 1;
}

static PIdx *idx_build(Pred *p)
{
  PIdx *x = (PIdx *)xmalloc(sizeof(PIdx));
  int i;
  memset(x, 0, sizeof(PIdx));
  x->v = p->v;
  for (i = 0; i < p->n; i++) {
    if (!idx_append(x, p->v[i], i)) {
      x->bad = 1;
      break;
    }
  }
  x->n = p->n;
  return x;
}

/* the candidate clauses for a call of p with first argument a (dereferenced);
   NULL when the predicate is too small or the argument is a variable */
static PIdx *idx_for_call(Pred *p, Term *a, IBkt **bk)
{
  const void *kp;
  double d;
  int ar, kind;
  PIdx *x;
  IEnt *e;
  static IBkt none;
  if (p->n < IDX_MIN) return NULL;
  kind = idx_key(a, &kp, &d, &ar);
  if (kind == 0) return NULL;
  x = p->idx;
  if (x == NULL || x->v != p->v || x->n != p->n) {
    zombie_idx(x);
    x = idx_build(p);
    p->idx = x;
  }
  if (x->bad) return NULL;
  e = idx_find(x, kind, kp, d, ar, 0);
  if (e != NULL) *bk = &e->b;
  else if (x->wild.n > 0) *bk = &x->wild;
  else *bk = &none;
  return x;
}


/* a snapshot of the clause list stays valid while clauses are added */
static void pred_add(Pred *p, Clause *c, int atz)
{
  int keep = atz && p->idx != NULL && !p->idx->bad && p->idx->v == p->v && p->idx->n == p->n;
  pred_note_listed(p);
  if (!keep) {
    zombie_idx(p->idx);
    p->idx = NULL;
  }
  if (atz) {
    if (p->n < p->cap) {
      p->v[p->n++] = c;
    } else {
      Clause **nv;
      int ncap = p->cap ? p->cap * 2 : 4;
      nv = (Clause **)xmalloc(sizeof(Clause *) * (size_t)ncap);
      if (p->n) memcpy(nv, p->v, sizeof(Clause *) * (size_t)p->n);
      nv[p->n++] = c;
      zombie_arr(p->v, sizeof(Clause *) * (size_t)p->cap);
      p->v = nv;
      p->cap = ncap;
    }
  } else {
    Clause **nv = (Clause **)xmalloc(sizeof(Clause *) * (size_t)(p->n + 4));
    if (p->n) memcpy(nv + 1, p->v, sizeof(Clause *) * (size_t)p->n);
    nv[0] = c;
    zombie_arr(p->v, sizeof(Clause *) * (size_t)p->cap);
    p->v = nv;
    p->n++;
    p->cap = p->n + 3;
  }
  if (keep) {
    p->idx->v = p->v;
    if (!idx_append(p->idx, c, p->n - 1)) {
      zombie_idx(p->idx);
      p->idx = NULL;
    }
  }
}

static void pred_set(Pred *p, Clause **keep, int n)
{
  Clause **nv = (Clause **)xmalloc(sizeof(Clause *) * (size_t)(n + 4));
  if (n) memcpy(nv, keep, sizeof(Clause *) * (size_t)n);
  zombie_arr(p->v, sizeof(Clause *) * (size_t)p->cap);
  zombie_idx(p->idx);
  p->v = nv;
  p->n = n;
  p->cap = n + 4;
  p->idx = NULL;
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

#define RND8(n) (((size_t)(n) + 7) & ~(size_t)7)

/* the bytes tmpl_build will take (aalloc rounds every piece to 8) */
static size_t tmpl_size(Term *t, VList *vl)
{
  size_t sz = 0;
  int i, n;
  for (;;) {
    t = deref(t);
    switch (t->tag) {
    case T_VAR:
      vl_index(vl, t);
      return sz + RND8(sizeof(Term));
    case T_NUM: return sz + RND8(sizeof(Term)) + RND8(strlen(t->u.n.txt) + 1);
    case T_CMP:
      n = t->u.c.ar;
      sz += RND8(sizeof(Term) + sizeof(Term *) * (size_t)(n ? n : 1));
      if (n == 0) return sz;
      for (i = 0; i < n - 1; i++) sz += tmpl_size(t->u.c.args[i], vl);
      t = t->u.c.args[n - 1];
      break;
    default: return sz;
    }
  }
}

static Term *tmpl_build(Arena *ca, Term *t, VList *vl)
{
  t = deref(t);
  switch (t->tag) {
  case T_VAR: {
    Term *x = (Term *)aalloc(ca, sizeof(Term));
    x->tag = T_TV;
    x->gnd = 0;
    x->u.x.idx = vl_index(vl, t);
    return x;
  }
  case T_NUM: return mk_numa(ca, t->u.n.txt);
  case T_CMP: {
    int i, n = t->u.c.ar, g = 1;
    Term *r = mk_cmpa(ca, t->u.c.fn, n);
    for (i = 0; i < n; i++) {
      r->u.c.args[i] = tmpl_build(ca, t->u.c.args[i], vl);
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
  Arena ca;
  size_t sz;
  if (term->tag == T_CMP && term->u.c.fn == S_NECK && term->u.c.ar == 2) {
    head = term->u.c.args[0];
    flatten_conj(term->u.c.args[1], &goals, &ng, &gcap);
  }
  fn = functor_of(head, &ar);
  p = pred_find(fn, ar, 1);
  g_clcount++;
  /* the clause lives in one block of exactly the size it needs, so that it can be
     freed after retract (see zombie_clause) */
  vl.v = NULL; vl.n = vl.cap = 0;
  nb.s = NULL; nb.n = nb.cap = 0;
  sz = RND8(sizeof(Clause)) + tmpl_size(head, &vl);
  for (i = 0; i < ng; i++) sz += tmpl_size(goals[i], &vl);
  sz += RND8(sizeof(Term *) * (size_t)(ng ? ng : 1));
  sz += RND8(sizeof(char *) * (size_t)(vl.n ? vl.n : 1));
  for (i = 0; i < vl.n; i++) {
    bclear(&nb);
    var_name(vl.v[i], &nb);
    sz += RND8(nb.n + 1);
  }
  vl.n = 0;
  memset(&ca, 0, sizeof ca);
  ca.ncap = ca.n = 1;
  ca.b = (Blk *)xmalloc(sizeof(Blk));
  ca.b[0].p = (char *)xmalloc(sz);
  ca.b[0].cap = sz;
  ca.b[0].used = 0;
  ca.b[0].base = 0;
  c = (Clause *)aalloc(&ca, sizeof(Clause));
  c->mem = ca.b[0].p;
  c->msz = sz;
  c->zmark = -1;
  c->id = g_clcount;
  c->head = tmpl_build(&ca, head, &vl);
  c->nhead = vl.n;
  c->nbody = ng;
  c->body = (Term **)aalloc(&ca, sizeof(Term *) * (size_t)(ng ? ng : 1));
  for (i = 0; i < ng; i++) c->body[i] = tmpl_build(&ca, goals[i], &vl);
  c->nvars = vl.n;
  c->vn = (char **)aalloc(&ca, sizeof(char *) * (size_t)(vl.n ? vl.n : 1));
  for (i = 0; i < vl.n; i++) {
    bclear(&nb);
    var_name(vl.v[i], &nb);
    c->vn[i] = astrdup(&ca, bstr(&nb));
  }
  if (ca.n != 1 || ca.b[0].used != sz) die("internal error: clause size");
  free(ca.b);
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
    Term *x = (Term *)aalloc_k(&g_heap, sizeof(Term), HK_TERM);
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
      zombie_clause(c);
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
    else zombie_clause(c);
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
  Term **nmap;              /* copies of the variables made inside the findall goal */
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
  int *ipos;               /* indexed call: original positions of v[] */
  long *pre;               /* index: prefix sums of nhead */
  int last, norig;         /* index: last position entered, clauses in the snapshot */
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

/* ---- elided frames ---------------------------------------------------------
   A user call that has entered its last candidate clause is no choice point any
   more, but its frame used to stay on the stack until the call failed or the whole
   query succeeded, so a deterministic recursion of N steps kept N frames.  Such a
   frame is now taken off the stack at once and replaced by a small record of what
   failing back through it would still do:
     - clear g_cutsig when the cut signal is for this call (or, after an eager cut
       of the call, always: that is the F_CUT frame plus the call frame);
     - advance the renaming counter g_rvc by the clauses that would still have
       been tried (and failed) or that an indexed call skipped;
     - undo the trail and release the heap to the frame's marks.
   The records between frame k-1 and frame k form the group of depth k (records of
   one run only, from g_tbase on).  A record whose body has been completely entered
   (the current continuation is its continuation) can no longer be the target of a
   cut, so it is dropped and its remaining effect is folded into the record below:
   (A, B, C) means "g_rvc += A if g_cutsig == 0, else g_rvc += B; then clear
   g_cutsig if C".  The lowest record of a group keeps its marks and becomes the
   group's base record (b = 0).  A tail-recursive loop keeps two records in all.
   A cut whose target lies below only plain call frames and records (no other cut,
   disjunction, if-then-else, negation or findall) is done at once: those frames
   and records are dropped and the target becomes a record with c = 1, which is
   what failing back into the F_CUT frame of the original engine would have done.
   Otherwise the F_CUT frame is pushed as before.  Answers, the order of side
   effects and the renaming counters (variable names) are exactly as before; the
   collector needs the bounded stack. */
typedef struct {
  long depth;              /* sits between g_stk[depth - 1] and g_stk[depth] */
  long b;                  /* barrier (call number) of the elided call; 0 = base record */
  long d;                  /* g_rvc advance when the call is exhausted (indexed call) */
  int c;                   /* the call was cut: failing through it clears g_cutsig */
  long acca, accb;         /* folded effect of dropped records above this one */
  int accc;
  Goal *rest;              /* the call's continuation */
  long tmark;              /* the frame's marks */
  AMark hmark;
} Tomb;

static Term **g_fr;             /* try_clause's variable frame */
static int g_frcap;

static Tomb *g_tomb;
static long g_tombn, g_tombcap, g_tbase;
static int g_elide = 1;
static unsigned long g_nelided;

/* ---- garbage collector controls (the collector itself is in p5_gc.c) ---- */
#ifndef GC_MIN
#define GC_MIN 4194304L
#endif
static int g_gc_on = 1, g_gc_req;
static long g_gc_stress, g_gc_tick;
static long g_gc_next = GC_MIN, g_gc_znext = GC_MIN;
static void gc_collect(Goal **gp);

#define HEAP_TOP() (g_heap.n ? g_heap.b[g_heap.cur].base + (long)g_heap.b[g_heap.cur].used : 0L)
#define GC_DUE() (g_gc_req || (g_gc_on && (g_gc_stress > 0 ? ++g_gc_tick >= g_gc_stress : \
                  (HEAP_TOP() > g_gc_next || g_zbytes > (unsigned long)g_gc_znext))))
/* drop the records whose body is complete (tomb_drop), cheap test first */
#define TOMB_DROP(cont) do { if (g_tombn > g_tbase + 1 && g_tomb[g_tombn - 1].rest == (cont)) \
                               tomb_drop(cont); } while (0)

/* C variables of the callers of run() that hold heap data while a collection can run */
enum { GR_TERM, GR_GOAL, GR_AMARK, GR_TMARK };
typedef struct { int kind; void *p; } GRoot;
static GRoot *g_roots;
static int g_nroots, g_rootcap;

static void groot(int kind, void *p)
{
  if (g_nroots >= g_rootcap) {
    g_rootcap = g_rootcap ? g_rootcap * 2 : 16;
    g_roots = (GRoot *)xrealloc(g_roots, sizeof(GRoot) * (size_t)g_rootcap);
  }
  g_roots[g_nroots].kind = kind;
  g_roots[g_nroots].p = p;
  g_nroots++;
}

static void tomb_push(long depth, long b, long d, int c, Goal *rest)
{
  Tomb *t;
  if (g_tombn >= g_tombcap) {
    g_tombcap = g_tombcap ? g_tombcap * 2 : 256;
    g_tomb = (Tomb *)xrealloc(g_tomb, sizeof(Tomb) * (size_t)g_tombcap);
  }
  t = &g_tomb[g_tombn++];
  t->depth = depth;
  t->b = b;
  t->d = d;
  t->c = c;
  t->acca = t->accb = 0;
  t->accc = 0;
  t->rest = rest;
  t->tmark = 0;
  t->hmark.blk = 0;
  t->hmark.off = 0;
}

/* take the top frame (a call in its last clause, or a cut one) off the stack; dskip:
   renaming numbers the remaining clauses would use up when tried (see run) */
static void elide_top(int cut, long dskip)
{
  Frame *fp = &g_stk[g_sp - 1];
  long d = dskip;
  if (!cut && fp->ipos != NULL) d = fp->pre[fp->norig] - fp->pre[fp->last + 1];
  g_sp--;
  tomb_push(g_sp, fp->barrier, d, cut, fp->rest);
  g_tomb[g_tombn - 1].tmark = fp->tmark;
  g_tomb[g_tombn - 1].hmark = fp->hmark;
  g_nelided++;
}

/* drop the records whose body has been entered completely (continuation == cont) */
static void tomb_drop(Goal *cont)
{
  while (g_tombn > g_tbase) {
    Tomb *t = &g_tomb[g_tombn - 1], *u = t - 1;
    long ea, eb;
    int ec, bottom = g_tombn - 1 == g_tbase || u->depth != t->depth;
    if (t->b == 0 || t->rest != cont || t->depth != g_sp || g_cutsig == t->b) return;
    if (!bottom && !t->c && !t->d && !t->accc && !t->acca && !t->accb) {   /* no effect */
      g_tombn--;
      continue;
    }
    /* E = acc(t) followed by t itself (g_cutsig can no longer name t) */
    ea = t->acca + (t->c ? 0 : t->d);
    eb = t->accb + (t->accc ? (t->c ? 0 : t->d) : 0);
    ec = t->accc || t->c;
    if (bottom) {            /* becomes the group's base record: marks and E only */
      t->b = 0;
      t->c = 0;
      t->d = 0;
      t->rest = NULL;
      t->acca = ea;
      t->accb = eb;
      t->accc = ec;
      return;
    }
    /* u.acc = E followed by u.acc */
    eb = eb + (ec ? u->acca : u->accb);
    u->acca = ea + u->acca;
    u->accb = eb;
    u->accc = ec || u->accc;
    g_tombn--;
  }
}

/* failing (v = 0) or succeeding (v = 1) through the group above the top frame */
static void tomb_unwind(int v)
{
  while (g_tombn > g_tbase && g_tomb[g_tombn - 1].depth == g_sp) {
    Tomb *t = &g_tomb[--g_tombn];
    if (v) continue;
    if (g_cutsig == 0) g_rvc += t->acca;
    else g_rvc += t->accb;
    if (t->accc) g_cutsig = 0;
    if (t->b == 0) {
      ;
    } else if (t->c || g_cutsig == t->b) {
      g_cutsig = 0;
    } else if (g_cutsig == 0) {
      g_rvc += t->d;
    }
    trail_undo(t->tmark);
    arelease(&g_heap, t->hmark);
  }
}

/* the cut of call b (see above); 0 when an F_CUT frame has to be pushed instead */
static int cut_now(long b, long base)
{
  long k = g_sp - 1, ti = g_tombn - 1;
  for (;;) {
    while (ti >= g_tbase && g_tomb[ti].depth == k + 1) {
      Tomb *t = &g_tomb[ti];
      if (t->b == b && !t->accc) {
        g_tombn = ti + 1;
        g_sp = k + 1;
        t->c = 1;
        t->d = 0;
        t->acca = t->accb = 0;
        return 1;
      }
      if (t->c || t->accc) return 0;
      ti--;
    }
    if (k < base || g_stk[k].kind != F_USER) return 0;
    if (g_stk[k].barrier == b) {
      g_tombn = ti + 1;
      g_sp = k + 1;
      elide_top(1, 0);
      return 1;
    }
    k--;
  }
}

static int bi_exec(int id, Term *goal);
static int bi_sort_list(Term *goal, int dedupe);
#ifndef NSORT_MIN
#define NSORT_MIN 32
#endif

/* length of a proper list, -1 if the term is not one */
static long proper_list_len(Term *t)
{
  long n = 0;
  t = deref(t);
  while (t->tag == T_CMP && t->u.c.fn == S_DOT && t->u.c.ar == 2) {
    n++;
    t = deref(t->u.c.args[1]);
  }
  return t == S_NIL->pt ? n : -1;
}
static int on_solution(void);
#define BI_FINDALL_ID 1

static Goal *new_goal(Term *t, Goal *next)
{
  Goal *g = (Goal *)aalloc_k(&g_heap, sizeof(Goal), HK_GOAL);
  g->t = t;
  g->next = next;
  return g;
}

/* can a clause whose first head argument is the template t match the first argument a
   (dereferenced, not a variable)?  The same test unify_head does first. */
static int may_match0(Term *t, Term *a)
{
  if (t->tag == T_TV) return 1;
  if (t->tag == T_CMP || a->tag == T_CMP)
    return t->tag == T_CMP && a->tag == T_CMP && t->u.c.fn == a->u.c.fn && t->u.c.ar == a->u.c.ar;
  if (t == a) return 1;
  return t->tag == T_NUM && a->tag == T_NUM && num_eq(t, a);
}

/* 0 when head unification of clause c with the goal certainly fails: an argument
   bound in the goal has another principal functor than the clause head's */
static int may_match(Clause *c, Term *goal)
{
  int k;
  Term *a;
  for (k = 0; k < goal->u.c.ar; k++) {
    if (c->head->u.c.args[k]->tag == T_TV) continue;   /* (no dereferencing for nothing) */
    a = deref(goal->u.c.args[k]);
    if (a->tag != T_VAR && !may_match0(c->head->u.c.args[k], a)) return 0;
  }
  return 1;
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
  f->rest = NULL;
  f->a = f->b = NULL;
  f->v = NULL;
  f->ipos = NULL;
  f->fa = f->savedfa = NULL;
  f->tmpl = f->savedtmpl = NULL;
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
      /* known by its name, as in the CREXX version where a term is its text (a
         variable of an earlier answer may have lived at the same address) */
      for (i = 0; i < fa->nmapn; i++) {
        c = fa->nmap[i];
        if (c->u.v.ctr == t->u.v.ctr && c->u.v.sep == t->u.v.sep &&
            strcmp(c->u.v.base, t->u.v.base) == 0) {
          *hole = c;
          return first;
        }
      }
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
        fa->nmap = (Term **)xrealloc(fa->nmap, sizeof(Term *) * (size_t)fa->nmapcap);
      }
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
  free(fa->nmap);
  free(fa);
}

static int run(Goal *goals)
{
  long base = g_sp, otbase = g_tbase;
  int v = 0, ar, ar2, i, k, ok, lastcl;
  long dskip;
  Term *goal, *g0, *left, **fr;
  Goal *rest;
  Sym *fn;
  Frame *fp;
  Clause *cl;
  Pred *pr;
  long cbase;

  g_tbase = g_tombn;
eval:
  /* the safe point: everything live is reachable from goals, the frames, the
     elided-frame records and the registered roots */
  TOMB_DROP(goals);
  if (GC_DUE()) gc_collect(&goals);
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
    if (!g_elide || !cut_now(goal->u.x.idx, base)) {
      fp = push_frame(F_CUT);
      fp->barrier = goal->u.x.idx;
    }
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
    groot(GR_GOAL, &rest);      /* consult/1 runs directives: a collection can happen */
    ok = bi_exec(fn->bi[ar], goal);
    g_nroots--;
    if (ok) goals = rest;
    else { v = 0; goto ret; }
    goto eval;
  }
  /* msort/2 and sort/2 of a long proper list: merge sort in C instead of the insertion sort
     of the library (quadratic).  Short lists keep the library path so that the variable
     renaming counters stay exactly as before. */
  if (ar == 2 && (fn == S_MSORT || fn == S_SORT)) {
    pr = pred_find(fn, ar, 0);
    if (pr != NULL && pr->n > 0 && pr->v[0]->id <= g_bootmax && pr->v[pr->n - 1]->id <= g_bootmax &&
        proper_list_len(goal->u.c.args[0]) >= NSORT_MIN) {
      if (bi_sort_list(goal, fn == S_SORT)) goals = rest;
      else { v = 0; goto ret; }
      goto eval;
    }
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
  TOMB_DROP(rest);
  fp = push_frame(F_USER);
  fp->a = goal;
  fp->rest = rest;
  fp->v = pr->v;
  fp->n = pr->n;
  fp->i = 0;
  fp->ipos = NULL;
  fp->barrier = ++g_callcount;
  if (ar >= 1 && pr->n >= IDX_MIN) {
    IBkt *bk = NULL;
    PIdx *ix = idx_for_call(pr, deref(goal->u.c.args[0]), &bk);
    if (ix != NULL) {
      fp->v = bk->c;
      fp->n = bk->n;
      fp->ipos = bk->pos;
      fp->pre = ix->pre;
      fp->last = -1;
      fp->norig = pr->n;
    }
  }

try_clause:
  fp = &g_stk[g_sp - 1];
  if (fp->i >= fp->n) {
    if (fp->ipos != NULL) g_rvc += fp->pre[fp->norig] - fp->pre[fp->last + 1];
    g_sp--;
    v = 0;
    goto ret;
  }
  fp->tmark = trail_mark();
  fp->hmark = amark(&g_heap);
  cl = fp->v[fp->i];
  if (fp->ipos != NULL) {   /* clauses skipped by the index still use up renaming numbers */
    g_rvc += fp->pre[fp->ipos[fp->i]] - fp->pre[fp->last + 1];
    fp->last = fp->ipos[fp->i];
  }
  /* no alternative after this clause: it is the last one, or the remaining ones (a few,
     not indexed) cannot match the goal's arguments -- trying them would only use up
     their renaming numbers, dskip */
  lastcl = 0;
  dskip = 0;
  if (g_elide) {
    if (fp->i == fp->n - 1) lastcl = 1;
    else if (fp->ipos == NULL && fp->n - fp->i <= 8 && fp->a->tag == T_CMP && fp->a->u.c.ar > 0) {
      for (k = fp->i + 1; k < fp->n; k++) {
        if (may_match(fp->v[k], fp->a)) break;
        dskip += fp->v[k]->nhead;
      }
      lastcl = k == fp->n;
    }
  }
  /* the clause's variables: only needed while the head and body are instantiated */
  if (cl->nvars + 1 > g_frcap) {
    g_frcap = cl->nvars + 64;
    g_fr = (Term **)xrealloc(g_fr, sizeof(Term *) * (size_t)g_frcap);
  }
  fr = g_fr;
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
    if (lastcl) elide_top(0, dskip);
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
  if (g_tombn > g_tbase && g_tomb[g_tombn - 1].depth == g_sp) tomb_unwind(v);
  if (g_sp == base) {
    g_tbase = otbase;
    return v;
  }
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
/* ===== p5_gc.c ===== */
/* p5_gc.c - garbage collector: heap, trail, retracted clauses

   The heap is an arena that backtracking releases to marks (AMark in every frame,
   positions in findall), so the collector keeps the allocation order: it is a
   mark-and-copy collector that copies the live objects, in address order, into a
   fresh arena and maps every mark to the position of the first live object at or
   after it.  Every heap object has a header (p0_base.c) with its size, kind and the
   mark bit, so the heap can be walked.

   It runs only at the top of the solver loop (run(), label eval), where everything
   live is reachable from:
     the current goal list; the frames (continuations, goals, findall results); the
     elided-frame records; g_fatmpl; the query and clause variable maps; the C
     variables registered with groot() by the callers of run().
   The trail is not a root: an entry whose variable is otherwise unreachable is
   dropped (resetting it could not be observed) and the trail marks are mapped.
   Retracted clauses and replaced clause arrays (p4_db.c) are freed when no frame
   can still try them and no live term points into them.
   Variable names, counters and the order of everything are unchanged; a variable's
   "born" position becomes its new position (it is only compared with marks). */

static unsigned long g_gc_count, g_gc_clfreed;
static double g_gc_secs, g_gc_freed;
static long g_gc_live, g_gc_peak;
static int g_gc_verbose;

typedef struct { char *lo, *hi; void *owner; } GSpan;
static GSpan *g_hsp, *g_zsp;            /* heap blocks / retracted clauses, by address */
static int g_hspn, g_zspn;

/* translated marks: kind 0 = AMark of the heap, 1 = heap position, 2 = trail index */
typedef struct { int kind; void *p; long pos; } GMark;
static GMark *g_gm;
static long g_gmn, g_gmcap;

static void **g_mst;                    /* mark stack */
static long g_msn, g_mscap;

static long *g_cpm;                     /* trail marks of the choice points, sorted */
static long g_cpmn, g_cpmcap;
static unsigned long g_gc_shunted;

static int gspan_cmp(const void *x, const void *y)
{
  const GSpan *a = (const GSpan *)x, *b = (const GSpan *)y;
  if (a->lo < b->lo) return -1;
  return a->lo > b->lo ? 1 : 0;
}

static int span_find(GSpan *v, int n, const void *p)
{
  const char *c = (const char *)p;
  int lo = 0, hi = n - 1, m;
  while (lo <= hi) {
    m = (lo + hi) / 2;
    if (c < v[m].lo) hi = m - 1;
    else if (c >= v[m].hi) lo = m + 1;
    else return m;
  }
  return -1;
}

/* which heap block holds p: a hash table on the address divided by the block size
   (a block of BLKSZ bytes touches at most two such "pages", a big block more) */
typedef struct { unsigned long pg; int sp; } HPg;
static HPg *g_hpg;
static unsigned long g_hpgcap;           /* a power of two */

static void hpg_build(void)
{
  int i;
  unsigned long n = 0, pg, last, h;
  for (i = 0; i < g_hspn; i++)
    n += ((unsigned long)g_hsp[i].hi - (unsigned long)g_hsp[i].lo) / (unsigned long)BLKSZ + 2;
  for (g_hpgcap = 64; g_hpgcap < n * 3; g_hpgcap *= 2) ;
  g_hpg = (HPg *)xrealloc(g_hpg, sizeof(HPg) * (size_t)g_hpgcap);
  for (h = 0; h < g_hpgcap; h++) g_hpg[h].sp = -1;
  for (i = 0; i < g_hspn; i++) {
    if (g_hsp[i].hi == g_hsp[i].lo) continue;
    last = ((unsigned long)g_hsp[i].hi - 1) / (unsigned long)BLKSZ;
    for (pg = (unsigned long)g_hsp[i].lo / (unsigned long)BLKSZ; pg <= last; pg++) {
      h = (pg * 2654435761UL) & (g_hpgcap - 1);
      while (g_hpg[h].sp >= 0) h = (h + 1) & (g_hpgcap - 1);
      g_hpg[h].pg = pg;
      g_hpg[h].sp = i;
    }
  }
}

static int in_old(const void *p)
{
  unsigned long pg, h;
  const char *c = (const char *)p;
  if (p == NULL || g_hspn == 0) return 0;
  pg = (unsigned long)c / (unsigned long)BLKSZ;
  h = (pg * 2654435761UL) & (g_hpgcap - 1);
  while (g_hpg[h].sp >= 0) {
    if (g_hpg[h].pg == pg && c >= g_hsp[g_hpg[h].sp].lo && c < g_hsp[g_hpg[h].sp].hi) return 1;
    h = (h + 1) & (g_hpgcap - 1);
  }
  return 0;
}

#define IN_OLD(p) in_old(p)
#define HDR(p) ((unsigned long *)((char *)(p) - HDRSZ))

static void *gc_fwd(void *p)
{
  if (!IN_OLD(p)) return p;
  return *(void **)p;
}

static void gc_mark(const void *p)
{
  unsigned long *h;
  if (p == NULL) return;
  if (!in_old(p)) {
    if (g_zspn > 0) {
      int z = span_find(g_zsp, g_zspn, p);
      if (z >= 0) ((Clause *)g_zsp[z].owner)->zmark = 1;
    }
    return;
  }
  h = HDR(p);
  if (*h & HK_MARK) return;
  *h |= HK_MARK;
  if ((*h & HK_KIND) == HK_RAW) return;
  if (g_msn >= g_mscap) {
    g_mscap = g_mscap ? g_mscap * 2 : 4096;
    g_mst = (void **)xrealloc(g_mst, sizeof(void *) * (size_t)g_mscap);
  }
  g_mst[g_msn++] = (void *)p;
}

/* Variable shunting.  The engine binds the caller's variable to the new clause
   variable, so an argument passed unbound down a recursion becomes a chain
   V0 -> V1 -> ... -> Vn.  During a collection a bound variable's "born" field holds
   the index of its trail entry; V -> W -> X may become V -> X when no choice point
   could undo W's binding but not V's: no trail mark m with tV < m <= tW.  Every
   dereference gives the same result as before, at any later time. */
static int shunt_ok(long tv, long tw)
{
  long lo = 0, hi = g_cpmn - 1, m;
  if (tv >= tw) return 1;
  while (lo <= hi) {                    /* first mark > tv */
    m = (lo + hi) / 2;
    if (g_cpm[m] > tv) hi = m - 1;
    else lo = m + 1;
  }
  return lo >= g_cpmn || g_cpm[lo] > tw;
}

static void cpm_add(long m)
{
  if (g_cpmn >= g_cpmcap) {
    g_cpmcap = g_cpmcap ? g_cpmcap * 2 : 1024;
    g_cpm = (long *)xrealloc(g_cpm, sizeof(long) * (size_t)g_cpmcap);
  }
  g_cpm[g_cpmn++] = m;
}

static int long_cmp(const void *x, const void *y)
{
  long a = *(const long *)x, b = *(const long *)y;
  return a < b ? -1 : (a > b ? 1 : 0);
}

static void gc_shunt_prepare(void)
{
  long i;
  g_cpmn = 0;
  for (i = 0; i < g_sp; i++) cpm_add(g_stk[i].tmark);
  for (i = 0; i < g_tombn; i++) cpm_add(g_tomb[i].tmark);
  for (i = 0; i < g_nroots; i++) if (g_roots[i].kind == GR_TMARK) cpm_add(*(long *)g_roots[i].p);
  qsort(g_cpm, (size_t)g_cpmn, sizeof(long), long_cmp);
  for (i = 0; i < g_tn; i++)
    if (IN_OLD(g_trail[i])) g_trail[i]->u.v.born = i;   /* reset by the copy */
}

static void gc_trace(void)
{
  int i;
  while (g_msn > 0) {
    void *p = g_mst[--g_msn];
    if ((*HDR(p) & HK_KIND) == HK_GOAL) {
      Goal *g = (Goal *)p;
      gc_mark(g->t);
      gc_mark(g->next);
    } else {
      Term *t = (Term *)p;
      switch (t->tag) {
      case T_VAR:
        while (t->u.v.ref != NULL && t->u.v.ref->tag == T_VAR && t->u.v.ref->u.v.ref != NULL &&
               IN_OLD(t->u.v.ref) && shunt_ok(t->u.v.born, t->u.v.ref->u.v.born)) {
          t->u.v.ref = t->u.v.ref->u.v.ref;
          g_gc_shunted++;
        }
        gc_mark(t->u.v.ref);
        gc_mark(t->u.v.base);
        break;
      case T_NUM: gc_mark(t->u.n.txt); break;
      case T_CMP:
        for (i = 0; i < t->u.c.ar; i++) gc_mark(t->u.c.args[i]);
        break;
      default: break;
      }
    }
  }
}

/* findall answers live in the findall's own arena and point to outer heap variables */
static void gc_fa_term(Term **tp, int upd)
{
  int i, n;
  Term *t;
  for (;;) {
    t = *tp;
    if (IN_OLD(t)) {
      if (upd) *tp = (Term *)gc_fwd(t);
      else gc_mark(t);
      return;
    }
    if (t->tag != T_CMP || t->u.c.ar == 0) return;
    n = t->u.c.ar;
    for (i = 0; i < n - 1; i++) gc_fa_term(&t->u.c.args[i], upd);
    tp = &t->u.c.args[n - 1];
  }
}

static void gc_t(Term **tp, int upd)
{
  if (upd) *tp = (Term *)gc_fwd(*tp);
  else gc_mark(*tp);
}

static void gc_g(Goal **gp, int upd)
{
  if (upd) *gp = (Goal *)gc_fwd(*gp);
  else gc_mark(*gp);
}

static void gc_vmap(VMap *m, int upd)
{
  int i;
  for (i = 0; i < m->n; i++) gc_t(&m->e[i].var, upd);
}

/* visit every root: mark (upd = 0) or replace by the new address (upd = 1) */
static void gc_roots(Goal **gp, int upd)
{
  long i, j;
  if (gp != NULL) gc_g(gp, upd);
  for (i = 0; i < g_nroots; i++) {
    if (g_roots[i].kind == GR_TERM) gc_t((Term **)g_roots[i].p, upd);
    else if (g_roots[i].kind == GR_GOAL) gc_g((Goal **)g_roots[i].p, upd);
  }
  for (i = 0; i < g_sp; i++) {
    Frame *f = &g_stk[i];
    gc_g(&f->rest, upd);
    gc_t(&f->a, upd);
    gc_t(&f->b, upd);
    gc_t(&f->tmpl, upd);
    gc_t(&f->savedtmpl, upd);
    if (f->kind == F_FA && f->fa != NULL)
      for (j = 0; j < f->fa->n; j++) gc_fa_term(&f->fa->res[j], upd);
  }
  for (i = 0; i < g_tombn; i++) gc_g(&g_tomb[i].rest, upd);
  gc_t(&g_fatmpl, upd);
  gc_vmap(&g_pvarmap, upd);
  gc_vmap(&g_qvarmap, upd);
}

static void gm_add(int kind, void *p)
{
  if (g_gmn >= g_gmcap) {
    g_gmcap = g_gmcap ? g_gmcap * 2 : 1024;
    g_gm = (GMark *)xrealloc(g_gm, sizeof(GMark) * (size_t)g_gmcap);
  }
  g_gm[g_gmn].kind = kind;
  g_gm[g_gmn].p = p;
  if (kind == 0) {
    AMark *m = (AMark *)p;
    g_gm[g_gmn].pos = g_heap.b[m->blk].base + (long)m->off;
  } else {
    g_gm[g_gmn].pos = *(long *)p;
  }
  g_gmn++;
}

static int gm_cmp(const void *x, const void *y)
{
  const GMark *a = (const GMark *)x, *b = (const GMark *)y;
  if (a->pos != b->pos) return a->pos < b->pos ? -1 : 1;
  return 0;
}

/* every heap mark (kind 0, 1) or trail mark (kind 2) */
static void gc_collect_marks(int trail)
{
  long i;
  g_gmn = 0;
  for (i = 0; i < g_nroots; i++) {
    if (!trail && g_roots[i].kind == GR_AMARK) gm_add(0, g_roots[i].p);
    if (trail && g_roots[i].kind == GR_TMARK) gm_add(2, g_roots[i].p);
  }
  for (i = 0; i < g_sp; i++) {
    if (trail) gm_add(2, &g_stk[i].tmark);
    else {
      gm_add(0, &g_stk[i].hmark);
      if (g_stk[i].kind == F_FA) gm_add(1, &g_stk[i].fapos);
    }
  }
  for (i = 0; i < g_tombn; i++) {
    if (trail) gm_add(2, &g_tomb[i].tmark);
    else gm_add(0, &g_tomb[i].hmark);
  }
  if (!trail) gm_add(1, &g_fapos);
  qsort(g_gm, (size_t)g_gmn, sizeof(GMark), gm_cmp);
}

static int zarr_cmp(const void *x, const void *y)
{
  const ZArr *a = (const ZArr *)x, *b = (const ZArr *)y;
  if ((const char *)a->p < (const char *)b->p) return -1;
  return (const char *)a->p > (const char *)b->p ? 1 : 0;
}

/* mark a retracted-storage array; 1 if p is one */
static int zarr_note(const void *p)
{
  long lo = 0, hi = g_zarn - 1, m;
  while (lo <= hi) {
    m = (lo + hi) / 2;
    if ((const char *)p < (const char *)g_zar[m].p) hi = m - 1;
    else if ((const char *)p > (const char *)g_zar[m].p) lo = m + 1;
    else {
      g_zar[m].mark = 1;
      return 1;
    }
  }
  return 0;
}

/* retracted clauses and old clause arrays that a frame can still try */
static void gc_zombie_frames(void)
{
  long i;
  int k;
  if (g_zarn == 0) return;
  qsort(g_zar, (size_t)g_zarn, sizeof(ZArr), zarr_cmp);
  for (i = 0; i < g_zarn; i++) g_zar[i].mark = 0;
  for (i = 0; i < g_sp; i++) {
    Frame *f = &g_stk[i];
    if (f->kind != F_USER || f->v == NULL) continue;
    if (f->ipos != NULL) {
      zarr_note(f->ipos);
      zarr_note(f->pre);
    }
    /* a current clause array holds no retracted clause: only look into old ones */
    if (!zarr_note(f->v)) continue;
    for (k = f->i; k < f->n; k++)
      if (f->v[k]->zmark == 0) f->v[k]->zmark = 1;
  }
}

static void gc_free_zombies(void)
{
  long i, k;
  g_zbytes = 0;
  for (i = k = 0; i < g_zcln; i++) {
    Clause *c = g_zcl[i];
    if (c->zmark == 1) {
      c->zmark = 0;
      g_zcl[k++] = c;
      g_zbytes += (unsigned long)c->msz;
    } else {
      free(c->mem);              /* the Clause itself is in this block */
      g_gc_clfreed++;
    }
  }
  g_zcln = k;
  for (i = k = 0; i < g_zarn; i++) {
    if (g_zar[i].mark) {
      g_zar[k] = g_zar[i];
      g_zar[k].mark = 0;
      g_zbytes += (unsigned long)g_zar[k].sz;
      k++;
    } else {
      free(g_zar[i].p);
    }
  }
  g_zarn = k;
}

static void gc_collect(Goal **gp)
{
  Arena na;
  long i, mi, k, before, opos, npos;
  int b;
  clock_t t0 = clock();
  g_gc_req = 0;
  g_gc_tick = 0;
  before = apos(&g_heap);
  if (before > g_gc_peak) g_gc_peak = before;

  /* the blocks of the heap, and the retracted clauses, sorted by address */
  g_hspn = 0;
  if (g_heap.n > 0) {
    g_hsp = (GSpan *)xrealloc(g_hsp, sizeof(GSpan) * (size_t)(g_heap.cur + 1));
    for (b = 0; b <= g_heap.cur; b++) {
      g_hsp[g_hspn].lo = g_heap.b[b].p;
      g_hsp[g_hspn].hi = g_heap.b[b].p + g_heap.b[b].used;
      g_hsp[g_hspn].owner = NULL;
      g_hspn++;
    }
    qsort(g_hsp, (size_t)g_hspn, sizeof(GSpan), gspan_cmp);
    hpg_build();
  }
  g_zspn = 0;
  if (g_zcln > 0) {
    g_zsp = (GSpan *)xrealloc(g_zsp, sizeof(GSpan) * (size_t)g_zcln);
    for (i = 0; i < g_zcln; i++) {
      g_zcl[i]->zmark = 0;
      g_zsp[i].lo = g_zcl[i]->mem;
      g_zsp[i].hi = g_zcl[i]->mem + g_zcl[i]->msz;
      g_zsp[i].owner = g_zcl[i];
    }
    g_zspn = (int)g_zcln;
    qsort(g_zsp, (size_t)g_zspn, sizeof(GSpan), gspan_cmp);
  }

  /* mark */
  if (g_heap.n > 0) gc_shunt_prepare();
  gc_zombie_frames();
  gc_roots(gp, 0);
  gc_trace();

  if (g_heap.n > 0) {
    /* copy the live objects in order; a heap mark goes to the first live object at
       or after it, or to the new top */
    gc_collect_marks(0);
    memset(&na, 0, sizeof na);
    mi = 0;
    for (b = 0; b <= g_heap.cur; b++) {
      Blk *bk = &g_heap.b[b];
      size_t off = 0, sz;
      while (off < bk->used) {
        char *o = bk->p + off, *nw;
        unsigned long h = *(unsigned long *)o;
        sz = (size_t)(h & ~7UL);
        if (sz < HDRSZ) die("internal error: heap header");
        if (h & HK_MARK) {
          nw = (char *)aalloc(&na, sz);
          npos = na.b[na.cur].base + (long)(na.b[na.cur].used - sz);
          opos = bk->base + (long)off;
          for (; mi < g_gmn && g_gm[mi].pos <= opos; mi++) {
            g_gm[mi].pos = npos;
            if (g_gm[mi].kind == 0) {
              ((AMark *)g_gm[mi].p)->blk = na.cur;
              ((AMark *)g_gm[mi].p)->off = na.b[na.cur].used - sz;
            }
          }
          memcpy(nw, o, sz);
          *(unsigned long *)nw = h & ~HK_MARK;
          if ((h & HK_KIND) == HK_TERM && ((Term *)(nw + HDRSZ))->tag == T_VAR)
            ((Term *)(nw + HDRSZ))->u.v.born = npos;
          *(void **)(o + HDRSZ) = nw + HDRSZ;      /* forwarding address */
        }
        off += sz;
      }
    }
    for (; mi < g_gmn; mi++) {
      g_gm[mi].pos = apos(&na);
      if (g_gm[mi].kind == 0) *(AMark *)g_gm[mi].p = amark(&na);
    }
    for (mi = 0; mi < g_gmn; mi++)
      if (g_gm[mi].kind == 1) *(long *)g_gm[mi].p = g_gm[mi].pos;

    /* new addresses inside the copies */
    for (b = 0; b < na.n && b <= na.cur; b++) {
      Blk *bk = &na.b[b];
      size_t off = 0, sz;
      while (off < bk->used) {
        char *o = bk->p + off;
        unsigned long h = *(unsigned long *)o;
        sz = (size_t)(h & ~7UL);
        if ((h & HK_KIND) == HK_GOAL) {
          Goal *g = (Goal *)(o + HDRSZ);
          g->t = (Term *)gc_fwd(g->t);
          g->next = (Goal *)gc_fwd(g->next);
        } else if ((h & HK_KIND) == HK_TERM) {
          Term *t = (Term *)(o + HDRSZ);
          int j;
          switch (t->tag) {
          case T_VAR:
            t->u.v.ref = (Term *)gc_fwd(t->u.v.ref);
            t->u.v.base = (const char *)gc_fwd((void *)t->u.v.base);
            break;
          case T_NUM: t->u.n.txt = (const char *)gc_fwd((void *)t->u.n.txt); break;
          case T_CMP:
            t->u.c.args = (Term **)(t + 1);
            for (j = 0; j < t->u.c.ar; j++) t->u.c.args[j] = (Term *)gc_fwd(t->u.c.args[j]);
            break;
          default: break;
          }
        }
        off += sz;
      }
    }
    gc_roots(gp, 1);

    /* the trail: drop entries of dead variables */
    gc_collect_marks(1);
    mi = 0;
    for (i = k = 0; i < g_tn; i++) {
      Term *v = g_trail[i];
      for (; mi < g_gmn && g_gm[mi].pos <= i; mi++) *(long *)g_gm[mi].p = k;
      if (!IN_OLD(v)) g_trail[k++] = v;
      else if (*HDR(v) & HK_MARK) g_trail[k++] = (Term *)gc_fwd(v);
    }
    for (; mi < g_gmn; mi++) *(long *)g_gm[mi].p = k;
    g_tn = k;

    /* the new heap replaces the old one */
    for (b = 0; b < g_heap.n; b++) free(g_heap.b[b].p);
    free(g_heap.b);
    g_heap = na;
    g_heap.hdr = 1;
  }
  g_hspn = 0;
  gc_free_zombies();
  g_zspn = 0;

  g_gc_live = apos(&g_heap);
  g_gc_freed += (double)(before - g_gc_live);
  /* next time when the heap has doubled; tripled... when this collection freed less
     than half (data kept by choice points), so that copying stays a small cost */
  i = g_gc_live * (before - g_gc_live < g_gc_live ? 3 : 1);
  g_gc_next = g_gc_live + (i > GC_MIN ? i : GC_MIN);
  g_gc_znext = (long)g_zbytes + (g_zbytes > (unsigned long)GC_MIN ? (long)g_zbytes : GC_MIN);
  g_gc_count++;
  g_gc_secs += (double)(clock() - t0) / (double)CLOCKS_PER_SEC;
  if (g_gc_verbose) {
    fprintf(stderr, "%% gc %lu: heap %ld -> %ld bytes, trail %ld, frames %ld, "
            "clauses waiting %ld\n", g_gc_count, before, g_gc_live, g_tn, g_sp, g_zcln);
  }
}

static void gc_stats(void)
{
  char b[160];
  say("% garbage collector:");
  sprintf(b, "%%   collections %lu (%s), %.3f s", g_gc_count,
          g_gc_on ? (g_gc_stress > 0 ? "stress mode" : "automatic") : "off", g_gc_secs);
  say(b);
  sprintf(b, "%%   heap now %ld bytes, live after last collection %ld, largest %ld",
          apos(&g_heap), g_gc_live, g_gc_peak > apos(&g_heap) ? g_gc_peak : apos(&g_heap));
  say(b);
  sprintf(b, "%%   reclaimed %.0f bytes, next collection at %ld", g_gc_freed, g_gc_next);
  say(b);
  sprintf(b, "%%   trail %ld entries, frames %ld, elided frames %lu", g_tn, g_sp, g_nelided);
  say(b);
  sprintf(b, "%%   retracted clauses freed %lu, waiting %ld (%lu bytes with old arrays)",
          g_gc_clfreed, g_zcln, g_zbytes);
  say(b);
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
  BI_LOAD, BI_CXCALL2, BI_CXCALL3, BI_HALT0, BI_HALT1, BI_GC0, BI_GC1, BI_STATS
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
  reg_bi("garbage_collect", 0, BI_GC0);
  reg_bi("gc", 1, BI_GC1);
  reg_bi("statistics", 0, BI_STATS);
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
      int ok;
      groot(GR_AMARK, &hm);
      groot(GR_TERM, &t);
      groot(GR_TMARK, &mark);
      ok = solve_once(t->u.c.args[0]);
      g_nroots -= 3;
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

/* stable bottom-up merge sort of terms in standard order */
static void sort_terms(Term **v, int n)
{
  Term **tmp;
  int w, i, j, k, m, mid, hi;
  if (n < 2) return;
  tmp = (Term **)xmalloc(sizeof(Term *) * (size_t)n);
  for (w = 1; w < n; w *= 2) {
    for (i = 0; i < n; i += 2 * w) {
      mid = i + w < n ? i + w : n;
      hi = i + 2 * w < n ? i + 2 * w : n;
      j = i;
      k = mid;
      m = i;
      while (j < mid && k < hi) {
        if (term_compare(v[k], v[j]) < 0) tmp[m++] = v[k++];
        else tmp[m++] = v[j++];
      }
      while (j < mid) tmp[m++] = v[j++];
      while (k < hi) tmp[m++] = v[k++];
    }
    memcpy(v, tmp, sizeof(Term *) * (size_t)n);
  }
  free(tmp);
}

/* msort/2 and sort/2 on a proper list (called from the solver for long lists); sort/2 also
   drops duplicates (term_compare == 0), keeping the last of each run like the library did */
static int bi_sort_list(Term *goal, int dedupe)
{
  Term **v, *r;
  int n, i, m;
  n = list_items(goal->u.c.args[0], &v);
  sort_terms(v, n);
  m = n;
  if (dedupe && n > 1) {
    m = 0;
    for (i = 0; i < n; i++) {
      if (i + 1 < n && term_compare(v[i], v[i + 1]) == 0) continue;
      v[m++] = v[i];
    }
  }
  r = S_NIL->pt;
  for (i = m; i > 0; i--) r = mk_cons(v[i - 1], r);
  return unify(goal->u.c.args[1], r);
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
  case BI_GC0:                  /* collected at the next safe point, right after this goal */
    g_gc_req = 1;
    return 1;
  case BI_GC1:                  /* gc(on), gc(off), gc(N): stress mode, every N inferences */
    v = deref(A[0]);
    if (v->tag == T_NUM) {
      g_gc_stress = (long)num_val(v->u.n.txt);
      g_gc_on = 1;
      return 1;
    }
    if (v->tag != T_ATOM && v->tag != T_QATOM) return 0;
    if (strcmp(v->u.a.sym->text, "on") == 0) g_gc_on = 1;
    else if (strcmp(v->u.a.sym->text, "off") == 0) g_gc_on = 0;
    else return 0;
    g_gc_stress = 0;
    return 1;
  case BI_STATS:
    gc_stats();
    return 1;
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
  g_tombn = g_tbase = 0;
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
        int k;
        for (k = 0; k < p->n; k++) zombie_clause(p->v[k]);
        zombie_arr(p->v, sizeof(Clause *) * (size_t)p->cap);
        zombie_idx(p->idx);
        p->v = NULL;
        p->cap = 0;
        p->n = 0;
        p->listed = 0;
        p->idx = NULL;
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
  else if (strcmp(up, ")GC") == 0) {
    gc_collect(NULL);
    gc_stats();
  } else if (strcmp(up, ")QUIT") == 0 || strcmp(up, ")EXIT") == 0) return 1;
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
  groot(GR_TMARK, &mark);
  ok = run(new_goal(g, NULL));
  g_nroots--;
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
    groot(GR_AMARK, &hm);
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
    g_nroots--;
    arelease(&g_heap, hm);
  }
}

/* PROLOG_GC: comma separated off | stress=N | verbose | noelide (see README.md) */
static void gc_options(void)
{
  const char *e = getenv("PROLOG_GC"), *p;
  if (e == NULL) return;
  for (p = e; *p; ) {
    if (strncmp(p, "off", 3) == 0) g_gc_on = 0;
    else if (strncmp(p, "verbose", 7) == 0) g_gc_verbose = 1;
    else if (strncmp(p, "noelide", 7) == 0) g_elide = 0;
    else if (strncmp(p, "stress", 6) == 0) {
      g_gc_stress = p[6] == '=' ? atol(p + 7) : 1;
      if (g_gc_stress < 1) g_gc_stress = 1;
    }
    while (*p && *p != ',') p++;
    if (*p == ',') p++;
  }
}

int main(int argc, char **argv)
{
  gc_options();
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
