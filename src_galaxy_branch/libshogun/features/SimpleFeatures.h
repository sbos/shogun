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

#ifndef _SIMPLEFEATURES__H__
#define _SIMPLEFEATURES__H__

#include "lib/common.h"
#include "lib/Mathematics.h"
#include "lib/Cache.h"
#include "lib/io.h"
#include "lib/Cache.h"
#include "lib/File.h"
#include "preproc/SimplePreProc.h"
#include "features/DotFeatures.h"
#include "features/StringFeatures.h"

#include <string.h>

template <class ST> class CStringFeatures;
template <class ST> class CSimpleFeatures;
template <class ST> class CSimplePreProc;

/** @brief The class SimpleFeatures implements dense feature matrices.
 *
 * The feature matrices are stored en-block in memory in fortran order, i.e.
 * column-by-column, where a column denotes a feature vector.
 *
 * There are get_num_vectors() many feature vectors, of dimension
 * get_num_features(). To access a feature vector call
 * get_feature_vector() and when you are done treating it call
 * free_feature_vector(). While free_feature_vector() is a NOP in most cases
 * feature vectors might have been generated on the fly (due to a number
 * preprocessors being attached to them).
 *
 * From this template class a number the following dense feature matrix types
 * are used and supported:
 *
 * \li bool matrix - CSimpleFeatures<bool>
 * \li 8bit char matrix - CSimpleFeatures<char>
 * \li 8bit Byte matrix - CSimpleFeatures<uint8_t>
 * \li 16bit Integer matrix - CSimpleFeatures<int16_t>
 * \li 16bit Word matrix - CSimpleFeatures<uint16_t>
 * \li 32bit Integer matrix - CSimpleFeatures<int32_t>
 * \li 32bit Unsigned Integer matrix - CSimpleFeatures<uint32_t>
 * \li 32bit Float matrix - CSimpleFeatures<float32_t>
 * \li 64bit Float matrix - CSimpleFeatures<float64_t>
 * \li 64bit Float matrix <b>in a file</b> - CRealFileFeatures
 * \li 64bit Tangent of posterior log-odds (TOP) features from HMM - CTOPFeatures
 * \li 64bit Fisher Kernel (FK) features from HMM - CTOPFeatures
 * \li 96bit Float matrix - CSimpleFeatures<floatmax_t>
 */
