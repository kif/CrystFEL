/*
 * templates.c
 *
 * Indexing by template matching
 *
 * (c) 2006-2010 Thomas White <taw@physics.org>
 *
 * Part of CrystFEL - crystallography with a FEL
 *
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif


#include "index.h"
#include "index-priv.h"
#include "symmetry.h"
#include "utils.h"
#include "geometry.h"
#include "hdf5-file.h"
#include "peaks.h"
#include "reflist.h"

#include <assert.h>


#define INTEGRATION_SQUARE_SIDE (10)
#define THRESHOLD (500)


/* Private data for template indexing */
struct _indexingprivate_template
{
	struct _indexingprivate base;
	UnitCell *cell;
	int n_templates;
	struct template *templates;
};


struct template {
	double omega;
	double phi;
	RefList *spots;
};


UnitCell *rotate_cell(UnitCell *in, double omega, double phi, double rot)
{
	UnitCell *out;
	double asx, asy, asz;
	double bsx, bsy, bsz;
	double csx, csy, csz;
	double xnew, ynew, znew;

	cell_get_reciprocal(in, &asx, &asy, &asz, &bsx, &bsy,
	                        &bsz, &csx, &csy, &csz);

	/* Rotate by "omega" about +z (parallel to c* and c unless triclinic) */
	xnew = asx*cos(omega) + asy*sin(omega);
	ynew = -asx*sin(omega) + asy*cos(omega);
	znew = asz;
	asx = xnew;  asy = ynew;  asz = znew;
	xnew = bsx*cos(omega) + bsy*sin(omega);
	ynew = -bsx*sin(omega) + bsy*cos(omega);
	znew = bsz;
	bsx = xnew;  bsy = ynew;  bsz = znew;
	xnew = csx*cos(omega) + csy*sin(omega);
	ynew = -csx*sin(omega) + csy*cos(omega);
	znew = csz;
	csx = xnew;  csy = ynew;  csz = znew;

	/* Rotate by "phi" about +x (not parallel to anything specific) */
	xnew = asx;
	ynew = asy*cos(phi) + asz*sin(phi);
	znew = -asy*sin(phi) + asz*cos(phi);
	asx = xnew;  asy = ynew;  asz = znew;
	xnew = bsx;
	ynew = bsy*cos(phi) + bsz*sin(phi);
	znew = -bsy*sin(phi) + bsz*cos(phi);
	bsx = xnew;  bsy = ynew;  bsz = znew;
	xnew = csx;
	ynew = csy*cos(phi) + csz*sin(phi);
	znew = -csy*sin(phi) + csz*cos(phi);
	csx = xnew;  csy = ynew;  csz = znew;

	/* Rotate by "rot" about the new +z (in-plane rotation) */
	xnew = asx*cos(rot) + asy*sin(rot);
	ynew = -asx*sin(rot) + asy*cos(rot);
	znew = asz;
	asx = xnew;  asy = ynew;  asz = znew;
	xnew = bsx*cos(rot) + bsy*sin(rot);
	ynew = -bsx*sin(rot) + bsy*cos(rot);
	znew = bsz;
	bsx = xnew;  bsy = ynew;  bsz = znew;
	xnew = csx*cos(rot) + csy*sin(rot);
	ynew = -csx*sin(rot) + csy*cos(rot);
	znew = csz;
	csx = xnew;  csy = ynew;  csz = znew;

	out = cell_new_from_cell(in);
	cell_set_reciprocal(out, asx, asy, asz, bsx, bsy, bsz, csx, csy, csz);

	return out;
}


/* Generate templates for the given cell using a representative image */
IndexingPrivate *generate_templates(UnitCell *cell, const char *filename,
                                    struct detector *det,
                                    double nominal_photon_energy)
{
	struct _indexingprivate_template *priv;
	const char *sym;
	double omega_max, phi_max;
	int n_templates;
	const double omega_step = deg2rad(0.5);
	const double phi_step = deg2rad(0.5);
	double omega, phi;
	struct image image;
	struct hdfile *hdfile;
	int i;

	hdfile = hdfile_open(filename);
	if ( hdfile == NULL ) {
		return NULL;
	} else if ( hdfile_set_image(hdfile, "/data/data0") ) {
		ERROR("Couldn't select path\n");
		return NULL;
	}
	hdf5_read(hdfile, &image, 0, nominal_photon_energy);
	hdfile_close(hdfile);
	image.det = det;

	priv = calloc(1, sizeof(struct _indexingprivate_template));
	priv->base.indm = INDEXING_TEMPLATE;

	sym = cell_get_pointgroup(cell);

	/* These define the orientation in space */
	if ( is_polyhedral(sym) ) {
		ERROR("WARNING: Point group is polyhedral.\n");
		ERROR("This means I can't properly determine the orientation");
		ERROR(" ranges for template matching.  Expect trouble.\n");
	}
	omega_max = 2.0*M_PI / rotational_order(sym);
	if ( has_bisecting_mirror_or_diad(sym) ) omega_max /= 2.0;
	phi_max = M_PI;
	if ( has_perpendicular_mirror(sym) ) phi_max /= 2.0;

	/* One more axis would define the rotation in the plane of the image */

	STATUS("Orientation ranges in %s: %.0f-%.0f, %.0f-%.0f deg.\n",
	       sym, 0.0, rad2deg(omega_max), 0.0, rad2deg(phi_max));

	n_templates = (omega_max * phi_max)/(omega_step * phi_step);
	STATUS("%i templates to be calculated.\n", n_templates);

	priv->templates = malloc(n_templates * sizeof(struct template));

	i = 0;
	for ( omega = 0.0; omega < omega_max-omega_step; omega += omega_step ) {
	for ( phi = 0.0; phi < phi_max-phi_step; phi += phi_step ) {

		RefList *reflections;
		UnitCell *cell_rot;

		assert(i < n_templates);

		cell_rot = rotate_cell(cell, omega, phi, 0.0);

		reflections = find_intersections(&image, cell_rot, 0);
		if ( reflections == NULL ) {
			ERROR("Template calculation failed.\n");
			return NULL;
		}

		priv->templates[i].omega = omega;
		priv->templates[i].phi = phi;
		priv->templates[i].spots = reflections;
		i++;

		free(cell_rot);

	}
	progress_bar(omega*1000.0, (omega_max-2.0*omega_step)*1000.0,
	             "Generating templates");
	}

	priv->n_templates = n_templates;
	priv->cell = cell_new_from_cell(cell);

	free(image.data);
	free(image.flags);

	return (struct _indexingprivate *)priv;
}


