/*
Copyright (c) 2006, Michael Kazhdan and Matthew Bolitho
All rights reserved.

Redistribution and use in source and binary forms, with or without modification,
are permitted provided that the following conditions are met:

Redistributions of source code must retain the above copyright notice, this list of
conditions and the following disclaimer. Redistributions in binary form must reproduce
the above copyright notice, this list of conditions and the following disclaimer
in the documentation and/or other materials provided with the distribution.

Neither the name of the Johns Hopkins University nor the names of its contributors
may be used to endorse or promote products derived from this software without specific
prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY
EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO THE IMPLIED WARRANTIES
OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT
SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED
TO, PROCUREMENT OF SUBSTITUTE  GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH
DAMAGE.
*/

#include "PreProcessor.h"
#include "PoissonRecon.h"

#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <float.h>
#include "MyMiscellany.h"
#include "CmdLineParser.h"
#include "PPolynomial.h"
#include "FEMTree.h"
#include "Ply.h"
#include "VertexFactory.h"
#include "Image.h"
#include "RegularGrid.h"

#include "DataIO.h"
#include "PoissonReconLib.h"

#include <TriMesh_algo.h>

#include <Octree.hpp>

std::shared_ptr<trimesh::TriMesh> inCloud; //输入点云，需要法向
std::shared_ptr<trimesh::TriMesh> outMesh; //输出网格
Real targetEdgeLength;

enum NormalType
{
	NORMALS_NONE,
	NORMALS_SAMPLES,
	NORMALS_GRADIENTS,
	NORMALS_COUNT
};
const char* NormalsNames[] = { "none", "samples", "gradients" };

cmdLineParameter<char*> In("in"), Out("out"), TempDir("tempDir"), Grid("grid"), Tree("tree"), Envelope("envelope"), EnvelopeGrid("envelopeGrid"),
    Transform("xForm");

cmdLineReadable Performance("performance"), ShowResidual("showResidual"), NoComments("noComments"), PolygonMesh("polygonMesh"), NonManifold("nonManifold"),
    ASCII("ascii"), Density("density"), LinearFit("linearFit"), PrimalGrid("primalGrid"), ExactInterpolation("exact"), Colors("colors"), InCore("inCore"),
    NoDirichletErode("noErode"), Verbose("verbose");

cmdLineParameter<int>
#ifndef FAST_COMPILE
    Degree("degree", DEFAULT_FEM_DEGREE),
#endif // !FAST_COMPILE
    Depth("depth", 8), KernelDepth("kernelDepth"), SolveDepth("solveDepth"), EnvelopeDepth("envelopeDepth"), Iters("iters", 8), FullDepth("fullDepth", 5),
    BaseDepth("baseDepth"), BaseVCycles("baseVCycles", 1),
#ifndef FAST_COMPILE
    BType("bType", DEFAULT_FEM_BOUNDARY + 1),
#endif // !FAST_COMPILE
    Normals("normals", NORMALS_NONE), MaxMemoryGB("maxMemory", 0),
#ifdef _OPENMP
    ParallelType("parallel", (int)ThreadPool::OPEN_MP),
#else // !_OPENMP
    ParallelType("parallel", (int)ThreadPool::THREAD_POOL),
#endif // _OPENMP
    ScheduleType("schedule", (int)ThreadPool::DefaultSchedule), ThreadChunkSize("chunkSize", (int)ThreadPool::DefaultChunkSize),
    Threads("threads", (int)std::thread::hardware_concurrency());

cmdLineParameter<float> DataX("data", 32.f), SamplesPerNode("samplesPerNode", 1.5f), Scale("scale", 1.1f), Width("width", 0.f), Confidence("confidence", 0.f),
    ConfidenceBias("confidenceBias", 0.f), CGSolverAccuracy("cgAccuracy", 1e-3f), LowDepthCutOff("lowDepthCutOff", 0.f), PointWeight("pointWeight");

cmdLineReadable* params[] = {
#ifndef FAST_COMPILE
	&Degree,
	&BType,
#endif // !FAST_COMPILE
	&In,
	&Depth,
	&Out,
	&Transform,
	&SolveDepth,
	&Envelope,
	&EnvelopeGrid,
	&Width,
	&Scale,
	&Verbose,
	&CGSolverAccuracy,
	&NoComments,
	&KernelDepth,
	&SamplesPerNode,
	&Confidence,
	&NonManifold,
	&PolygonMesh,
	&ASCII,
	&ShowResidual,
	&EnvelopeDepth,
	&NoDirichletErode,
	&ConfidenceBias,
	&BaseDepth,
	&BaseVCycles,
	&PointWeight,
	&Grid,
	&Threads,
	&Tree,
	&Density,
	&FullDepth,
	&Iters,
	&DataX,
	&Colors,
	&Normals,
	&LinearFit,
	&PrimalGrid,
	&TempDir,
	&ExactInterpolation,
	&Performance,
	&MaxMemoryGB,
	&InCore,
	&ParallelType,
	&ScheduleType,
	&ThreadChunkSize,
	&LowDepthCutOff,
	NULL
};

void ShowUsage(char* ex)
{
	printf("Usage: %s\n", ex);
	printf("\t --%s <input points>\n", In.name);
	printf("\t[--%s <input envelope>\n", Envelope.name);
	printf("\t[--%s <ouput triangle mesh>]\n", Out.name);
	printf("\t[--%s <ouput grid>]\n", Grid.name);
	printf("\t[--%s <output envelope grid>\n", EnvelopeGrid.name);
	printf("\t[--%s <ouput fem tree>]\n", Tree.name);
#ifndef FAST_COMPILE
	printf("\t[--%s <b-spline degree>=%d]\n", Degree.name, Degree.value);
	printf("\t[--%s <boundary type>=%d]\n", BType.name, BType.value);
	for (int i = 0; i < BOUNDARY_COUNT; i++)
		printf("\t\t%d] %s\n", i + 1, BoundaryNames[i]);
#endif // !FAST_COMPILE
	printf("\t[--%s <maximum reconstruction depth>=%d]\n", Depth.name, Depth.value);
	printf("\t[--%s <maximum solution depth>=%d]\n", SolveDepth.name, SolveDepth.value);
	printf("\t[--%s <grid width>]\n", Width.name);
	printf("\t[--%s <full depth>=%d]\n", FullDepth.name, FullDepth.value);
	printf("\t[--%s <envelope depth>=%d]\n", EnvelopeDepth.name, EnvelopeDepth.value);
	printf("\t[--%s <coarse MG solver depth>]\n", BaseDepth.name);
	printf("\t[--%s <coarse MG solver v-cycles>=%d]\n", BaseVCycles.name, BaseVCycles.value);
	printf("\t[--%s <scale factor>=%f]\n", Scale.name, Scale.value);
	printf("\t[--%s <minimum number of samples per node>=%f]\n", SamplesPerNode.name, SamplesPerNode.value);
	printf("\t[--%s <interpolation weight>=%.3e * <b-spline degree>]\n", PointWeight.name, DefaultPointWeightMultiplier * DEFAULT_FEM_DEGREE);
	printf("\t[--%s <iterations>=%d]\n", Iters.name, Iters.value);
	printf("\t[--%s]\n", ExactInterpolation.name);
	printf("\t[--%s <pull factor>=%f]\n", DataX.name, DataX.value);
	printf("\t[--%s]\n", Colors.name);
	printf("\t[--%s <normal type>=%d]\n", Normals.name, Normals.value);
	for (int i = 0; i < NORMALS_COUNT; i++)
		printf("\t\t%d] %s\n", i, NormalsNames[i]);
	printf("\t[--%s <num threads>=%d]\n", Threads.name, Threads.value);
	printf("\t[--%s <parallel type>=%d]\n", ParallelType.name, ParallelType.value);
	for (size_t i = 0; i < ThreadPool::ParallelNames.size(); i++)
		printf("\t\t%d] %s\n", (int)i, ThreadPool::ParallelNames[i].c_str());
	printf("\t[--%s <schedue type>=%d]\n", ScheduleType.name, ScheduleType.value);
	for (size_t i = 0; i < ThreadPool::ScheduleNames.size(); i++)
		printf("\t\t%d] %s\n", (int)i, ThreadPool::ScheduleNames[i].c_str());
	printf("\t[--%s <thread chunk size>=%d]\n", ThreadChunkSize.name, ThreadChunkSize.value);
	printf("\t[--%s <low depth cut-off>=%f]\n", LowDepthCutOff.name, LowDepthCutOff.value);
	printf("\t[--%s <normal confidence exponent>=%f]\n", Confidence.name, Confidence.value);
	printf("\t[--%s <normal confidence bias exponent>=%f]\n", ConfidenceBias.name, ConfidenceBias.value);
	printf("\t[--%s]\n", NonManifold.name);
	printf("\t[--%s]\n", PolygonMesh.name);
	printf("\t[--%s <cg solver accuracy>=%g]\n", CGSolverAccuracy.name, CGSolverAccuracy.value);
	printf("\t[--%s <maximum memory (in GB)>=%d]\n", MaxMemoryGB.name, MaxMemoryGB.value);
	printf("\t[--%s]\n", NoDirichletErode.name);
	printf("\t[--%s]\n", Performance.name);
	printf("\t[--%s]\n", Density.name);
	printf("\t[--%s]\n", LinearFit.name);
	printf("\t[--%s]\n", PrimalGrid.name);
	printf("\t[--%s]\n", ASCII.name);
	printf("\t[--%s]\n", NoComments.name);
	printf("\t[--%s]\n", TempDir.name);
	printf("\t[--%s]\n", InCore.name);
	printf("\t[--%s]\n", Verbose.name);
}

