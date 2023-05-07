
#define utf8(str) ((const char*)(u8 ## str))


#pragma warning(disable : 4067)

#include <Windows.h>;
#undef min
#undef max


#include <iostream>;
#include <vector>;
#include <fstream>;
#include <thread>;
#include <format>

#include <gdal.h>;
#include <gdal_priv.h>;
#include <ogrsf_frmts.h>;
#pragma comment(lib, "gdal.lib")
#pragma comment(lib, "proj.lib")


#include <ksn/math_vec.hpp>
#include <ksn/stuff.hpp>;
#include <ksn/window.hpp>;
#pragma comment(lib, "libksn_window.lib")
#pragma comment(lib, "libksn_time.lib")

#include <intrin.h>
#include <mmintrin.h>
#include <immintrin.h>



void error(std::wstring_view desc, std::wstring_view title = L"Ошибка")
{
	MessageBoxW(GetConsoleWindow(), desc.data(), title.data(), MB_ICONERROR);
	exit(-1);
}

int silent_handler = 0;
void gdal_error_handler(CPLErr category, CPLErrorNum code, const char* msg)
{
	if (silent_handler < 0)
		error(L"silent_handler < 0");

	if (silent_handler > 0)
		return;

	if (category < CE_Failure)
		return;

	if (code == CPLE_OpenFailed)
		return (void)MessageBoxA(GetConsoleWindow(), "Не удалось открыть файл", "GDAL", MB_ICONERROR);

	MessageBoxA(GetConsoleWindow(), msg, "GDAL", MB_ICONERROR);
}


