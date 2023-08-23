#pragma once
#include <TriMesh.h>

namespace PoissonReconLib
{
	/// \param[in] preciseTrimming ���Ϊtrue��ͨ�����ƱȽ϶����������о�ȷ�޼�������ʹ�ò��ɵ��ܶȹ��ƽ����޼�
	extern std::shared_ptr<trimesh::TriMesh> poissonRecon(std::vector<std::shared_ptr<trimesh::TriMesh>> meshList, float targetEdgeLength, bool preciseTrimming);
}