static int fast_integrate_peak(struct image *image, int xp, int yp)
{
	int x, y;
	double total = 0;
	int r = INTEGRATION_SQUARE_SIDE;

	for ( x=xp-r; x<=xp+r; x++ ) {
	for ( y=yp-r; y<=yp+r; y++ ) {

		int val;

		if ( (x>=image->width) || (x<0) ) continue;
		if ( (y>=image->height) || (y<0) ) continue;

		val = image->data[x+image->width*y];

		if ( val < THRESHOLD ) continue;
		total += val;

	}
	}

	return total;
}


static double integrate_all_rot(struct image *image, RefList *reflections,
                                double rot)
{
	double itot = 0.0;
	double cosr, sinr;
	int num_int = 0;
	Reflection *refl;
	RefListIterator *iter;

	cosr = cos(rot);
	sinr = sin(rot);

	for ( refl = first_refl(reflections, &iter);
	      refl != NULL;
	      refl = next_refl(refl, iter) ) {

		float xp, yp;
		double x, y;

		get_detector_pos(refl, &x, &y);

		xp =  cosr*x + sinr*y;
		yp = -sinr*x + cosr*y;

		itot += fast_integrate_peak(image, rint(xp), rint(yp));
		num_int++;

	}

	return itot / num_int;
}


/* Return the mean of the distances between peaks in the image and peaks from
 * the given template. */
static double mean_distance(struct image *image, RefList *reflections,
                            double rot)
{
	double dtot = 0.0;
	double cosr, sinr;
	int num_dist = 0;
	Reflection *refl;
	RefListIterator *iter;

	cosr = cos(rot);
	sinr = sin(rot);

	/* For each template peak */
	for ( refl = first_refl(reflections, &iter);
	      refl != NULL;
	      refl = next_refl(refl, iter) ) {

		float xp, yp;
		int j;
		double min_dsq;
		double x, y;

		get_detector_pos(refl, &x, &y);

		xp =  cosr*x + sinr*y;
		yp = -sinr*x + cosr*y;

		/* Compare to every real peak */
		min_dsq = +INFINITY;
		for ( j=0; j<image_feature_count(image->features); j++ ) {

			struct imagefeature *f;
			double this_dsq;

			f = image_get_feature(image->features, j);

			this_dsq = pow(f->x - xp, 2.0) + pow(f->y - yp, 2.0);
			if ( this_dsq < min_dsq ) min_dsq = this_dsq;

		}

		if ( min_dsq < pow(50.0, 2.0) ) {
			dtot += sqrt(min_dsq);
			num_dist++;
		}

	}

	return dtot / num_dist;
}


void match_templates(struct image *image, IndexingPrivate *ipriv)
{
	struct _indexingprivate_template *priv
	        = (struct _indexingprivate_template *)ipriv;
	int i, max_i;
	double max, rot, rot_max, rot_step, rot_best;
	const int peaks = 0;

	if ( peaks ) {
		max = INFINITY;
	} else {
		max = 0.0;
	}
	max_i = 0;
	rot_max = 2.0*M_PI;
	rot_step = 2.0*M_PI / 360.0;  /* 1 deg steps */
	rot_best = 0.0;

	for ( i=0; i<priv->n_templates; i++ ) {
	for ( rot=0.0; rot<rot_max; rot+=rot_step ) {

		double val;
		int best;

		if ( !peaks ) {
			val = integrate_all_rot(image, priv->templates[i].spots,
			                        rot);
			best = val > max;
		} else {
			val = mean_distance(image, priv->templates[i].spots,
			                    rot);
			best = val < max;
		}

		if ( best ) {
			max = val;
			max_i = i;
			rot_best = rot;
		}

	}
	progress_bar(i, priv->n_templates-1, "Indexing");
	}

	STATUS("%i (%.2f, %.2f, %.2f): %.2f\n", max_i,
	                                  rad2deg(priv->templates[max_i].omega),
	                                  rad2deg(priv->templates[max_i].phi),
	                                  rad2deg(rot_best), max);

	image->ncells = 1;
	image->candidate_cells[0] = rotate_cell(priv->cell,
	                                        priv->templates[max_i].omega,
	                                        priv->templates[max_i].phi,
	                                        rot_best);
}


void free_templates(IndexingPrivate *priv)
{
	int i;
	struct _indexingprivate_template *tpriv
	        = (struct _indexingprivate_template *)priv;

	for ( i=0; i<tpriv->n_templates; i++ ) {
		free(tpriv->templates[i].spots);
	}

	free(tpriv->templates);
	cell_free(tpriv->cell);
	free(tpriv);
}