template <class ST> class CSimpleFeatures: public CDotFeatures
{
	public:
		/** constructor
		 *
		 * @param size cache size
		 */
		CSimpleFeatures(int32_t size=0)
		: CDotFeatures(size), num_vectors(0), num_features(0),
			feature_matrix(NULL), feature_cache(NULL) {}

		/** copy constructor */
		CSimpleFeatures(const CSimpleFeatures & orig)
		: CDotFeatures(orig), num_vectors(orig.num_vectors),
			num_features(orig.num_features),
			feature_matrix(orig.feature_matrix),
			feature_cache(orig.feature_cache)
		{
			if (orig.feature_matrix)
			{
				free_feature_matrix();
				feature_matrix=new ST(num_vectors*num_features);
				memcpy(feature_matrix, orig.feature_matrix, sizeof(float64_t)*num_vectors*num_features);
			}
		}

		/** constructor
		 *
		 * @param src feature matrix
		 * @param num_feat number of features in matrix
		 * @param num_vec number of vectors in matrix
		 */
		CSimpleFeatures(ST* src, int32_t num_feat, int32_t num_vec)
		: CDotFeatures(0), num_vectors(0), num_features(0),
			feature_matrix(NULL), feature_cache(NULL)
		{
			copy_feature_matrix(src, num_feat, num_vec);
		}

		/** constructor
		 *
		 * NOT IMPLEMENTED!
		 *
		 * @param fname filename to load features from
		 */
		CSimpleFeatures(char* fname)
		: CDotFeatures(fname), num_vectors(0), num_features(0),
			feature_matrix(NULL), feature_cache(NULL) {}

		/** duplicate feature object
		 *
		 * @return feature object
		 */
		virtual CFeatures* duplicate() const
		{
			return new CSimpleFeatures<ST>(*this);
		}

		virtual ~CSimpleFeatures()
		{
			SG_DEBUG("deleting simplefeatures (0x%p)\n", this);
			free_features();
		}

		/** free feature matrix
		 *
		 */
		void free_feature_matrix()
		{
            delete[] feature_matrix;
            feature_matrix = NULL;
            num_vectors=0;
            num_features=0;
		}

		/** free feature matrix and cache
		 *
		 */
		void free_features()
		{
			free_feature_matrix();
            delete feature_cache;
            feature_cache = NULL;
		}

		/** get feature vector
		 * for sample num from the matrix as it is if matrix is
		 * initialized, else return preprocessed compute_feature_vector
		 *
		 * @param num index of feature vector
		 * @param len length is returned by reference
		 * @param dofree whether returned vector must be freed by
		 * caller via free_feature_vector
		 * @return feature vector
		 */
		ST* get_feature_vector(int32_t num, int32_t& len, bool& dofree)
		{
			len=num_features;

			if (feature_matrix)
			{
				dofree=false;
				return &feature_matrix[num*num_features];
			} 
			else
			{
				SG_DEBUG( "compute feature!!!\n") ;

				ST* feat=NULL;
				dofree=false;

				if (feature_cache)
				{
					feat=feature_cache->lock_entry(num);

					if (feat)
						return feat;
					else
					{
						feat=feature_cache->set_entry(num);
					}
				}

				if (!feat)
					dofree=true;
				feat=compute_feature_vector(num, len, feat);


				if (get_num_preproc())
				{
					int32_t tmp_len=len;
					ST* tmp_feat_before = feat;
					ST* tmp_feat_after = NULL;

					for (int32_t i=0; i<get_num_preproc(); i++)
					{
						CSimplePreProc<ST>* p = (CSimplePreProc<ST>*) get_preproc(i);
						tmp_feat_after=p->apply_to_feature_vector(tmp_feat_before, tmp_len);
						SG_UNREF(p);

						if (i!=0)	// delete feature vector, except for the the first one, i.e., feat
							delete[] tmp_feat_before;
						tmp_feat_before=tmp_feat_after;
					}

					memcpy(feat, tmp_feat_after, sizeof(ST)*tmp_len);
					delete[] tmp_feat_after;

					len=tmp_len ;
					SG_DEBUG( "len: %d len2: %d\n", len, num_features);
				}
				return feat ;
			}
		}

		/** set feature vector num
		 *
		 * ( only available in-memory feature matrices )
		 *
		 * @param src vector
		 * @param len length of vector
		 * @param num index where to put vector to
		 */
		void set_feature_vector(ST* src, int32_t len, int32_t num)
		{
			if (num>=num_vectors)
			{
				SG_ERROR("Index out of bounds (number of vectors %d, you "
						"requested %d)\n", num_vectors, num);
			}

			if (!feature_matrix)
				SG_ERROR("Requires a in-memory feature matrix\n");

			if (len != num_features)
				SG_ERROR("Vector not of length %d (has %d)\n", num_features, len);

			memcpy(&feature_matrix[num*num_features], src, num_features*sizeof(ST));
		}

		/** get feature vector num
		 *
		 * @param dst destination to store vector in
		 * @param len length of vector
		 * @param num index of vector
		 */
		void get_feature_vector(ST** dst, int32_t* len, int32_t num)
		{
			if (num>=num_vectors)
			{
				SG_ERROR("Index out of bounds (number of vectors %d, you "
						"requested %d)\n", num_vectors, num);
			}

			int32_t vlen=0;
			bool free_vec;

			ST* vec= get_feature_vector(num, vlen, free_vec);

			*len=vlen;
			*dst=(ST*) malloc(vlen*sizeof(ST));
			memcpy(*dst, vec, vlen*sizeof(ST));

			free_feature_vector(vec, num, free_vec);
		}

		/** free feature vector
		 *
		 * @param feat_vec feature vector to free
		 * @param num index in feature cache
		 * @param dofree if vector should be really deleted
		 */
		void free_feature_vector(ST* feat_vec, int32_t num, bool dofree)
		{
			if (feature_cache)
				feature_cache->unlock_entry(num);

			if (dofree)
				delete[] feat_vec ;
		}

		/** get a copy of the feature matrix
		 * num_feat,num_vectors are returned by reference
		 *
		 * @param dst destination to store matrix in
		 * @param num_feat number of features (rows of matrix)
		 * @param num_vec number of vectors (columns of matrix)
		 */
		void get_feature_matrix(ST** dst, int32_t* num_feat, int32_t* num_vec)
		{
			ASSERT(feature_matrix);

			int64_t num=num_features*num_vectors;
			*num_feat=num_features;
			*num_vec=num_vectors;
			*dst=(ST*) malloc(sizeof(ST)*num);
			memcpy(*dst, feature_matrix, num * sizeof(ST));
		}

		/** get the pointer to the feature matrix
		 * num_feat,num_vectors are returned by reference
		 *
		 * @param num_feat number of features in matrix
		 * @param num_vec number of vectors in matrix
		 * @return feature matrix
		 */
		ST* get_feature_matrix(int32_t &num_feat, int32_t &num_vec)
		{
			num_feat=num_features;
			num_vec=num_vectors;
			return feature_matrix;
		}

		/** set feature matrix
		 * necessary to set feature_matrix, num_features,
		 * num_vectors, where num_features is the column offset,
		 * and columns are linear in memory
		 * see below for definition of feature_matrix
		 *
		 * @param fm feature matrix to se
		 * @param num_feat number of features in matrix
		 * @param num_vec number of vectors in matrix
		 */
		virtual void set_feature_matrix(ST* fm, int32_t num_feat, int32_t num_vec)
		{
			free_feature_matrix();
			feature_matrix=fm;
			num_features=num_feat;
			num_vectors=num_vec;
		}

		/** copy feature matrix
		 * store copy of feature_matrix, where num_features is the
		 * column offset, and columns are linear in memory
		 * see below for definition of feature_matrix
		 *
		 * @param src feature matrix to copy
		 * @param num_feat number of features in matrix
		 * @param num_vec number of vectors in matrix
		 */
		virtual void copy_feature_matrix(ST* src, int32_t num_feat, int32_t num_vec)
		{
			free_feature_matrix();
			feature_matrix=new ST[((int64_t) num_feat)*num_vec];
			memcpy(feature_matrix, src, (sizeof(ST)*((int64_t) num_feat)*num_vec));

			num_features=num_feat;
			num_vectors=num_vec;
		}

		/** apply preprocessor
		 *
		 * @param force_preprocessing if preprocssing shall be forced
		 * @return if applying was successful
		 */
		virtual bool apply_preproc(bool force_preprocessing=false)
		{
			SG_DEBUG( "force: %d\n", force_preprocessing);

			if ( feature_matrix && get_num_preproc())
			{

				for (int32_t i=0; i<get_num_preproc(); i++)
				{ 
					if ( (!is_preprocessed(i) || force_preprocessing) )
					{
						set_preprocessed(i);
						CSimplePreProc<ST>* p = (CSimplePreProc<ST>*) get_preproc(i);
						SG_INFO( "preprocessing using preproc %s\n", p->get_name());
						if (p->apply_to_feature_matrix(this) == NULL)
						{
							SG_UNREF(p);
							return false;
						}
						SG_UNREF(p);
					}
				}
				return true;
			}
			else
			{
				if (!feature_matrix)
					SG_ERROR( "no feature matrix\n");

				if (!get_num_preproc())
					SG_ERROR( "no preprocessors available\n");

				return false;
			}
		}

		/** get memory footprint of one feature
		 *
		 * @return memory footprint of one feature
		 */
		virtual int32_t get_size() { return sizeof(ST); }


		/** get number of feature vectors
		 *
		 * @return number of feature vectors
		 */
		virtual inline int32_t  get_num_vectors() { return num_vectors; }

		/** get number of features
		 *
		 * @return number of features
		 */
		inline int32_t  get_num_features() { return num_features; }

		/** set number of features
		 *
		 * @param num number to set
		 */
		inline void set_num_features(int32_t num)
		{ 
			num_features= num;

			if (num_features && num_vectors)
			{
				delete feature_cache;
				feature_cache= new CCache<ST>(get_cache_size(), num_features, num_vectors);
			}
		}

		/** set number of vectors
		 *
		 * @param num number to set
		 */
		inline void set_num_vectors(int32_t num)
		{
			num_vectors= num;
			if (num_features && num_vectors)
			{
				delete feature_cache;
				feature_cache= new CCache<ST>(get_cache_size(), num_features, num_vectors);
			}
		}

		/** get feature class
		 *
		 * @return feature class SIMPLE
		 */
		inline virtual EFeatureClass get_feature_class() { return C_SIMPLE; }

		/** get feature type
		 *
		 * @return templated feature type
		 */
		inline virtual EFeatureType get_feature_type();

		/** reshape
		 *
		 * @param p_num_features new number of features
		 * @param p_num_vectors new number of vectors
		 * @return if reshaping was successful
		 */
		virtual bool reshape(int32_t p_num_features, int32_t p_num_vectors)
		{
			if (p_num_features*p_num_vectors == this->num_features * this->num_vectors)
			{
				this->num_features=p_num_features;
				this->num_vectors=p_num_vectors;
				return true;
			}
			else
				return false;
		}

		/** obtain the dimensionality of the feature space
		 *
		 * (not mix this up with the dimensionality of the input space, usually
		 * obtained via get_num_features())
		 *
		 * @return dimensionality
		 */
		virtual int32_t get_dim_feature_space()
		{
			return num_features;
		}

		/** compute dot product between vector1 and vector2,
		 * appointed by their indices
		 *
		 * @param vec_idx1 index of first vector
		 * @param vec_idx2 index of second vector
		 */
		virtual float64_t dot(int32_t vec_idx1, int32_t vec_idx2)
		{
			int32_t len1, len2;
			bool free1, free2;

			ST* vec1= get_feature_vector(vec_idx1, len1, free1);
			ST* vec2= get_feature_vector(vec_idx2, len2, free2);

			float64_t result=CMath::dot(vec1, vec2, len1);

			free_feature_vector(vec1, vec_idx1, free1);
			free_feature_vector(vec2, vec_idx2, free2);

			return result;
		}

		/** compute dot product between vector1 and a dense vector
		 *
		 * @param vec_idx1 index of first vector
		 * @param vec2 pointer to real valued vector
		 * @param vec2_len length of real valued vector
		 */
		virtual float64_t dense_dot(int32_t vec_idx1, const float64_t* vec2, int32_t vec2_len);

		/** add vector 1 multiplied with alpha to dense vector2
		 *
		 * @param alpha scalar alpha
		 * @param vec_idx1 index of first vector
		 * @param vec2 pointer to real valued vector
		 * @param vec2_len length of real valued vector
		 * @param abs_val if true add the absolute value
		 */
		virtual void add_to_dense_vec(float64_t alpha, int32_t vec_idx1, float64_t* vec2, int32_t vec2_len, bool abs_val=false)
		{
			ASSERT(vec2_len == num_features);

			int32_t vlen;
			bool vfree;
			ST* vec1=get_feature_vector(vec_idx1, vlen, vfree);

			ASSERT(vlen == num_features);

			if (abs_val)
			{
				for (int32_t i=0; i<num_features; i++)
					vec2[i]+=alpha*CMath::abs(vec1[i]);
			}
			else
			{
				for (int32_t i=0; i<num_features; i++)
					vec2[i]+=alpha*vec1[i];
			}

			free_feature_vector(vec1, vec_idx1, vfree);
		}

		/** get number of non-zero features in vector
		 *
		 * @param num which vector
		 * @return number of non-zero features in vector
		 */
		virtual inline int32_t get_nnz_features_for_vector(int32_t num)
		{
			return num_features;
		}

		/** align char features
		 *
		 * @param cf char features
		 * @param Ref other char features
		 * @param gapCost gap cost
		 * @return if aligning was successful
		 */
		virtual inline bool Align_char_features(
			CStringFeatures<char>* cf, CStringFeatures<char>* Ref, float64_t gapCost)
		{
			return false;
		}

		/** load features from file
		 *
		 * @param fname filename to load from
		 * @return if loading was successful
		 */
		virtual bool load(char* fname)
		{
			bool status=false;
			num_vectors=1;
			num_features=0;
			CFile f(fname, 'r', get_feature_type());
			int64_t numf=0;
			free_feature_matrix();
			feature_matrix=f.load_data<ST>(NULL, numf);
			num_features=numf;

			if (!f.is_ok())
				SG_ERROR( "loading file \"%s\" failed", fname);
			else
				status=true;

			return status;
		}

		/** save features to file
		 *
		 * @param fname filename to save to
		 * @return if saving was successful
		 */
		virtual bool save(char* fname)
		{
			int32_t len;
			bool free;
			ST* fv;

			CFile f(fname, 'w', get_feature_type());

			for (int32_t i=0; i< (int32_t) num_vectors && f.is_ok(); i++)
			{
				if (!(i % (num_vectors/10+1)))
					SG_PRINT( "%02d%%.", (int) (100.0*i/num_vectors));
				else if (!(i % (num_vectors/200+1)))
					SG_PRINT( ".");

				fv=get_feature_vector(i, len, free);
				f.save_data<ST>(fv, len);
				free_feature_vector(fv, i, free) ;
			}

			if (f.is_ok())
				SG_INFO( "%d vectors with %d features each successfully written (filesize: %ld)\n", num_vectors, num_features, num_vectors*num_features*sizeof(float64_t));

			return true;
		}

		/** @return object name */
		inline virtual const char* get_name() const { return "SimpleFeatures"; }

	protected:
		/** compute feature vector for sample num
		 * if target is set the vector is written to target
		 * len is returned by reference
		 *
		 * NOT IMPLEMENTED!
		 *
		 * @param num num
		 * @param len len
		 * @param target
		 * @return feature vector
		 */
		virtual ST* compute_feature_vector(int32_t num, int32_t& len, ST* target=NULL)
		{
			len=0;
			return NULL;
		}

		/// number of vectors in cache
		int32_t num_vectors;

		/// number of features in cache
		int32_t num_features;

		/** feature matrix */
		ST* feature_matrix;

		/** feature cache */
		CCache<ST>* feature_cache;
};

