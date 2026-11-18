#include "ImageFile.h"

#include "ByteOrder.h"
#include "ContentError.h"
#include "TextureFile.h"  // kMaxTextureSize

#include <algorithm>
#include <array>
#include <cctype>
#include <cstddef>
#include <cstring>
#include <memory>

#if TRAINZ_HAVE_JPEG
#include <csetjmp>
#include <cstdio>  // jpeglib.h needs FILE and size_t first

#include <jerror.h>
#include <jpeglib.h>
#endif

namespace trainz::content {

namespace {

void checkSize(const char* what, std::int64_t w, std::int64_t h) {
    if (w <= 0 || h <= 0 || w > kMaxTextureSize || h > kMaxTextureSize) {
        throw ContentError(std::string(what) + " size " + std::to_string(w) + "x" + std::to_string(h) +
                           " is not allowed (1.." + std::to_string(kMaxTextureSize) + " per side)");
    }
}

std::uint8_t expand5(unsigned v) { return static_cast<std::uint8_t>((v * 255 + 15) / 31); }

// Reads bytes in order and throws once the input runs out.
class Reader {
public:
    Reader(std::span<const std::uint8_t> d, std::size_t pos, const char* what) : d_(d), pos_(pos), what_(what) {}

    const std::uint8_t* take(std::size_t n) {
        if (n > d_.size() || pos_ > d_.size() - n) {
            throw ContentError(std::string(what_) + " is truncated at byte " + std::to_string(d_.size()) +
                               " (needs " + std::to_string(n) + " more at " + std::to_string(pos_) + ")");
        }
        const std::uint8_t* p = d_.data() + pos_;
        pos_ += n;
        return p;
    }
    std::uint8_t byte() { return *take(1); }
    std::size_t left() const { return d_.size() - pos_; }

private:
    std::span<const std::uint8_t> d_;
    std::size_t pos_;
    const char* what_;
};

// ---------------------------------------------------------------- TGA

const char* tgaTypeName(unsigned t) {
    switch (t) {
        case 1: return "colour-mapped";
        case 2: return "true-colour";
        case 3: return "grey";
        case 9: return "RLE colour-mapped";
        case 10: return "RLE true-colour";
        case 11: return "RLE grey";
        default: return "unknown";
    }
}

// One pixel of `bytes` bytes (BGR(A) order, 16-bit as A1R5G5B5) to RGBA.
void tgaColour(const std::uint8_t* s, unsigned bytes, bool alpha16, std::uint8_t* d) {
    switch (bytes) {
        case 1:
            d[0] = d[1] = d[2] = s[0], d[3] = 255;
            break;
        case 2: {
            const unsigned v = loadU16LE(s);
            d[0] = expand5((v >> 10) & 31u), d[1] = expand5((v >> 5) & 31u), d[2] = expand5(v & 31u);
            d[3] = alpha16 ? ((v & 0x8000u) != 0 ? 255 : 0) : 255;
            break;
        }
        case 3:
            d[0] = s[2], d[1] = s[1], d[2] = s[0], d[3] = 255;
            break;
        default:  // 4
            d[0] = s[2], d[1] = s[1], d[2] = s[0], d[3] = s[3];
            break;
    }
}

}  // namespace

const char* imageKindName(ImageKind k) {
    switch (k) {
        case ImageKind::Tga: return "TGA";
        case ImageKind::Bmp: return "BMP";
        case ImageKind::Jpeg: return "JPEG";
        case ImageKind::Unknown: break;
    }
    return "unknown";
}

ImageKind imageKindFromName(std::string_view name) {
    const auto dot = name.rfind('.');
    if (dot == std::string_view::npos) {
        return ImageKind::Unknown;
    }
    std::string ext(name.substr(dot + 1));
    std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (ext == "tga") {
        return ImageKind::Tga;
    }
    if (ext == "bmp") {
        return ImageKind::Bmp;
    }
    if (ext == "jpg" || ext == "jpeg") {
        return ImageKind::Jpeg;
    }
    return ImageKind::Unknown;
}

RgbaImage decodeTga(std::span<const std::uint8_t> data) {
    Reader r(data, 0, "TGA");
    const std::uint8_t* h = r.take(18);
    const unsigned idLength = h[0];
    const unsigned mapType = h[1];
    const unsigned type = h[2];
    const unsigned mapFirst = loadU16LE(h + 3);
    const unsigned mapLength = loadU16LE(h + 5);
    const unsigned mapBits = h[7];
    const unsigned width = loadU16LE(h + 12);
    const unsigned height = loadU16LE(h + 14);
    const unsigned bpp = h[16];
    const unsigned desc = h[17];
    const unsigned alphaBits = desc & 15u;
    const bool topOrigin = (desc & 0x20u) != 0;
    const bool rightToLeft = (desc & 0x10u) != 0;

    if (type != 1 && type != 2 && type != 3 && type != 9 && type != 10 && type != 11) {
        throw ContentError("TGA image type " + std::to_string(type) +
                           (type == 0 ? " (no image data)" : "") + " is not supported");
    }
    if (mapType > 1) {
        throw ContentError("TGA colour map type " + std::to_string(mapType) + " is not valid");
    }
    checkSize("TGA", width, height);
    const bool mapped = type == 1 || type == 9;
    const bool rle = type >= 9;
    if (mapped) {
        if (mapType != 1 || bpp != 8) {
            throw ContentError("colour-mapped TGA needs a colour map and 8-bit indices (has map type " +
                               std::to_string(mapType) + ", " + std::to_string(bpp) + " bpp)");
        }
        if (mapBits != 15 && mapBits != 16 && mapBits != 24 && mapBits != 32) {
            throw ContentError("TGA colour map entries of " + std::to_string(mapBits) + " bits are not supported");
        }
        if (mapLength == 0 || mapFirst + mapLength > 65536u) {
            throw ContentError("TGA colour map range " + std::to_string(mapFirst) + "+" + std::to_string(mapLength) +
                               " is not valid");
        }
    } else if (type == 3 || type == 11) {
        if (bpp != 8) {
            throw ContentError("grey TGA with " + std::to_string(bpp) + " bpp is not supported (8 only)");
        }
    } else if (bpp != 15 && bpp != 16 && bpp != 24 && bpp != 32) {
        throw ContentError("true-colour TGA with " + std::to_string(bpp) + " bpp is not supported");
    }

    RgbaImage img;
    img.width = width;
    img.height = height;
    r.take(idLength);

    // The colour map: skipped for true-colour/grey images (the spec allows one there).
    std::vector<std::array<std::uint8_t, 4>> palette;
    if (mapType == 1) {
        const unsigned entryBytes = (mapBits + 7) / 8;
        if (entryBytes < 2 || entryBytes > 4) {
            throw ContentError("TGA colour map entries of " + std::to_string(mapBits) + " bits are not supported");
        }
        const std::uint8_t* m = r.take(static_cast<std::size_t>(mapLength) * entryBytes);
        if (mapped) {
            palette.assign(mapFirst + mapLength, {0, 0, 0, 255});
            for (unsigned i = 0; i < mapLength; ++i) {
                tgaColour(m + i * entryBytes, entryBytes, mapBits == 16 && alphaBits == 1, palette[mapFirst + i].data());
            }
        }
    }

    const unsigned pixelBytes = (bpp + 7) / 8;
    const std::size_t pixels = static_cast<std::size_t>(width) * height;
    // Refuse before allocating if the remaining bytes cannot possibly hold the image
    // (an RLE packet of 1 + pixelBytes bytes gives at most 128 pixels).
    const std::size_t maxPixels = rle ? (r.left() / (1 + pixelBytes)) * 128 : r.left() / pixelBytes;
    if (maxPixels < pixels) {
        throw ContentError("TGA is truncated: " + std::to_string(r.left()) + " bytes cannot hold " +
                           std::to_string(width) + "x" + std::to_string(height) + " pixels");
    }

    // 16-bit: the top bit is alpha only when the descriptor says 1 attribute bit.
    // 32-bit: the 4th byte is used as alpha even when the descriptor says 0
    // attribute bits (420 disc files do that; see docs/formats/image.md).
    const bool alpha16 = bpp == 16 && alphaBits == 1;
    std::vector<std::uint8_t> stream(pixels * 4);
    auto put = [&](std::size_t i, const std::uint8_t* src) {
        std::uint8_t* d = stream.data() + 4 * i;
        if (mapped) {
            const unsigned idx = src[0];
            if (idx >= palette.size() || idx < mapFirst) {
                throw ContentError("TGA colour index " + std::to_string(idx) + " is outside the colour map");
            }
            std::memcpy(d, palette[idx].data(), 4);
        } else {
            tgaColour(src, pixelBytes, alpha16, d);
        }
    };
    if (!rle) {
        const std::uint8_t* p = r.take(pixels * pixelBytes);
        for (std::size_t i = 0; i < pixels; ++i) {
            put(i, p + i * pixelBytes);
        }
    } else {
        // Packets may run on from one row to the next (TGA 1.0 allowed it).
        std::size_t i = 0;
        while (i < pixels) {
            const unsigned head = r.byte();
            const std::size_t count = (head & 0x7fu) + 1;
            if (count > pixels - i) {
                throw ContentError("TGA RLE packet at pixel " + std::to_string(i) + " runs past the image end");
            }
            if ((head & 0x80u) != 0) {
                const std::uint8_t* p = r.take(pixelBytes);
                for (std::size_t k = 0; k < count; ++k) {
                    put(i + k, p);
                }
            } else {
                const std::uint8_t* p = r.take(count * pixelBytes);
                for (std::size_t k = 0; k < count; ++k) {
                    put(i + k, p + k * pixelBytes);
                }
            }
            i += count;
        }
    }

    // Reorder to top row first, left to right.
    img.rgba.resize(pixels * 4);
    for (unsigned y = 0; y < height; ++y) {
        const unsigned srcY = topOrigin ? y : height - 1 - y;
        for (unsigned x = 0; x < width; ++x) {
            const unsigned srcX = rightToLeft ? width - 1 - x : x;
            std::memcpy(img.rgba.data() + (static_cast<std::size_t>(y) * width + x) * 4,
                        stream.data() + (static_cast<std::size_t>(srcY) * width + srcX) * 4, 4);
        }
    }
    img.alphaFromFile = mapped ? (mapBits == 32 || (mapBits == 16 && alphaBits == 1)) : (bpp == 32 || alpha16);
    img.description = "TGA type " + std::to_string(type) + " (" + tgaTypeName(type) + "), " + std::to_string(bpp) +
                      " bpp, " + std::to_string(alphaBits) + " alpha bits, " + (topOrigin ? "top" : "bottom") +
                      (rightToLeft ? "-right" : "-left") + " origin";
    return img;
}

// ---------------------------------------------------------------- BMP

RgbaImage decodeBmp(std::span<const std::uint8_t> data) {
    Reader r(data, 0, "BMP");
    const std::uint8_t* fh = r.take(14);
    if (fh[0] != 'B' || fh[1] != 'M') {
        throw ContentError("not a BMP (no 'BM' signature)");
    }
    const std::uint32_t pixelOffset = loadU32LE(fh + 10);
    const std::uint8_t* hs = r.take(4);
    const std::uint32_t headerSize = loadU32LE(hs);
    if (headerSize != 40 && headerSize != 52 && headerSize != 56 && headerSize != 108 && headerSize != 124) {
        throw ContentError("BMP info header of " + std::to_string(headerSize) +
                           " bytes is not supported (40, 52, 56, 108 or 124)");
    }
    const std::uint8_t* ih = r.take(36);  // the rest of the 40-byte BITMAPINFOHEADER
    const auto width = static_cast<std::int32_t>(loadU32LE(ih));
    const auto rawHeight = static_cast<std::int32_t>(loadU32LE(ih + 4));
    const unsigned planes = loadU16LE(ih + 8);
    const unsigned bpp = loadU16LE(ih + 10);
    const std::uint32_t compression = loadU32LE(ih + 12);
    const std::uint32_t colorsUsed = loadU32LE(ih + 28);
    // INT32_MIN cannot be negated; checkSize refuses it as too large anyway.
    const bool topDown = rawHeight < 0;
    const std::int64_t height = topDown ? -static_cast<std::int64_t>(rawHeight) : rawHeight;
    checkSize("BMP", width, height);
    if (planes != 1) {
        throw ContentError("BMP has " + std::to_string(planes) + " planes (must be 1)");
    }
    if (bpp != 1 && bpp != 4 && bpp != 8 && bpp != 24 && bpp != 32) {
        throw ContentError("BMP with " + std::to_string(bpp) + " bits per pixel is not supported");
    }
    const bool rle8 = compression == 1;
    if (!(compression == 0 || (rle8 && bpp == 8))) {
        throw ContentError("BMP compression " + std::to_string(compression) + " with " + std::to_string(bpp) +
                           " bpp is not supported (only uncompressed, and RLE8 for 8 bpp)");
    }
    if (rle8 && topDown) {
        throw ContentError("RLE8 BMP cannot be top-down");
    }

    RgbaImage img;
    const auto w = static_cast<std::uint32_t>(width);
    const auto h = static_cast<std::uint32_t>(height);
    img.width = w;
    img.height = h;

    std::vector<std::array<std::uint8_t, 4>> palette;
    if (bpp <= 8) {
        const std::uint32_t maxColours = 1u << bpp;
        const std::uint32_t n = colorsUsed == 0 ? maxColours : colorsUsed;
        if (n > maxColours) {
            throw ContentError("BMP palette of " + std::to_string(n) + " colours is too big for " +
                               std::to_string(bpp) + " bpp");
        }
        Reader pr(data, 14 + std::size_t{headerSize}, "BMP palette");
        const std::uint8_t* p = pr.take(std::size_t{n} * 4);
        palette.resize(n);
        for (std::uint32_t i = 0; i < n; ++i) {
            palette[i] = {p[4 * i + 2], p[4 * i + 1], p[4 * i], 255};  // B, G, R, reserved
        }
    }

    const std::size_t pixels = static_cast<std::size_t>(w) * h;
    if (pixelOffset >= data.size()) {
        throw ContentError("BMP pixel data offset " + std::to_string(pixelOffset) + " is past the end (" +
                           std::to_string(data.size()) + " bytes)");
    }
    img.rgba.resize(pixels * 4);
    std::size_t badIndex = 0;
    auto putIndex = [&](std::uint32_t x, std::uint32_t row, unsigned idx) {
        std::uint8_t* d = img.rgba.data() + (static_cast<std::size_t>(row) * w + x) * 4;
        if (idx < palette.size()) {
            std::memcpy(d, palette[idx].data(), 4);
        } else {
            ++badIndex;
            d[0] = d[1] = d[2] = 0, d[3] = 255;
        }
    };
    // File row `fileRow` (0 = first stored) to the output row.
    auto outRow = [&](std::uint32_t fileRow) { return topDown ? fileRow : h - 1 - fileRow; };

    if (!rle8) {
        const std::size_t stride = ((static_cast<std::size_t>(w) * bpp + 31) / 32) * 4;
        Reader pr(data, pixelOffset, "BMP pixel data");
        // Bounded: w, h <= 8192, so stride * h < 2^31.
        const std::uint8_t* base = pr.take(stride * h);
        for (std::uint32_t fy = 0; fy < h; ++fy) {
            const std::uint8_t* s = base + stride * fy;
            const std::uint32_t y = outRow(fy);
            for (std::uint32_t x = 0; x < w; ++x) {
                if (bpp <= 8) {
                    const std::size_t bit = static_cast<std::size_t>(x) * bpp;
                    const unsigned idx = (s[bit / 8] >> (8 - bpp - bit % 8)) & ((1u << bpp) - 1);
                    putIndex(x, y, idx);
                } else {
                    std::uint8_t* d = img.rgba.data() + (static_cast<std::size_t>(y) * w + x) * 4;
                    const std::uint8_t* p = s + static_cast<std::size_t>(x) * (bpp / 8);
                    // 32 bpp BI_RGB: the 4th byte is "reserved" in the spec, so opaque.
                    d[0] = p[2], d[1] = p[1], d[2] = p[0], d[3] = 255;
                }
            }
        }
        img.description = "BMP " + std::to_string(bpp) + " bpp uncompressed, " + (topDown ? "top-down" : "bottom-up");
    } else {
        // RLE8. Pixels the stream skips (delta, early end of line/bitmap) get palette index 0.
        for (std::size_t i = 0; i < pixels; ++i) {
            putIndex(static_cast<std::uint32_t>(i % w), static_cast<std::uint32_t>(i / w), 0);
        }
        Reader pr(data, pixelOffset, "BMP RLE8 data");
        std::uint32_t x = 0;
        std::uint32_t fy = 0;
        bool sawEnd = false;
        bool overrun = false;
        while (!sawEnd) {
            const unsigned n = pr.byte();
            const unsigned v = pr.byte();
            if (n > 0) {
                for (unsigned k = 0; k < n; ++k, ++x) {
                    if (x < w && fy < h) {
                        putIndex(x, outRow(fy), v);
                    } else {
                        overrun = true;
                    }
                }
            } else if (v == 0) {  // end of line
                x = 0;
                ++fy;
            } else if (v == 1) {  // end of bitmap
                sawEnd = true;
            } else if (v == 2) {  // delta
                const std::uint8_t* d = pr.take(2);
                x += d[0];
                fy += d[1];
            } else {  // absolute run of v bytes, padded to 2
                const std::uint8_t* p = pr.take(v + (v & 1u));
                for (unsigned k = 0; k < v; ++k, ++x) {
                    if (x < w && fy < h) {
                        putIndex(x, outRow(fy), p[k]);
                    } else {
                        overrun = true;
                    }
                }
            }
            if (fy > h) {
                throw ContentError("BMP RLE8 data runs past the last row");
            }
        }
        if (overrun) {
            img.warnings.push_back("RLE8 data writes past the end of a row; the extra pixels were dropped");
        }
        img.description = "BMP 8 bpp RLE8, bottom-up";
    }
    if (badIndex > 0) {
        img.warnings.push_back(std::to_string(badIndex) + " pixels use colour indices past the " +
                               std::to_string(palette.size()) + "-entry palette (drawn black)");
    }
    return img;
}

// ---------------------------------------------------------------- JPEG

#if TRAINZ_HAVE_JPEG

namespace {

// libjpeg reports fatal errors through error_exit, which must not return.
// We longjmp back to runJpeg(). Only C objects (libjpeg's own frames and the
// structs below, which live in the caller's frame) are skipped, so no C++
// destructor is bypassed.
struct JpegJob {
    jpeg_decompress_struct cinfo;
    jpeg_error_mgr err;
    jpeg_progress_mgr progress;
    std::jmp_buf jump;
    char message[JMSG_LENGTH_MAX];
    std::vector<std::uint8_t>* out;  // RGBA, sized after the header has been checked
    char warning[4][JMSG_LENGTH_MAX];  // the first few libjpeg warnings (copied out afterwards)
    int warningCount;
    bool addedEoi;  // the input did not end with FF D9 and runJpeg got a copy with one appended
    std::uint32_t width;
    std::uint32_t height;
    int components;
    bool progressive;
};

constexpr int kMaxJpegScans = 500;  // libjpeg-turbo's advice against "scan bombs"

JpegJob* jobOf(j_common_ptr c) {
    // err is a member of JpegJob; recover the job from the pointer libjpeg hands us.
    return reinterpret_cast<JpegJob*>(reinterpret_cast<char*>(c->err) - offsetof(JpegJob, err));
}

extern "C" void jpegErrorExit(j_common_ptr c) {
    JpegJob* job = jobOf(c);
    (*c->err->format_message)(c, job->message);
    std::longjmp(job->jump, 1);
}

extern "C" void jpegEmitMessage(j_common_ptr c, int level) {
    if (level >= 0) {
        return;  // trace messages
    }
    JpegJob* job = jobOf(c);
    // Truncation. libjpeg would carry on and pad the rest of the picture with grey;
    // we refuse instead. Without our appended end marker it shows as "premature
    // end of file", with it as "hit marker" (the scan data stops early).
    if (c->err->msg_code == JWRN_JPEG_EOF || (job->addedEoi && c->err->msg_code == JWRN_HIT_MARKER)) {
        (*c->err->format_message)(c, job->message);
        std::size_t used = std::strlen(job->message);
        std::snprintf(job->message + used, sizeof job->message - used, " (the file is truncated)");
        std::longjmp(job->jump, 1);
    }
    if (job->warningCount < 4) {
        (*c->err->format_message)(c, job->warning[job->warningCount]);
    }
    ++job->warningCount;
    ++c->err->num_warnings;
}

extern "C" void jpegProgress(j_common_ptr c) {
    if (c->is_decompressor) {
        auto* d = reinterpret_cast<j_decompress_ptr>(c);
        if (d->input_scan_number > kMaxJpegScans) {
            JpegJob* job = jobOf(c);
            std::snprintf(job->message, sizeof job->message, "JPEG has more than %d scans", kMaxJpegScans);
            std::longjmp(job->jump, 1);
        }
    }
}

// Returns false with job->message set on any libjpeg error. Has no C++ objects of its
// own, so the longjmp skips nothing that needs a destructor. Everything it changes
// after setjmp lives in *job (not a local of this function).
bool runJpeg(const std::uint8_t* data, std::size_t size, JpegJob* job) {
    job->cinfo.err = jpeg_std_error(&job->err);
    job->err.error_exit = jpegErrorExit;
    job->err.emit_message = jpegEmitMessage;
    job->message[0] = '\0';
    if (setjmp(job->jump) != 0) {
        jpeg_destroy_decompress(&job->cinfo);
        return false;
    }
    jpeg_CreateDecompress(&job->cinfo, JPEG_LIB_VERSION, sizeof(jpeg_decompress_struct));  // the macro uses a C cast
    job->progress.progress_monitor = jpegProgress;
    job->cinfo.progress = &job->progress;
    job->cinfo.mem->max_memory_to_use = std::size_t{1} << 30;
    jpeg_mem_src(&job->cinfo, data, static_cast<unsigned long>(size));
    jpeg_read_header(&job->cinfo, TRUE);
    job->width = job->cinfo.image_width;
    job->height = job->cinfo.image_height;
    job->components = job->cinfo.num_components;
    job->progressive = job->cinfo.progressive_mode != 0;
    if (job->width == 0 || job->height == 0 || job->width > kMaxTextureSize || job->height > kMaxTextureSize) {
        std::snprintf(job->message, sizeof job->message, "JPEG size %ux%u is not allowed (1..%u per side)",
                      job->width, job->height, kMaxTextureSize);
        jpeg_destroy_decompress(&job->cinfo);
        return false;
    }
    if (job->cinfo.jpeg_color_space != JCS_GRAYSCALE && job->cinfo.jpeg_color_space != JCS_YCbCr &&
        job->cinfo.jpeg_color_space != JCS_RGB) {
        std::snprintf(job->message, sizeof job->message,
                      "JPEG colour space %d (%d components) is not supported (grey, YCbCr or RGB only)",
                      static_cast<int>(job->cinfo.jpeg_color_space), job->components);
        jpeg_destroy_decompress(&job->cinfo);
        return false;
    }
    job->cinfo.out_color_space = JCS_EXT_RGBA;
    jpeg_start_decompress(&job->cinfo);
    if (job->cinfo.output_width != job->width || job->cinfo.output_height != job->height ||
        job->cinfo.output_components != 4) {
        std::snprintf(job->message, sizeof job->message, "libjpeg output does not match the header");
        jpeg_destroy_decompress(&job->cinfo);
        return false;
    }
    const std::size_t stride = std::size_t{job->width} * 4;
    job->out->resize(stride * job->height);  // bounded: at most 8192 * 8192 * 4 bytes
    while (job->cinfo.output_scanline < job->cinfo.output_height) {
        JSAMPROW row = job->out->data() + stride * job->cinfo.output_scanline;
        jpeg_read_scanlines(&job->cinfo, &row, 1);
    }
    // A progressive file cut between two scans decodes without any libjpeg
    // warning once we append the end marker, just with coefficients missing.
    // coef_bits[c][k] is the precision still missing for coefficient k of
    // component c (-1 = none received, 0 = complete), so require all zero.
    if (job->addedEoi && job->progressive && job->cinfo.coef_bits != nullptr) {
        for (int c = 0; c < job->cinfo.num_components; ++c) {
            for (int k = 0; k < DCTSIZE2; ++k) {
                if (job->cinfo.coef_bits[c][k] != 0) {
                    std::snprintf(job->message, sizeof job->message,
                                  "progressive JPEG stops before its last scan (the file is truncated)");
                    jpeg_destroy_decompress(&job->cinfo);
                    return false;
                }
            }
        }
    }
    jpeg_finish_decompress(&job->cinfo);
    jpeg_destroy_decompress(&job->cinfo);
    return true;
}

}  // namespace

bool jpegSupported() { return true; }

RgbaImage decodeJpeg(std::span<const std::uint8_t> data) {
    if (data.size() < 4 || data[0] != 0xFF || data[1] != 0xD8) {
        throw ContentError("not a JPEG (no FF D8 start-of-image marker)");
    }
    RgbaImage img;
    // Heap-allocated, zeroed C struct: setjmp/longjmp state lives outside runJpeg's frame.
    auto job = std::make_unique<JpegJob>();
    std::memset(static_cast<void*>(job.get()), 0, sizeof(JpegJob));
    job->out = &img.rgba;
    // Three disc files stop without the FF D9 end marker. libjpeg then reports
    // "premature end of file" even when all the picture data is there, so decode
    // a copy with the marker added; real truncation still fails (see jpegEmitMessage).
    std::vector<std::uint8_t> withEoi;
    std::span<const std::uint8_t> input = data;
    if (data[data.size() - 2] != 0xFF || data[data.size() - 1] != 0xD9) {
        withEoi.reserve(data.size() + 2);
        withEoi.assign(data.begin(), data.end());
        withEoi.push_back(0xFF);
        withEoi.push_back(0xD9);
        input = withEoi;
        job->addedEoi = true;
    }
    if (!runJpeg(input.data(), input.size(), job.get())) {
        img.rgba.clear();
        throw ContentError(std::string("JPEG: ") + job->message);
    }
    for (int i = 0; i < std::min(job->warningCount, 4); ++i) {
        img.warnings.push_back(std::string("libjpeg: ") + job->warning[i]);
    }
    if (job->warningCount > 4) {
        img.warnings.push_back(std::to_string(job->warningCount - 4) + " more libjpeg warnings");
    }
    if (job->addedEoi) {
        img.warnings.push_back("the file does not end with an FF D9 end-of-image marker");
    }
    img.width = job->width;
    img.height = job->height;
    img.description = std::string("JPEG ") + (job->progressive ? "progressive" : "baseline") + ", " +
                      std::to_string(job->components) + (job->components == 1 ? " component" : " components");
    return img;
}

#else  // !TRAINZ_HAVE_JPEG

bool jpegSupported() { return false; }

RgbaImage decodeJpeg(std::span<const std::uint8_t>) {
    throw ContentError("this build has no JPEG support (install libjpeg62-turbo-dev and rebuild)");
}

#endif

RgbaImage decodeImage(ImageKind kind, std::span<const std::uint8_t> data) {
    switch (kind) {
        case ImageKind::Tga: return decodeTga(data);
        case ImageKind::Bmp: return decodeBmp(data);
        case ImageKind::Jpeg: return decodeJpeg(data);
        case ImageKind::Unknown: break;
    }
    throw ContentError("unknown image kind");
}

}  // namespace trainz::content