namespace
{
bool trimsOff(const trimesh::TriMesh* inCloud, trimesh::TriMesh* outMesh, float targetEdgeLength)
{
	clock_t time = clock();

	//偏离点标记
	std::vector<bool> farPoint(outMesh->vertices.size(), false);
	{
		unibn::Octree<trimesh::point> octree;
		octree.initialize(inCloud->vertices);
#pragma omp parallel for
		for (int i = 0; i < outMesh->vertices.size(); ++i)
		{
			int vId = octree.findNeighbor(outMesh->vertices[i]);
			if (vId >= 0 && trimesh::dist(inCloud->vertices[vId], outMesh->vertices[i]) > targetEdgeLength)
			{
				farPoint[i] = true;
			}
		}
	}

	//删除偏离区域
	{
		std::vector<bool> rmv(outMesh->vertices.size(), false);
		std::vector<bool> visited(outMesh->vertices.size(), false);
		outMesh->need_neighbors();
		for (int i = 0; i < outMesh->vertices.size(); ++i)
		{
			if (visited[i] || !farPoint[i])
			{
				continue;
			}
			std::vector<int> cached = { i };
			visited[i] = true;
			std::pair<int, int> searchRegion(0, cached.size());
			while (searchRegion.first < searchRegion.second)
			{
				for (int j = searchRegion.first; j < searchRegion.second; ++j)
				{
					for (int ring : outMesh->neighbors[cached[j]])
					{
						if (!visited[ring] && farPoint[ring])
						{
							visited[ring] = true;
							cached.push_back(ring);
						}
					}
				}
				searchRegion.first = searchRegion.second;
				searchRegion.second = cached.size();
			}
			if (cached.size() > 10)
			{
				for (int vid : cached)
				{
					rmv[vid] = true;
				}
			}
		}
		outMesh->neighbors.clear();
		trimesh::remove_vertices(outMesh, rmv);
	}

	//删除小组件
	{
		std::vector<bool> rmv(outMesh->vertices.size(), false);
		std::vector<bool> visited(outMesh->vertices.size(), false);
		outMesh->need_neighbors();
		std::vector<int> cached;
		cached.reserve(outMesh->vertices.size());
		for (int i = 0; i < outMesh->vertices.size(); ++i)
		{
			if (visited[i])
			{
				continue;
			}
			cached.clear();
			cached.push_back(i);
			visited[i] = true;
			std::pair<int, int> searchRegion(0, cached.size());
			while (searchRegion.first < searchRegion.second)
			{
				for (int j = searchRegion.first; j < searchRegion.second; ++j)
				{
					for (int ring : outMesh->neighbors[cached[j]])
					{
						if (!visited[ring])
						{
							visited[ring] = true;
							cached.push_back(ring);
						}
					}
				}
				searchRegion.first = searchRegion.second;
				searchRegion.second = cached.size();
			}
			if (cached.size() < 100)
			{
				for (int vid : cached)
				{
					rmv[vid] = true;
				}
			}
		}
		outMesh->neighbors.clear();
		trimesh::remove_vertices(outMesh, rmv);
	}

	std::cout << "trimmer time: " << clock() - time << std::endl;
	return true;
}

} // namespace

double Weight(double v, double start, double end)
{
	v = (v - start) / (end - start);
	if (v < 0)
		return 1.;
	else if (v > 1)
		return 0.;
	else
	{
		// P(x) = a x^3 + b x^2 + c x + d
		//		P (0) = 1 , P (1) = 0 , P'(0) = 0 , P'(1) = 0
		// =>	d = 1 , a + b + c + d = 0 , c = 0 , 3a + 2b + c = 0
		// =>	c = 0 , d = 1 , a + b = -1 , 3a + 2b = 0
		// =>	a = 2 , b = -3 , c = 0 , d = 1
		// =>	P(x) = 2 x^3 - 3 x^2 + 1
		return 2. * v * v * v - 3. * v * v + 1.;
	}
}

