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
  default: break;
  }
  say("prolog: internal error -- unimplemented builtin");
  return 0;
}