auto gdal_open(const char* name)
{
	silent_handler++;
	
	auto handle = GDALOpen(name, GA_ReadOnly);
	if (!handle)
		handle = GDALOpenEx(name, GDAL_OF_VECTOR, 0, 0, 0);

	silent_handler--;
	
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

using bgra = ksn::vec<4, uint8_t>;


struct bgra_image
{
	std::vector<bgra> data;
	uint32_t size[2]{};

	auto& operator[](this auto&& self, size_t n)
	{
		return self.data[n];
	}

	void create(uint32_t w, uint32_t h)
	{
		this->data.resize(w * h);
		this->size[0] = w;
		this->size[1] = h;
	}
	void draw(ksn::window_t& win, uint32_t x = 0, uint32_t y = 0)
	{
		win.draw_pixels_bgra_front(this->data.data(), x, y, this->size[0], this->size[1]);
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


template<class T, class fp = T>
T lerp(T a, T b, fp t)
{
	return a + (b - a) * t;
}


template<class T, class fp = double>
T bilerp(T a1, T a2, T b1, T b2, fp tab, fp t12)
{
	T a = lerp(a1, a2, t12);
	T b = lerp(b1, b2, t12);
	return lerp(a, b, tab);
}



#define _mm_cvt_pu8_ps(x) (_mm_cvtepi32_ps(_mm_cvtepu8_epi32(*(__m128i*)&x)))

__declspec(noinline)
bgra bilerp_bgra(bgra _a1, bgra _a2, bgra _b1, bgra _b2, float _tab, float _t12)
{
	__m128 a1 = _mm_cvt_pu8_ps(_a1);
	__m128 a2 = _mm_cvt_pu8_ps(_a2);
	__m128 b1 = _mm_cvt_pu8_ps(_b1);
	__m128 b2 = _mm_cvt_pu8_ps(_b2);
	__m128 tab = _mm_set_ps1(_tab);
	__m128 t12 = _mm_set_ps1(_t12);

	a2 = _mm_sub_ps(a2, a1);
	b2 = _mm_sub_ps(b2, b1);
	a2 = _mm_mul_ps(a2, t12);
	b2 = _mm_mul_ps(b2, t12);
	a1 = _mm_add_ps(a1, a2);
	b1 = _mm_add_ps(b1, b2);
	b1 = _mm_sub_ps(b1, a1);
	b1 = _mm_mul_ps(b1, tab);
	a1 = _mm_add_ps(a1, b1);

	__m128i i = _mm_cvtps_epi32(a1);

	__m128i ord;
	ord.m128i_i32[0] = 0x03020100 * 4;
	i = _mm_shuffle_epi8(i, ord);

	bgra x;
	_mm_storeu_si32(&x, i);
	return x;
}



void copy_resize_image(bgra_image& dst, const bgra_image& src)
{
	const auto& w1 = src.size[0];
	const auto& h1 = src.size[1];
	const auto& w2 = dst.size[0];
	const auto& h2 = dst.size[1];

	dst.data.resize(w2 * h2);

	if (w1 == 0 || h1 == 0)
		return;

	using fp = float;
	const fp xratio = w2 ? (fp)(w1 - 1) / (w2 - 1) * (1 - FLT_EPSILON) : 0;
	const fp yratio = h2 ? (fp)(h1 - 1) / (h2 - 1) * (1 - FLT_EPSILON) : 0;

	auto index = []
	(size_t x, size_t y, size_t x_size)
	{
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

	auto translate = []
	(size_t w, fp ratio)
	{
		fp whole;
		std::pair<size_t, fp> result;
		result.second = std::modf(w * ratio, &whole);
		result.first = (size_t)whole;
		return result;
	};

	auto worker = [&]
	(size_t offset, size_t iters)
	{
		size_t x = offset % w2;
		auto [ys, yt] = translate(offset / w2, yratio);
		auto [xs, xt] = translate(x, xratio);

		auto idx_dst = offset;
		auto idx_src = ys * w1 + xs;

		while (iters-- > 0)
		{
			dst[idx_dst++] = bilerp_bgra(
				src[idx_src], src[idx_src + 1],
				src[idx_src + w1], src[idx_src + w1 + 1],
				yt, xt
			);

			xt += xratio;
			while (xt >= 1)
			{
				--xt;
				++idx_src;
			}
			if (++x < w2) [[likely]]
				continue;
			x = 0;
			xt = 0;
			inc_split(yt, ys, yratio);
			idx_src = ys * w1;
		}
	};

	auto divide_round_up = []
	(auto x, auto y)
	{
		return (x + y - 1) / y;
	};

	std::thread threads[16];
	const size_t N = dst.data.size();
	
	const size_t n = std::thread::hardware_concurrency();
	const size_t split_size = divide_round_up(N, n);

	for (size_t work_offset = 0, i = 0; work_offset < N; ++i)
	{
		const size_t work_end = std::min(N, work_offset + split_size);
		threads[i] = std::thread(worker, work_offset, work_end - work_offset);
		work_offset = work_end;
	}

	for (size_t i = 0; i < n; ++i)
	{
		if (threads[i].joinable())
			threads[i].join();
	}
}

std::string get_read_filename()
{
	std::string file(1024, 0);

	OPENFILENAMEA data{};
	data.lStructSize = sizeof(data);
	data.hwndOwner = GetConsoleWindow();
	data.lpstrFile = file.data();
	data.nMaxFile = (DWORD)file.size();
	data.Flags = OFN_DONTADDTORECENT | OFN_FILEMUSTEXIST;
	
	GetOpenFileNameA(&data);
	return file;
}


std::string safe_string(const char* s, const char* def = "")
{
	return s ? s : def;
}

template<class char_t, class traits_t, class ...Args>
void print(std::basic_ostream<char_t, traits_t>& os, const char_t* const fmt, Args&& ...args)
{
	os << std::vformat(fmt, std::make_format_args(std::forward<Args>(args)...));
}
template<class ...Args>
void print(const char* const fmt, Args&& ...args)
{
	print(std::cout, fmt, std::forward<Args>(args)...);
}

void print_dataset_info(const GDALDatasetUniquePtr& dataset_ptr)
{
	print("Driver: {}/{}\n",
		dataset_ptr->GetDriver()->GetDescription(),
		dataset_ptr->GetDriver()->GetMetadataItem(GDAL_DMD_LONGNAME)
	);

	if (dataset_ptr->GetRasterCount())
		print("Raster size: {}x{}x{}\n",
			dataset_ptr->GetRasterXSize(), 
			dataset_ptr->GetRasterYSize(),
			dataset_ptr->GetRasterCount()
		);

	std::string proj = safe_string(dataset_ptr->GetProjectionRef());
	if (!proj.empty())
		print("Projection name: {}\n", dataset_ptr->GetProjectionRef());

	double geotransform_data[6];
	if (dataset_ptr->GetGeoTransform(geotransform_data) == CE_None)
	{
		print("Origin: ({:.6f}, {:.6f})\n",
			geotransform_data[0], geotransform_data[3]);
		print("Pixel Size: ({:.6f}, {:.6f})\n",
			geotransform_data[1], geotransform_data[5]);
	}
}

void vector_playground(const GDALDatasetUniquePtr& dataset)
{
	auto& fout = std::cout;

	print(fout, "Layers count: {}\n", dataset->GetLayerCount());
	for (auto&& layer : dataset->GetLayers())
	{
		print(fout, "\nLayer \"{}\":\n", layer->GetName());
		print(fout, "Features count: {}\n", layer->GetFeatureCount());
		for (auto&& feature : *layer)
		{
			print(fout, "Feature {}: ", feature->GetFID());
			for (auto&& field : feature)
			{
				print(fout, "'{}'=\"{}\" ", field.GetName(), field.GetAsString());
			}
			fout << '\n';
		}
	}

	exit(0);
}

int main()
{
	CPLErrorHandlerPusher error_handler_frame(gdal_error_handler, 0);
	GDALAllRegister();

	auto dataset = gdal_open(get_read_filename().c_str());
	//auto dataset = gdal_open("DATA\\world\\cities.shp");

	if (!dataset)
		error(L"Не удалось открыть указанный файл");

	print_dataset_info(dataset);

	bgra_image im1;
	bgra_image im2;

	if (!gdal_read_bgra_image(dataset.get(), im1))
		vector_playground(dataset);

	uint16_t w = 1280;
	uint16_t h = 720;
	ksn::window_t win;
	win.open(w, h);
	win.set_framerate(20);

	bool render_pending = true;

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

			case ksn::event_type_t::close:
				win.close();
				break;

			case ksn::event_type_t::resize:
				w = ev.window_resize_data.width_new;
				h = ev.window_resize_data.height_new;
				render_pending = true;
				break;

			default:
				break;
			}
		}

		if (kb_down[(int)ksn::keyboard_button_t::esc])
			win.close();

		if (!win.is_open())
			break;

		if (render_pending)
		{
			im2.create(w, h);
			copy_resize_image(im2, im1);
			im2.draw(win);
			render_pending = false;
		}

		win.tick();
	}
	return 0;
}
