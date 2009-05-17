/*
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 *
 * Written (W) 1999-2009 Soeren Sonnenburg
 * Written (W) 1999-2008 Gunnar Raetsch
 * Copyright (C) 1999-2009 Fraunhofer Institute FIRST and Max-Planck-Society
 */

#include "lib/common.h"
#include "lib/io.h"
#include "lib/Signal.h"
#include "lib/Trie.h"
#include "base/Parallel.h"

#include "kernel/WeightedDegreeStringKernel.h"
#include "kernel/FirstElementKernelNormalizer.h"
#include "features/Features.h"
#include "features/StringFeatures.h"

#ifndef WIN32
#include <pthread.h>
#endif

#ifndef DOXYGEN_SHOULD_SKIP_THIS
struct S_THREAD_PARAM
{
	int32_t* vec;
	float64_t* result;
	float64_t* weights;
	CWeightedDegreeStringKernel* kernel;
	CTrie<DNATrie>* tries;
	float64_t factor;
	int32_t j;
	int32_t start;
	int32_t end;
	int32_t length;
	int32_t* vec_idx;
};
#endif // DOXYGEN_SHOULD_SKIP_THIS

CWeightedDegreeStringKernel::CWeightedDegreeStringKernel (
	int32_t degree_, EWDKernType type_)
: CStringKernel<char>(10),weights(NULL),position_weights(NULL),
	weights_buffer(NULL), mkl_stepsize(1),degree(degree_), length(0),
	max_mismatch(0), seq_length(0), block_computation(true),
	num_block_weights_external(0), block_weights_external(NULL),
	block_weights(NULL), type(type_), which_degree(-1), tries(NULL),
	tree_initialized(false), alphabet(NULL)
{
	properties |= KP_LINADD | KP_KERNCOMBINATION | KP_BATCHEVALUATION;
	lhs=NULL;
	rhs=NULL;

	if (type!=E_EXTERNAL)
		set_wd_weights_by_type(type);

	set_normalizer(new CFirstElementKernelNormalizer());
}

CWeightedDegreeStringKernel::CWeightedDegreeStringKernel (
	float64_t *weights_, int32_t degree_)
: CStringKernel<char>(10), weights(NULL), position_weights(NULL),
	weights_buffer(NULL), mkl_stepsize(1), degree(degree_), length(0),
	max_mismatch(0), seq_length(0), block_computation(true),
	num_block_weights_external(0), block_weights_external(NULL),
	block_weights(NULL), type(E_EXTERNAL), which_degree(-1), tries(NULL),
	tree_initialized(false), alphabet(NULL)
{
	properties |= KP_LINADD | KP_KERNCOMBINATION | KP_BATCHEVALUATION;
	lhs=NULL;
	rhs=NULL;

	weights=new float64_t[degree*(1+max_mismatch)];
	for (int32_t i=0; i<degree*(1+max_mismatch); i++)
		weights[i]=weights_[i];
	set_normalizer(new CFirstElementKernelNormalizer());
}

CWeightedDegreeStringKernel::CWeightedDegreeStringKernel(
	CStringFeatures<char>* l, CStringFeatures<char>* r, int32_t degree_)
: CStringKernel<char>(10), weights(NULL), position_weights(NULL),
	weights_buffer(NULL), mkl_stepsize(1), degree(degree_), length(0),
	max_mismatch(0), seq_length(0), block_computation(true),
	num_block_weights_external(0), block_weights_external(NULL),
	block_weights(NULL), type(E_WD), which_degree(-1), tries(NULL),
	tree_initialized(false), alphabet(NULL)
{
	properties |= KP_LINADD | KP_KERNCOMBINATION | KP_BATCHEVALUATION;

	set_wd_weights_by_type(type);
	set_normalizer(new CFirstElementKernelNormalizer());
	init(l, r);
}

