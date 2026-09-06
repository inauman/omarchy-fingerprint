/*
 * Offline evaluation tool for the elanpress matcher.
 *
 *   elanpress-eval pair A.pgm B.pgm            score one pair (8-bit PGMs)
 *   elanpress-eval reprocess BG.pgm RAW.pgm OUT.pgm
 *                                              16-bit raw + background -> 8-bit
 *   elanpress-eval pool DIR [ENROLL_TOUCHES]   genuine/impostor statistics
 *   elanpress-eval synth A.pgm                 self-test on one image
 *
 * Pool naming: <finger>-t<NNN>-f<NN>.pgm (what the driver's dump mode
 * writes with FP_ELANPRESS_DUMP_DIR, prefixed "img-"; rename the prefix
 * to the finger label). Images from the same touch are never paired.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "elanpress-match.h"

#include <glib.h>
#include <glib/gstdio.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct
{
  char   *label;
  int     touch;
  int     frame;
  guint8 *img;
} Img;

static int W = 0, H = 0;

static Img *
load (const char *path)
{
  int w, h;
  guint8 *px = elanpress_pgm_read8 (path, &w, &h);
  Img *im;
  g_autofree char *base = g_path_get_basename (path);
  char *dash;

  if (!px)
    return NULL;
  if (W == 0)
    {
      W = w;
      H = h;
    }
  if (w != W || h != H)
    {
      fprintf (stderr, "%s: %dx%d, expected %dx%d, skipped\n", path, w, h, W, H);
      g_free (px);
      return NULL;
    }
  im = g_new0 (Img, 1);
  im->img = px;
  im->label = g_strdup (base);
  /* the touch tag is "-t" followed by digits; labels may contain "-t" */
  dash = im->label;
  while ((dash = strstr (dash, "-t")) && !g_ascii_isdigit (dash[2]))
    dash++;
  if (dash)
    {
      *dash = 0;
      sscanf (dash + 2, "%d-f%d", &im->touch, &im->frame);
    }
  return im;
}

static int
cmd_pair (const char *a, const char *b, const ElanpressMatchParams *p)
{
  Img *ia = load (a), *ib = load (b);
  ElanpressMatchResult r;
  double s;

  if (!ia || !ib)
    {
      fprintf (stderr, "could not read images\n");
      return 1;
    }
  s = elanpress_match (ia->img, ib->img, W, H, p, &r);
  printf ("%.4f peak=%.4f sidelobe=%.4f dx=%d dy=%d rot=%.0f overlap=%d\n",
          s, r.peak, r.sidelobe, r.dx, r.dy, r.rot, r.overlap);
  return 0;
}

static int
cmd_reprocess (const char *bgp, const char *rawp, const char *outp,
               const ElanpressMatchParams *p)
{
  int w, h, w2, h2;
  guint16 *bg = elanpress_pgm_read16 (bgp, &w, &h);
  guint16 *raw = elanpress_pgm_read16 (rawp, &w2, &h2);
  ElanpressQuality q;
  guint8 *out;

  if (!bg || !raw || w != w2 || h != h2)
    {
      fprintf (stderr, "could not read 16-bit frames or sizes differ\n");
      return 1;
    }
  out = elanpress_process_frame (raw, bg, w, h, p->norm_win, &q);
  printf ("mean %.0f contrast %.1f coverage %.2f\n", q.mean, q.contrast, q.coverage);
  return elanpress_pgm_write8 (outp, out, w, h) ? 0 : 1;
}

static int
cmp_double (const void *a, const void *b)
{
  double x = *(const double *) a, y = *(const double *) b;

  return x < y ? -1 : x > y ? 1 : 0;
}

static void
print_dist (const char *name, GArray *arr)
{
  double *v = (double *) arr->data;
  guint n = arr->len;

  if (n == 0)
    {
      printf ("%-10s n=0\n", name);
      return;
    }
  qsort (v, n, sizeof (double), cmp_double);
  printf ("%-10s n=%-5u min %.3f p10 %.3f p50 %.3f p90 %.3f max %.3f\n",
          name, n, v[0], v[n / 10], v[n / 2], v[(n * 9) / 10], v[n - 1]);
}

