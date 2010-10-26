/*
 * beam-parameters.c
 *
 * Beam parameters
 *
 * (c) 2006-2010 Thomas White <taw@physics.org>
 *
 * Part of CrystFEL - crystallography with a FEL
 *
 */


#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <stdio.h>
#include <stdlib.h>

#include "beam-parameters.h"
#include "utils.h"


struct beam_params *get_beam_parameters(const char *filename)
{
	FILE *fh;
	struct beam_params *b;
	char *rval;
	int reject;

	fh = fopen(filename, "r");
	if ( fh == NULL ) return NULL;

	b = calloc(1, sizeof(struct beam_params));
	if ( b == NULL ) return NULL;

	b->fluence = -1.0;
	b->beam_radius = -1.0;
	b->photon_energy = -1.0;
	b->bandwidth = -1.0;
	b->dqe = -1.0;
	b->adu_per_photon = -1.0;
	b->water_radius = -1.0;

	do {

		int n1;
		char line[1024];
		int i;
		char **bits;

		rval = fgets(line, 1023, fh);
		if ( rval == NULL ) break;
		chomp(line);

		n1 = assplode(line, " \t", &bits, ASSPLODE_NONE);
		if ( n1 < 3 ) {
			for ( i=0; i<n1; i++ ) free(bits[i]);
			free(bits);
			continue;
		}

		if ( bits[1][0] != '=' ) {
			for ( i=0; i<n1; i++ ) free(bits[i]);
			free(bits);
			continue;
		}

		if ( strcmp(bits[0], "beam/fluence") == 0 ) {
			b->fluence = atof(bits[2]);
		} else if ( strcmp(bits[0], "beam/radius") == 0 ) {
			b->beam_radius = atof(bits[2]);
		} else if ( strcmp(bits[0], "beam/photon_energy") == 0 ) {
			b->photon_energy = atof(bits[2]);
		} else if ( strcmp(bits[0], "beam/bandwidth") == 0 ) {
			b->bandwidth = atof(bits[2]);
		} else if ( strcmp(bits[0], "detector/dqe") == 0 ) {
			b->dqe = atof(bits[2]);
		} else if ( strcmp(bits[0], "detector/adu_per_photon") == 0 ) {
			b->adu_per_photon = atof(bits[2]);
		} else if ( strcmp(bits[0], "jet/radius") == 0 ) {
			b->water_radius = atof(bits[2]);
		} else {
			ERROR("Unrecognised field '%s'\n", bits[0]);
		}

		for ( i=0; i<n1; i++ ) free(bits[i]);
		free(bits);

	} while ( rval != NULL );
	fclose(fh);

	reject = 0;

	if ( b->fluence < 0.0 ) {
		ERROR("Invalid or unspecified value for 'beam/fluence'.\n");
		reject = 1;
	}
	if ( b->beam_radius < 0.0 ) {
		ERROR("Invalid or unspecified value for 'beam/radius'.\n");
		reject = 1;
	}
	if ( b->photon_energy < 0.0 ) {
		ERROR("Invalid or unspecified value for"
		      " 'beam/photon_energy'.\n");
		reject = 1;
	}
	if ( b->bandwidth < 0.0 ) {
		ERROR("Invalid or unspecified value for 'beam/bandwidth'.\n");
		reject = 1;
	}
	if ( b->dqe < 0.0 ) {
		ERROR("Invalid or unspecified value for 'detector/dqe'.\n");
		reject = 1;
	}
	if ( b->adu_per_photon < 0.0 ) {
		ERROR("Invalid or unspecified value for"
		      " 'detector/adu_per_photon'.\n");
		reject = 1;
	}
	if ( b->water_radius < 0.0 ) {
		ERROR("Invalid or unspecified value for 'jet/radius'.\n");
		reject = 1;
	}

	if ( reject ) {
		ERROR("Please fix the above problems with the beam"
		      " parameters file and try again.\n");
		return NULL;
	}

	return b;
}