
#pragma comment(lib, "proj.lib")
#pragma comment(lib, "gdal.lib")

#include <gdal.h>
#include <gdal_priv.h>

auto gdal_open_file(const char* name, GDALAccess access_mode = GA_ReadOnly)
{
	auto handle = GDALOpen(name, access_mode);
	if (!handle)
		return GDALDatasetUniquePtr(nullptr);
	auto dataset_ptr = GDALDataset::FromHandle(handle);
	return GDALDatasetUniquePtr(dataset_ptr);
}


int main()
{
	auto dataset = gdal_open_file("aboba.shp");
	return 0;
}
