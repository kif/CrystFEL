/*
 * predict-refine.c
 *
 * Prediction refinement
 *
 * Copyright © 2012-2015 Deutsches Elektronen-Synchrotron DESY,
 *                       a research centre of the Helmholtz Association.
 *
 * Authors:
 *   2010-2015 Thomas White <taw@physics.org>
 *
 * This file is part of CrystFEL.
 *
 * CrystFEL is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * CrystFEL is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with CrystFEL.  If not, see <http://www.gnu.org/licenses/>.
 *
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif


#include <stdlib.h>
#include <assert.h>
#include <gsl/gsl_matrix.h>
#include <gsl/gsl_vector.h>

#include "image.h"
#include "geometry.h"
#include "cell-utils.h"


/* Maximum number of iterations of NLSq to do for each image per macrocycle. */
#define MAX_CYCLES (10)

/* Weighting of excitation error term (m^-1) compared to position term (m) */
#define EXC_WEIGHT (4e-20)

struct reflpeak {
	Reflection *refl;
	struct imagefeature *peak;
	double Ih;   /* normalised */
	struct panel *panel;  /* panel the reflection appears on
                               * (we assume this never changes) */
};


static void twod_mapping(double fs, double ss, double *px, double *py,
                         struct panel *p)
{
	double xs, ys;

	xs = fs*p->fsx + ss*p->ssx;
	ys = fs*p->fsy + ss*p->ssy;

	*px = (xs + p->cnx) / p->res;
	*py = (ys + p->cny) / p->res;
}


static double r_dev(struct reflpeak *rp)
{
	/* Excitation error term */
	double rlow, rhigh, p;
	get_partial(rp->refl, &rlow, &rhigh, &p);
	return (rlow+rhigh)/2.0;
}


static double x_dev(struct reflpeak *rp, struct detector *det)
{
	/* Peak position term */
	double xpk, ypk, xh, yh;
	double fsh, ssh;
	twod_mapping(rp->peak->fs, rp->peak->ss, &xpk, &ypk, rp->panel);
	get_detector_pos(rp->refl, &fsh, &ssh);
	twod_mapping(fsh, ssh, &xh, &yh, rp->panel);
	return xh-xpk;
}


static double y_dev(struct reflpeak *rp, struct detector *det)
{
	/* Peak position term */
	double xpk, ypk, xh, yh;
	double fsh, ssh;
	twod_mapping(rp->peak->fs, rp->peak->ss, &xpk, &ypk, rp->panel);
	get_detector_pos(rp->refl, &fsh, &ssh);
	twod_mapping(fsh, ssh, &xh, &yh, rp->panel);
	return yh-ypk;
}


/* Associate a Reflection with each peak in "image" which is close to Bragg.
 * Reflections will be added to "reflist", which can be NULL if this is not
 * needed.  "rps" must be an array of sufficient size for all the peaks */
static int pair_peaks(struct image *image, Crystal *cr,
                      RefList *reflist, struct reflpeak *rps)
{
	int i;
	const double min_dist = 0.05;
	int n_acc = 0;

	for ( i=0; i<image_feature_count(image->features); i++ ) {

		struct imagefeature *f;
		double h, k, l, hd, kd, ld;

		/* Assume all image "features" are genuine peaks */
		f = image_get_feature(image->features, i);
		if ( f == NULL ) continue;

		double ax, ay, az;
		double bx, by, bz;
		double cx, cy, cz;

		cell_get_cartesian(crystal_get_cell(cr),
		                   &ax, &ay, &az, &bx, &by, &bz, &cx, &cy, &cz);

		/* Decimal and fractional Miller indices of nearest
		 * reciprocal lattice point */
		hd = f->rx * ax + f->ry * ay + f->rz * az;
		kd = f->rx * bx + f->ry * by + f->rz * bz;
		ld = f->rx * cx + f->ry * cy + f->rz * cz;
		h = lrint(hd);
		k = lrint(kd);
		l = lrint(ld);

		/* Check distance */
		if ( (fabs(h - hd) < min_dist)
		  && (fabs(k - kd) < min_dist)
		  && (fabs(l - ld) < min_dist) )
		{
			Reflection *refl;
			struct panel *p;

			refl = reflection_new(h, k, l);
			if ( refl == NULL ) {
				ERROR("Failed to create reflection\n");
				return 0;
			}

			if ( reflist != NULL ) add_refl_to_list(refl, reflist);
			set_symmetric_indices(refl, h, k, l);

			/* It doesn't matter if the actual predicted location
			 * doesn't fall on this panel.  We're only interested
			 * in how far away it is from the peak location.
			 * The predicted position and excitation errors will be
			 * filled in by update_partialities(). */
			p = find_panel(image->det, f->fs, f->ss);
			set_panel(refl, p);

			rps[n_acc].refl = refl;
			rps[n_acc].peak = f;
			rps[n_acc].panel = p;
			n_acc++;
		}

	}

	return n_acc;
}


