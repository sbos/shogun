/*
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 *
 * Written (W) 2006-2009 Christian Gehl
 * Copyright (C) 2006-2009 Fraunhofer Institute FIRST and Max-Planck-Society
 */

#ifndef _CHEBYSHEWMETRIC_H___
#define _CHEBYSHEWMETRIC_H___

#include "lib/common.h"
#include "distance/SimpleDistance.h"
#include "features/SimpleFeatures.h"

/** @brief class ChebyshewMetric 
 *
 * The Chebyshev distance (\f$L_{\infty}\f$ norm) returns the maximum of
 * absolute feature dimension differences between two data points.
 *
 * \f[\displaystyle
 *  d(\bf{x},\bf{x'}) = max|\bf{x_{i}}-\bf{x'_{i}}| \quad x,x' \in R^{n}
 * \f]
 * 
 * @see <a href="http://en.wikipedia.org/wiki/Chebyshev_distance">Wikipedia: Chebyshev distance</a>
 */
class CChebyshewMetric: public CSimpleDistance<float64_t>
{
	public:
		/** default constructor */
		CChebyshewMetric();

		/** constructor
		 *
		 * @param l features of left-hand side
		 * @param r features of right-hand side
		 */
		CChebyshewMetric(CSimpleFeatures<float64_t>* l, CSimpleFeatures<float64_t>* r);
		virtual ~CChebyshewMetric();

		/** init distance
		 *
		 * @param l features of left-hand side
		 * @param r features of right-hand side
		 * @return if init was successful
		 */
		virtual bool init(CFeatures* l, CFeatures* r);

		/** cleanup distance */
		virtual void cleanup();

		/** load init data from file
		 *
		 * @param src file to load from
		 * @return if loading was successful
		 */
		virtual bool load_init(FILE* src);

		/** save init data to file
		 *
		 * @param dest file to save to
		 * @return if saving was successful
		 */
		virtual bool save_init(FILE* dest);

		/** get distance type we are
		 *
		 * @return distance type CHEBYSHEW
		 */
		virtual EDistanceType get_distance_type() { return D_CHEBYSHEW; }

		/** get name of the distance
		 *
		 * @return name Chebyshew-Metric
		 */
		virtual const char* get_name() const { return "Chebyshew-Metric"; }

	protected:
		/// compute distance for features a and b
		/// idx_{a,b} denote the index of the feature vectors
		/// in the corresponding feature object
		virtual float64_t compute(int32_t idx_a, int32_t idx_b);
};

#endif  /* _CHEBYSHEWMETRIC_H___ */