#ifndef DOXYGEN_SHOULD_SKIP_THIS
/** get feature type the BOOL feature can deal with
 *
 * @return feature type BOOL
 */
template<> inline EFeatureType CSimpleFeatures<bool>::get_feature_type()
{
	return F_BOOL;
}

/** get feature type the CHAR feature can deal with
 *
 * @return feature type CHAR
 */
template<> inline EFeatureType CSimpleFeatures<char>::get_feature_type()
{
	return F_CHAR;
}

/** get feature type the BYTE feature can deal with
 *
 * @return feature type BYTE
 */
template<> inline EFeatureType CSimpleFeatures<uint8_t>::get_feature_type()
{
	return F_BYTE;
}

/** get feature type the SHORT feature can deal with
 *
 * @return feature type SHORT
 */
template<> inline EFeatureType CSimpleFeatures<int16_t>::get_feature_type()
{
	return F_SHORT;
}

/** get feature type the WORD feature can deal with
 *
 * @return feature type WORD
 */
template<> inline EFeatureType CSimpleFeatures<uint16_t>::get_feature_type()
{
	return F_WORD;
}


/** get feature type the INT feature can deal with
 *
 * @return feature type INT
 */
template<> inline EFeatureType CSimpleFeatures<int32_t>::get_feature_type()
{
	return F_INT;
}

