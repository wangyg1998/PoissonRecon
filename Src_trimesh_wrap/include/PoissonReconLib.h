#pragma once
#include <TriMesh.h>

namespace PoissonReconLib
{
	extern std::shared_ptr<trimesh::TriMesh> triangulation(std::vector<std::shared_ptr<trimesh::TriMesh>> meshList, float targetEdgeLength);
}
