#include <iostream>
#include <io.h>
#include <TriMesh.h>
#include <PoissonReconLib.h>

static void getAllFiles(std::string path, std::vector<std::string>& files, std::string fileType)
{
	// 文件句柄
	intptr_t hFile = 0;
	// 文件信息
	struct _finddata_t fileinfo;

	std::string p;

	if ((hFile = _findfirst(p.assign(path).append("\\*" + fileType).c_str(), &fileinfo)) != -1)
	{
		do
		{
			// 保存文件的全路径
			files.push_back(p.assign(path).append("\\").append(fileinfo.name));

		} while (_findnext(hFile, &fileinfo) == 0); //寻找下一个，成功返回0，否则-1

		_findclose(hFile);
	}
}


int main()
{
	clock_t time = clock();
	trimesh::TriMesh::set_verbose(0);

	std::vector<std::string> files;
	getAllFiles("D:\\test", files, ".ply");
	std::vector<std::shared_ptr<trimesh::TriMesh>> meshList(files.size());
#pragma omp parallel for
	for (int i = 0; i < meshList.size(); ++i)
	{
		meshList[i].reset(trimesh::TriMesh::read(files[i]));
	}
	std::shared_ptr<trimesh::TriMesh> output = PoissonReconLib::poissonRecon(meshList, 0.09f, true);
	output->write("D:\\output.ply");

	std::cout << "time: " << clock() - time << std::endl;
	system("pause");
}