CWeightedDegreeStringKernel::~CWeightedDegreeStringKernel()
{
	cleanup();

	delete[] weights;
	weights=NULL;

	delete[] block_weights;
	block_weights=NULL;

	delete[] position_weights;
	position_weights=NULL;

	delete[] weights_buffer;
	weights_buffer=NULL;
}


void CWeightedDegreeStringKernel::remove_lhs()
{ 
	SG_DEBUG( "deleting CWeightedDegreeStringKernel optimization\n");
	delete_optimization();

	if (tries!=NULL)
		tries->destroy();

	CKernel::remove_lhs();
}

void CWeightedDegreeStringKernel::create_empty_tries()
{
	seq_length=((CStringFeatures<char>*) lhs)->get_max_vector_length();

	if (tries!=NULL)
	{
		tries->destroy() ;
		tries->create(seq_length, max_mismatch==0) ;
	}
}
  
bool CWeightedDegreeStringKernel::init(CFeatures* l, CFeatures* r)
{
	int32_t lhs_changed=(lhs!=l);
	int32_t rhs_changed=(rhs!=r);

	CStringKernel<char>::init(l,r);

	SG_DEBUG("lhs_changed: %i\n", lhs_changed);
	SG_DEBUG("rhs_changed: %i\n", rhs_changed);

	CStringFeatures<char>* sf_l=(CStringFeatures<char>*) l;
	CStringFeatures<char>* sf_r=(CStringFeatures<char>*) r;

	int32_t len=sf_l->get_max_vector_length();
	if (lhs_changed && !sf_l->have_same_length(len))
		SG_ERROR("All strings in WD kernel must have same length (lhs wrong)!\n");

	if (rhs_changed && !sf_r->have_same_length(len))
		SG_ERROR("All strings in WD kernel must have same length (rhs wrong)!\n");

	SG_UNREF(alphabet);
	alphabet=sf_l->get_alphabet();
	CAlphabet* ralphabet=sf_r->get_alphabet();
	
	if (!((alphabet->get_alphabet()==DNA) || (alphabet->get_alphabet()==RNA)))
		properties &= ((uint64_t) (-1)) ^ (KP_LINADD | KP_BATCHEVALUATION);

	ASSERT(ralphabet->get_alphabet()==alphabet->get_alphabet());
	SG_UNREF(ralphabet);

	if (tries!=NULL) {
		tries->delete_trees(max_mismatch==0);
		SG_UNREF(tries);
	}
	tries=new CTrie<DNATrie>(degree, max_mismatch==0);
	create_empty_tries();

	init_block_weights();

	return init_normalizer();
}

void CWeightedDegreeStringKernel::cleanup()
{
	SG_DEBUG("deleting CWeightedDegreeStringKernel optimization\n");
	delete_optimization();

	delete[] block_weights;
	block_weights=NULL;

	if (tries!=NULL)
	{
		tries->destroy();
		SG_UNREF(tries);
		tries=NULL;
	}

	seq_length=0;
	tree_initialized = false;

	SG_UNREF(alphabet);
	alphabet=NULL;

	CKernel::cleanup();
}

bool CWeightedDegreeStringKernel::load_init(FILE* src)
{
	return false;
}

bool CWeightedDegreeStringKernel::save_init(FILE* dest)
{
	return false;
}
  

bool CWeightedDegreeStringKernel::init_optimization(int32_t count, int32_t* IDX, float64_t* alphas, int32_t tree_num)
{
	if (tree_num<0)
		SG_DEBUG( "deleting CWeightedDegreeStringKernel optimization\n");

	delete_optimization();

	if (tree_num<0)
		SG_DEBUG( "initializing CWeightedDegreeStringKernel optimization\n") ;

	for (int32_t i=0; i<count; i++)
	{
		if (tree_num<0)
		{
			if ( (i % (count/10+1)) == 0)
				SG_PROGRESS(i, 0, count);
			
			if (max_mismatch==0)
				add_example_to_tree(IDX[i], alphas[i]) ;
			else
				add_example_to_tree_mismatch(IDX[i], alphas[i]) ;

			//SG_DEBUG( "number of used trie nodes: %i\n", tries.get_num_used_nodes()) ;
		}
		else
		{
			if (max_mismatch==0)
				add_example_to_single_tree(IDX[i], alphas[i], tree_num) ;
			else
				add_example_to_single_tree_mismatch(IDX[i], alphas[i], tree_num) ;
		}
	}
	
	if (tree_num<0)
		SG_DONE();

	//tries.compact_nodes(NO_CHILD, 0, weights) ;

	set_is_initialized(true) ;
	return true ;
}

