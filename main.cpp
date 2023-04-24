
import <iostream>;
import <vector>;
import <fstream>;
//import std;


#include <gdal.h>
#include <gdal_priv.h>
#pragma comment(lib, "proj.lib")
#pragma comment(lib, "gdal.lib")


#include <ksn/stuff.hpp>

#include <ksn/window.hpp>
#pragma comment(lib, "libksn_window.lib")
#pragma comment(lib, "libksn_time.lib")





auto gdal_open_file(const char* name, GDALAccess access_mode = GA_ReadOnly)
{
	auto handle = GDALOpen(name, access_mode);
	auto dataset_ptr = GDALDataset::FromHandle(handle);
	return GDALDatasetUniquePtr(dataset_ptr);
}

template<class T>
GDALDataType get_gdal_type()
{
	if constexpr (std::is_same_v<T, uint32_t>)
		return GDALDataType::GDT_UInt32;
	else if constexpr (std::is_integral_v<T> && sizeof(T) == 1)
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

struct bgra
{
	uint8_t c[4];
	uint8_t& operator[](size_t n) { return c[n]; }
};

struct bgra_image
{
	std::vector<bgra> data;
	uint32_t size[2];

	auto& operator[](this auto&& self, size_t n)
	{
		return self.data[n];
	}
};

bool gdal_read_bgra_image(GDALDataset* parr, bgra_image& img)
{
	if (!parr)
		return false;
	auto& arr = *parr;
	auto bands = arr.GetBands();

	if (bands.size() != 3)
		return false;

	auto& buffer = img.data;
	auto& w = img.size[0] = arr.GetRasterXSize();
	auto& h = img.size[1] = arr.GetRasterYSize();

	const size_t N = w * h;

	buffer.resize(N);

	std::vector<uint8_t> band_data;
	for (size_t i = 0; i < bands.size(); ++i)
	{
		gdal_read_2darray(bands[i]->AsMDArray().get(), band_data);
		for (size_t j = 0; j < N; ++j)
			buffer[j][2 - i] = band_data[j];
	}
	for (auto& x : buffer)
		x[3] = 0xFF;

	return true;
}




template<class T, class fp = double>
T bilerp(T a1, T a2, T b1, T b2, fp tab, fp t12)
{
	T a = std::lerp(a1, a2, t12);
	T b = std::lerp(b1, b2, t12);
	return std::lerp(a, b, tab);
}

template<>
bgra bilerp<bgra, double>(bgra a1, bgra a2, bgra b1, bgra b2, double tab, double t12)
{
	bgra result;
	for (size_t i = 0; i < 4; ++i)
		result[i] = (uint8_t)bilerp<double>(a1[i], a2[i], b1[i], b2[i], tab, t12);
	return result;
}

void copy_resize_image(bgra_image& dst, const bgra_image& src)
{
	const auto& w1 = src.size[0];
	const auto& h1 = src.size[1];
	const auto& w2 = dst.size[0];
	const auto& h2 = dst.size[1];

	dst.data.resize(w2 * h2);

	using fp = double;
	const fp xratio = h1 ? (fp)(h1 - 1) / (h2 - 1) : 0;
	const fp yratio = w1 ? (fp)(w1 - 1) / (w2 - 1) : 0;

	auto index = []
	(size_t x, size_t y, size_t x_size, size_t y_size)
	{
		x = std::min(x, x_size - 1);
		y = std::min(y, y_size - 1);
		return y * x_size + x;
	};

	auto inc_split = []
	(fp& fractional, size_t& integral, fp dx)
	{
		fractional += dx;
		while (fractional >= 1)
		{
			--fractional;
			++integral;
		}
	};

	size_t ys = 0;  fp yt = 0;
	for (size_t y = 0; y < h2; ++y)
	{
		size_t xs = 0; fp xt = 0;
		for (size_t x = 0; x < w2; ++x)
		{
			dst[index(x, y, w2, h2)] = bilerp(
				src[index(xs + 0, ys + 0, w1, h1)], src[index(xs + 1, ys + 0, w1, h1)],
				src[index(xs + 0, ys + 1, w1, h1)], src[index(xs + 1, ys + 1, w1, h1)],
				yt, xt
			);
			inc_split(xt, xs, xratio);
		}
		inc_split(yt, ys, yratio);
	}
}


//int main()
//{
//	std::ifstream fin;
//	std::ofstream fout("data_shifted.txt");
//
//	fin.open("data.txt");
//	double min = INFINITY;
//	while (true)
//	{
//		double x;
//		fin >> x;
//		if (!fin)
//			break;
//		min = std::min(min, x);
//	}
//	fin.close();
//	fin.open("data.txt");
//	while (true)
//	{
//		double x;
//		fin >> x;
//		if (!fin)
//			break;
//		fout << x - min << '\n';
//	}
//	fin.close();
//}


int main()
{
	GDALAllRegister();

	auto dataset = gdal_open_file("data/spot.tiff");
	GUInt64 start = 0;
	bgra_image im1;
	gdal_read_bgra_image(dataset.get(), im1);

	const size_t w = 600;
	const size_t h = 450;
	ksn::window_t win;
	win.open(w, h);

	bgra_image im2;
	im2.size[0] = w;
	im2.size[1] = h;
	copy_resize_image(im2, im1);
	win.draw_pixels_bgra_front(im2.data.data(), 0, 0, w, h);
	win.set_framerate(20);

	bool kb_down[(int)ksn::keyboard_button_t::buttons_count]{};
	while (true)
	{
		ksn::event_t ev;
		while (win.poll_event(ev))
		{
			switch (ev.type)
			{
			case ksn::event_type_t::keyboard_press:
				kb_down[(int)ev.keyboard_button_data.button] = 1;
				break;

			case ksn::event_type_t::keyboard_release:
				break;
				kb_down[(int)ev.keyboard_button_data.button] = 0;

			default:
				break;
			}
		}

		if (kb_down[(int)ksn::keyboard_button_t::esc])
			win.close();

		if (!win.is_open())
			break;

		win.tick();
	}

	return 0;
}