/** get feature type the UINT feature can deal with
 *
 * @return feature type UINT
 */
template<> inline EFeatureType CSimpleFeatures<uint32_t>::get_feature_type()
{
	return F_UINT;
}

/** get feature type the LONG feature can deal with
 *
 * @return feature type LONG
 */
template<> inline EFeatureType CSimpleFeatures<int64_t>::get_feature_type()
{
	return F_LONG;
}

/** get feature type the ULONG feature can deal with
 *
 * @return feature type ULONG
 */
template<> inline EFeatureType CSimpleFeatures<uint64_t>::get_feature_type()
{
	return F_ULONG;
}

/** get feature type the SHORTREAL feature can deal with
 *
 * @return feature type SHORTREAL
 */
template<> inline EFeatureType CSimpleFeatures<float32_t>::get_feature_type()
{
	return F_SHORTREAL;
}

/** get feature type the DREAL feature can deal with
 *
 * @return feature type DREAL
 */
template<> inline EFeatureType CSimpleFeatures<float64_t>::get_feature_type()
{
	return F_DREAL;
}

/** get feature type the LONGREAL feature can deal with
 *
 * @return feature type LONGREAL
 */
template<> inline EFeatureType CSimpleFeatures<floatmax_t>::get_feature_type()
{
	return F_LONGREAL;
}