bool CWeightedDegreeStringKernel::delete_optimization()
{ 
	if (get_is_initialized())
	{
		if (tries!=NULL)
			tries->delete_trees(max_mismatch==0); 
		set_is_initialized(false);
		return true;
	}
	
	return false;
}


float64_t CWeightedDegreeStringKernel::compute_with_mismatch(
	char* avec, int32_t alen, char* bvec, int32_t blen)
{
	float64_t sum = 0.0;

	for (int32_t i=0; i<alen; i++)
	{
		float64_t sumi = 0.0;
		int32_t mismatches=0;

		for (int32_t j=0; (i+j<alen) && (j<degree); j++)
		{
			if (avec[i+j]!=bvec[i+j])
			{
				mismatches++ ;
				if (mismatches>max_mismatch)
					break ;
			} ;
			sumi += weights[j+degree*mismatches];
		}
		if (position_weights!=NULL)
			sum+=position_weights[i]*sumi ;
		else
			sum+=sumi ;
	}
	return sum ;
}

float64_t CWeightedDegreeStringKernel::compute_using_block(
	char* avec, int32_t alen, char* bvec, int32_t blen)
{
	ASSERT(alen==blen);

	float64_t sum=0;
	int32_t match_len=-1;

	for (int32_t i=0; i<alen; i++)
	{
		if (avec[i]==bvec[i])
			match_len++;
		else
		{
			if (match_len>=0)
				sum+=block_weights[match_len];
			match_len=-1;
		}
	}

	if (match_len>=0)
		sum+=block_weights[match_len];

	return sum;
}

float64_t CWeightedDegreeStringKernel::compute_without_mismatch(
	char* avec, int32_t alen, char* bvec, int32_t blen)
{
	float64_t sum = 0.0;

	for (int32_t i=0; i<alen; i++)
	{
		float64_t sumi = 0.0;

		for (int32_t j=0; (i+j<alen) && (j<degree); j++)
		{
			if (avec[i+j]!=bvec[i+j])
				break ;
			sumi += weights[j];
		}
		if (position_weights!=NULL)
			sum+=position_weights[i]*sumi ;
		else
			sum+=sumi ;
	}
	return sum ;
}

float64_t CWeightedDegreeStringKernel::compute_without_mismatch_matrix(
	char* avec, int32_t alen, char* bvec, int32_t blen)
{
	float64_t sum = 0.0;

	for (int32_t i=0; i<alen; i++)
	{
		float64_t sumi=0.0;
		for (int32_t j=0; (i+j<alen) && (j<degree); j++)
		{
			if (avec[i+j]!=bvec[i+j])
				break;
			sumi += weights[i*degree+j];
		}
		if (position_weights!=NULL)
			sum += position_weights[i]*sumi ;
		else
			sum += sumi ;
	}

	return sum ;
}


float64_t CWeightedDegreeStringKernel::compute(int32_t idx_a, int32_t idx_b)
{
	int32_t alen, blen;
	char* avec=((CStringFeatures<char>*) lhs)->get_feature_vector(idx_a, alen);
	char* bvec=((CStringFeatures<char>*) rhs)->get_feature_vector(idx_b, blen);
	float64_t result=0;

	if (max_mismatch==0 && length==0 && block_computation)
		result=compute_using_block(avec, alen, bvec, blen);
	else
	{
		if (max_mismatch>0)
			result=compute_with_mismatch(avec, alen, bvec, blen);
		else if (length==0)
			result=compute_without_mismatch(avec, alen, bvec, blen);
		else
			result=compute_without_mismatch_matrix(avec, alen, bvec, blen);
	}

	return result;
}


