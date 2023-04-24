
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

#include <span>

template<class T>
GDALDataType get_gdal_type()
{
	if constexpr (std::is_same_v<T, uint32_t>)
		return GDALDataType::GDT_UInt32;
	else if constexpr (sizeof(T) == 1)
		return GDALDataType::GDT_Byte;
	else
	{
		__debugbreak();
	}
}
template<class T>
GDALExtendedDataType get_gdal_etype()
{
	return GDALExtendedDataType::Create(get_gdal_type<T>());
}

template<class T>
bool gdal_read_2darray(GDALMDArray* parr, std::vector<T>& buffer)
{
	if (parr == nullptr)
		return false;

	auto& arr = *parr;
	if (arr.GetDimensionCount() != 2)
		return false;
	
	size_t count[2];
	for (size_t i = 0; i < 2; ++i)
		count[i] = arr.GetDimensions()[i]->GetSize();

	buffer.clear();
	buffer.resize(count[0] * count[1]);

	const size_t from[2]{};

	arr.Read(from, count, nullptr, nullptr, get_gdal_etype<T>(), buffer.data());

	return true;
}

bool gdal_read_bgra_image(GDALDataset* parr, std::vector<uint32_t>& buffer)
{
	if (!parr)
		return false;
	auto& arr = *parr;
	auto bands = arr.GetBands();

	if (bands.size() != 3)
		return false;

	const size_t N = arr.GetRasterXSize() * arr.GetRasterYSize();

	buffer.clear();
	buffer.resize(N);

	std::vector<uint8_t> band_data;
	for (size_t i = 0; i < bands.size(); ++i)
	{
		gdal_read_2darray(bands[i]->AsMDArray().get(), band_data);
		for (size_t j = 0; j < N; ++j)
			buffer[j] |= band_data[j] << (16 - 8 * i);
	}
	return true;
}



#include <ksn/window.hpp>
#pragma comment(lib, "libksn_window.lib")
#pragma comment(lib, "libksn_time.lib")

int main()
{
	GDALAllRegister();

	auto dataset = gdal_open_file("data/spot.tiff");
	GUInt64 start = 0;
	std::vector<uint32_t> v;
	gdal_read_bgra_image(dataset.get(), v);

	ksn::window_t win;
	win.open(1200, 900);
	win.draw_pixels_bgra_front(v.data(), 0, 0, 1200, 900);

	getchar();

	return 0;
}
