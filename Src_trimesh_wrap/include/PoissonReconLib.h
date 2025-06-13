#pragma once
#include <TriMesh.h>

namespace PoissonReconLib
{
/// \param[in] preciseTrimming 如果为true，通过点云比较对输出网格进行精确修剪；否则，使用泊松的密度估计进行修剪
std::shared_ptr<trimesh::TriMesh> poissonRecon(std::vector<std::shared_ptr<trimesh::TriMesh>> meshList, float targetEdgeLength, bool preciseTrimming);

} // namespace PoissonReconLib