void CWeightedDegreeStringKernel::add_example_to_tree(
	int32_t idx, float64_t alpha)
{
	ASSERT(alphabet);
	ASSERT(alphabet->get_alphabet()==DNA || alphabet->get_alphabet()==RNA);

	int32_t len=0;
	char* char_vec=((CStringFeatures<char>*) lhs)->get_feature_vector(idx, len);
	ASSERT(max_mismatch==0);
	int32_t *vec=new int32_t[len];

	for (int32_t i=0; i<len; i++)
		vec[i]=alphabet->remap_to_bin(char_vec[i]);
	
	if (length == 0 || max_mismatch > 0)
	{
		for (int32_t i=0; i<len; i++)
		{
			float64_t alpha_pw=alpha;
			/*if (position_weights!=NULL)
			  alpha_pw *= position_weights[i] ;*/
			if (alpha_pw==0.0)
				continue;
			ASSERT(tries);
			tries->add_to_trie(i, 0, vec, normalizer->normalize_lhs(alpha_pw, idx), weights, (length!=0));
		}
	}
	else
	{
		for (int32_t i=0; i<len; i++)
		{
			float64_t alpha_pw=alpha;
			/*if (position_weights!=NULL) 
			  alpha_pw = alpha*position_weights[i] ;*/
			if (alpha_pw==0.0)
				continue ;
			ASSERT(tries);
			tries->add_to_trie(i, 0, vec, normalizer->normalize_lhs(alpha_pw, idx), weights, (length!=0));
		}
	}
	delete[] vec ;
	tree_initialized=true ;
}

void CWeightedDegreeStringKernel::add_example_to_single_tree(
	int32_t idx, float64_t alpha, int32_t tree_num) 
{
	ASSERT(alphabet);
	ASSERT(alphabet->get_alphabet()==DNA || alphabet->get_alphabet()==RNA);

	int32_t len ;
	char* char_vec=((CStringFeatures<char>*) lhs)->get_feature_vector(idx, len);
	ASSERT(max_mismatch==0);
	int32_t *vec = new int32_t[len] ;

	for (int32_t i=tree_num; i<tree_num+degree && i<len; i++)
		vec[i]=alphabet->remap_to_bin(char_vec[i]);
	
	ASSERT(tries);
	if (alpha!=0.0)
		tries->add_to_trie(tree_num, 0, vec, normalizer->normalize_lhs(alpha, idx), weights, (length!=0));

	delete[] vec ;
	tree_initialized=true ;
}

void CWeightedDegreeStringKernel::add_example_to_tree_mismatch(int32_t idx, float64_t alpha)
{
	ASSERT(tries);
	ASSERT(alphabet);
	ASSERT(alphabet->get_alphabet()==DNA || alphabet->get_alphabet()==RNA);

	int32_t len ;
	char* char_vec=((CStringFeatures<char>*) lhs)->get_feature_vector(idx, len);
	
	int32_t *vec = new int32_t[len] ;
	
	for (int32_t i=0; i<len; i++)
		vec[i]=alphabet->remap_to_bin(char_vec[i]);
	
	for (int32_t i=0; i<len; i++)
	{
		if (alpha!=0.0)
			tries->add_example_to_tree_mismatch_recursion(NO_CHILD, i, normalizer->normalize_lhs(alpha, idx), &vec[i], len-i, 0, 0, max_mismatch, weights);
	}
	
	delete[] vec ;
	tree_initialized=true ;
}

