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
