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
