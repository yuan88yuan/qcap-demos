#ifndef SC6F0_DANTE_RX_OUTPUT_H
#define SC6F0_DANTE_RX_OUTPUT_H

#include <array>
#include <cmath>
#include <cstdint>
#include <string>
#include <linux/media-bus-format.h>

namespace sc6f0_rx_output {
enum class Backend { HDMI, SDI };

inline Backend from_product(const std::string& product) {
	return product.find("SC6F0 SDI Dante RX") == 0 ? Backend::SDI : Backend::HDMI;
}

inline bool sdi_size(uint32_t width, uint32_t height) {
	return (width == 1920 && height == 1080) || (width == 3840 && height == 2160);
}

inline bool sdi_mode(uint32_t width, uint32_t height, double fps,
	uint32_t clock_khz, bool interlaced) {
	return sdi_size(width, height) && !interlaced && std::isfinite(fps) &&
		std::fabs(fps - 60.0) < 0.005 &&
		clock_khz == (width == 1920 ? 148500u : 594000u);
}

struct Property { const char* name; uint64_t value; };

inline std::array<Property, 11> sdi_properties(uint32_t width, uint32_t height) {
	return {{{"sdi_mode", width == 1920 ? 2u : 5u},
		{"sdi_data_stream", width == 1920 ? 2u : 8u}, {"is_frac", 0},
		{"sdi_420_in", 0}, {"sdi_420_out", 0}, {"sdi_444_out", 0},
		{"width_out", width}, {"height_out", height},
		{"in_fmt", MEDIA_BUS_FMT_RGB888_1X24},
		{"out_fmt", MEDIA_BUS_FMT_UYVY10_1X20}, {"c_encoding", 0}}};
}

inline bool sdi_card_id(const std::string& id) {
	std::string normalized;
	for (char c : id) if (c != '-' && c != '_') normalized += c;
	const std::string prefix = "xlnxsdi";
	if (normalized.compare(0, prefix.size(), prefix) != 0) return false;
	for (size_t i = prefix.size(); i < normalized.size(); ++i)
		if (normalized[i] < '0' || normalized[i] > '9') return false;
	return true;
}

/* DAU protocol capability descriptor, not EDID read from an SDI cable.
 * Advertise exactly the configured integer timing, RGB8 and 2ch 48kHz
 * 24-bit PCM. Dante's scaler must target the configured SDI output, not
 * choose another resolution from a list of monitor capabilities.
 * No HDMI VSDB, HDCP, HDR, deep-color or monitor-size claims. Do not add a
 * CTA video VIC: the 60-family VIC also admits 59.94. The exact preferred
 * DTD is the only video timing; CTA carries the audio descriptor only.
 */
inline bool sdi_capabilities(uint32_t width, uint32_t height, uint32_t fps,
	std::array<unsigned char, 256>& e) {
	e.fill(0);
	if (!sdi_size(width, height) || fps != 60) return false;
	const unsigned char header[] = {0, 255, 255, 255, 255, 255, 255, 0};
	for (size_t i = 0; i < 8; ++i) e[i] = header[i];
	e[8] = 0x66; e[9] = 0xa1; e[10] = 1; e[17] = 36;
	e[18] = 1; e[19] = 4; e[20] = 0xa0; e[23] = 0xff; e[24] = 0x02;
	for (size_t i = 38; i < 54; ++i) e[i] = 1;
	const unsigned char hd[] = {0x02,0x3a,0x80,0x18,0x71,0x38,0x2d,0x40,
		0x58,0x2c,0x45,0,0,0,0,0,0,0x1e};
	const unsigned char uhd[] = {0x08,0xe8,0,0x30,0xf2,0x70,0x5a,0x80,
		0xb0,0x58,0x8a,0,0,0,0,0,0,0x1e};
	const unsigned char* timing = width == 1920 ? hd : uhd;
	for (size_t i = 0; i < 18; ++i) e[54+i] = timing[i];
	e[75] = 0x10; // unused descriptor, not a second timing
	e[93] = 0xfc;
	const char name[] = "SC6F0 SDI RX\n";
	for (size_t i = 0; i < 13; ++i) e[95+i] = i < sizeof(name)-1 ? name[i] : ' ';
	e[111] = 0x10; e[126] = 1;
	e[128] = 0x02; e[129] = 0x03; e[130] = 8;
	// RGB-only flags, no Video Data Block or fractional-rate alternative.
	e[132] = 0x23; e[133] = 0x09; e[134] = 0x04; e[135] = 0x04;
	for (size_t base : {size_t(0), size_t(128)}) {
		unsigned int sum = 0;
		for (size_t i = 0; i < 127; ++i) sum += e[base+i];
		e[base+127] = static_cast<unsigned char>(0u-sum);
	}
	return true;
}
}
#endif
