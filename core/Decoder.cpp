#include "Decoder.h"

#include "ReadBarcode.h"
#include "Version.h"

#include "StringUtils.h"

#include <algorithm>
#include <cmath>

namespace {

// Число выборок бинарного профиля вдоль линии сканирования. Плотность выше
// числа модулей плотного EAN-13 (95), иначе сопровождение сдвига штрихов
// на наклонном коде грубее ширины штриха.
constexpr int kProfileSamples = 128;

// Профиль яркости вдоль отрезка (x0,y0)-(x1,y1); false — отрезок вышел за кадр.
bool SampleProfile(const uint8_t* data, int width, int height,
	float x0, float y0, float x1, float y1, uint8_t profile[kProfileSamples])
{
	for (int i = 0; i < kProfileSamples; ++i) {
		const float t = (i + 0.5f) / kProfileSamples;
		const int x = static_cast<int>(std::lround(x0 + (x1 - x0) * t));
		const int y = static_cast<int>(std::lround(y0 + (y1 - y0) * t));

		if (x < 0 || x >= width || y < 0 || y >= height)
			return false;

		profile[i] = data[static_cast<size_t>(y) * width + x];
	}

	return true;
}

// Бинаризация профиля порогом посередине его диапазона; false — контраст ниже
// minContrast (однотонная строка, штрихов в ней нет).
bool BinarizeProfile(const uint8_t profile[kProfileSamples], int minContrast,
	bool bits[kProfileSamples])
{
	int lo = 255;
	int hi = 0;

	for (int i = 0; i < kProfileSamples; ++i) {
		lo = std::min(lo, static_cast<int>(profile[i]));
		hi = std::max(hi, static_cast<int>(profile[i]));
	}

	if (hi - lo < minContrast)
		return false;

	const int threshold = (lo + hi) / 2;

	for (int i = 0; i < kProfileSamples; ++i)
		bits[i] = profile[i] > threshold;

	return true;
}

// Для линейного кода zxing возвращает не углы символа, а отрезок вдоль линии
// сканирования: высота штрихов декодеру не нужна, все четыре «угла» лежат на одной
// прямой, и маркеры на превью схлопываются в линию. Восстанавливаем высоту сами:
// строки штрихкода по вертикали одинаковы, поэтому сдвигаем линию по нормали в обе
// стороны, пока её бинарный профиль совпадает с профилем линии декода, и разводим
// пары углов по найденным границам.
void ExpandLinearCorners(const uint8_t* data, int width, int height, ZXing::PointI corners[4])
{
	const float ax = 0.5f * (corners[0].x + corners[3].x);
	const float ay = 0.5f * (corners[0].y + corners[3].y);
	const float bx = 0.5f * (corners[1].x + corners[2].x);
	const float by = 0.5f * (corners[1].y + corners[2].y);

	const float dx = bx - ax;
	const float dy = by - ay;
	const float len = std::sqrt(dx * dx + dy * dy);

	if (len < 8.0f)
		return;

	// Профиль берётся с отступом от концов отрезка: тихие зоны по краям кода
	// одинаково пусты на любой высоте и размывали бы сравнение.
	const float inset = 0.08f;
	const float sx = ax + dx * inset;
	const float sy = ay + dy * inset;
	const float ex = bx - dx * inset;
	const float ey = by - dy * inset;

	uint8_t profile[kProfileSamples];
	bool reference[kProfileSamples];

	if (!SampleProfile(data, width, height, sx, sy, ex, ey, profile)
			|| !BinarizeProfile(profile, 32, reference))
		return;

	// Нормаль к линии сканирования; направляем к верху кадра, чтобы сохранить
	// смысл верхней и нижней пар углов.
	float nx = -dy / len;
	float ny = dx / len;

	if (ny > 0.0f) {
		nx = -nx;
		ny = -ny;
	}

	// Наклон и изгиб поверхности (этикетка на бутылке) сдвигают штрихи вдоль
	// линии с каждым шагом по нормали. Совпадение ищется с допуском ±3 выборки
	// относительно накопленного дрейфа; сам дрейф ограничен четвертью профиля.
	const auto matchShifted = [&](const bool bits[kProfileSamples], int drift, int& outShift) {
		bool ok = false;
		float best = 0.0f;

		for (int shift = drift - 3; shift <= drift + 3; ++shift) {

			if (shift < -kProfileSamples / 4 || shift > kProfileSamples / 4)
				continue;

			// Совпадение считается по половинам профиля: у наклонного края линия
			// выходит за штрихи «клином» с одного конца, и общая доля ещё держится,
			// когда половина уже вне кода.
			int same[2] = {};
			int valid[2] = {};

			for (int i = 0; i < kProfileSamples; ++i) {
				const int j = i + shift;

				if (j < 0 || j >= kProfileSamples)
					continue;

				const int half = i < kProfileSamples / 2 ? 0 : 1;
				++valid[half];
				same[half] += bits[i] == reference[j] ? 1 : 0;
			}

			if (valid[0] < kProfileSamples / 4 || valid[1] < kProfileSamples / 4)
				continue;

			const float score0 = static_cast<float>(same[0]) / valid[0];
			const float score1 = static_cast<float>(same[1]) / valid[1];
			const float score = 0.5f * (score0 + score1);

			if (score0 >= 0.70f && score1 >= 0.70f && score >= 0.75f && score > best) {
				best = score;
				outShift = shift;
				ok = true;
			}

		}

		return ok;
	};

	// Сдвигаем линию с шагом 2 px, допуская до двух шумных строк подряд; дальше
	// ширины кода не ищем — выше собственной ширины линейные коды не бывают.
	const auto probe = [&](float dirX, float dirY) {
		float boundary = 0.0f;
		int misses = 0;
		int drift = 0;

		for (float offset = 2.0f; offset <= len; offset += 2.0f) {
			bool bits[kProfileSamples];
			int shift = drift;
			const bool match = SampleProfile(data, width, height,
					sx + dirX * offset, sy + dirY * offset,
					ex + dirX * offset, ey + dirY * offset, profile)
				&& BinarizeProfile(profile, 24, bits)
				&& matchShifted(bits, drift, shift);

			if (match) {
				boundary = offset;
				misses = 0;
				drift = shift;
			} else if (++misses >= 2) {
				break;
			}

		}

		return boundary;
	};

	float up = probe(nx, ny);
	float down = probe(-nx, -ny);

	// Если поиск сорвался целиком (перспектива, блик), рамка не тоньше 12% ширины;
	// поодиночке стороны не добиваем — нулевая граница означает линию декода
	// у самого края символа, а не срыв поиска.
	const float minTotal = len * 0.12f;

	if (up + down < minTotal) {
		const float pad = 0.5f * (minTotal - (up + down));
		up += pad;
		down += pad;
	}

	corners[0] = {static_cast<int>(std::lround(ax + nx * up)),
		static_cast<int>(std::lround(ay + ny * up))};
	corners[1] = {static_cast<int>(std::lround(bx + nx * up)),
		static_cast<int>(std::lround(by + ny * up))};
	corners[2] = {static_cast<int>(std::lround(bx - nx * down)),
		static_cast<int>(std::lround(by - ny * down))};
	corners[3] = {static_cast<int>(std::lround(ax - nx * down)),
		static_cast<int>(std::lround(ay - ny * down))};
}

} // namespace

