from numpy import *
from numpy.random import *
from shogun.Features import *
from shogun.Kernel import *
from shogun.Library import FULL_NORMALIZATION
from shogun.Classifier import *
from shogun.Distance import *

import fileop
import featop
import dataop
from config import KERNEL, T_KERNEL

##################################################################
## subkernel funs
##################################################################

def _compute_subkernels (name, feats, kernel, output):
	output['name']=name
	kernel.init(feats['train'], feats['train'])
	output['km_train']=kernel.get_kernel_matrix()
	kernel.init(feats['train'], feats['test'])
	output['km_test']=kernel.get_kernel_matrix()
	output.update(fileop.get_output_params(name, T_KERNEL))

	fileop.write(T_KERNEL, output)

def _get_subkernel_args (subkernel):
	args=''
	i=0
	while 1:
		try:
			args+=', '+(str(subkernel[1+i]))
			i+=1
		except IndexError:
			break

	return args

def _get_subkernel_output_params (subkernel, data, num):
	prefix='subkernel'+num+'_'
	output={}

	output[prefix+'name']=subkernel[0]
	#FIXME: size soon to be removed from constructor
	output[prefix+'kernel_arg0_size']='10'
	output[prefix+'data_train']=matrix(data['train'])
	output[prefix+'data_test']=matrix(data['test'])
	output.update(fileop.get_output_params(
		subkernel[0], T_KERNEL, subkernel[1:], prefix, 1))

	return output

def _run_auc ():
	data=dataop.get_rand()
	feats=featop.get_simple('Real', data)
	width=1.5
	subkernels=[['Gaussian', width]]
	sk=GaussianKernel(feats['train'], feats['test'], width)
	output=_get_subkernel_output_params(subkernels[0], data, '0')

	data=dataop.get_rand(ushort, rows=2, max_train=dataop.LEN_TRAIN,
		max_test=dataop.LEN_TEST)
	feats=featop.get_simple('Word', data)
	#FIXME: size soon to be removed from constructor
	kernel=AUCKernel(10, sk)
	output['data_train']=matrix(data['train'])
	output['data_test']=matrix(data['test'])

	_compute_subkernels('AUC', feats, kernel, output)

def _run_combined ():
	kernel=CombinedKernel()
	feats={'train':CombinedFeatures(), 'test':CombinedFeatures()}
	subkernels=[
		['FixedDegreeString', 3],
		['PolyMatchString', 3, True],
		['LinearString'],
#		['Gaussian', 1.7],
	]
	output={}

	for i in range(0, len(subkernels)):
		str_i=str(i)
		kdata=KERNEL[subkernels[i][0]]
		args=_get_subkernel_args(subkernels[i])
		#FIXME: size soon to be removed from constructor
		sk=eval(subkernels[i][0]+'Kernel(10'+args+')')
		kernel.append_kernel(sk)
		data_sk=eval('dataop.get_'+kdata[0][0]+'('+kdata[0][1]+')')
		feats_sk=eval('featop.get_'+kdata[1][0]+"('"+kdata[1][1]+"', data_sk)")
		feats['train'].append_feature_obj(feats_sk['train'])
		feats['test'].append_feature_obj(feats_sk['test'])
		output.update(_get_subkernel_output_params(
			subkernels[i], data_sk, str(i)))

	_compute_subkernels('Combined', feats, kernel, output)

def _run_subkernels ():
	_run_auc()
	_run_combined()

##################################################################
## compute/kernel funcs
##################################################################

def _compute (name, feats, data, *args):
	fun=eval(name+'Kernel')
	kernel=fun(feats['train'], feats['train'], *args)
	km_train=kernel.get_kernel_matrix()
	kernel.init(feats['train'], feats['test'])
	km_test=kernel.get_kernel_matrix()

	output={
		'name':name,
		'km_train':km_train,
		'km_test':km_test,
		'data_train':matrix(data['train']),
		'data_test':matrix(data['test'])
	}
	output.update(fileop.get_output_params(name, T_KERNEL, args))

	fileop.write(T_KERNEL, output)

def _compute_pie (name, feats, data):
	pie=PluginEstimate()
	fun=eval(name+'Kernel')

	num_vec=feats['train'].get_num_vectors();
	labels=rand(num_vec).round()*2-1
	l=Labels(labels)
	pie.train(feats['train'], l, .1, -.1)
	kernel=fun(feats['train'], feats['train'], pie)

	kernel.init(feats['train'], feats['test'])
	pie.set_testfeatures(feats['test'])
	pie.test()
	classified=pie.classify().get_labels()

	output={
		'name':name,
		'data_train':matrix(data['train']),
		'data_test':matrix(data['test']),
		'labels':labels,
		'classified':classified
	}
	output.update(fileop._get_output_params(name, T_KERNEL))

	fileop.write(T_KERNEL, output)

##################################################################
## run funcs
##################################################################