/** @return object name */
template<> inline const char* CSimpleFeatures<bool>::get_name() const
{
	return "BoolFeatures";
}

/** @return object name */
template<> inline const char* CSimpleFeatures<char>::get_name() const
{
	return "CharFeatures";
}

/** @return object name */
template<> inline const char* CSimpleFeatures<uint8_t>::get_name() const
{
	return "ByteFeatures";
}

/** @return object name */
template<> inline const char* CSimpleFeatures<int16_t>::get_name() const
{
	return "ShortFeatures";
}

/** @return object name */
template<> inline const char* CSimpleFeatures<uint16_t>::get_name() const
{
	return "WordFeatures";
}

/** @return object name */
template<> inline const char* CSimpleFeatures<int32_t>::get_name() const
{
	return "IntFeatures";
}

/** @return object name */
template<> inline const char* CSimpleFeatures<uint32_t>::get_name() const
{
	return "UIntFeatures";
}

/** @return object name */
template<> inline const char* CSimpleFeatures<int64_t>::get_name() const
{
	return "LongIntFeatures";
}

/** @return object name */
template<> inline const char* CSimpleFeatures<uint64_t>::get_name() const
{
	return "ULongIntFeatures";
}

/** @return object name */
template<> inline const char* CSimpleFeatures<float32_t>::get_name() const
{
	return "ShortRealFeatures";
}

