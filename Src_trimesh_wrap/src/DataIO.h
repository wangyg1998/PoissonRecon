#pragma once

#include "VertexStream.h"
#include <TriMesh.h>

#ifdef USE_DOUBLE
typedef double Real;
#else // !USE_DOUBLE
typedef float  Real;
#endif // USE_DOUBLE

template<typename Factory >
class TriMeshInputDataStream :public InputDataStream<typename Factory::VertexType>
{
	typedef typename Factory::VertexType Data;
public:
	TriMeshInputDataStream(const trimesh::TriMesh* _mesh) :mesh_(_mesh), currentIndex_(0)
	{
		hasColor_ = (mesh_->colors.size() == mesh_->vertices.size());
	}
	void reset(void)
	{
		currentIndex_ = 0;
	}
	bool next(Data& d)
	{
		if (currentIndex_ >= mesh_->vertices.size())
		{
			return false;
		}

		Point<Real, 3>& p = d.get<0>();
		Point<Real, 3>& n = d.get<1>().get<0>();
		for (int i = 0; i < 3; ++i)
		{
			p[i] = mesh_->vertices[currentIndex_][i];
			n[i] = mesh_->normals[currentIndex_][i];
		}
		if (hasColor_)
		{
			/*Point<Real, 3>& c = d.get<1>().get<1>();
			c[0] = mesh_->colors[currentIndex_][0];
			c[1] = mesh_->colors[currentIndex_][1];
			c[2] = mesh_->colors[currentIndex_][2];*/
			Real* ptr = (Real*)&d;
			ptr[0] = mesh_->colors[currentIndex_][0];
			ptr[1] = mesh_->colors[currentIndex_][1];
			ptr[2] = mesh_->colors[currentIndex_][2];
			
		}

		currentIndex_ += 1;
		return true;
	}

private:
	const trimesh::TriMesh* mesh_;
	size_t currentIndex_;
	bool hasColor_;
};