namespace bsz {

DecodeResult DecodeLuminanceEx(const uint8_t* data, int width, int height)
{
	DecodeResult result;

	if (!data || width <= 0 || height <= 0) {
		result.json = R"({"found":0,"barcodes":[],"error":"invalid frame parameters"})";
		return result;
	}

	const ZXing::ImageView image(data, width, height, ZXing::ImageFormat::Lum);

	// TryHarder/TryRotate/TryInvert — целимся в плотные мятые QR, скорость вторична.
	// Матричные коды плюс вся линейная линейка (розничные EAN/UPC, Code128 и пр.).
	const auto options = ZXing::ReaderOptions()
		.setFormats(ZXing::BarcodeFormat::AllMatrix | ZXing::BarcodeFormat::AllLinear)
		.setTryHarder(true)
		.setTryRotate(true)
		.setTryInvert(true)
		.setMaxNumberOfSymbols(1);

	const auto barcodes = ZXing::ReadBarcodes(image, options);
	result.found = static_cast<int>(barcodes.size());

	std::string json = "{\"found\":" + std::to_string(barcodes.size()) + ",\"barcodes\":[";

	bool first = true;
	for (const auto& barcode : barcodes) {
		if (!first)
			json += ',';

		const auto& position = barcode.position();

		ZXing::PointI pts[4] = {position.topLeft(), position.topRight(),
			position.bottomRight(), position.bottomLeft()};

		if (first) {

			result.firstText = ZXing::ToString(barcode.format()) + "|" + barcode.text();

			// У линейных кодов пары углов слиты в отрезок — развести по высоте штрихов.
			if (barcode.format() <= ZXing::BarcodeFormat::AllLinear)
				ExpandLinearCorners(data, width, height, pts);

			// Нормированные углы первого кода — для маркеров на превью.
			for (int i = 0; i < 4; ++i) {
				result.points[i * 2] = static_cast<float>(pts[i].x) / width;
				result.points[i * 2 + 1] = static_cast<float>(pts[i].y) / height;
			}

		}

		first = false;
		json += "{\"format\":\"" + ZXing::ToString(barcode.format())
			+ "\",\"text\":\"" + EscapeJson(barcode.text())
			+ "\",\"points\":[";

		for (int i = 0; i < 4; ++i) {
			if (i)
				json += ',';
			json += "[" + std::to_string(pts[i].x) + "," + std::to_string(pts[i].y) + "]";
		}

		json += "]}";
	}

	json += "]}";
	result.json = std::move(json);
	return result;
}

std::string DecodeLuminance(const uint8_t* data, int width, int height)
{
	return DecodeLuminanceEx(data, width, height).json;
}

std::string ZXingVersion()
{
	return ZXING_VERSION_STR;
}

} // namespace bsz