/** @return object name */
template<> inline const char* CSimpleFeatures<float64_t>::get_name() const
{
	return "RealFeatures";
}

/** @return object name */
template<> inline const char* CSimpleFeatures<floatmax_t>::get_name() const
{
	return "LongRealFeatures";
}

template<> inline bool CSimpleFeatures<float64_t>::Align_char_features(
		CStringFeatures<char>* cf, CStringFeatures<char>* Ref, float64_t gapCost)
{
	ASSERT(cf);
	/*num_vectors=cf->get_num_vectors();
	num_features=Ref->get_num_vectors();

	int64_t len=((int64_t) num_vectors)*num_features;
	free_feature_matrix();
	feature_matrix=new float64_t[len];
	int32_t num_cf_feat=0;
	int32_t num_cf_vec=0;
	int32_t num_ref_feat=0;
	int32_t num_ref_vec=0;
	char* fm_cf=NULL; //cf->get_feature_matrix(num_cf_feat, num_cf_vec);
	char* fm_ref=NULL; //Ref->get_feature_matrix(num_ref_feat, num_ref_vec);

	ASSERT(num_cf_vec==num_vectors);
	ASSERT(num_ref_vec==num_features);

	SG_INFO( "computing aligments of %i vectors to %i reference vectors: ", num_cf_vec, num_ref_vec) ;
	for (int32_t i=0; i< num_ref_vec; i++)
	{
		SG_PROGRESS(i, num_ref_vec) ;
		for (int32_t j=0; j<num_cf_vec; j++)
			feature_matrix[i+j*num_features] = CMath::Align(&fm_cf[j*num_cf_feat], &fm_ref[i*num_ref_feat], num_cf_feat, num_ref_feat, gapCost);
	} ;

	SG_INFO( "created %i x %i matrix (0x%p)\n", num_features, num_vectors, feature_matrix) ;*/
	return true;
}