static int cmpd2(const void *av, const void *bv)
{
	struct reflpeak *a, *b;

	a = (struct reflpeak *)av;
	b = (struct reflpeak *)bv;

	if ( fabs(r_dev(a)) < fabs(r_dev(b)) ) return -1;
	return 1;
}


void refine_radius(Crystal *cr, struct image *image)
{
	int n, n_acc;
	struct reflpeak *rps;
	RefList *reflist;

	/* Maximum possible size */
	rps = malloc(image_feature_count(image->features)
	                  * sizeof(struct reflpeak));
	if ( rps == NULL ) return;

	reflist = reflist_new();
	n_acc = pair_peaks(image, cr, reflist, rps);
	if ( n_acc < 3 ) {
		ERROR("Too few paired peaks (%i) to determine radius\n", n_acc);
		free(rps);
		return;
	}
	crystal_set_reflections(cr, reflist);
	update_partialities(cr, PMODEL_SCSPHERE);
	crystal_set_reflections(cr, NULL);

	qsort(rps, n_acc, sizeof(struct reflpeak), cmpd2);
	n = (n_acc-1) - n_acc/50;
	if ( n < 2 ) n = 2; /* n_acc is always >= 2 */
	crystal_set_profile_radius(cr, fabs(r_dev(&rps[n])));

	reflist_free(reflist);
	free(rps);
}


/* Returns d(xh-xpk)/dP + d(yh-ypk)/dP, where P = any parameter */
static double x_gradient(int param, struct reflpeak *rp, struct detector *det,
                         double lambda, UnitCell *cell)
{
	signed int h, k, l;
	double xpk, ypk, xh, yh;
	double fsh, ssh;
	double tt, clen, azi, azf;

	twod_mapping(rp->peak->fs, rp->peak->ss, &xpk, &ypk, rp->panel);
	get_detector_pos(rp->refl, &fsh, &ssh);
	twod_mapping(fsh, ssh, &xh, &yh, rp->panel);
	get_indices(rp->refl, &h, &k, &l);

	tt = asin(lambda * resolution(cell, h, k, l));
	clen = rp->panel->clen;
	azi = atan2(yh, xh);
	azf = cos(azi);

	switch ( param ) {

		case GPARAM_ASX :
		return h * lambda * clen / cos(tt);

		case GPARAM_BSX :
		return k * lambda * clen / cos(tt);

		case GPARAM_CSX :
		return l * lambda * clen / cos(tt);

		case GPARAM_ASY :
		return 0.0;

		case GPARAM_BSY :
		return 0.0;

		case GPARAM_CSY :
		return 0.0;

		case GPARAM_ASZ :
		return -h * lambda * clen * 2*azf * sin(tt) / (cos(tt)*cos(tt));

		case GPARAM_BSZ :
		return -k * lambda * clen * 2*azf * sin(tt) / (cos(tt)*cos(tt));

		case GPARAM_CSZ :
		return -l * lambda * clen * 2*azf * sin(tt) / (cos(tt)*cos(tt));

		case GPARAM_DETX :
		return -1;

		case GPARAM_DETY :
		return 0;

		case GPARAM_CLEN :
		return azf * cos(tt);

	}

	ERROR("Positional gradient requested for parameter %i?\n", param);
	abort();
}


