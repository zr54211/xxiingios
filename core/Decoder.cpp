#include "Decoder.h"

#include "ReadBarcode.h"
#include "Version.h"

#include "StringUtils.h"

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
	// Только двумерные коды (QR, DataMatrix, Aztec...) — линейные не нужны.
	const auto options = ZXing::ReaderOptions()
		.setFormats(ZXing::BarcodeFormat::AllMatrix)
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

		if (first) {

			result.firstText = ZXing::ToString(barcode.format()) + "|" + barcode.text();

			// Нормированные углы первого кода — для маркеров на превью.
			const ZXing::PointI corners[4] = {position.topLeft(), position.topRight(),
				position.bottomRight(), position.bottomLeft()};

			for (int i = 0; i < 4; ++i) {
				result.points[i * 2] = static_cast<float>(corners[i].x) / width;
				result.points[i * 2 + 1] = static_cast<float>(corners[i].y) / height;
			}

		}

		first = false;
		json += "{\"format\":\"" + ZXing::ToString(barcode.format())
			+ "\",\"text\":\"" + EscapeJson(barcode.text())
			+ "\",\"points\":[";

		const ZXing::PointI pts[4] = {position.topLeft(), position.topRight(),
			position.bottomRight(), position.bottomLeft()};

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
