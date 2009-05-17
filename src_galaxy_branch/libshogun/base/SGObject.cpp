/*
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 *
 * Written (W) 2008-2009 Soeren Sonnenburg
 * Copyright (C) 2008-2009 Fraunhofer Institute FIRST and Max Planck Society
 */

#include "base/SGObject.h"
#include "lib/io.h"
#include "lib/Mathematics.h"
#include "base/Parallel.h"
#include "base/init.h"
#include "base/Version.h"

#include <stdlib.h>
#include <stdio.h>

#ifdef HAVE_BOOST_SERIALIZATION
#include <boost/serialization/access.hpp>
#include <boost/archive/text_oarchive.hpp>
#include <boost/archive/text_iarchive.hpp>
#include <boost/archive/binary_oarchive.hpp>
#include <boost/archive/binary_iarchive.hpp>
#include <boost/serialization/export.hpp>
//#include <boost/serialization/vector.hpp>
//
////TODO xml will not work right away, every class needs name-value-pairs (NVP)
////TODO we SHOULD FIX THIS NOW!
////will have to be defined using respective boost macros
////#include <boost/archive/xml_oarchive.hpp>
////#include <boost/archive/xml_iarchive.hpp>
////some STL modules needed for serialization
#include <sstream>
#include <iostream>
#include <string>
#include <fstream>
//#include <vector>
BOOST_IS_ABSTRACT(CSGObject);
#endif //HAVE_BOOST_SERIALIZATION


extern CParallel* sg_parallel;
extern CIO* sg_io;
extern CVersion* sg_version;
extern CMath* sg_math;

void CSGObject::set_global_objects()
{
	if (!sg_io || !sg_parallel || !sg_version)
	{
		fprintf(stderr, "call init_shogun() before using the library, dying.\n");
		exit(1);
	}

	SG_REF(sg_io);
	SG_REF(sg_parallel);
	SG_REF(sg_version);

	io=sg_io;
	parallel=sg_parallel;
	version=sg_version;
}

#ifdef HAVE_BOOST_SERIALIZATION
std::string CSGObject::to_string() const
{
	std::ostringstream s;
	boost::archive::text_oarchive oa(s);
	oa << this;
	return s.str();
}

void CSGObject::from_string(std::string str)
{
	std::istringstream is(str);
	boost::archive::text_iarchive ia(is);

	//cast away constness
	CSGObject* tmp = const_cast<CSGObject*>(this);

	ia >> tmp;
	*this = *tmp;
}

void CSGObject::to_file(std::string filename) const
{
	std::ofstream os(filename.c_str(), std::ios::binary);
	boost::archive::binary_oarchive oa(os);
	oa << this;
}

void CSGObject::from_file(std::string filename)
{
	std::ifstream is(filename.c_str(), std::ios::binary);
	boost::archive::binary_iarchive ia(is);
	CSGObject* tmp= const_cast<CSGObject*>(this);
	ia >> tmp; 
}
#endif //HAVE_BOOST_SERIALIZATION