/* Returns d(yh-ypk)/dP, where P = any parameter */
static double y_gradient(int param, struct reflpeak *rp, struct detector *det,
                         double lambda, UnitCell *cell)
{
	signed int h, k, l;
	double xpk, ypk, xh, yh;
	double fsh, ssh;
	double tt, clen, azi, azf;

	twod_mapping(rp->peak->fs, rp->peak->ss, &xpk, &ypk, rp->panel);
	get_detector_pos(rp->refl, &fsh, &ssh);
	twod_mapping(fsh, ssh, &xh, &yh, rp->panel);
	get_indices(rp->refl, &h, &k, &l);

	tt = asin(lambda * resolution(cell, h, k, l));
	clen = rp->panel->clen;
	azi = atan2(yh, xh);
	azf = sin(azi);

	switch ( param ) {

		case GPARAM_ASX :
		return 0.0;

		case GPARAM_BSX :
		return 0.0;

		case GPARAM_CSX :
		return 0.0;

		case GPARAM_ASY :
		return h * lambda * clen / cos(tt);

		case GPARAM_BSY :
		return k * lambda * clen / cos(tt);

		case GPARAM_CSY :
		return l * lambda * clen / cos(tt);

		case GPARAM_ASZ :
		return -h * lambda * clen * 2*azf * sin(tt) / (cos(tt)*cos(tt));

		case GPARAM_BSZ :
		return -k * lambda * clen * 2*azf * sin(tt) / (cos(tt)*cos(tt));

		case GPARAM_CSZ :
		return -l * lambda * clen * 2*azf * sin(tt) / (cos(tt)*cos(tt));

		case GPARAM_DETX :
		return 0;

		case GPARAM_DETY :
		return -1;

		case GPARAM_CLEN :
		return azf * cos(tt);
	}

	ERROR("Positional gradient requested for parameter %i?\n", param);
	abort();
}


static int iterate(struct reflpeak *rps, int n, UnitCell *cell,
                   struct image *image)
{
	int i;
	gsl_matrix *M;
	gsl_vector *v;
	gsl_vector *shifts;
	double asx, asy, asz;
	double bsx, bsy, bsz;
	double csx, csy, csz;

	/* Number of parameters to refine */
	M = gsl_matrix_calloc(9, 9);
	v = gsl_vector_calloc(9);

	for ( i=0; i<n; i++ ) {

		int k;
		double gradients[9];
		double w;

		/* Excitation error terms */
		w = EXC_WEIGHT * rps[i].Ih;

		for ( k=0; k<9; k++ ) {
			gradients[k] = r_gradient(cell, k, rps[i].refl, image);
		}

		for ( k=0; k<9; k++ ) {

			int g;
			double v_c, v_curr;

			for ( g=0; g<9; g++ ) {

				double M_c, M_curr;

				/* Matrix is symmetric */
				if ( g > k ) continue;

				M_c = w * gradients[g] * gradients[k];
				M_curr = gsl_matrix_get(M, k, g);
				gsl_matrix_set(M, k, g, M_curr + M_c);
				gsl_matrix_set(M, g, k, M_curr + M_c);

			}

			v_c = w * r_dev(&rps[i]);
			v_c *= -gradients[k];
			v_curr = gsl_vector_get(v, k);
			gsl_vector_set(v, k, v_curr + v_c);

		}

		/* Positional x terms */
		for ( k=0; k<9; k++ ) {
			gradients[k] = x_gradient(k, &rps[i], image->det,
			                          image->lambda, cell);
		}

		for ( k=0; k<9; k++ ) {

			int g;
			double v_c, v_curr;

			for ( g=0; g<9; g++ ) {

				double M_c, M_curr;

				/* Matrix is symmetric */
				if ( g > k ) continue;

				M_c = gradients[g] * gradients[k];
				M_curr = gsl_matrix_get(M, k, g);
				gsl_matrix_set(M, k, g, M_curr + M_c);
				gsl_matrix_set(M, g, k, M_curr + M_c);

			}

			v_c = x_dev(&rps[i], image->det);
			v_c *= -gradients[k];
			v_curr = gsl_vector_get(v, k);
			gsl_vector_set(v, k, v_curr + v_c);

		}

		/* Positional y terms */
		for ( k=0; k<9; k++ ) {
			gradients[k] = y_gradient(k, &rps[i], image->det,
			                          image->lambda, cell);
		}

		for ( k=0; k<9; k++ ) {

			int g;
			double v_c, v_curr;

			for ( g=0; g<9; g++ ) {

				double M_c, M_curr;

				/* Matrix is symmetric */
				if ( g > k ) continue;

				M_c = gradients[g] * gradients[k];
				M_curr = gsl_matrix_get(M, k, g);
				gsl_matrix_set(M, k, g, M_curr + M_c);
				gsl_matrix_set(M, g, k, M_curr + M_c);

			}

			v_c = y_dev(&rps[i], image->det);
			v_c *= -gradients[k];
			v_curr = gsl_vector_get(v, k);
			gsl_vector_set(v, k, v_curr + v_c);

		}

	}