template <typename Real, typename SetVertexFunction, typename InputSampleDataType, typename VertexFactory, unsigned int... FEMSigs>
void ExtractMesh(UIntPack<FEMSigs...>,
                 FEMTree<sizeof...(FEMSigs), Real>& tree,
                 const DenseNodeData<Real, UIntPack<FEMSigs...>>& solution,
                 Real isoValue,
                 const std::vector<typename FEMTree<sizeof...(FEMSigs), Real>::PointSample>* samples,
                 std::vector<InputSampleDataType>* sampleData,
                 const typename FEMTree<sizeof...(FEMSigs), Real>::template DensityEstimator<WEIGHT_DEGREE>* density,
                 const VertexFactory& vertexFactory,
                 const InputSampleDataType& zeroInputSampleDataType,
                 SetVertexFunction SetVertex,
                 std::vector<std::string>& comments,
                 XForm<Real, sizeof...(FEMSigs) + 1> unitCubeToModel)
{
	static const int Dim = sizeof...(FEMSigs);
	typedef UIntPack<FEMSigs...> Sigs;
	typedef typename VertexFactory::VertexType Vertex;

	static const unsigned int DataSig = FEMDegreeAndBType<DATA_DEGREE, BOUNDARY_FREE>::Signature;
	typedef typename FEMTree<Dim, Real>::template DensityEstimator<WEIGHT_DEGREE> DensityEstimator;

	Profiler profiler(20);

	char tempHeader[2048];
	{
		char tempPath[1024];
		tempPath[0] = 0;
		if (TempDir.set)
			strcpy(tempPath, TempDir.value);
		else
			SetTempDirectory(tempPath, sizeof(tempPath));
		if (strlen(tempPath) == 0)
			sprintf(tempPath, ".%c", FileSeparator);
		if (tempPath[strlen(tempPath) - 1] == FileSeparator)
			sprintf(tempHeader, "%sPR_", tempPath);
		else
			sprintf(tempHeader, "%s%cPR_", tempPath, FileSeparator);
	}
	StreamingMesh<Vertex, node_index_type>* mesh;
	if (InCore.set)
		mesh = new VectorStreamingMesh<Vertex, node_index_type>();
	else
		mesh = new FileStreamingMesh<VertexFactory, node_index_type>(vertexFactory, tempHeader);

	profiler.reset();
	typename LevelSetExtractor<Dim, Real, Vertex>::Stats stats;
	if (sampleData)
	{
		SparseNodeData<ProjectiveData<InputSampleDataType, Real>, IsotropicUIntPack<Dim, DataSig>> _sampleData =
		    tree.template setExtrapolatedDataField<DataSig, false>(*samples, *sampleData, (DensityEstimator*)NULL);
		auto nodeFunctor = [&](const RegularTreeNode<Dim, FEMTreeNodeData, depth_and_offset_type>* n)
		{
			ProjectiveData<InputSampleDataType, Real>* clr = _sampleData(n);
			if (clr)
				(*clr) *= (Real)pow(DataX.value, tree.depth(n));
		};
		tree.tree().processNodes(nodeFunctor);
		stats = LevelSetExtractor<Dim, Real, Vertex>::template Extract<InputSampleDataType>(Sigs(),
		                                                                                    UIntPack<WEIGHT_DEGREE>(),
		                                                                                    UIntPack<DataSig>(),
		                                                                                    tree,
		                                                                                    density,
		                                                                                    &_sampleData,
		                                                                                    solution,
		                                                                                    isoValue,
		                                                                                    *mesh,
		                                                                                    zeroInputSampleDataType,
		                                                                                    SetVertex,
		                                                                                    !LinearFit.set,
		                                                                                    Normals.value == NORMALS_GRADIENTS,
		                                                                                    !NonManifold.set,
		                                                                                    PolygonMesh.set,
		                                                                                    false);
	}
#if defined(__GNUC__) && __GNUC__ < 5
#ifdef SHOW_WARNINGS
#warning "you've got me gcc version<5"
#endif // SHOW_WARNINGS
	else
		stats = LevelSetExtractor<Dim, Real, Vertex>::template Extract<InputSampleDataType>(
		    Sigs(),
		    UIntPack<WEIGHT_DEGREE>(),
		    UIntPack<DataSig>(),
		    tree,
		    density,
		    (SparseNodeData<ProjectiveData<InputSampleDataType, Real>, IsotropicUIntPack<Dim, DataSig>>*)NULL,
		    solution,
		    isoValue,
		    *mesh,
		    zeroInputSampleDataType,
		    SetVertex,
		    !LinearFit.set,
		    Normals.value == NORMALS_GRADIENTS,
		    !NonManifold.set,
		    PolygonMesh.set,
		    false);
#else // !__GNUC__ || __GNUC__ >=5
	else
		stats = LevelSetExtractor<Dim, Real, Vertex>::template Extract<InputSampleDataType>(Sigs(),
		                                                                                    UIntPack<WEIGHT_DEGREE>(),
		                                                                                    UIntPack<DataSig>(),
		                                                                                    tree,
		                                                                                    density,
		                                                                                    NULL,
		                                                                                    solution,
		                                                                                    isoValue,
		                                                                                    *mesh,
		                                                                                    zeroInputSampleDataType,
		                                                                                    SetVertex,
		                                                                                    !LinearFit.set,
		                                                                                    Normals.value == NORMALS_GRADIENTS,
		                                                                                    !NonManifold.set,
		                                                                                    PolygonMesh.set,
		                                                                                    false);
#endif // __GNUC__ || __GNUC__ < 4
	if (Verbose.set)
	{
		std::cout << "Vertices / Polygons: " << mesh->vertexNum() << " / " << mesh->polygonNum() << std::endl;
		std::cout << stats.toString() << std::endl;
		if (PolygonMesh.set)
			std::cout << "#         Got polygons: " << profiler << std::endl;
		else
			std::cout << "#        Got triangles: " << profiler << std::endl;
	}

	std::vector<std::string> noComments;
	typename VertexFactory::Transform unitCubeToModelTransform(unitCubeToModel);
	auto xForm = [&](typename VertexFactory::VertexType& v) { unitCubeToModelTransform.inPlace(v); };

	//输出为trimesh
	{
		outMesh.reset(new trimesh::TriMesh);
		size_t nv = mesh->vertexNum();
		size_t nf = mesh->polygonNum();
		mesh->resetIterator();
		outMesh->vertices.reserve(nv);
		outMesh->faces.reserve(nf);
		bool hasColor = (inCloud->colors.size() == inCloud->vertices.size());
		if (hasColor)
		{
			outMesh->colors.reserve(nv);
		}
		if (Density.set)
		{
			outMesh->confidences.reserve(nv);
		}

		//顶点
		const auto& vFactory = vertexFactory;
		for (size_t i = 0; i < nv; i++)
		{
			typename VertexFactory::VertexType vertex = vFactory();
			mesh->nextVertex(vertex);
			xForm(vertex);
			Point<Real, 3>& p = vertex.get<0>();
			outMesh->vertices.push_back(trimesh::point(p[0], p[1], p[2]));
			if (hasColor)
			{
				Real* ptr = (Real*)&vertex;
				outMesh->colors.push_back(trimesh::Color(ptr[0], ptr[1], ptr[2]));
			}
			if (Density.set)
			{
				Real* ptr = (Real*)&vertex;
				if (!hasColor)
				{
					outMesh->confidences.push_back(ptr[1]);
				}
				else
				{
					outMesh->confidences.push_back(ptr[3]);
				}
			}
		}

		//面片
		std::vector<int> polygon;
		for (size_t i = 0; i < nf; i++)
		{
			mesh->nextPolygon(polygon);
			outMesh->faces.push_back(trimesh::TriMesh::Face(polygon[0], polygon[1], polygon[2]));
		}

		//根据密度裁剪
		if (outMesh->confidences.size() == outMesh->vertices.size())
		{
			std::vector<bool> rmv(outMesh->vertices.size(), false);
			for (int i = 0; i < outMesh->vertices.size(); ++i)
			{
				if (outMesh->confidences[i] < 8.2f)
				{
					rmv[i] = true;
				}
			}
			outMesh->confidences.clear();
			trimesh::remove_vertices(outMesh.get(), rmv);
			trimesh::remove_unused_vertices(outMesh.get());
		}
		else
		{
			trimsOff(inCloud.get(), outMesh.get(), targetEdgeLength);
		}
	}
	delete mesh;
}

template <typename Real, unsigned int Dim>
void WriteGrid(const char* fileName, ConstPointer(Real) values, unsigned int res, XForm<Real, Dim + 1> voxelToModel, bool verbose)
{
	char* ext = GetFileExtension(fileName);

	if (Dim == 2 && ImageWriter::ValidExtension(ext))
	{
		unsigned int totalResolution = 1;
		for (int d = 0; d < Dim; d++)
			totalResolution *= res;

		// Compute average
		Real avg = 0;
		std::vector<Real> avgs(ThreadPool::NumThreads(), 0);
		ThreadPool::Parallel_for(0, totalResolution, [&](unsigned int thread, size_t i) { avgs[thread] += values[i]; });
		for (unsigned int t = 0; t < ThreadPool::NumThreads(); t++)
			avg += avgs[t];
		avg /= (Real)totalResolution;

		// Compute standard deviation
		Real std = 0;
		std::vector<Real> stds(ThreadPool::NumThreads(), 0);
		ThreadPool::Parallel_for(0, totalResolution, [&](unsigned int thread, size_t i) { stds[thread] += (values[i] - avg) * (values[i] - avg); });
		for (unsigned int t = 0; t < ThreadPool::NumThreads(); t++)
			std += stds[t];
		std = (Real)sqrt(std / totalResolution);

		if (verbose)
		{
			printf("Grid to image: [%.2f,%.2f] -> [0,255]\n", avg - 2 * std, avg + 2 * std);
			printf("Transform:\n");
			for (int i = 0; i < Dim + 1; i++)
			{
				printf("\t");
				for (int j = 0; j < Dim + 1; j++)
					printf(" %f", voxelToModel(j, i));
				printf("\n");
			}
		}

		unsigned char* pixels = new unsigned char[totalResolution * 3];
		ThreadPool::Parallel_for(0,
		                         totalResolution,
		                         [&](unsigned int, size_t i)
		                         {
			                         Real v = (Real)std::min<Real>((Real)1., std::max<Real>((Real)-1., (values[i] - avg) / (2 * std)));
			                         v = (Real)((v + 1.) / 2. * 256.);
			                         unsigned char color = (unsigned char)std::min<Real>((Real)255., std::max<Real>((Real)0., v));
			                         for (int c = 0; c < 3; c++)
				                         pixels[i * 3 + c] = color;
		                         });
		ImageWriter::Write(fileName, pixels, res, res, 3);
		delete[] pixels;
	}
	else if (!strcasecmp(ext, "iso"))
	{
		FILE* fp = fopen(fileName, "wb");
		if (!fp)
			ERROR_OUT("Failed to open file for writing: ", fileName);
		int r = (int)res;
		fwrite(&r, sizeof(int), 1, fp);
		size_t count = 1;
		for (unsigned int d = 0; d < Dim; d++)
			count *= res;
		fwrite(values, sizeof(Real), count, fp);
		fclose(fp);
	}
	else
	{
		unsigned int _res[Dim];
		for (int d = 0; d < Dim; d++)
			_res[d] = res;
		RegularGrid<Real, Dim>::Write(fileName, _res, values, voxelToModel);
	}
	delete[] ext;
}

