/*
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 *
 * Written (W) 1999-2009 Soeren Sonnenburg
 * Copyright (C) 1999-2009 Fraunhofer Institute FIRST and Max-Planck-Society
 */

#include "lib/common.h"
#include "lib/io.h"
#include "base/Parallel.h"
#include "base/Parameter.h"

#include "classifier/svm/SVM.h"
#include "classifier/mkl/MKL.h"

#include <string.h>

#ifndef WIN32
#include <pthread.h>
#endif

using namespace shogun;

CSVM::CSVM(int32_t num_sv)
: CKernelMachine()
{
	set_defaults(num_sv);
}

CSVM::CSVM(float64_t C, CKernel* k, CLabels* lab)
: CKernelMachine()
{
	set_defaults();
	set_C(C,C);
	set_labels(lab);
	set_kernel(k);
}

CSVM::~CSVM()
{
	delete[] m_linear_term;
	SG_UNREF(mkl);
}

void CSVM::set_defaults(int32_t num_sv)
{
	m_parameters->add(&C1, "C1");
	m_parameters->add(&C2, "C2");
	m_parameters->add(&svm_loaded, "svm_loaded",
					  "SVM is loaded.");
	m_parameters->add(&epsilon, "epsilon");
	m_parameters->add(&tube_epsilon, "tube_epsilon",
					  "Tube epsilon for support vector regression.");
	m_parameters->add(&nu, "nu");
	m_parameters->add(&objective, "objective");
	m_parameters->add(&qpsize, "qpsize");
	m_parameters->add(&use_shrinking, "use_shrinking",
					  "Shrinking shall be used.");
	m_parameters->add((CSGObject**) &mkl, "mkl",
					  "MKL object that svm optimizers need.");
	m_parameters->add_vector(&m_linear_term, &m_linear_term_len,
							 "linear_term",
							 "Linear term in qp.");

	callback=NULL;
	mkl=NULL;

	svm_loaded=false;

	epsilon=1e-5;
	tube_epsilon=1e-2;

	nu=0.5;
	C1=1;
	C2=1;

	objective=0;

	qpsize=41;
	use_bias=true;
	use_shrinking=true;
	use_batch_computation=true;
	use_linadd=true;

	m_linear_term = NULL;
	m_linear_term_len = 0;

    if (num_sv>0)
        create_new_model(num_sv);
}

bool CSVM::load(FILE* modelfl)
{
	bool result=true;
	char char_buffer[1024];
	int32_t int_buffer;
	float64_t double_buffer;
	int32_t line_number=1;

	SG_SET_LOCALE_C;

	if (fscanf(modelfl,"%4s\n", char_buffer)==EOF)
	{
		result=false;
		SG_ERROR( "error in svm file, line nr:%d\n", line_number);
	}
	else
	{
		char_buffer[4]='\0';
		if (strcmp("%SVM", char_buffer)!=0)
		{
			result=false;
			SG_ERROR( "error in svm file, line nr:%d\n", line_number);
		}
		line_number++;
	}

	int_buffer=0;
	if (fscanf(modelfl," numsv=%d; \n", &int_buffer) != 1)
	{
		result=false;
		SG_ERROR( "error in svm file, line nr:%d\n", line_number);
	}

	if (!feof(modelfl))
		line_number++;

	SG_INFO( "loading %ld support vectors\n",int_buffer);
	create_new_model(int_buffer);

	if (fscanf(modelfl," kernel='%s'; \n", char_buffer) != 1)
	{
		result=false;
		SG_ERROR( "error in svm file, line nr:%d\n", line_number);
	}

	if (!feof(modelfl))
		line_number++;

	double_buffer=0;

	if (fscanf(modelfl," b=%lf; \n", &double_buffer) != 1)
	{
		result=false;
		SG_ERROR( "error in svm file, line nr:%d\n", line_number);
	}

	if (!feof(modelfl))
		line_number++;

	set_bias(double_buffer);

	if (fscanf(modelfl,"%8s\n", char_buffer) == EOF)
	{
		result=false;
		SG_ERROR( "error in svm file, line nr:%d\n", line_number);
	}
	else
	{
		char_buffer[9]='\0';
		if (strcmp("alphas=[", char_buffer)!=0)
		{
			result=false;
			SG_ERROR( "error in svm file, line nr:%d\n", line_number);
		}
		line_number++;
	}

	for (int32_t i=0; i<get_num_support_vectors(); i++)
	{
		double_buffer=0;
		int_buffer=0;

		if (fscanf(modelfl," \[%lf,%d]; \n", &double_buffer, &int_buffer) != 2)
		{
			result=false;
			SG_ERROR( "error in svm file, line nr:%d\n", line_number);
		}

		if (!feof(modelfl))
			line_number++;

		set_support_vector(i, int_buffer);
		set_alpha(i, double_buffer);
	}

	if (fscanf(modelfl,"%2s", char_buffer) == EOF)
	{
		result=false;
		SG_ERROR( "error in svm file, line nr:%d\n", line_number);
	}
	else
	{
		char_buffer[3]='\0';
		if (strcmp("];", char_buffer)!=0)
		{
			result=false;
			SG_ERROR( "error in svm file, line nr:%d\n", line_number);
		}
		line_number++;
	}

	svm_loaded=result;
	SG_RESET_LOCALE;
	return result;
}