void CWeightedDegreeStringKernel::add_example_to_single_tree_mismatch(
	int32_t idx, float64_t alpha, int32_t tree_num)
{
	ASSERT(tries);
	ASSERT(alphabet);
	ASSERT(alphabet->get_alphabet()==DNA || alphabet->get_alphabet()==RNA);

	int32_t len=0;
	char* char_vec=((CStringFeatures<char>*) lhs)->get_feature_vector(idx, len);
	int32_t *vec=new int32_t[len];

	for (int32_t i=tree_num; i<len && i<tree_num+degree; i++)
		vec[i]=alphabet->remap_to_bin(char_vec[i]);

	if (alpha!=0.0)
	{
		tries->add_example_to_tree_mismatch_recursion(
			NO_CHILD, tree_num, normalizer->normalize_lhs(alpha, idx), &vec[tree_num], len-tree_num,
			0, 0, max_mismatch, weights);
	}

	delete[] vec;
	tree_initialized=true;
}


float64_t CWeightedDegreeStringKernel::compute_by_tree(int32_t idx)
{
	ASSERT(alphabet);
	ASSERT(alphabet->get_alphabet()==DNA || alphabet->get_alphabet()==RNA);

	int32_t len=0;
	char* char_vec=((CStringFeatures<char>*) rhs)->get_feature_vector(idx, len);
	ASSERT(char_vec && len>0);
	int32_t *vec=new int32_t[len];

	for (int32_t i=0; i<len; i++)
		vec[i]=alphabet->remap_to_bin(char_vec[i]);

	float64_t sum=0;
	ASSERT(tries);
	for (int32_t i=0; i<len; i++)
		sum+=tries->compute_by_tree_helper(vec, len, i, i, i, weights, (length!=0));

	delete[] vec;
	return normalizer->normalize_rhs(sum, idx);
}

void CWeightedDegreeStringKernel::compute_by_tree(
	int32_t idx, float64_t* LevelContrib)
{
	ASSERT(alphabet);
	ASSERT(alphabet->get_alphabet()==DNA || alphabet->get_alphabet()==RNA);

	int32_t len ;
	char* char_vec=((CStringFeatures<char>*) rhs)->get_feature_vector(idx, len);

	int32_t *vec = new int32_t[len] ;

	for (int32_t i=0; i<len; i++)
		vec[i]=alphabet->remap_to_bin(char_vec[i]);

	ASSERT(tries);
	for (int32_t i=0; i<len; i++)
	{
		tries->compute_by_tree_helper(vec, len, i, i, i, LevelContrib,
				normalizer->normalize_rhs(1.0, idx),
				mkl_stepsize, weights, (length!=0));
	}

	delete[] vec ;
}

float64_t *CWeightedDegreeStringKernel::compute_abs_weights(int32_t &len)
{
	ASSERT(tries);
	return tries->compute_abs_weights(len);
}

bool CWeightedDegreeStringKernel::set_wd_weights_by_type(EWDKernType p_type)
{
	ASSERT(degree>0);
	ASSERT(p_type==E_WD); /// if we know a better weighting later on do a switch

	delete[] weights;
	weights=new float64_t[degree];
	if (weights)
	{
		int32_t i;
		float64_t sum=0;
		for (i=0; i<degree; i++)
		{
			weights[i]=degree-i;
			sum+=weights[i];
		}
		for (i=0; i<degree; i++)
			weights[i]/=sum;

		for (i=0; i<degree; i++)
		{
			for (int32_t j=1; j<=max_mismatch; j++)
			{
				if (j<i+1)
				{
					int32_t nk=CMath::nchoosek(i+1, j);
					weights[i+j*degree]=weights[i]/(nk*CMath::pow(3.0,j));
				}
				else
					weights[i+j*degree]= 0;
			}
		}

		if (which_degree>=0)
		{
			ASSERT(which_degree<degree);
			for (i=0; i<degree; i++)
			{
				if (i!=which_degree)
					weights[i]=0;
				else
					weights[i]=1;
			}
		}
		return true;
	}
	else
		return false;
}

