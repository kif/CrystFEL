/*
 * statistics.h
 *
 * Structure-factor statistics
 *
 * (c) 2006-2010 Thomas White <taw@physics.org>
 *
 * Part of CrystFEL - crystallography with a FEL
 *
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#ifndef STATISTICS_H
#define STATISTICS_H


#include "utils.h"

extern double stat_scale_intensity(const double *ref1, const double *ref2,
                                   ReflItemList *items);

extern double stat_rmerge(const double *ref1, const double *ref2,
                          ReflItemList *items, double *scalep);

extern double stat_r2(const double *ref1, const double *ref2,
                      ReflItemList *items, double *scalep);

extern double stat_pearson(const double *ref1, const double *ref2,
                           ReflItemList *items);


#endif	/* STATISTICS_H */
