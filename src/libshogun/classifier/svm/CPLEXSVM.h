/*
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 *
 * Written (W) 1999-2008 Soeren Sonnenburg
 * Copyright (C) 1999-2008 Fraunhofer Institute FIRST and Max-Planck-Society
 */

#ifndef _CPLEXSVM_H___
#define _CPLEXSVM_H___
#include "lib/common.h"

#ifdef USE_CPLEX
#include "classifier/svm/SVM.h"
#include "lib/Cache.h"

class CCPLEXSVM : public CSVM
{
	public:
		CCPLEXSVM();
		virtual ~CCPLEXSVM();

		virtual bool train();

		virtual inline EClassifierType get_classifier_type() { return CT_CPLEXSVM; }
};

#endif
#endif