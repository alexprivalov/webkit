/*
    Copyright (C) 2026 The re_ebook authors

    This library is free software; you can redistribute it and/or
    modify it under the terms of the GNU Library General Public
    License as published by the Free Software Foundation; either
    version 2 of the License, or (at your option) any later version.

    This library is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
    Library General Public License for more details.

    You should have received a copy of the GNU Library General Public License
    along with this library; see the file COPYING.LIB.  If not, write to
    the Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
    Boston, MA 02110-1301, USA.
*/

#include "config.h"
#include "GraphicsContextGL.h"

#if ENABLE(WEBGL) && PLATFORM(QT)

#include "GraphicsContextGLImageExtractor.h"
#include "Image.h"
#include "NativeImage.h"
#include "PixelBuffer.h"

#include <QImage>

namespace WebCore {

// Both halves of the port's contract with WebGL, and both are about the same conversion.
// Qt's QImage stores a pixel as one 32-bit 0xAARRGGBB word, which on a little-endian machine
// is the byte sequence B, G, R, A - so a QImage buffer is directly a BGRA8 texture source,
// while WebGL's own pixel buffers are RGBA8. Everything below is that swap plus the question
// of whether alpha is premultiplied.

// Out of line so that the header can forward-declare QImage instead of including it.
GraphicsContextGLImageExtractor::~GraphicsContextGLImageExtractor() = default;

bool GraphicsContextGLImageExtractor::extractImage(bool premultiplyAlpha, bool, bool)
{
    if (!m_image)
        return false;

    auto nativeImage = m_image->nativeImageForCurrentFrame();
    if (!nativeImage)
        return false;

    const QImage& source = nativeImage->platformImage();
    if (source.isNull())
        return false;

    // Converting to exactly what was asked for means the caller has nothing left to do; the
    // other ports hand back whatever they happen to hold and describe it with an AlphaOp.
    const QImage::Format wanted = premultiplyAlpha
        ? QImage::Format_ARGB32_Premultiplied
        : QImage::Format_ARGB32;

    // convertToFormat returns the original when the format already matches, which would leave
    // m_imagePixelData pointing into the frame rather than into something we own.
    // Not makeUnique: that one insists the type be WTF_MAKE_FAST_ALLOCATED, which a Qt class
    // is not.
    m_qtImage = makeUniqueWithoutFastMallocCheck<QImage>(source.format() == wanted ? source.copy() : source.convertToFormat(wanted));
    if (m_qtImage->isNull())
        return false;

    m_alphaOp = AlphaOp::DoNothing;
    m_imageWidth = m_qtImage->width();
    m_imageHeight = m_qtImage->height();
    if (!m_imageWidth || !m_imageHeight)
        return false;

    // Qt aligns each scanline to 4 bytes, and at 4 bytes per pixel there is never any padding,
    // but read it back rather than assume - a future QImage format would break this silently.
    unsigned unpackAlignment = 1;
    const qsizetype bytesPerRow = m_qtImage->bytesPerLine();
    const qsizetype padding = bytesPerRow - 4 * qsizetype(m_imageWidth);
    if (padding > 0) {
        unpackAlignment = unsigned(padding) + 1;
        while (bytesPerRow % qsizetype(unpackAlignment))
            ++unpackAlignment;
    }

    m_imagePixelData = m_qtImage->constBits();
    m_imageSourceFormat = DataFormat::BGRA8;
    m_imageSourceUnpackAlignment = unpackAlignment;
    return true;
}

RefPtr<NativeImage> GraphicsContextGL::createNativeImageFromPixelBuffer(const GraphicsContextGLAttributes& sourceContextAttributes, Ref<PixelBuffer>&& pixelBuffer)
{
    ASSERT(!pixelBuffer->size().isEmpty());

    const auto imageSize = pixelBuffer->size();
    const size_t totalBytes = pixelBuffer->sizeInBytes();
    uint8_t* pixels = pixelBuffer->bytes();

    // RGBA8 -> BGRA8, in place: the buffer belongs to this call.
    for (size_t i = 0; i < totalBytes; i += 4)
        std::swap(pixels[i], pixels[i + 2]);

    // A context that was not asked for premultiplied alpha hands back straight alpha, which is
    // not what QImage::Format_ARGB32_Premultiplied means. Premultiply rather than mislabel it.
    if (!sourceContextAttributes.premultipliedAlpha) {
        for (size_t i = 0; i < totalBytes; i += 4) {
            pixels[i + 0] = uint8_t(pixels[i + 0] * pixels[i + 3] / 255);
            pixels[i + 1] = uint8_t(pixels[i + 1] * pixels[i + 3] / 255);
            pixels[i + 2] = uint8_t(pixels[i + 2] * pixels[i + 3] / 255);
        }
    }

    // QImage over borrowed memory does not copy, and the PixelBuffer goes away with this call,
    // so take a copy. Deep enough to matter, small enough not to: this runs once per readback.
    QImage image(pixels, imageSize.width(), imageSize.height(), imageSize.width() * 4,
        QImage::Format_ARGB32_Premultiplied);
    return NativeImage::create(image.copy());
}

} // namespace WebCore

#endif // ENABLE(WEBGL) && PLATFORM(QT)