template <class Real, typename AuxDataFactory, unsigned int... FEMSigs>
void Execute(UIntPack<FEMSigs...>, const AuxDataFactory& auxDataFactory)
{
	static const int Dim = sizeof...(FEMSigs);
	typedef UIntPack<FEMSigs...> Sigs;
	typedef UIntPack<FEMSignature<FEMSigs>::Degree...> Degrees;
	typedef UIntPack<FEMDegreeAndBType<NORMAL_DEGREE, DerivativeBoundary<FEMSignature<FEMSigs>::BType, 1>::BType>::Signature...> NormalSigs;
	static const unsigned int DataSig = FEMDegreeAndBType<DATA_DEGREE, BOUNDARY_FREE>::Signature;
	typedef typename FEMTree<Dim, Real>::template DensityEstimator<WEIGHT_DEGREE> DensityEstimator;
	typedef typename FEMTree<Dim, Real>::template InterpolationInfo<Real, 0> InterpolationInfo;
	using namespace VertexFactory;

	// The factory for constructing an input sample
	typedef Factory<Real, PositionFactory<Real, Dim>, Factory<Real, NormalFactory<Real, Dim>, AuxDataFactory>> InputSampleFactory;

	// The factory for constructing an input sample's data
	typedef Factory<Real, NormalFactory<Real, Dim>, AuxDataFactory> InputSampleDataFactory;

	// The input point stream information: First piece of data is the normal; the remainder is the auxiliary data
	typedef InputOrientedPointStreamInfo<Real, Dim, typename AuxDataFactory::VertexType> InputPointStreamInfo;

	// The type of the input sample
	typedef typename InputPointStreamInfo::PointAndDataType InputSampleType;

	// The type of the input sample's data
	typedef typename InputPointStreamInfo::DataType InputSampleDataType;

	typedef InputDataStream<InputSampleType> InputPointStream;
	typedef TransformedInputDataStream<InputSampleType> XInputPointStream;

	InputSampleFactory inputSampleFactory(PositionFactory<Real, Dim>(), InputSampleDataFactory(NormalFactory<Real, Dim>(), auxDataFactory));
	InputSampleDataFactory inputSampleDataFactory(NormalFactory<Real, Dim>(), auxDataFactory);

	typedef RegularTreeNode<Dim, FEMTreeNodeData, depth_and_offset_type> FEMTreeNode;
	typedef typename FEMTreeInitializer<Dim, Real>::GeometryNodeType GeometryNodeType;
	std::vector<std::string> comments;
	if (Verbose.set)
	{
		std::cout << "*************************************************************" << std::endl;
		std::cout << "*************************************************************" << std::endl;
		std::cout << "** Running Screened Poisson Reconstruction (Version " << VERSION << ") **" << std::endl;
		std::cout << "*************************************************************" << std::endl;
		std::cout << "*************************************************************" << std::endl;
		if (!Threads.set)
			std::cout << "Running with " << Threads.value << " threads" << std::endl;
	}

	bool needNormalData = DataX.value > 0 && Normals.value;
	bool needAuxData = DataX.value > 0 && auxDataFactory.bufferSize();

	XForm<Real, Dim + 1> modelToUnitCube, unitCubeToModel;
	if (Transform.set)
	{
		FILE* fp = fopen(Transform.value, "r");
		if (!fp)
		{
			WARN("Could not read x-form from: ", Transform.value);
			modelToUnitCube = XForm<Real, Dim + 1>::Identity();
		}
		else
		{
			for (int i = 0; i < Dim + 1; i++)
				for (int j = 0; j < Dim + 1; j++)
				{
					float f;
					if (fscanf(fp, " %f ", &f) != 1)
						ERROR_OUT("Failed to read xform");
					modelToUnitCube(i, j) = (Real)f;
				}
			fclose(fp);
		}
	}
	else
		modelToUnitCube = XForm<Real, Dim + 1>::Identity();

	char str[1024];
	for (int i = 0; params[i]; i++)
		if (params[i]->set)
		{
			params[i]->writeValue(str);
			if (Verbose.set)
				if (strlen(str))
					std::cout << "\t--" << params[i]->name << " " << str << std::endl;
				else
					std::cout << "\t--" << params[i]->name << std::endl;
		}

	double startTime = Time();
	Real isoValue = 0;

	FEMTree<Dim, Real> tree(MEMORY_ALLOCATOR_BLOCK_SIZE);
	Profiler profiler(20);

	if (Depth.set && Width.value > 0)
	{
		WARN("Both --", Depth.name, " and --", Width.name, " set, ignoring --", Width.name);
		Width.value = 0;
	}

	size_t pointCount;

	ProjectiveData<Point<Real, 2>, Real> pointDepthAndWeight;
	std::vector<typename FEMTree<Dim, Real>::PointSample>* samples = new std::vector<typename FEMTree<Dim, Real>::PointSample>();
	DenseNodeData<GeometryNodeType, IsotropicUIntPack<Dim, FEMTrivialSignature>> geometryNodeDesignators;
	std::vector<InputSampleDataType>* sampleData = NULL;
	DensityEstimator* density = NULL;
	SparseNodeData<Point<Real, Dim>, NormalSigs>* normalInfo = NULL;
	Real targetValue = (Real)0.5;

	// Read in the samples (and color data)
	{
		profiler.reset();
		sampleData = new std::vector<InputSampleDataType>();

		InputPointStream* pointStream = new TriMeshInputDataStream<InputSampleFactory>(inCloud.get());

		typename InputSampleFactory::Transform _modelToUnitCube(modelToUnitCube);
		auto XFormFunctor = [&](InputSampleType& p) { _modelToUnitCube.inPlace(p); };
		XInputPointStream _pointStream(XFormFunctor, *pointStream);

		{
			typename InputSampleDataFactory::VertexType zeroData = inputSampleDataFactory();
			modelToUnitCube = Scale.value > 0 ?
			                      GetPointXForm<Real, Dim, typename AuxDataFactory::VertexType>(_pointStream, zeroData, (Real)Scale.value) * modelToUnitCube :
			                      modelToUnitCube;
		}
		if (Width.value > 0)
		{
			Real maxScale = 0;
			for (unsigned int i = 0; i < Dim; i++)
				maxScale = std::max<Real>(maxScale, (Real)1. / modelToUnitCube(i, i));
			Depth.value = (unsigned int)ceil(std::max<double>(0., log(maxScale / Width.value) / log(2.)));
		}
		if (SolveDepth.value > Depth.value)
		{
			WARN("Solution depth cannot exceed system depth: ", SolveDepth.value, " <= ", Depth.value);
			SolveDepth.value = Depth.value;
		}
		if (FullDepth.value > Depth.value)
		{
			WARN("Full depth cannot exceed system depth: ", FullDepth.value, " <= ", Depth.value);
			FullDepth.value = Depth.value;
		}
		if (BaseDepth.value > FullDepth.value)
		{
			if (BaseDepth.set)
				WARN("Base depth must be smaller than full depth: ", BaseDepth.value, " <= ", FullDepth.value);
			BaseDepth.value = FullDepth.value;
		}

		{
			typename InputSampleFactory::Transform _modelToUnitCube(modelToUnitCube);
			auto XFormFunctor = [&](InputSampleType& p) { _modelToUnitCube.inPlace(p); };
			XInputPointStream _pointStream(XFormFunctor, *pointStream);
			auto ProcessDataWithConfidence = [&](const Point<Real, Dim>& p, typename InputPointStreamInfo::DataType& d)
			{
				Real l = (Real)Length(d.template get<0>());
				if (!l || !std::isfinite(l))
					return (Real)-1.;
				return (Real)pow(l, Confidence.value);
			};
			auto ProcessData = [](const Point<Real, Dim>& p, typename InputPointStreamInfo::DataType& d)
			{
				Real l = (Real)Length(d.template get<0>());
				if (!l || !std::isfinite(l))
					return (Real)-1.;
				d.template get<0>() /= l;
				return (Real)1.;
			};

			typename InputSampleDataFactory::VertexType zeroData = inputSampleDataFactory();
			typename FEMTreeInitializer<Dim, Real>::StreamInitializationData sid;
			if (Confidence.value > 0)
				pointCount = FEMTreeInitializer<Dim, Real>::template Initialize<InputSampleDataType>(sid,
				                                                                                     tree.spaceRoot(),
				                                                                                     _pointStream,
				                                                                                     zeroData,
				                                                                                     Depth.value,
				                                                                                     *samples,
				                                                                                     *sampleData,
				                                                                                     true,
				                                                                                     tree.nodeAllocators[0],
				                                                                                     tree.initializer(),
				                                                                                     ProcessDataWithConfidence);
			else
				pointCount = FEMTreeInitializer<Dim, Real>::template Initialize<InputSampleDataType>(sid,
				                                                                                     tree.spaceRoot(),
				                                                                                     _pointStream,
				                                                                                     zeroData,
				                                                                                     Depth.value,
				                                                                                     *samples,
				                                                                                     *sampleData,
				                                                                                     true,
				                                                                                     tree.nodeAllocators[0],
				                                                                                     tree.initializer(),
				                                                                                     ProcessData);
		}

		unitCubeToModel = modelToUnitCube.inverse();
		delete pointStream;

		if (Verbose.set)
		{
			std::cout << "Input Points / Samples: " << pointCount << " / " << samples->size() << std::endl;
			std::cout << "# Read input into tree: " << profiler << std::endl;
		}
	}

	DenseNodeData<Real, Sigs> solution;
	{
		DenseNodeData<Real, Sigs> constraints;
		InterpolationInfo* iInfo = NULL;
		int solveDepth = Depth.value;

		tree.resetNodeIndices(0, std::make_tuple());

		// Get the kernel density estimator
		{
			profiler.reset();
			density = tree.template setDensityEstimator<1, WEIGHT_DEGREE>(*samples, KernelDepth.value, SamplesPerNode.value);
			if (Verbose.set)
				std::cout << "#   Got kernel density: " << profiler << std::endl;
		}

		// Transform the Hermite samples into a vector field
		{
			profiler.reset();
			normalInfo = new SparseNodeData<Point<Real, Dim>, NormalSigs>();
			std::function<bool(InputSampleDataType, Point<Real, Dim>&)> ConversionFunction = [](InputSampleDataType in, Point<Real, Dim>& out)
			{
				Point<Real, Dim> n = in.template get<0>();
				Real l = (Real)Length(n);
				// It is possible that the samples have non-zero normals but there are two co-located samples with negative normals...
				if (!l)
					return false;
				out = n / l;
				return true;
			};
			std::function<bool(InputSampleDataType, Point<Real, Dim>&, Real&)> ConversionAndBiasFunction =
			    [](InputSampleDataType in, Point<Real, Dim>& out, Real& bias)
			{
				Point<Real, Dim> n = in.template get<0>();
				Real l = (Real)Length(n);
				// It is possible that the samples have non-zero normals but there are two co-located samples with negative normals...
				if (!l)
					return false;
				out = n / l;
				bias = (Real)(log(l) * ConfidenceBias.value / log(1 << (Dim - 1)));
				return true;
			};
			if (ConfidenceBias.value > 0)
				*normalInfo = tree.setInterpolatedDataField(NormalSigs(),
				                                            *samples,
				                                            *sampleData,
				                                            density,
				                                            BaseDepth.value,
				                                            Depth.value,
				                                            (Real)LowDepthCutOff.value,
				                                            pointDepthAndWeight,
				                                            ConversionAndBiasFunction);
			else
				*normalInfo = tree.setInterpolatedDataField(NormalSigs(),
				                                            *samples,
				                                            *sampleData,
				                                            density,
				                                            BaseDepth.value,
				                                            Depth.value,
				                                            (Real)LowDepthCutOff.value,
				                                            pointDepthAndWeight,
				                                            ConversionFunction);
			ThreadPool::Parallel_for(0, normalInfo->size(), [&](unsigned int, size_t i) { (*normalInfo)[i] *= (Real)-1.; });
			if (Verbose.set)
			{
				std::cout << "#     Got normal field: " << profiler << std::endl;
				std::cout << "Point depth / Point weight / Estimated measure: " << pointDepthAndWeight.value()[0] << " / " << pointDepthAndWeight.value()[1]
				          << " / " << pointCount * pointDepthAndWeight.value()[1] << std::endl;
			}
		}

		// Get the geometry designators indicating if the space node are interior to, exterior to, or contain the envelope boundary
		if (Envelope.set)
		{
			profiler.reset();
			{
				// Make the octree complete up to the base depth
				FEMTreeInitializer<Dim, Real>::Initialize(
				    tree.spaceRoot(),
				    BaseDepth.value,
				    [](int, int[]) { return true; },
				    tree.nodeAllocators.size() ? tree.nodeAllocators[0] : NULL,
				    tree.initializer());

				// Read in the envelope geometry
				std::vector<Point<Real, Dim>> vertices;
				std::vector<SimplexIndex<Dim - 1, node_index_type>> simplices;
				{
					std::vector<typename PositionFactory<Real, Dim>::VertexType> _vertices;
					std::vector<std::vector<int>> polygons;
					std::vector<std::string> comments;
					int file_type;
					PLY::ReadPolygons(Envelope.value, PositionFactory<Real, Dim>(), _vertices, polygons, file_type, comments);
					vertices.resize(_vertices.size());
					for (int i = 0; i < vertices.size(); i++)
						vertices[i] = modelToUnitCube * _vertices[i];
					simplices.resize(polygons.size());
					for (int i = 0; i < polygons.size(); i++)
						if (polygons[i].size() != Dim)
							ERROR_OUT("Not a simplex");
						else
							for (int j = 0; j < Dim; j++)
								simplices[i][j] = polygons[i][j];
				}
				// Get the interior/boundary/exterior designators
				geometryNodeDesignators = FEMTreeInitializer<Dim, Real>::template GetGeometryNodeDesignators(
				    &tree.spaceRoot(), vertices, simplices, BaseDepth.value, EnvelopeDepth.value, tree.nodeAllocators, tree.initializer());

				// Make nodes in the support of the vector field @{ExactDepth} interior
				if (!NoDirichletErode.set)
				{
					// What to do if we find a node in the support of the vector field
					auto SetScratchFlag = [&](FEMTreeNode* node)
					{
						if (node)
						{
							while (node->depth() > BaseDepth.value)
								node = node->parent;
							node->nodeData.setScratchFlag(true);
						}
					};

					std::function<void(FEMTreeNode*)> PropagateToLeaves = [&](const FEMTreeNode* node)
					{
						geometryNodeDesignators[node] = GeometryNodeType::INTERIOR;
						if (node->children)
							for (int c = 0; c < (1 << Dim); c++)
								PropagateToLeaves(node->children + c);
					};

					// Flags indicating if a node contains a non-zero vector field coefficient
					std::vector<bool> isVectorFieldElement(tree.nodeCount(), false);

					// Get the set of base nodes
					std::vector<FEMTreeNode*> baseNodes;
					auto nodeFunctor = [&](FEMTreeNode* node)
					{
						if (node->depth() == BaseDepth.value)
							baseNodes.push_back(node);
						return node->depth() < BaseDepth.value;
					};
					tree.spaceRoot().processNodes(nodeFunctor);

					std::vector<node_index_type> vectorFieldElementCounts(baseNodes.size());
					for (int i = 0; i < vectorFieldElementCounts.size(); i++)
						vectorFieldElementCounts[i] = 0;

					// In parallel, iterate over the base nodes and mark the nodes containing non-zero vector field coefficients
					ThreadPool::Parallel_for(0,
					                         baseNodes.size(),
					                         [&](unsigned int t, size_t i)
					                         {
						                         auto nodeFunctor = [&](FEMTreeNode* node)
						                         {
							                         Point<Real, Dim>* n = (*normalInfo)(node);
							                         if (n && Point<Real, Dim>::SquareNorm(*n))
								                         isVectorFieldElement[node->nodeData.nodeIndex] = true, vectorFieldElementCounts[i]++;
						                         };
						                         baseNodes[i]->processNodes(nodeFunctor);
					                         });
					size_t vectorFieldElementCount = 0;
					for (int i = 0; i < vectorFieldElementCounts.size(); i++)
						vectorFieldElementCount += vectorFieldElementCounts[i];

					// Get the subset of nodes containing non-zero vector field coefficients and disable the "scratch" flag
					std::vector<FEMTreeNode*> vectorFieldElements;
					vectorFieldElements.reserve(vectorFieldElementCount);
					{
						std::vector<std::vector<FEMTreeNode*>> _vectorFieldElements(baseNodes.size());
						for (int i = 0; i < _vectorFieldElements.size(); i++)
							_vectorFieldElements[i].reserve(vectorFieldElementCounts[i]);
						ThreadPool::Parallel_for(0,
						                         baseNodes.size(),
						                         [&](unsigned int t, size_t i)
						                         {
							                         auto nodeFunctor = [&](FEMTreeNode* node)
							                         {
								                         if (isVectorFieldElement[node->nodeData.nodeIndex])
									                         _vectorFieldElements[i].push_back(node);
								                         node->nodeData.setScratchFlag(false);
							                         };
							                         baseNodes[i]->processNodes(nodeFunctor);
						                         });
						for (int i = 0; i < _vectorFieldElements.size(); i++)
							vectorFieldElements.insert(vectorFieldElements.end(), _vectorFieldElements[i].begin(), _vectorFieldElements[i].end());
					}

					// Set the scratch flag for the base nodes on which the vector field is supported
#ifdef SHOW_WARNINGS
#pragma message("[WARNING] In principal, we should unlock finite elements whose support overlaps the vector field")
#endif // SHOW_WARNINGS
					tree.template processNeighboringLeaves<-BSplineSupportSizes<NORMAL_DEGREE>::SupportStart, BSplineSupportSizes<NORMAL_DEGREE>::SupportEnd>(
					    &vectorFieldElements[0], vectorFieldElements.size(), SetScratchFlag, false);

					// Set sub-trees rooted at interior nodes @ ExactDepth to interior
					ThreadPool::Parallel_for(0,
					                         baseNodes.size(),
					                         [&](unsigned int, size_t i)
					                         {
						                         if (baseNodes[i]->nodeData.getScratchFlag())
							                         PropagateToLeaves(baseNodes[i]);
					                         });

					// Adjust the coarser node designators in case exterior nodes have become boundary.
					ThreadPool::Parallel_for(0,
					                         baseNodes.size(),
					                         [&](unsigned int, size_t i)
					                         { FEMTreeInitializer<Dim, Real>::PullGeometryNodeDesignatorsFromFiner(baseNodes[i], geometryNodeDesignators); });
					FEMTreeInitializer<Dim, Real>::PullGeometryNodeDesignatorsFromFiner(&tree.spaceRoot(), geometryNodeDesignators, BaseDepth.value);
				}
			}
			if (Verbose.set)
				std::cout << "#               Initialized envelope constraints: " << profiler << std::endl;
		}

		if (!Density.set)
			delete density, density = NULL;
		if (!needNormalData && !needAuxData)
			delete sampleData, sampleData = NULL;

		// Add the interpolation constraints
		if (PointWeight.value > 0)
		{
			profiler.reset();
			if (ExactInterpolation.set)
				iInfo = FEMTree<Dim, Real>::template InitializeExactPointInterpolationInfo<Real, 0>(
				    tree,
				    *samples,
				    ConstraintDual<Dim, Real>(targetValue, (Real)PointWeight.value * pointDepthAndWeight.value()[1]),
				    SystemDual<Dim, Real>((Real)PointWeight.value * pointDepthAndWeight.value()[1]),
				    true,
				    false);
			else
				iInfo = FEMTree<Dim, Real>::template InitializeApproximatePointInterpolationInfo<Real, 0>(
				    tree,
				    *samples,
				    ConstraintDual<Dim, Real>(targetValue, (Real)PointWeight.value * pointDepthAndWeight.value()[1]),
				    SystemDual<Dim, Real>((Real)PointWeight.value * pointDepthAndWeight.value()[1]),
				    true,
				    Depth.value,
				    1);
			if (Verbose.set)
				std::cout << "#Initialized point interpolation constraints: " << profiler << std::endl;
		}

		// Trim the tree and prepare for multigrid
		{
			profiler.reset();
			constexpr int MAX_DEGREE = NORMAL_DEGREE > Degrees::Max() ? NORMAL_DEGREE : Degrees::Max();
			typename FEMTree<Dim, Real>::template HasNormalDataFunctor<NormalSigs> hasNormalDataFunctor(*normalInfo);
			auto hasDataFunctor = [&](const FEMTreeNode* node) { return hasNormalDataFunctor(node); };
			auto addNodeFunctor = [&](int d, const int off[Dim]) { return d <= FullDepth.value; };
			if (geometryNodeDesignators.size())
				tree.template finalizeForMultigridWithDirichlet<MAX_DEGREE, Degrees::Max()>(
				    BaseDepth.value,
				    addNodeFunctor,
				    hasDataFunctor,
				    [&](const FEMTreeNode* node)
				    {
					    return node->nodeData.nodeIndex < (node_index_type)geometryNodeDesignators.size() &&
					           geometryNodeDesignators[node] == GeometryNodeType::EXTERIOR;
				    },
				    std::make_tuple(iInfo),
				    std::make_tuple(normalInfo, density, &geometryNodeDesignators));
			else
				tree.template finalizeForMultigrid<MAX_DEGREE, Degrees::Max()>(
				    BaseDepth.value, addNodeFunctor, hasDataFunctor, std::make_tuple(iInfo), std::make_tuple(normalInfo, density));

			if (geometryNodeDesignators.size() && EnvelopeGrid.set)
			{
				FEMTreeInitializer<Dim, Real>::PushGeometryNodeDesignatorsToFiner(&tree.spaceRoot(), geometryNodeDesignators);
				FEMTreeInitializer<Dim, Real>::PullGeometryNodeDesignatorsFromFiner(&tree.spaceRoot(), geometryNodeDesignators);

				auto WriteEnvelopeGrid = [&](bool showFinest)
				{
					int res = 0;
					DenseNodeData<Real, IsotropicUIntPack<Dim, FEMTrivialSignature>> coefficients =
					    tree.initDenseNodeData(IsotropicUIntPack<Dim, FEMTrivialSignature>());
					auto nodeFunctor = [&](const FEMTreeNode* n)
					{
						if (n->nodeData.nodeIndex != -1 &&
						    ((showFinest && !n->children) || (!showFinest && geometryNodeDesignators[n->parent] == GeometryNodeType::BOUNDARY)))
						{
#if 0 // Randomize the colors
									auto Value = [](double v, double eps) { return (Real)(v + Random< double >() * 2. * eps - eps); };
									// Show the octree structure
									if (geometryNodeDesignators[n] == GeometryNodeType::INTERIOR) coefficients[n] = Value(0.75, 0.25);
									else if (geometryNodeDesignators[n] == GeometryNodeType::EXTERIOR) coefficients[n] = Value(-0.75, 0.25);

#else
							if (geometryNodeDesignators[n] == GeometryNodeType::INTERIOR)
								coefficients[n] = (Real)1.;
							else if (geometryNodeDesignators[n] == GeometryNodeType::EXTERIOR)
								coefficients[n] = (Real)-1.;
#endif
						}
					};
					tree.spaceRoot().processNodes(nodeFunctor);
					Pointer(Real) values = tree.template regularGridEvaluate<true>(coefficients, res, -1, false);
					XForm<Real, Dim + 1> voxelToUnitCube = XForm<Real, Dim + 1>::Identity();
					for (int d = 0; d < Dim; d++)
						voxelToUnitCube(d, d) = (Real)(1. / res), voxelToUnitCube(Dim, d) = (Real)(0.5 / res);

					WriteGrid<Real, DEFAULT_DIMENSION>(EnvelopeGrid.value, values, res, unitCubeToModel * voxelToUnitCube, Verbose.set);
					DeletePointer(values);
				};

				WriteEnvelopeGrid(true);
			}

			if (Verbose.set)
				std::cout << "#       Finalized tree: " << profiler << std::endl;
		}

		// Add the FEM constraints
		{
			profiler.reset();
			constraints = tree.initDenseNodeData(Sigs());
			typename FEMIntegrator::template Constraint<Sigs, IsotropicUIntPack<Dim, 1>, NormalSigs, IsotropicUIntPack<Dim, 0>, Dim> F;
			unsigned int derivatives2[Dim];
			for (int d = 0; d < Dim; d++)
				derivatives2[d] = 0;
			typedef IsotropicUIntPack<Dim, 1> Derivatives1;
			typedef IsotropicUIntPack<Dim, 0> Derivatives2;
			for (int d = 0; d < Dim; d++)
			{
				unsigned int derivatives1[Dim];
				for (int dd = 0; dd < Dim; dd++)
					derivatives1[dd] = dd == d ? 1 : 0;
				F.weights[d][TensorDerivatives<Derivatives1>::Index(derivatives1)][TensorDerivatives<Derivatives2>::Index(derivatives2)] = 1;
			}
			tree.addFEMConstraints(F, *normalInfo, constraints, solveDepth);
			if (Verbose.set)
				std::cout << "#  Set FEM constraints: " << profiler << std::endl;
		}

		// Free up the normal info
		delete normalInfo, normalInfo = NULL;

		// Add the interpolation constraints
		if (PointWeight.value > 0)
		{
			profiler.reset();
			tree.addInterpolationConstraints(constraints, solveDepth, std::make_tuple(iInfo));
			if (Verbose.set)
				std::cout << "#Set point constraints: " << profiler << std::endl;
		}

		if (Verbose.set)
		{
			std::cout << "All Nodes / Active Nodes / Ghost Nodes / Dirichlet Supported Nodes: " << tree.allNodes() << " / " << tree.activeNodes() << " / "
			          << tree.ghostNodes() << " / " << tree.dirichletElements() << std::endl;
			std::cout << "Memory Usage: " << float(MemoryInfo::Usage()) / (1 << 20) << " MB" << std::endl;
		}

		// Solve the linear system
		{
			profiler.reset();
			typename FEMTree<Dim, Real>::SolverInfo sInfo;
			sInfo.cgDepth = 0, sInfo.cascadic = true, sInfo.vCycles = 1, sInfo.iters = Iters.value, sInfo.cgAccuracy = CGSolverAccuracy.value,
			sInfo.verbose = Verbose.set, sInfo.showResidual = ShowResidual.set, sInfo.showGlobalResidual = SHOW_GLOBAL_RESIDUAL_NONE, sInfo.sliceBlockSize = 1;
			sInfo.baseVCycles = BaseVCycles.value;
			typename FEMIntegrator::template System<Sigs, IsotropicUIntPack<Dim, 1>> F({ 0., 1. });
			solution = tree.solveSystem(Sigs(), F, constraints, BaseDepth.value, SolveDepth.value, sInfo, std::make_tuple(iInfo));
			if (Verbose.set)
				std::cout << "# Linear system solved: " << profiler << std::endl;
			if (iInfo)
				delete iInfo, iInfo = NULL;
		}
	}

	{
		profiler.reset();
		double valueSum = 0, weightSum = 0;
		typename FEMTree<Dim, Real>::template MultiThreadedEvaluator<Sigs, 0> evaluator(&tree, solution);
		std::vector<double> valueSums(ThreadPool::NumThreads(), 0), weightSums(ThreadPool::NumThreads(), 0);
		ThreadPool::Parallel_for(0,
		                         samples->size(),
		                         [&](unsigned int thread, size_t j)
		                         {
			                         ProjectiveData<Point<Real, Dim>, Real>& sample = (*samples)[j].sample;
			                         Real w = sample.weight;
			                         if (w > 0)
				                         weightSums[thread] += w,
				                             valueSums[thread] += evaluator.values(sample.data / sample.weight, thread, (*samples)[j].node)[0] * w;
		                         });
		for (size_t t = 0; t < valueSums.size(); t++)
			valueSum += valueSums[t], weightSum += weightSums[t];
		isoValue = (Real)(valueSum / weightSum);
		if (!needNormalData && !needAuxData)
			delete samples, samples = NULL;
		if (Verbose.set)
		{
			std::cout << "Got average: " << profiler << std::endl;
			std::cout << "Iso-Value: " << isoValue << " = " << valueSum << " / " << weightSum << std::endl;
		}
	}
	if (Tree.set)
	{
		FILE* fp = fopen(Tree.value, "wb");
		if (!fp)
			ERROR_OUT("Failed to open file for writing: ", Tree.value);
		FileStream fs(fp);
		FEMTree<Dim, Real>::WriteParameter(fs);
		DenseNodeData<Real, Sigs>::WriteSignatures(fs);
		tree.write(fs, modelToUnitCube, false);
		solution.write(fs);
		if (sampleData)
		{
			SparseNodeData<ProjectiveData<InputSampleDataType, Real>, IsotropicUIntPack<Dim, DataSig>> _sampleData =
			    tree.template setExtrapolatedDataField<DataSig, false>(*samples, *sampleData, (DensityEstimator*)NULL);
			auto nodeFunctor = [&](const RegularTreeNode<Dim, FEMTreeNodeData, depth_and_offset_type>* n)
			{
				ProjectiveData<InputSampleDataType, Real>* clr = _sampleData(n);
				if (clr)
					(*clr) *= (Real)pow(DataX.value, tree.depth(n));
			};
			tree.tree().processNodes(nodeFunctor);
			_sampleData.write(fs);
		}
		if (density)
			density->write(fs);
		fclose(fp);
	}

	if (Grid.set)
	{
		int res = 0;
		profiler.reset();
		Pointer(Real) values = tree.template regularGridEvaluate<true>(solution, res, -1, PrimalGrid.set);
		if (Verbose.set)
			std::cout << "Got grid: " << profiler << std::endl;
		XForm<Real, Dim + 1> voxelToUnitCube = XForm<Real, Dim + 1>::Identity();
		if (PrimalGrid.set)
			for (int d = 0; d < Dim; d++)
				voxelToUnitCube(d, d) = (Real)(1. / (res - 1));
		else
			for (int d = 0; d < Dim; d++)
				voxelToUnitCube(d, d) = (Real)(1. / res), voxelToUnitCube(Dim, d) = (Real)(0.5 / res);
		WriteGrid<Real, DEFAULT_DIMENSION>(Grid.value, values, res, unitCubeToModel * voxelToUnitCube, Verbose.set);
		DeletePointer(values);
	}

	if (Out.set)
	{
		if (Normals.value)
		{
			if (Density.set)
			{
				typedef Factory<Real, PositionFactory<Real, Dim>, NormalFactory<Real, Dim>, ValueFactory<Real>, AuxDataFactory> VertexFactory;
				VertexFactory vertexFactory(PositionFactory<Real, Dim>(), NormalFactory<Real, Dim>(), ValueFactory<Real>(), auxDataFactory);
				if (Normals.value == NORMALS_SAMPLES)
				{
					auto SetVertex = [](typename VertexFactory::VertexType& v, Point<Real, Dim> p, Point<Real, Dim> g, Real w, InputSampleDataType d)
					{ v.template get<0>() = p, v.template get<1>() = d.template get<0>(), v.template get<2>() = w, v.template get<3>() = d.template get<1>(); };
					ExtractMesh(UIntPack<FEMSigs...>(),
					            tree,
					            solution,
					            isoValue,
					            samples,
					            sampleData,
					            density,
					            vertexFactory,
					            inputSampleDataFactory(),
					            SetVertex,
					            comments,
					            unitCubeToModel);
				}
				else if (Normals.value == NORMALS_GRADIENTS)
				{
					auto SetVertex = [](typename VertexFactory::VertexType& v, Point<Real, Dim> p, Point<Real, Dim> g, Real w, InputSampleDataType d)
					{
						v.template get<0>() = p, v.template get<1>() = -g / (1 << Depth.value), v.template get<2>() = w,
						           v.template get<3>() = d.template get<1>();
					};
					ExtractMesh(UIntPack<FEMSigs...>(),
					            tree,
					            solution,
					            isoValue,
					            samples,
					            sampleData,
					            density,
					            vertexFactory,
					            inputSampleDataFactory(),
					            SetVertex,
					            comments,
					            unitCubeToModel);
				}
			}
			else
			{
				typedef Factory<Real, PositionFactory<Real, Dim>, NormalFactory<Real, Dim>, AuxDataFactory> VertexFactory;
				VertexFactory vertexFactory(PositionFactory<Real, Dim>(), NormalFactory<Real, Dim>(), auxDataFactory);
				if (Normals.value == NORMALS_SAMPLES)
				{
					auto SetVertex = [](typename VertexFactory::VertexType& v, Point<Real, Dim> p, Point<Real, Dim> g, Real w, InputSampleDataType d)
					{ v.template get<0>() = p, v.template get<1>() = d.template get<0>(), v.template get<2>() = d.template get<1>(); };
					ExtractMesh(UIntPack<FEMSigs...>(),
					            tree,
					            solution,
					            isoValue,
					            samples,
					            sampleData,
					            density,
					            vertexFactory,
					            inputSampleDataFactory(),
					            SetVertex,
					            comments,
					            unitCubeToModel);
				}
				else if (Normals.value == NORMALS_GRADIENTS)
				{
					auto SetVertex = [](typename VertexFactory::VertexType& v, Point<Real, Dim> p, Point<Real, Dim> g, Real w, InputSampleDataType d)
					{ v.template get<0>() = p, v.template get<1>() = -g / (1 << Depth.value), v.template get<2>() = d.template get<1>(); };
					ExtractMesh(UIntPack<FEMSigs...>(),
					            tree,
					            solution,
					            isoValue,
					            samples,
					            sampleData,
					            density,
					            vertexFactory,
					            inputSampleDataFactory(),
					            SetVertex,
					            comments,
					            unitCubeToModel);
				}
			}
		}
		else
		{
			if (Density.set)
			{
				typedef Factory<Real, PositionFactory<Real, Dim>, ValueFactory<Real>, AuxDataFactory> VertexFactory;
				VertexFactory vertexFactory(PositionFactory<Real, Dim>(), ValueFactory<Real>(), auxDataFactory);
				auto SetVertex = [](typename VertexFactory::VertexType& v, Point<Real, Dim> p, Point<Real, Dim> g, Real w, InputSampleDataType d)
				{ v.template get<0>() = p, v.template get<1>() = w, v.template get<2>() = d.template get<1>(); };
				ExtractMesh(UIntPack<FEMSigs...>(),
				            tree,
				            solution,
				            isoValue,
				            samples,
				            sampleData,
				            density,
				            vertexFactory,
				            inputSampleDataFactory(),
				            SetVertex,
				            comments,
				            unitCubeToModel);
			}
			else
			{
				typedef Factory<Real, PositionFactory<Real, Dim>, AuxDataFactory> VertexFactory;
				VertexFactory vertexFactory(PositionFactory<Real, Dim>(), auxDataFactory);
				auto SetVertex = [](typename VertexFactory::VertexType& v, Point<Real, Dim> p, Point<Real, Dim> g, Real w, InputSampleDataType d)
				{ v.template get<0>() = p, v.template get<1>() = d.template get<1>(); };
				ExtractMesh(UIntPack<FEMSigs...>(),
				            tree,
				            solution,
				            isoValue,
				            samples,
				            sampleData,
				            density,
				            vertexFactory,
				            inputSampleDataFactory(),
				            SetVertex,
				            comments,
				            unitCubeToModel);
			}
		}
		if (sampleData)
		{
			delete sampleData;
			sampleData = NULL;
		}
	}
	if (density)
		delete density, density = NULL;
	if (Verbose.set)
		std::cout << "#          Total Solve: " << Time() - startTime << " (s), " << MemoryInfo::PeakMemoryUsageMB() << " (MB)" << std::endl;
}