static int
cmd_pool (const char *dir, int enroll_touches, const ElanpressMatchParams *p)
{
  g_autoptr(GPtrArray) imgs = g_ptr_array_new ();
  GDir *d = g_dir_open (dir, 0, NULL);
  const char *name;
  g_autoptr(GArray) genuine = g_array_new (FALSE, FALSE, sizeof (double));
  g_autoptr(GArray) impostor = g_array_new (FALSE, FALSE, sizeof (double));
  g_autoptr(GHashTable) labels = g_hash_table_new (g_str_hash, g_str_equal);
  GHashTableIter it;
  gpointer key;
  double far0_thresh;

  if (!d)
    {
      fprintf (stderr, "cannot open %s\n", dir);
      return 1;
    }
  while ((name = g_dir_read_name (d)))
    {
      g_autofree char *path = g_build_filename (dir, name, NULL);
      Img *im;

      if (!g_str_has_suffix (name, ".pgm") || g_str_has_prefix (name, "raw-") ||
          g_str_has_prefix (name, "bg-"))
        continue;
      im = load (path);
      if (im)
        {
          g_ptr_array_add (imgs, im);
          g_hash_table_add (labels, im->label);
        }
    }
  g_dir_close (d);
  printf ("%u images, %u labels, %dx%d\n", imgs->len, g_hash_table_size (labels), W, H);

  /* pairwise protocol: every ordered pair from different touches */
  for (guint i = 0; !g_getenv ("EVAL_SKIP_PAIRWISE") && i < imgs->len; i++)
    {
      Img *a = g_ptr_array_index (imgs, i);
      ElanpressProbe *probe = elanpress_probe_new (a->img, W, H, p);

      for (guint j = 0; j < imgs->len; j++)
        {
          Img *b = g_ptr_array_index (imgs, j);
          double s;

          if (i == j)
            continue;
          if (strcmp (a->label, b->label) == 0 && a->touch == b->touch)
            continue;
          s = elanpress_probe_match (probe, b->img, NULL);
          if (strcmp (a->label, b->label) == 0)
            g_array_append_val (genuine, s);
          else
            g_array_append_val (impostor, s);
        }
      elanpress_probe_free (probe);
      fprintf (stderr, "\r%u/%u", i + 1, imgs->len);
    }
  fprintf (stderr, "\n");
  printf ("pairwise scores (probe image vs single template image):\n");
  print_dist ("genuine", genuine);
  print_dist ("impostor", impostor);

  if (impostor->len && genuine->len)
    {
      double *g = (double *) genuine->data, *im = (double *) impostor->data;
      double max_imp = im[impostor->len - 1];
      int above = 0;

      for (guint i = 0; i < genuine->len; i++)
        if (g[i] > max_imp)
          above++;
      printf ("pairwise: at FAR=0 (threshold just above %.3f) genuine pairs accepted: %d/%u (%.1f%%)\n",
              max_imp, above, genuine->len, 100.0 * above / genuine->len);
      /* EER */
      {
        double best_eer = 1, best_t = 0;
        for (double t = -0.2; t <= 1.0; t += 0.005)
          {
            int fa = 0, fr = 0;
            for (guint i = 0; i < impostor->len; i++)
              if (im[i] >= t)
                fa++;
            for (guint i = 0; i < genuine->len; i++)
              if (g[i] < t)
                fr++;
            double farr = (double) fa / impostor->len, frr = (double) fr / genuine->len;
            if (fabs (farr - frr) < best_eer)
              {
                best_eer = fabs (farr - frr);
                best_t = t;
              }
          }
        {
          int fa = 0;
          for (guint i = 0; i < impostor->len; i++)
            if (im[i] >= best_t)
              fa++;
          printf ("pairwise: EER approx %.1f%% at threshold %.3f\n",
                  100.0 * fa / impostor->len, best_t);
        }
      }
    }

  /* enrolled protocol: template = images of touches 1..E per label, probes
   * = every other image; score = max over template */
  if (enroll_touches > 0)
    {
      g_autoptr(GArray) egen = g_array_new (FALSE, FALSE, sizeof (double));
      g_autoptr(GArray) eimp = g_array_new (FALSE, FALSE, sizeof (double));
      FILE *csv = g_getenv ("EVAL_CSV") ? fopen (g_getenv ("EVAL_CSV"), "w") : NULL;

      if (csv)
        fprintf (csv, "template,probe,touch,frame,genuine,score,peak,sidelobe,dx,dy,rot,overlap\n");

      g_hash_table_iter_init (&it, labels);
      while (g_hash_table_iter_next (&it, &key, NULL))
        {
          const char *lab = key;

          for (guint i = 0; i < imgs->len; i++)
            {
              Img *a = g_ptr_array_index (imgs, i);
              ElanpressProbe *probe;
              double best = -2;
              ElanpressMatchResult br = { 0 };

              if (strcmp (a->label, lab) == 0 && a->touch <= enroll_touches)
                continue;
              probe = elanpress_probe_new (a->img, W, H, p);
              for (guint j = 0; j < imgs->len; j++)
                {
                  Img *b = g_ptr_array_index (imgs, j);
                  ElanpressMatchResult r;
                  double s;

                  if (strcmp (b->label, lab) != 0 || b->touch > enroll_touches)
                    continue;
                  s = elanpress_probe_match (probe, b->img, &r);
                  if (s > best)
                    {
                      best = s;
                      br = r;
                    }
                }
              elanpress_probe_free (probe);
              if (csv)
                fprintf (csv, "%s,%s,%d,%d,%d,%.4f,%.4f,%.4f,%d,%d,%.0f,%d\n", lab, a->label,
                         a->touch, a->frame, strcmp (a->label, lab) == 0, best, br.peak,
                         br.sidelobe, br.dx, br.dy, br.rot, br.overlap);
              if (strcmp (a->label, lab) == 0)
                g_array_append_val (egen, best);
              else
                g_array_append_val (eimp, best);
            }
        }
      if (csv)
        fclose (csv);
      printf ("\nenrolled protocol (template = touches 1..%d, score = max over template):\n",
              enroll_touches);
      print_dist ("genuine", egen);
      print_dist ("impostor", eimp);
      if (eimp->len && egen->len)
        {
          double *g = (double *) egen->data, *im = (double *) eimp->data;
          int above = 0, at_thr = 0, fa_thr = 0;

          far0_thresh = im[eimp->len - 1];
          for (guint i = 0; i < egen->len; i++)
            {
              if (g[i] > far0_thresh)
                above++;
              if (g[i] >= p->threshold)
                at_thr++;
            }
          for (guint i = 0; i < eimp->len; i++)
            if (im[i] >= p->threshold)
              fa_thr++;
          printf ("enrolled: TAR at FAR=0 (threshold just above %.3f): %d/%u (%.1f%%)\n",
                  far0_thresh, above, egen->len, 100.0 * above / egen->len);
          printf ("enrolled: at driver threshold %.3f: TAR %d/%u (%.1f%%), FAR %d/%u\n",
                  p->threshold, at_thr, egen->len, 100.0 * at_thr / egen->len,
                  fa_thr, eimp->len);
        }
    }

  /* leave-one-touch-out: template = every image of the label except the
   * probe's own touch (genuine) or every image of the label (impostor);
   * estimates what a large enrolment achieves */
  if (g_getenv ("EVAL_LOO"))
    {
      g_autoptr(GArray) lgen = g_array_new (FALSE, FALSE, sizeof (double));
      g_autoptr(GArray) limp = g_array_new (FALSE, FALSE, sizeof (double));
      const char *only = g_getenv ("EVAL_LOO_LABEL");

      g_hash_table_iter_init (&it, labels);
      while (g_hash_table_iter_next (&it, &key, NULL))
        {
          const char *lab = key;

          if (only && strcmp (only, lab) != 0)
            continue;
          for (guint i = 0; i < imgs->len; i++)
            {
              Img *a = g_ptr_array_index (imgs, i);
              ElanpressProbe *probe = elanpress_probe_new (a->img, W, H, p);
              double best = -2;

              for (guint j = 0; j < imgs->len; j++)
                {
                  Img *b = g_ptr_array_index (imgs, j);

                  if (strcmp (b->label, lab) != 0)
                    continue;
                  if (strcmp (a->label, lab) == 0 && a->touch == b->touch)
                    continue;
                  best = MAX (best, elanpress_probe_match (probe, b->img, NULL));
                }
              elanpress_probe_free (probe);
              if (strcmp (a->label, lab) == 0)
                g_array_append_val (lgen, best);
              else
                g_array_append_val (limp, best);
            }
        }
      printf ("\nleave-one-touch-out protocol (template = all other touches of the finger):\n");
      print_dist ("genuine", lgen);
      print_dist ("impostor", limp);
      if (limp->len && lgen->len)
        {
          double *g = (double *) lgen->data, *im = (double *) limp->data;
          double t = im[limp->len - 1];
          int above = 0;

          for (guint i = 0; i < lgen->len; i++)
            if (g[i] > t)
              above++;
          printf ("loo: TAR at FAR=0 (threshold just above %.3f): %d/%u (%.1f%%)\n",
                  t, above, lgen->len, 100.0 * above / lgen->len);
          for (double thr = t - 0.10; thr < t; thr += 0.02)
            {
              int fa = 0, ta = 0;
              for (guint i = 0; i < limp->len; i++)
                if (im[i] >= thr)
                  fa++;
              for (guint i = 0; i < lgen->len; i++)
                if (g[i] >= thr)
                  ta++;
              printf ("loo: threshold %.3f: TAR %d/%u (%.1f%%), FAR %d/%u (%.2f%%)\n",
                      thr, ta, lgen->len, 100.0 * ta / lgen->len, fa, limp->len,
                      100.0 * fa / limp->len);
            }
        }
    }

  /* leave-one-touch-out, several score variants at once -> CSV */
  if (g_getenv ("EVAL_VARIANTS"))
    {
      const char *lab = g_getenv ("EVAL_LOO_LABEL") ? g_getenv ("EVAL_LOO_LABEL") : "right-index";
      FILE *csv = fopen (g_getenv ("EVAL_VARIANTS"), "w");

      fprintf (csv, "probe,touch,frame,genuine,peak,bmin,bmean,c25,c50,pk_n\n");
      for (guint i = 0; i < imgs->len; i++)
        {
          Img *a = g_ptr_array_index (imgs, i);
          ElanpressProbe *probe = elanpress_probe_new (a->img, W, H, p);
          double vpeak = -2, vbmin = -2, vbmean = -2, vc25 = -2, vc50 = -2;
          int pk_n = 0;

          for (guint j = 0; j < imgs->len; j++)
            {
              Img *b = g_ptr_array_index (imgs, j);
              ElanpressMatchResult r;

              if (strcmp (b->label, lab) != 0)
                continue;
              if (strcmp (a->label, lab) == 0 && a->touch == b->touch)
                continue;
              elanpress_probe_match (probe, b->img, &r);
              if (r.peak > vpeak)
                {
                  vpeak = r.peak;
                  pk_n = r.overlap;
                }
              vbmin = MAX (vbmin, r.block_min);
              vbmean = MAX (vbmean, r.block_mean);
              vc25 = MAX (vc25, 0.75 * r.peak + 0.25 * r.block_min);
              vc50 = MAX (vc50, 0.5 * r.peak + 0.5 * r.block_min);
            }
          elanpress_probe_free (probe);
          fprintf (csv, "%s,%d,%d,%d,%.4f,%.4f,%.4f,%.4f,%.4f,%d\n", a->label, a->touch, a->frame,
                   strcmp (a->label, lab) == 0, vpeak, vbmin, vbmean, vc25, vc50, pk_n);
          fprintf (stderr, "\r%u/%u", i + 1, imgs->len);
        }
      fclose (csv);
      fprintf (stderr, "\n");
    }

  /* leave-one-touch-out with mosaic templates */
  if (g_getenv ("EVAL_MOSAIC"))
    {
      const char *lab = g_getenv ("EVAL_LOO_LABEL") ? g_getenv ("EVAL_LOO_LABEL") : "right-index";
      g_autoptr(GArray) mgen = g_array_new (FALSE, FALSE, sizeof (double));
      g_autoptr(GArray) mimp = g_array_new (FALSE, FALSE, sizeof (double));
      g_autoptr(GPtrArray) all = g_ptr_array_new ();
      GPtrArray *frags;
      int max_touch = 0;

      for (guint i = 0; i < imgs->len; i++)
        {
          Img *a = g_ptr_array_index (imgs, i);
          if (strcmp (a->label, lab) == 0)
            {
              g_ptr_array_add (all, a->img);
              max_touch = MAX (max_touch, a->touch);
            }
        }

      /* full mosaic: impostors, and a picture of it */
      frags = elanpress_mosaic_build (all, W, H, p, p->reg_thresh, p->max_side);
      printf ("\nmosaic of %u %s images: %u fragments:", all->len, lab, frags->len);
      for (guint f = 0; f < frags->len; f++)
        {
          ElanpressCanvas *c = g_ptr_array_index (frags, f);
          g_autofree char *name = g_strdup_printf ("%s/mosaic-%s-%u.pgm",
                                                   g_getenv ("EVAL_OUT") ? g_getenv ("EVAL_OUT") : ".", lab, f);
          g_autofree guint8 *vis = g_new (guint8, c->w * c->h);

          printf (" %dx%d(%d)", c->w, c->h, c->n_images);
          for (int i = 0; i < c->w * c->h; i++)
            vis[i] = c->mask[i] ? c->px[i] : 0;
          elanpress_pgm_write8 (name, vis, c->w, c->h);
        }
      printf ("\n");
      for (guint i = 0; i < imgs->len; i++)
        {
          Img *a = g_ptr_array_index (imgs, i);
          ElanpressProbe *probe;
          double best = -2;

          if (strcmp (a->label, lab) == 0)
            continue;
          probe = elanpress_probe_new (a->img, W, H, p);
          for (guint f = 0; f < frags->len; f++)
            {
              ElanpressCanvas *c = g_ptr_array_index (frags, f);
              best = MAX (best, elanpress_probe_match_canvas (probe, c->px, c->mask, c->w, c->h, NULL));
            }
          elanpress_probe_free (probe);
          g_array_append_val (mimp, best);
        }
      g_ptr_array_unref (frags);

      /* genuine: one mosaic per held-out touch */
      for (int t = 1; t <= max_touch; t++)
        {
          g_autoptr(GPtrArray) rest = g_ptr_array_new ();
          gboolean any = FALSE;

          for (guint i = 0; i < imgs->len; i++)
            {
              Img *a = g_ptr_array_index (imgs, i);
              if (strcmp (a->label, lab) != 0)
                continue;
              if (a->touch == t)
                any = TRUE;
              else
                g_ptr_array_add (rest, a->img);
            }
          if (!any)
            continue;
          frags = elanpress_mosaic_build (rest, W, H, p, p->reg_thresh, p->max_side);
          for (guint i = 0; i < imgs->len; i++)
            {
              Img *a = g_ptr_array_index (imgs, i);
              ElanpressProbe *probe;
              double best = -2;
              ElanpressMatchResult br = { 0 };

              if (strcmp (a->label, lab) != 0 || a->touch != t)
                continue;
              probe = elanpress_probe_new (a->img, W, H, p);
              for (guint f = 0; f < frags->len; f++)
                {
                  ElanpressCanvas *c = g_ptr_array_index (frags, f);
                  ElanpressMatchResult r;
                  double sc = elanpress_probe_match_canvas (probe, c->px, c->mask, c->w, c->h, &r);
                  if (sc > best)
                    {
                      best = sc;
                      br = r;
                    }
                }
              elanpress_probe_free (probe);
              fprintf (stderr, "genuine t%d f%d: %.3f n %d rot %.0f (%u frags)\n", t, a->frame,
                       best, br.overlap, br.rot, frags->len);
              g_array_append_val (mgen, best);
            }
          g_ptr_array_unref (frags);
        }

      printf ("\nmosaic leave-one-touch-out protocol:\n");
      print_dist ("genuine", mgen);
      print_dist ("impostor", mimp);
      if (mimp->len && mgen->len)
        {
          double *g = (double *) mgen->data, *im = (double *) mimp->data;
          double t = im[mimp->len - 1];
          int above = 0;

          for (guint i = 0; i < mgen->len; i++)
            if (g[i] > t)
              above++;
          printf ("mosaic: TAR at FAR=0 (threshold just above %.3f): %d/%u (%.1f%%)\n",
                  t, above, mgen->len, 100.0 * above / mgen->len);
          for (double thr = t - 0.10; thr < t; thr += 0.02)
            {
              int fa = 0, ta = 0;
              for (guint i = 0; i < mimp->len; i++)
                if (im[i] >= thr)
                  fa++;
              for (guint i = 0; i < mgen->len; i++)
                if (g[i] >= thr)
                  ta++;
              printf ("mosaic: threshold %.3f: TAR %d/%u (%.1f%%), FAR %d/%u (%.2f%%)\n",
                      thr, ta, mgen->len, 100.0 * ta / mgen->len, fa, mimp->len,
                      100.0 * fa / mimp->len);
            }
        }
    }

  return 0;
}