def _run_custom ():
	dim_square=7
	name='Custom'
	data=dataop.get_rand(dim_square=dim_square)
	feats=featop.get_simple('Real', data)
	data=data['train']
	symdata=data+data.T

	lowertriangle=array([ symdata[(x,y)] for x in xrange(symdata.shape[1]) for y in xrange(symdata.shape[0]) if y<=x ])
	kernel=CustomKernel(feats['train'], feats['train'])
	kernel.set_triangle_kernel_matrix_from_triangle(lowertriangle)
	km_triangletriangle=kernel.get_kernel_matrix()
	kernel.set_triangle_kernel_matrix_from_full(symdata)
	km_fulltriangle=kernel.get_kernel_matrix()
	kernel.set_full_kernel_matrix_from_full(data)
	km_fullfull=kernel.get_kernel_matrix()

	output={
		'name':name,
		'km_triangletriangle':km_triangletriangle,
		'km_fulltriangle':km_fulltriangle,
		'km_fullfull':km_fullfull,
		'symdata':matrix(symdata),
		'data':matrix(data),
		'dim_square':dim_square
	}
	output.update(fileop.get_output_params(name, T_KERNEL))

	fileop.write(T_KERNEL, output)

def _run_distance ():
	data=dataop.get_rand()
	feats=featop.get_simple('Real', data)
	distance=CanberraMetric()
	_compute('Distance', feats, data, 1.7, distance)

def _run_feats_byte ():
	data=dataop.get_rand(type=ubyte)
	feats=featop.get_simple('Byte', data, RAWBYTE)

	_compute('LinearByte', feats, data)

def _run_mindygram ():
	data=dataop.get_dna()
	feats={'train':MindyGramFeatures('DNA', 'freq', '%20.,', 0),
		'test':MindyGramFeatures('DNA', 'freq', '%20.,', 0)}

	_compute('MindyGram', feats, data, 'MEASURE', 1.5)

def _run_feats_real ():
	data=dataop.get_rand()
	feats=featop.get_simple('Real', data)

	_compute('Chi2', feats, data, 1.2, 10)
	_compute('Const', feats, data, 23.)
	_compute('Diag', feats, data, 23.)
	_compute('Gaussian', feats, data, 1.3)
	_compute('GaussianShift', feats, data, 1.3, 2, 1)
	_compute('Linear', feats, data, 1.)
	_compute('Poly', feats, data, 3, True, True)
	_compute('Poly', feats, data, 3, False, True)
	_compute('Poly', feats, data, 3, True, False)
	_compute('Poly', feats, data, 3, False, False)
	_compute('Sigmoid', feats, data, 10, 1.1, 1.3)
	_compute('Sigmoid', feats, data, 10, 0.5, 0.7)

	feats=featop.get_simple('Real', data, sparse=True)
	_compute('SparseGaussian', feats, data, 1.3)
	_compute('SparseLinear', feats, data, 1.)
	_compute('SparsePoly', feats, data, 10, 3, True, True)

def _run_feats_string ():
	data=dataop.get_dna()
	feats=featop.get_string('Char', data)

	_compute('FixedDegreeString', feats, data, 3)
	_compute('LinearString', feats, data)
	_compute('LocalAlignmentString', feats, data)
	_compute('PolyMatchString', feats, data, 3, True)
	_compute('PolyMatchString', feats, data, 3, False)
	_compute('SimpleLocalityImprovedString', feats, data, 5, 7, 5)

	_compute('WeightedDegreeString', feats, data, 20, 0)
	_compute('WeightedDegreePositionString', feats, data, 20)

	# buggy:
	#_compute('LocalityImprovedString', feats, data, 51, 5, 7)


def _run_feats_word ():
	#FIXME: greater max, lower variance?
	max=42
	data=dataop.get_rand(type=ushort, max_train=max, max_test=max)
	feats=featop.get_simple('Word', data)

	_compute('LinearWord', feats, data)
	_compute('PolyMatchWord', feats, data, 3, True)
	_compute('PolyMatchWord', feats, data, 3, False)
	_compute('WordMatch', feats, data, 3)

def _run_feats_string_complex ():
	data=dataop.get_dna()
	feats=featop.get_string_complex('Word', data)

	_compute('CommWordString', feats, data, False, FULL_NORMALIZATION)
	_compute('WeightedCommWordString', feats, data, False, FULL_NORMALIZATION)

	feats=featop.get_string_complex('Ulong', data)
	_compute('CommUlongString', feats, data, False, FULL_NORMALIZATION)

def _run_pie ():
	data=dataop.get_rand(type=chararray)
	charfeats=featop.get_simple('Char', data)
	data=dataop.get_rand(type=ushort)
	feats=featop.get_simple('Word', data)
	feats['train'].obtain_from_char_features(charfeats['train'], 0, 1)
	feats['test'].obtain_from_char_features(charfeats['test'], 0, 1)

	_compute_pie('HistogramWord', feats, data)
	_compute_pie('SalzbergWord', feats, data)


def run ():
	#_run_mindygram()
	#_run_pie()

	_run_custom()
	_run_distance()
	_run_subkernels()

	_run_feats_byte()
	_run_feats_real()
	_run_feats_string()
	_run_feats_string_complex()
	_run_feats_word()