#ifndef FAST_COMPILE
template <unsigned int Dim, class Real, BoundaryType BType, typename AuxDataFactory>
void Execute(const AuxDataFactory& auxDataFactory)
{
	switch (Degree.value)
	{
	case 1:
		return Execute<Real>(IsotropicUIntPack<Dim, FEMDegreeAndBType<1, BType>::Signature>(), auxDataFactory);
	case 2:
		return Execute<Real>(IsotropicUIntPack<Dim, FEMDegreeAndBType<2, BType>::Signature>(), auxDataFactory);
		//		case 3: return Execute< Real >( IsotropicUIntPack< Dim , FEMDegreeAndBType< 3 , BType >::Signature >() , auxDataFactory );
		//		case 4: return Execute< Real >( IsotropicUIntPack< Dim , FEMDegreeAndBType< 4 , BType >::Signature >() , auxDataFactory );
	default:
		ERROR_OUT("Only B-Splines of degree 1 - 2 are supported");
	}
}

template <unsigned int Dim, class Real, typename AuxDataFactory>
void Execute(const AuxDataFactory& auxDataFactory)
{
	switch (BType.value)
	{
	case BOUNDARY_FREE + 1:
		return Execute<Dim, Real, BOUNDARY_FREE>(auxDataFactory);
	case BOUNDARY_NEUMANN + 1:
		return Execute<Dim, Real, BOUNDARY_NEUMANN>(auxDataFactory);
	case BOUNDARY_DIRICHLET + 1:
		return Execute<Dim, Real, BOUNDARY_DIRICHLET>(auxDataFactory);
	default:
		ERROR_OUT("Not a valid boundary type: ", BType.value);
	}
}
#endif // !FAST_COMPILE