/* synthetic self-test: shifted/rotated crops of one image must match, the
 * mirrored image must not */
static int
cmd_synth (const char *path, const ElanpressMatchParams *p)
{
  Img *a = load (path);
  int fails = 0;

  if (!a)
    return 1;

  struct { int dx, dy; double rot; } cases[] = {
    { 0, 0, 0 }, { 5, 0, 0 }, { 0, 7, 0 }, { -9, 6, 0 }, { 0, 0, 8 },
    { 4, -3, -12 }, { 12, 15, 0 }, { -15, 20, 6 },
  };

  for (guint c = 0; c < G_N_ELEMENTS (cases); c++)
    {
      guint8 *b = g_new (guint8, W * H);
      double th = cases[c].rot * M_PI / 180, cs = cos (th), sn = sin (th);
      double cx = (W - 1) / 2.0, cy = (H - 1) / 2.0;
      ElanpressMatchResult r;
      double s;

      /* b(x, y) = a(R(x - dx, y - dy)) : template shifted by (dx,dy) */
      for (int y = 0; y < H; y++)
        for (int x = 0; x < W; x++)
          {
            double ux = x - cases[c].dx - cx, uy = y - cases[c].dy - cy;
            double sx = cs * ux - sn * uy + cx, sy = sn * ux + cs * uy + cy;
            int xi = (int) lrint (sx), yi = (int) lrint (sy);
            b[y * W + x] = (xi >= 0 && yi >= 0 && xi < W && yi < H) ?
                           a->img[yi * W + xi] : 128;
          }
      s = elanpress_match (a->img, b, W, H, p, &r);
      printf ("shift (%3d,%3d) rot %4.0f -> score %.3f found dx %3d dy %3d rot %4.0f overlap %d %s\n",
              cases[c].dx, cases[c].dy, cases[c].rot, s, r.dx, r.dy, r.rot,
              r.overlap, s >= p->threshold ? "MATCH" : "no match");
      if (s < p->threshold)
        fails++;
      g_free (b);
    }

  {
    guint8 *m = g_new (guint8, W * H);
    ElanpressMatchResult r;
    double s;

    for (int y = 0; y < H; y++)
      for (int x = 0; x < W; x++)
        m[y * W + x] = a->img[y * W + (W - 1 - x)];
    s = elanpress_match (a->img, m, W, H, p, &r);
    printf ("mirrored impostor          -> score %.3f %s\n", s,
            s >= p->threshold ? "FALSE ACCEPT" : "rejected");
    if (s >= p->threshold)
      fails++;
    g_free (m);
  }
  return fails ? 1 : 0;
}

int
main (int argc, char **argv)
{
  ElanpressMatchParams p;

  elanpress_match_params_from_env (&p);

  if (argc >= 4 && strcmp (argv[1], "pair") == 0)
    return cmd_pair (argv[2], argv[3], &p);
  if (argc >= 5 && strcmp (argv[1], "reprocess") == 0)
    return cmd_reprocess (argv[2], argv[3], argv[4], &p);
  if (argc >= 3 && strcmp (argv[1], "pool") == 0)
    return cmd_pool (argv[2], argc >= 4 ? atoi (argv[3]) : 0, &p);
  if (argc >= 3 && strcmp (argv[1], "synth") == 0)
    return cmd_synth (argv[2], &p);

  fprintf (stderr,
           "usage: %s pair A.pgm B.pgm\n"
           "       %s reprocess BG16.pgm RAW16.pgm OUT8.pgm\n"
           "       %s pool DIR [ENROLL_TOUCHES]\n"
           "       %s synth IMG8.pgm\n"
           "env: FP_ELANPRESS_THRESHOLD ROT_RANGE ROT_STEP MIN_OVERLAP NORM_WIN CANDIDATES\n",
           argv[0], argv[0], argv[0], argv[0]);
  return 2;
}