bool CSVM::save(FILE* modelfl)
{
	SG_SET_LOCALE_C;

	if (!kernel)
		SG_ERROR("Kernel not defined!\n");

	SG_INFO( "Writing model file...");
	fprintf(modelfl,"%%SVM\n");
	fprintf(modelfl,"numsv=%d;\n", get_num_support_vectors());
	fprintf(modelfl,"kernel='%s';\n", kernel->get_name());
	fprintf(modelfl,"b=%+10.16e;\n",get_bias());

	fprintf(modelfl, "alphas=\[\n");

	for(int32_t i=0; i<get_num_support_vectors(); i++)
		fprintf(modelfl,"\t[%+10.16e,%d];\n",
				CSVM::get_alpha(i), get_support_vector(i));

	fprintf(modelfl, "];\n");

	SG_DONE();
	SG_RESET_LOCALE;
	return true ;
}

void CSVM::set_callback_function(CMKL* m, bool (*cb)
		(CMKL* mkl, const float64_t* sumw, const float64_t suma))
{
	SG_UNREF(mkl);
	mkl=m;
	SG_REF(mkl);

	callback=cb;
}

float64_t CSVM::compute_svm_dual_objective()
{
	int32_t n=get_num_support_vectors();

	if (labels && kernel)
	{
		objective=0;
		for (int32_t i=0; i<n; i++)
		{
			int32_t ii=get_support_vector(i);
			objective-=get_alpha(i)*labels->get_label(ii);

			for (int32_t j=0; j<n; j++)
			{
				int32_t jj=get_support_vector(j);
				objective+=0.5*get_alpha(i)*get_alpha(j)*kernel->kernel(ii,jj);
			}
		}
	}
	else
		SG_ERROR( "cannot compute objective, labels or kernel not set\n");

	return objective;
}

float64_t CSVM::compute_svm_primal_objective()
{
	int32_t n=get_num_support_vectors();
	float64_t regularizer=0;
	float64_t loss=0;


	
	if (labels && kernel)
	{
		float64_t C2_tmp=C1;
		if(C2>0)
		{
			C2_tmp=C2;
		}
		
		for (int32_t i=0; i<n; i++)
		{
			int32_t ii=get_support_vector(i);
			for (int32_t j=0; j<n; j++)
			{
				int32_t jj=get_support_vector(j);
				regularizer-=0.5*get_alpha(i)*get_alpha(j)*kernel->kernel(ii,jj);
			}

			loss-=(C1*(-get_label(ii)+1)/2.0 + C2_tmp*(get_label(ii)+1)/2.0 )*CMath::max(0.0, 1.0-get_label(ii)*classify_example(ii));
		}	

	}
	else
		SG_ERROR( "cannot compute objective, labels or kernel not set\n");

	return regularizer+loss;
}

float64_t* CSVM::get_linear_term_array()
{
	if (m_linear_term_len == 0)
		return NULL;

	float64_t* a = new float64_t[m_linear_term_len];
	memcpy(a, m_linear_term, m_linear_term_len*sizeof (float64_t));

	return a;
}

void CSVM::set_linear_term(float64_t* linear_term, index_t len)
{
	ASSERT(linear_term);

	if (!labels)
		SG_ERROR("Please assign labels first!\n");

	int32_t num_labels=labels->get_num_labels();

	if (num_labels != len)
	{
		SG_ERROR("Number of labels (%d) does not match number"
				"of entries (%d) in linear term \n", num_labels, len);
	}

	delete[] m_linear_term;

	m_linear_term_len = len;
	m_linear_term = new float64_t[len];
	memcpy(m_linear_term, linear_term, len*sizeof (float64_t));
}

float64_t* CSVM::get_linear_term_ptr(index_t* y)
{
	if (y == NULL)
		return NULL;

	*y = m_linear_term_len;
	return m_linear_term;
}