template<> inline float64_t CSimpleFeatures<bool>:: dense_dot(int32_t vec_idx1, const float64_t* vec2, int32_t vec2_len)
{
	ASSERT(vec2_len == num_features);

	int32_t vlen;
	bool vfree;
	bool* vec1= get_feature_vector(vec_idx1, vlen, vfree);

	ASSERT(vlen == num_features);
	float64_t result=0;

	for (int32_t i=0 ; i<num_features; i++)
		result+=vec1[i] ? vec2[i] : 0;

	free_feature_vector(vec1, vec_idx1, vfree);

	return result;
}


template<> inline float64_t CSimpleFeatures<char>:: dense_dot(int32_t vec_idx1, const float64_t* vec2, int32_t vec2_len)
{
	ASSERT(vec2_len == num_features);

	int32_t vlen;
	bool vfree;
	char* vec1= get_feature_vector(vec_idx1, vlen, vfree);

	ASSERT(vlen == num_features);
	float64_t result=0;

	for (int32_t i=0 ; i<num_features; i++)
		result+=vec1[i]*vec2[i];

	free_feature_vector(vec1, vec_idx1, vfree);

	return result;
}

template<> inline float64_t CSimpleFeatures<uint8_t>:: dense_dot(int32_t vec_idx1, const float64_t* vec2, int32_t vec2_len)
{
	ASSERT(vec2_len == num_features);

	int32_t vlen;
	bool vfree;
	uint8_t* vec1= get_feature_vector(vec_idx1, vlen, vfree);

	ASSERT(vlen == num_features);
	float64_t result=0;

	for (int32_t i=0 ; i<num_features; i++)
		result+=vec1[i]*vec2[i];

	free_feature_vector(vec1, vec_idx1, vfree);

	return result;
}

template<> inline float64_t CSimpleFeatures<int16_t>:: dense_dot(int32_t vec_idx1, const float64_t* vec2, int32_t vec2_len)
{
	ASSERT(vec2_len == num_features);

	int32_t vlen;
	bool vfree;
	int16_t* vec1= get_feature_vector(vec_idx1, vlen, vfree);

	ASSERT(vlen == num_features);
	float64_t result=0;

	for (int32_t i=0 ; i<num_features; i++)
		result+=vec1[i]*vec2[i];

	free_feature_vector(vec1, vec_idx1, vfree);

	return result;
}


template<> inline float64_t CSimpleFeatures<uint16_t>:: dense_dot(int32_t vec_idx1, const float64_t* vec2, int32_t vec2_len)
{
	ASSERT(vec2_len == num_features);

	int32_t vlen;
	bool vfree;
	uint16_t* vec1= get_feature_vector(vec_idx1, vlen, vfree);

	ASSERT(vlen == num_features);
	float64_t result=0;

	for (int32_t i=0 ; i<num_features; i++)
		result+=vec1[i]*vec2[i];

	free_feature_vector(vec1, vec_idx1, vfree);

	return result;
}

