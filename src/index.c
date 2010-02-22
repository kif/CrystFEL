/*
 * index.c
 *
 * Perform indexing (somehow)
 *
 * (c) 2006-2010 Thomas White <taw@physics.org>
 *
 * Part of CrystFEL - crystallography with a FEL
 *
 */


#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <stdlib.h>
#include <stdio.h>
#include <math.h>
#include <string.h>
#include <assert.h>

#include "image.h"
#include "utils.h"
#include "peaks.h"
#include "dirax.h"
#include "sfac.h"
#include "detector.h"
#include "index.h"


/* x,y in pixels relative to central beam */
int map_position(struct image *image, double dx, double dy,
                 double *rx, double *ry, double *rz)
{
	double d;
	double twotheta, psi;
	const double k = 1.0 / image->lambda;
	struct panel *p;
	double x = 0.0;
	double y = 0.0;

	p = find_panel(&image->det, dx, dy);

	x = ((double)dx - p->cx);
	y = ((double)dy - p->cy);

	/* Convert pixels to metres */
	x /= p->res;
	y /= p->res;	/* Convert pixels to metres */
	d = sqrt((x*x) + (y*y));
	twotheta = atan2(d, p->clen);

	psi = atan2(y, x);

	*rx = k*sin(twotheta)*cos(psi);
	*ry = k*sin(twotheta)*sin(psi);
	*rz = k - k*cos(twotheta);

	return 0;
}


static void write_drx(struct image *image)
{
	FILE *fh;
	int i;

	STATUS("Writing xfel.drx file.  Remember that it uses units of "
	       "reciprocal Angstroms!\n");

	fh = fopen("xfel.drx", "w");
	if ( !fh ) {
		ERROR("Couldn't open temporary file xfel.drx\n");
		return;
	}
	fprintf(fh, "%f\n", 0.5);  /* Lie about the wavelength.  */

	for ( i=0; i<image_feature_count(image->features); i++ ) {

		struct imagefeature *f;

		f = image_get_feature(image->features, i);
		if ( f == NULL ) continue;

		fprintf(fh, "%10f %10f %10f %8f\n",
		        f->rx/1e10, f->ry/1e10, f->rz/1e10, 1.0);

	}
	fclose(fh);
}


void index_pattern(struct image *image, IndexingMethod indm, int no_match)
{
	int i;
	UnitCell *new_cell = NULL;
	int nc = 0;

	/* Map positions to 3D */
	for ( i=0; i<image_feature_count(image->features); i++ ) {

		struct imagefeature *f;
		int c;

		f = image_get_feature(image->features, i);
		if ( f == NULL ) continue;

		c = map_position(image, f->x, f->y, &f->rx, &f->ry, &f->rz);
		if ( c != 0 ) nc++;

	}
	if ( nc ) {
		ERROR("Failed to map %i reflections\n", nc);
	}

	write_drx(image);

	/* Index (or not) as appropriate */
	if ( indm == INDEXING_NONE ) return;
	if ( indm == INDEXING_DIRAX ) run_dirax(image);

	if ( image->indexed_cell == NULL ) {
		STATUS("No cell found.\n");
		return;
	} else {
		STATUS("--------------------\n");
		STATUS("The indexed cell (before matching):\n");
		cell_print(image->indexed_cell);
		STATUS("--------------------\n");
	}

	if ( !no_match ) {
		new_cell = match_cell(image->indexed_cell,
			              image->molecule->cell);
		free(image->indexed_cell);
		image->indexed_cell = new_cell;
	}
}