	//show_matrix_eqn(M, v);
	shifts = solve_svd(v, M, NULL, 0);
	if ( shifts == NULL ) {
		ERROR("Failed to solve equations.\n");
		gsl_matrix_free(M);
		gsl_vector_free(v);
		return 1;
	}

	for ( i=0; i<9; i++ ) {
	//	STATUS("Shift %i = %e\n", i, gsl_vector_get(shifts, i));
	}

	/* Apply shifts */
	cell_get_reciprocal(cell, &asx, &asy, &asz,
	                          &bsx, &bsy, &bsz,
	                          &csx, &csy, &csz);
	asx += gsl_vector_get(shifts, 0);
	asy += gsl_vector_get(shifts, 1);
	asz += gsl_vector_get(shifts, 2);
	bsx += gsl_vector_get(shifts, 3);
	bsy += gsl_vector_get(shifts, 4);
	bsz += gsl_vector_get(shifts, 5);
	csx += gsl_vector_get(shifts, 6);
	csy += gsl_vector_get(shifts, 7);
	csz += gsl_vector_get(shifts, 8);
	cell_set_reciprocal(cell, asx, asy, asz, bsx, bsy, bsz, csx, csy, csz);

	gsl_vector_free(shifts);
	gsl_matrix_free(M);
	gsl_vector_free(v);

	return 0;
}


static double residual(struct reflpeak *rps, int n, struct detector *det)
{
	int i;
	double res = 0.0;
	double r;

	r = 0.0;
	for ( i=0; i<n; i++ ) {
		r += EXC_WEIGHT * rps[i].Ih * pow(r_dev(&rps[i]), 2.0);
	}
	printf("%e ", r);
	res += r;

	r = 0.0;
	for ( i=0; i<n; i++ ) {
		r += pow(x_dev(&rps[i], det), 2.0);
	}
	printf("%e ", r);
	res += r;

	r = 0.0;
	for ( i=0; i<n; i++ ) {
		r += pow(y_dev(&rps[i], det), 2.0);
	}
	printf("%e ", r);
	res += r;

	return res;
}


int refine_prediction(struct image *image, Crystal *cr)
{
	int n;
	int i;
	struct reflpeak *rps;
	double max_I;
	RefList *reflist;

	rps = malloc(image_feature_count(image->features)
	                       * sizeof(struct reflpeak));
	if ( rps == NULL ) return 1;

	reflist = reflist_new();
	n = pair_peaks(image, cr, reflist, rps);
	if ( n < 10 ) {
		ERROR("Too few paired peaks (%i) to refine orientation.\n", n);
		free(rps);
		reflist_free(reflist);
		return 1;
	}
	crystal_set_reflections(cr, reflist);

	/* Normalise the intensities to max 1 */
	max_I = -INFINITY;
	for ( i=0; i<n; i++ ) {
		double cur_I = rps[i].peak->intensity;
		if ( cur_I > max_I ) max_I = cur_I;
	}
	if ( max_I <= 0.0 ) {
		ERROR("All peaks negative?\n");
		free(rps);
		return 1;
	}
	for ( i=0; i<n; i++ ) {
		rps[i].Ih = rps[i].peak->intensity / max_I;
	}

	/* Refine */
	for ( i=0; i<MAX_CYCLES; i++ ) {
		update_partialities(cr, PMODEL_SCSPHERE);
		if ( iterate(rps, n, crystal_get_cell(cr), image) ) return 1;
	}

	crystal_set_reflections(cr, NULL);
	reflist_free(reflist);
	free(rps);
	return 0;
}
