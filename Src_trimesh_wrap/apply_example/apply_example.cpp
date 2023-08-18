#include <iostream>
#include <TriMesh.h>
#include <PoissonReconLib.h>

int main()
{
	std::shared_ptr<trimesh::TriMesh> result = triangulation();
	result->write("D:\\result.ply");
	system("pause");
}