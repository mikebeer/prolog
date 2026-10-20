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
