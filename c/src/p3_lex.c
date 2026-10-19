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
