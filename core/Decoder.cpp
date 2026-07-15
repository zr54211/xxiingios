#include "Decoder.h"

#include "ReadBarcode.h"
#include "Version.h"

#include "StringUtils.h"

namespace bsz {

std::string DecodeLuminance(const uint8_t* data, int width, int height)
{
	if (!data || width <= 0 || height <= 0)
		return R"({"found":0,"barcodes":[],"error":"invalid frame parameters"})";

	const ZXing::ImageView image(data, width, height, ZXing::ImageFormat::Lum);

	// TryHarder/TryRotate/TryInvert — целимся в плотные мятые QR, скорость вторична.
	const auto options = ZXing::ReaderOptions()
		.setTryHarder(true)
		.setTryRotate(true)
		.setTryInvert(true)
		.setMaxNumberOfSymbols(1);

	const auto barcodes = ZXing::ReadBarcodes(image, options);

	std::string json = "{\"found\":" + std::to_string(barcodes.size()) + ",\"barcodes\":[";

	bool first = true;
	for (const auto& barcode : barcodes) {
		if (!first)
			json += ',';

		first = false;
		json += "{\"format\":\"" + ZXing::ToString(barcode.format())
			+ "\",\"text\":\"" + EscapeJson(barcode.text()) + "\"}";
	}

	json += "]}";
	return json;
}

std::string ZXingVersion()
{
	return ZXING_VERSION_STR;
}

} // namespace bsz