template<> inline float64_t CSimpleFeatures<int32_t>:: dense_dot(int32_t vec_idx1, const float64_t* vec2, int32_t vec2_len)
{
	ASSERT(vec2_len == num_features);

	int32_t vlen;
	bool vfree;
	int32_t* vec1= get_feature_vector(vec_idx1, vlen, vfree);

	ASSERT(vlen == num_features);
	float64_t result=0;

	for (int32_t i=0 ; i<num_features; i++)
		result+=vec1[i]*vec2[i];

	free_feature_vector(vec1, vec_idx1, vfree);

	return result;
}

template<> inline float64_t CSimpleFeatures<uint32_t>:: dense_dot(int32_t vec_idx1, const float64_t* vec2, int32_t vec2_len)
{
	ASSERT(vec2_len == num_features);

	int32_t vlen;
	bool vfree;
	uint32_t* vec1= get_feature_vector(vec_idx1, vlen, vfree);

	ASSERT(vlen == num_features);
	float64_t result=0;

	for (int32_t i=0 ; i<num_features; i++)
		result+=vec1[i]*vec2[i];

	free_feature_vector(vec1, vec_idx1, vfree);

	return result;
}

template<> inline float64_t CSimpleFeatures<int64_t>:: dense_dot(int32_t vec_idx1, const float64_t* vec2, int32_t vec2_len)
{
	ASSERT(vec2_len == num_features);

	int32_t vlen;
	bool vfree;
	int64_t* vec1= get_feature_vector(vec_idx1, vlen, vfree);

	ASSERT(vlen == num_features);
	float64_t result=0;

	for (int32_t i=0 ; i<num_features; i++)
		result+=vec1[i]*vec2[i];

	free_feature_vector(vec1, vec_idx1, vfree);

	return result;
}

template<> inline float64_t CSimpleFeatures<uint64_t>:: dense_dot(int32_t vec_idx1, const float64_t* vec2, int32_t vec2_len)
{
	ASSERT(vec2_len == num_features);

	int32_t vlen;
	bool vfree;
	uint64_t* vec1= get_feature_vector(vec_idx1, vlen, vfree);

	ASSERT(vlen == num_features);
	float64_t result=0;

	for (int32_t i=0 ; i<num_features; i++)
		result+=vec1[i]*vec2[i];

	free_feature_vector(vec1, vec_idx1, vfree);

	return result;
}

template<> inline float64_t CSimpleFeatures<float32_t>:: dense_dot(int32_t vec_idx1, const float64_t* vec2, int32_t vec2_len)
{
	ASSERT(vec2_len == num_features);

	int32_t vlen;
	bool vfree;
	float32_t* vec1= get_feature_vector(vec_idx1, vlen, vfree);

	ASSERT(vlen == num_features);
	float64_t result=0;

	for (int32_t i=0 ; i<num_features; i++)
		result+=vec1[i]*vec2[i];

	free_feature_vector(vec1, vec_idx1, vfree);

	return result;
}

template<> inline float64_t CSimpleFeatures<float64_t>:: dense_dot(int32_t vec_idx1, const float64_t* vec2, int32_t vec2_len)
{
	ASSERT(vec2_len == num_features);

	int32_t vlen;
	bool vfree;
	float64_t* vec1= get_feature_vector(vec_idx1, vlen, vfree);

	ASSERT(vlen == num_features);
	float64_t result=CMath::dot(vec1, vec2, num_features);

	free_feature_vector(vec1, vec_idx1, vfree);

	return result;
}

template<> inline float64_t CSimpleFeatures<floatmax_t>:: dense_dot(int32_t vec_idx1, const float64_t* vec2, int32_t vec2_len)
{
	ASSERT(vec2_len == num_features);

	int32_t vlen;
	bool vfree;
	floatmax_t* vec1= get_feature_vector(vec_idx1, vlen, vfree);

	ASSERT(vlen == num_features);
	float64_t result=0;

	for (int32_t i=0 ; i<num_features; i++)
		result+=vec1[i]*vec2[i];

	free_feature_vector(vec1, vec_idx1, vfree);

	return result;
}
#endif // DOXYGEN_SHOULD_SKIP_THIS
#endif // _SIMPLEFEATURES__H__