namespace PoissonReconLib
{
std::shared_ptr<trimesh::TriMesh> poissonRecon(std::vector<std::shared_ptr<trimesh::TriMesh>> meshList, float _targetEdgeLength, bool preciseTrimming)
{
	trimesh::TriMesh::set_verbose(0);
	clock_t time = clock();

	targetEdgeLength = _targetEdgeLength;
	//单帧数据合并
	inCloud.reset(new trimesh::TriMesh);
	for (int k = 0; k < meshList.size(); ++k)
	{
		trimesh::TriMesh* mesh = meshList[k].get();
		mesh->need_normals();
		inCloud->vertices.insert(inCloud->vertices.end(), mesh->vertices.begin(), mesh->vertices.end());
		inCloud->normals.insert(inCloud->normals.end(), mesh->normals.begin(), mesh->normals.end());
		inCloud->colors.insert(inCloud->colors.end(), mesh->colors.begin(), mesh->colors.end());
	}
	if (inCloud->colors.size() != inCloud->vertices.size())
	{
		inCloud->colors.clear();
	}

	//调整包围盒比例
	trimesh::point minPt(FLT_MAX), maxPt(-FLT_MAX);
	for (int i = 0; i < inCloud->vertices.size(); ++i)
	{
		const auto& p = inCloud->vertices[i];
		for (int j = 0; j < 3; ++j)
		{
			minPt[j] = std::min(minPt[j], p[j]);
			maxPt[j] = std::max(maxPt[j], p[j]);
		}
	}
	float boxMaxEdgeLength = (maxPt - minPt).max();
	int depth = 6;
	while ((targetEdgeLength * std::pow(2.f, depth)) < boxMaxEdgeLength)
	{
		depth += 1;
	}
	Scale.value = (targetEdgeLength * std::pow(2.f, depth)) / boxMaxEdgeLength;
	Depth.value = depth;
	Out.set = true;
	if (!preciseTrimming)
	{
		Density.set = true;
	}

	Timer timer;
#ifdef ARRAY_DEBUG
	WARN("Array debugging enabled");
#endif // ARRAY_DEBUG
	if (MaxMemoryGB.value > 0)
		SetPeakMemoryMB(MaxMemoryGB.value << 10);
	ThreadPool::DefaultChunkSize = ThreadChunkSize.value;
	ThreadPool::DefaultSchedule = (ThreadPool::ScheduleType)ScheduleType.value;
	ThreadPool::Init((ThreadPool::ParallelType)ParallelType.value, Threads.value);

	if (!BaseDepth.set)
		BaseDepth.value = FullDepth.value;
	if (!SolveDepth.set)
		SolveDepth.value = Depth.value;

	if (BaseDepth.value > FullDepth.value)
	{
		if (BaseDepth.set)
			WARN("Base depth must be smaller than full depth: ", BaseDepth.value, " <= ", FullDepth.value);
		BaseDepth.value = FullDepth.value;
	}
	if (SolveDepth.value > Depth.value)
	{
		WARN("Solution depth cannot exceed system depth: ", SolveDepth.value, " <= ", Depth.value);
		SolveDepth.value = Depth.value;
	}
	if (!KernelDepth.set)
		KernelDepth.value = Depth.value - 2;
	if (KernelDepth.value > Depth.value)
	{
		WARN("Kernel depth should not exceed depth: ", KernelDepth.name, " <= ", KernelDepth.value);
		KernelDepth.value = Depth.value;
	}

	if (!EnvelopeDepth.set)
		EnvelopeDepth.value = BaseDepth.value;
	if (EnvelopeDepth.value > Depth.value)
	{
		WARN(EnvelopeDepth.name, " can't be greater than ", Depth.name, ": ", EnvelopeDepth.value, " <= ", Depth.value);
		EnvelopeDepth.value = Depth.value;
	}
	if (EnvelopeDepth.value < BaseDepth.value)
	{
		WARN(EnvelopeDepth.name, " can't be less than ", BaseDepth.name, ": ", EnvelopeDepth.value, " >= ", BaseDepth.value);
		EnvelopeDepth.value = BaseDepth.value;
	}

#ifdef FAST_COMPILE

	static const int Degree = DEFAULT_FEM_DEGREE;
	static const BoundaryType BType = DEFAULT_FEM_BOUNDARY;
	typedef IsotropicUIntPack<DEFAULT_DIMENSION, FEMDegreeAndBType<Degree, BType>::Signature> FEMSigs;
	if (!PointWeight.set)
		PointWeight.value = DefaultPointWeightMultiplier * Degree;

	if (inCloud->colors.size() == inCloud->vertices.size())
	{
		Execute<Real>(FEMSigs(), VertexFactory::RGBColorFactory<Real>());
	}
	else
	{
		Execute<Real>(FEMSigs(), VertexFactory::EmptyFactory<Real>());
	}

#else

	if (!PointWeight.set)
		PointWeight.value = DefaultPointWeightMultiplier * Degree.value;
	//执行
	{
		if (inCloud->colors.size() == inCloud->vertices.size())
		{
			Execute<DEFAULT_DIMENSION, Real>(VertexFactory::RGBColorFactory<Real>());
		}
		else
		{
			Execute<DEFAULT_DIMENSION, Real>(VertexFactory::EmptyFactory<Real>());
		}
	}

#endif

	if (Performance.set)
	{
		printf("Time (Wall/CPU): %.2f / %.2f\n", timer.wallTime(), timer.cpuTime());
		printf("Peak Memory (MB): %d\n", MemoryInfo::PeakMemoryUsageMB());
	}

	ThreadPool::Terminate();
	std::cout << "triangulation time: " << clock() - time << std::endl;
	return outMesh;
}
} // namespace PoissonReconLib