bool CWeightedDegreeStringKernel::set_weights(
	float64_t* ws, int32_t d, int32_t len)
{
	SG_DEBUG("degree = %i  d=%i\n", degree, d);
	degree=d;
	ASSERT(tries);
	tries->set_degree(degree);
	length=len;
	
	if (length==0) length=1;
	int32_t num_weights=degree*(length+max_mismatch);
	delete[] weights;
	weights=new float64_t[num_weights];
	if (weights)
	{
		for (int32_t i=0; i<num_weights; i++) {
			if (ws[i]) // len(ws) might be != num_weights?
				weights[i]=ws[i];
		}
		return true;
	}
	else
		return false;
}

bool CWeightedDegreeStringKernel::set_position_weights(
	float64_t* pws, int32_t len)
{
	if (len==0)
	{
		delete[] position_weights;
		position_weights=NULL;
		ASSERT(tries);
		tries->set_position_weights(position_weights);
	}

	if (seq_length!=len)
		SG_ERROR("seq_length = %i, position_weights_length=%i\n", seq_length, len);

	delete[] position_weights;
	position_weights=new float64_t[len];
	ASSERT(tries);
	tries->set_position_weights(position_weights);
	
	if (position_weights)
	{
		for (int32_t i=0; i<len; i++)
			position_weights[i]=pws[i];
		return true;
	}
	else
		return false;
}

bool CWeightedDegreeStringKernel::init_block_weights_from_wd()
{
	delete[] block_weights;
	block_weights=new float64_t[CMath::max(seq_length,degree)];

	if (block_weights)
	{
		int32_t k;
		float64_t d=degree; // use float to evade rounding errors below

		for (k=0; k<degree; k++)
			block_weights[k]=
				(-CMath::pow(k, 3)+(3*d-3)*CMath::pow(k, 2)+(9*d-2)*k+6*d)/(3*d*(d+1));
		for (k=degree; k<seq_length; k++)
			block_weights[k]=(-d+3*k+4)/3;
	}

	return (block_weights!=NULL);
}

bool CWeightedDegreeStringKernel::init_block_weights_from_wd_external()
{
	ASSERT(weights);
	delete[] block_weights;
	block_weights=new float64_t[CMath::max(seq_length,degree)];

	if (block_weights)
	{
		int32_t i=0;
		block_weights[0]=weights[0];
		for (i=1; i<CMath::max(seq_length,degree); i++)
			block_weights[i]=0;

		for (i=1; i<CMath::max(seq_length,degree); i++)
		{
			block_weights[i]=block_weights[i-1];

			float64_t contrib=0;
			for (int32_t j=0; j<CMath::min(degree,i+1); j++)
				contrib+=weights[j];

			block_weights[i]+=contrib;
		}
	}

	return (block_weights!=NULL);
}

bool CWeightedDegreeStringKernel::init_block_weights_const()
{
	delete[] block_weights;
	block_weights=new float64_t[seq_length];

	if (block_weights)
	{
		for (int32_t i=1; i<seq_length+1 ; i++)
			block_weights[i-1]=1.0/seq_length;
	}

	return (block_weights!=NULL);
}

bool CWeightedDegreeStringKernel::init_block_weights_linear()
{
	delete[] block_weights;
	block_weights=new float64_t[seq_length];

	if (block_weights)
	{
		for (int32_t i=1; i<seq_length+1 ; i++)
			block_weights[i-1]=degree*i;
	}

	return (block_weights!=NULL);
}

bool CWeightedDegreeStringKernel::init_block_weights_sqpoly()
{
	delete[] block_weights;
	block_weights=new float64_t[seq_length];

	if (block_weights)
	{
		for (int32_t i=1; i<degree+1 ; i++)
			block_weights[i-1]=((float64_t) i)*i;

		for (int32_t i=degree+1; i<seq_length+1 ; i++)
			block_weights[i-1]=i;
	}

	return (block_weights!=NULL);
}

