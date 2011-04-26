/*
 * partial_sim.c
 *
 * Generate partials for testing scaling
 *
 * (c) 2006-2011 Thomas White <taw@physics.org>
 *
 * Part of CrystFEL - crystallography with a FEL
 *
 */


#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <stdarg.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <getopt.h>
#include <assert.h>

#include "utils.h"
#include "reflist-utils.h"
#include "symmetry.h"
#include "beam-parameters.h"
#include "detector.h"
#include "geometry.h"
#include "stream.h"



static void mess_up_cell(UnitCell *cell)
{
	double ax, ay, az;
	double bx, by, bz;
	double cx, cy, cz;

	cell_get_cartesian(cell, &ax, &ay, &az, &bx, &by, &bz, &cx, &cy, &cz);
	ax += 0.008*ax;
	cell_set_cartesian(cell, ax, ay, az, bx, by, bz, cx, cy, cz);
}


/* For each reflection in "partial", fill in what the intensity would be
 * according to "full" */
static void calculate_partials(RefList *partial, double osf,
                               RefList *full, const char *sym)
{
	Reflection *refl;
	RefListIterator *iter;

	for ( refl = first_refl(partial, &iter);
	      refl != NULL;
	      refl = next_refl(refl, iter) ) {

		signed int h, k, l;
		Reflection *rfull;
		double p;
		double Ip;

		get_indices(refl, &h, &k, &l);
		get_asymm(h, k, l, &h, &k, &l, sym);
		p = get_partiality(refl);

		rfull = find_refl(full, h, k, l);
		if ( rfull == NULL ) {
			set_redundancy(refl, 0);
		} else {
			Ip = osf * p * get_intensity(rfull);
			set_int(refl, Ip);
		}

	}
}


static void show_help(const char *s)
{
	printf("Syntax: %s [options]\n\n", s);
	printf(
"Generate a stream containing partials from a reflection list.\n"
"\n"
" -h, --help              Display this help message.\n"
"\n"
"You need to provide the following basic options:\n"
" -i, --input=<file>      Read reflections from <file>.\n"
" -o, --output=<file>     Write partials in stream format to <file>.\n"
" -g. --geometry=<file>   Get detector geometry from file.\n"
" -b, --beam=<file>       Get beam parameters from file\n"
" -p, --pdb=<file>        PDB file from which to get the unit cell.\n"
"\n"
" -y, --symmetry=<sym>    Symmetry of the input reflection list.\n"
);
}


int main(int argc, char *argv[])
{
	int c;
	char *input_file = NULL;
	char *output_file = NULL;
	char *beamfile = NULL;
	char *geomfile = NULL;
	char *cellfile = NULL;
	struct detector *det = NULL;
	struct beam_params *beam = NULL;
	RefList *full;
	char *sym = NULL;
	UnitCell *cell = NULL;
	struct quaternion orientation;
	struct image image;
	FILE *ofh;
	UnitCell *new;

	/* Long options */
	const struct option longopts[] = {
		{"help",               0, NULL,               'h'},
		{"output",             1, NULL,               'o'},
		{"input",              1, NULL,               'i'},
		{"beam",               1, NULL,               'b'},
		{"pdb",                1, NULL,               'p'},
		{"geometry",           1, NULL,               'g'},
		{"symmetry",           1, NULL,               'y'},
		{0, 0, NULL, 0}
	};

	/* Short options */
	while ((c = getopt_long(argc, argv, "hi:o:b:p:g:y:",
	                        longopts, NULL)) != -1) {

		switch (c) {
		case 'h' :
			show_help(argv[0]);
			return 0;

		case 'o' :
			output_file = strdup(optarg);
			break;

		case 'i' :
			input_file = strdup(optarg);
			break;

		case 'b' :
			beamfile = strdup(optarg);
			break;

		case 'p' :
			cellfile = strdup(optarg);
			break;

		case 'g' :
			geomfile = strdup(optarg);
			break;

		case 'y' :
			sym = strdup(optarg);
			break;

		case 0 :
			break;

		default :
			return 1;
		}

	}

	/* Load beam */
	if ( beamfile == NULL ) {
		ERROR("You need to provide a beam parameters file.\n");
		return 1;
	}
	beam = get_beam_parameters(beamfile);
	if ( beam == NULL ) {
		ERROR("Failed to load beam parameters from '%s'\n", beamfile);
		return 1;
	}
	free(beamfile);

	/* Load cell */
	if ( cellfile == NULL ) {
		ERROR("You need to give a PDB file with the unit cell.\n");
		return 1;
	}
	cell = load_cell_from_pdb(cellfile);
	if ( cell == NULL ) {
		ERROR("Failed to get cell from '%s'\n", cellfile);
		return 1;
	}
	free(cellfile);

	/* Load geometry */
	if ( geomfile == NULL ) {
		ERROR("You need to give a geometry file.\n");
		return 1;
	}
	det = get_detector_geometry(geomfile);
	if ( cell == NULL ) {
		ERROR("Failed to read geometry from '%s'\n", geomfile);
		return 1;
	}
	free(geomfile);

	/* Load (full) reflections */
	if ( input_file == NULL ) {
		ERROR("You must provide a reflection list.\n");
		return 1;
	}
	full = read_reflections(input_file);
	if ( full == NULL ) {
		ERROR("Failed to read reflections from '%s'\n", input_file);
		return 1;
	}
	free(input_file);
	if ( check_list_symmetry(full, sym) ) {
		ERROR("The input reflection list does not appear to"
		      " have symmetry %s\n", sym);
		return 1;
	}

	if ( output_file == NULL ) {
		ERROR("You must pgive a filename for the output.\n");
		return 1;
	}
	ofh = fopen(output_file, "w");
	if ( ofh == NULL ) {
		ERROR("Couldn't open output file '%s'\n", output_file);
		return 1;
	}
	free(output_file);
	write_stream_header(ofh, argc, argv);

	/* Set up a random orientation */
	orientation = random_quaternion();
	image.indexed_cell = cell_rotate(cell, orientation);

	image.det = det;
	image.width = det->max_fs;
	image.height = det->max_ss;

	image.lambda = ph_en_to_lambda(eV_to_J(beam->photon_energy));
	image.div = beam->divergence;
	image.bw = beam->bandwidth;
	image.profile_radius = 0.005e9;
	image.i0_available = 0;
	image.filename = "(simulated 1)";
	image.reflections = find_intersections(&image, image.indexed_cell, 0);
	calculate_partials(image.reflections, 1.0, full, sym);
	write_chunk(ofh, &image, STREAM_INTEGRATED);
	reflist_free(image.reflections);

	/* Alter the cell by a tiny amount */
	image.filename = "(simulated 2)";
	new = rotate_cell(cell, deg2rad(1.0), deg2rad(0.0), 0.0);
	cell_free(image.indexed_cell);
	image.indexed_cell = new;

	/* Calculate new partials */
	image.reflections = find_intersections(&image, image.indexed_cell, 0);
	calculate_partials(image.reflections, 0.5, full, sym);

	/* Give a slightly incorrect cell in the stream */
	mess_up_cell(image.indexed_cell);
	write_chunk(ofh, &image, STREAM_INTEGRATED);
	reflist_free(image.reflections);

	fclose(ofh);
	cell_free(cell);
	free_detector_geometry(det);
	free(beam);
	free(sym);
	reflist_free(full);

	return 0;
}