bool CWeightedDegreeStringKernel::init_block_weights_cubicpoly()
{
	delete[] block_weights;
	block_weights=new float64_t[seq_length];

	if (block_weights)
	{
		for (int32_t i=1; i<degree+1 ; i++)
			block_weights[i-1]=((float64_t) i)*i*i;

		for (int32_t i=degree+1; i<seq_length+1 ; i++)
			block_weights[i-1]=i;
	}

	return (block_weights!=NULL);
}

bool CWeightedDegreeStringKernel::init_block_weights_exp()
{
	delete[] block_weights;
	block_weights=new float64_t[seq_length];

	if (block_weights)
	{
		for (int32_t i=1; i<degree+1 ; i++)
			block_weights[i-1]=exp(((float64_t) i/10.0));

		for (int32_t i=degree+1; i<seq_length+1 ; i++)
			block_weights[i-1]=i;
	}

	return (block_weights!=NULL);
}

bool CWeightedDegreeStringKernel::init_block_weights_log()
{
	delete[] block_weights;
	block_weights=new float64_t[seq_length];

	if (block_weights)
	{
		for (int32_t i=1; i<degree+1 ; i++)
			block_weights[i-1]=CMath::pow(CMath::log((float64_t) i),2);

		for (int32_t i=degree+1; i<seq_length+1 ; i++)
			block_weights[i-1]=i-degree+1+CMath::pow(CMath::log(degree+1.0),2);
	}

	return (block_weights!=NULL);
}

bool CWeightedDegreeStringKernel::init_block_weights_external()
{
	if (block_weights_external && (seq_length == num_block_weights_external) )
	{
		delete[] block_weights;
		block_weights=new float64_t[seq_length];

		if (block_weights)
		{
			for (int32_t i=0; i<seq_length; i++)
				block_weights[i]=block_weights_external[i];
		}
	}
	else {
      SG_ERROR( "sequence longer then weights (seqlen:%d, wlen:%d)\n", seq_length, block_weights_external);
   }
	return (block_weights!=NULL);
}

bool CWeightedDegreeStringKernel::init_block_weights()
{
	switch (type)
	{
		case E_WD:
			return init_block_weights_from_wd();
		case E_EXTERNAL:
			return init_block_weights_from_wd_external();
		case E_BLOCK_CONST:
			return init_block_weights_const();
		case E_BLOCK_LINEAR:
			return init_block_weights_linear();
		case E_BLOCK_SQPOLY:
			return init_block_weights_sqpoly();
		case E_BLOCK_CUBICPOLY:
			return init_block_weights_cubicpoly();
		case E_BLOCK_EXP:
			return init_block_weights_exp();
		case E_BLOCK_LOG:
			return init_block_weights_log();
		case E_BLOCK_EXTERNAL:
			return init_block_weights_external();
		default:
			return false;
	};
}


void* CWeightedDegreeStringKernel::compute_batch_helper(void* p)
{
	S_THREAD_PARAM* params = (S_THREAD_PARAM*) p;
	int32_t j=params->j;
	CWeightedDegreeStringKernel* wd=params->kernel;
	CTrie<DNATrie>* tries=params->tries;
	float64_t* weights=params->weights;
	int32_t length=params->length;
	int32_t* vec=params->vec;
	float64_t* result=params->result;
	float64_t factor=params->factor;
	int32_t* vec_idx=params->vec_idx;

	CStringFeatures<char>* rhs_feat=((CStringFeatures<char>*) wd->get_rhs());
	CAlphabet* alpha=wd->alphabet;

	for (int32_t i=params->start; i<params->end; i++)
	{
		int32_t len=0;
		char* char_vec=rhs_feat->get_feature_vector(vec_idx[i], len);
		for (int32_t k=j; k<CMath::min(len,j+wd->get_degree()); k++)
			vec[k]=alpha->remap_to_bin(char_vec[k]);

		ASSERT(tries);

		result[i]+=factor*
			wd->normalizer->normalize_rhs(tries->compute_by_tree_helper(vec, len, j, j, j, weights, (length!=0)), vec_idx[i]);
	}

	SG_UNREF(rhs_feat);

	return NULL;
}

void CWeightedDegreeStringKernel::compute_batch(
	int32_t num_vec, int32_t* vec_idx, float64_t* result, int32_t num_suppvec,
	int32_t* IDX, float64_t* alphas, float64_t factor)
{
	ASSERT(tries);
	ASSERT(alphabet);
	ASSERT(alphabet->get_alphabet()==DNA || alphabet->get_alphabet()==RNA);
	ASSERT(rhs);
	ASSERT(num_vec<=rhs->get_num_vectors());
	ASSERT(num_vec>0);
	ASSERT(vec_idx);
	ASSERT(result);
	create_empty_tries();

	int32_t num_feat=((CStringFeatures<char>*) rhs)->get_max_vector_length();
	ASSERT(num_feat>0);
	int32_t num_threads=parallel->get_num_threads();
	ASSERT(num_threads>0);
	int32_t* vec=new int32_t[num_threads*num_feat];

	if (num_threads < 2)
	{
#ifdef CYGWIN
		for (int32_t j=0; j<num_feat; j++)
#else
        CSignal::clear_cancel();
		for (int32_t j=0; j<num_feat && !CSignal::cancel_computations(); j++)
#endif
		{
			init_optimization(num_suppvec, IDX, alphas, j);
			S_THREAD_PARAM params;
			params.vec=vec;
			params.result=result;
			params.weights=weights;
			params.kernel=this;
			params.tries=tries;
			params.factor=factor;
			params.j=j;
			params.start=0;
			params.end=num_vec;
			params.length=length;
			params.vec_idx=vec_idx;
			compute_batch_helper((void*) &params);

			SG_PROGRESS(j,0,num_feat);
		}
	}
#ifndef WIN32
	else
	{
        CSignal::clear_cancel();
		for (int32_t j=0; j<num_feat && !CSignal::cancel_computations(); j++)
		{
			init_optimization(num_suppvec, IDX, alphas, j);
			pthread_t* threads = new pthread_t[num_threads-1];
			S_THREAD_PARAM* params = new S_THREAD_PARAM[num_threads];
			int32_t step= num_vec/num_threads;
			int32_t t;

			for (t=0; t<num_threads-1; t++)
			{
				params[t].vec=&vec[num_feat*t];
				params[t].result=result;
				params[t].weights=weights;
				params[t].kernel=this;
				params[t].tries=tries;
				params[t].factor=factor;
				params[t].j=j;
				params[t].start = t*step;
				params[t].end = (t+1)*step;
				params[t].length=length;
				params[t].vec_idx=vec_idx;
				pthread_create(&threads[t], NULL, CWeightedDegreeStringKernel::compute_batch_helper, (void*)&params[t]);
			}
			params[t].vec=&vec[num_feat*t];
			params[t].result=result;
			params[t].weights=weights;
			params[t].kernel=this;
			params[t].tries=tries;
			params[t].factor=factor;
			params[t].j=j;
			params[t].start=t*step;
			params[t].end=num_vec;
			params[t].length=length;
			params[t].vec_idx=vec_idx;
			compute_batch_helper((void*) &params[t]);

			for (t=0; t<num_threads-1; t++)
				pthread_join(threads[t], NULL);
			SG_PROGRESS(j,0,num_feat);

			delete[] params;
			delete[] threads;
		}
	}
#endif

	delete[] vec;

	//really also free memory as this can be huge on testing especially when
	//using the combined kernel
	create_empty_tries();
}

bool CWeightedDegreeStringKernel::set_max_mismatch(int32_t max)
{
	if (type==E_EXTERNAL && max!=0) {
		return false;
	}

	max_mismatch=max;

	if (lhs!=NULL && rhs!=NULL)
		return init(lhs, rhs);
	else
		return true;
}