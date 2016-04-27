/*
 *  Copyright (c) 2010, Dmitry Kazakov <dimula73@gmail.com>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 */
#ifndef KIS_TEXTURE_TILE_UPDATE_INFO_H_
#define KIS_TEXTURE_TILE_UPDATE_INFO_H_

#include <QMessageBox>
#include <QThreadStorage>
#include <QScopedArrayPointer>

#include <KoColorSpace.h>
#include "kis_image.h"
#include "kis_paint_device.h"
#include "kis_config.h"
#include <KoColorConversionTransformation.h>
#include <KoChannelInfo.h>
#include <kis_lod_transform.h>


class KisTextureTileUpdateInfo;
typedef QSharedPointer<KisTextureTileUpdateInfo> KisTextureTileUpdateInfoSP;
typedef QVector<KisTextureTileUpdateInfoSP> KisTextureTileUpdateInfoSPList;


class ConversionCache {
public:
    class Buffer {
    public:
        Buffer () : m_size(0) {}

        inline void swap(Buffer &rhs) {
            m_data.swap(rhs.m_data);
            qSwap(m_size, rhs.m_size);
        }

        inline quint8* data() const {
            return m_data.data();
        }

        inline void ensureNotSmaller(int size) {
            if (size > m_size) {
                try {
                    m_data.reset(new quint8[size]);
                    m_size = size;
                }
                catch (std::bad_alloc) {
                    QMessageBox::critical(0,
                                          i18nc("@title:window", "Fatal Error"),
                                          i18n("Krita has run out of memory and has to close."));
                    qFatal("KisTextureTileUpdateInfo: Could not allocate enough memory");
                }
            }
        }

    private:
        QScopedArrayPointer<quint8> m_data;
        int m_size;
    };

public:
    inline void swap(Buffer &rhs) {
        m_cache.localData()->swap(rhs);
    }

    inline quint8* data() const {
        return m_cache.localData()->data();
    }

    inline void ensureNotSmaller(int size) {
        if (!m_cache.hasLocalData()) {
            m_cache.setLocalData(new Buffer());
        }
        m_cache.localData()->ensureNotSmaller(size);
    }

private:
    QThreadStorage<Buffer*> m_cache;
};

class KisTextureTileUpdateInfo
{
public:
    KisTextureTileUpdateInfo()
        : m_patchPixelsLength(0)
    {
    }

    KisTextureTileUpdateInfo(qint32 col, qint32 row,
                             const QRect &tileRect, const QRect &updateRect, const QRect &currentImageRect,
                             int levelOfDetail)
        : m_patchPixelsLength(0)
    {
        m_tileCol = col;
        m_tileRow = row;
        m_tileRect = tileRect;
        m_originalTileRect = m_tileRect;
        m_patchRect = m_tileRect & updateRect;
        m_originalPatchRect = m_patchRect;
        m_currentImageRect = currentImageRect;

        m_patchLevelOfDetail = levelOfDetail;

        if (m_patchLevelOfDetail) {
            m_originalPatchRect = KisLodTransform::alignedRect(m_originalPatchRect, m_patchLevelOfDetail);
            m_patchRect = KisLodTransform::scaledRect(m_originalPatchRect, m_patchLevelOfDetail);
            m_tileRect = KisLodTransform::scaledRect(m_originalTileRect, m_patchLevelOfDetail);
        }
    }

    ~KisTextureTileUpdateInfo() {
        if (m_patchPixels.data()) {
            m_patchPixelsCache.swap(m_patchPixels);
        }
    }

    void retrieveData(KisImageWSP image, const QBitArray &channelFlags, bool onlyOneChannelSelected, int selectedChannelIndex)
    {
        m_patchColorSpace = image->projection()->colorSpace();

        m_patchPixelsLength = m_patchColorSpace->pixelSize() * m_patchRect.width() * m_patchRect.height();
        m_patchPixelsCache.ensureNotSmaller(m_patchPixelsLength);
        m_patchPixelsCache.swap(m_patchPixels);

        image->projection()->readBytes(m_patchPixels.data(),
                                       m_patchRect.x(), m_patchRect.y(),
                                       m_patchRect.width(), m_patchRect.height());

        // XXX: if the paint colorspace is rgb, we should do the channel swizzling in
        //      the display shader
        if (!channelFlags.isEmpty()) {
            m_conversionCache.ensureNotSmaller(m_patchPixelsLength);

            QList<KoChannelInfo*> channelInfo = m_patchColorSpace->channels();
            int channelSize = channelInfo[selectedChannelIndex]->size();
            int pixelSize = m_patchColorSpace->pixelSize();
            quint32 numPixels = m_patchRect.width() * m_patchRect.height();

            KisConfig cfg;

            if (onlyOneChannelSelected && !cfg.showSingleChannelAsColor()) {
                int selectedChannelPos = channelInfo[selectedChannelIndex]->pos();
                for (uint pixelIndex = 0; pixelIndex < numPixels; ++pixelIndex) {
                    for (uint channelIndex = 0; channelIndex < m_patchColorSpace->channelCount(); ++channelIndex) {

                        if (channelInfo[channelIndex]->channelType() == KoChannelInfo::COLOR) {
                            memcpy(m_conversionCache.data() + (pixelIndex * pixelSize) + (channelIndex * channelSize),
                                   m_patchPixels.data() + (pixelIndex * pixelSize) + selectedChannelPos,
                                   channelSize);
                        }
                        else if (channelInfo[channelIndex]->channelType() == KoChannelInfo::ALPHA) {
                            memcpy(m_conversionCache.data() + (pixelIndex * pixelSize) + (channelIndex * channelSize),
                                   m_patchPixels.data() + (pixelIndex * pixelSize) + (channelIndex * channelSize),
                                   channelSize);
                        }
                    }
                }
            }
            else {
                for (uint pixelIndex = 0; pixelIndex < numPixels; ++pixelIndex) {
                    for (uint channelIndex = 0; channelIndex < m_patchColorSpace->channelCount(); ++channelIndex) {
                        if (channelFlags.testBit(channelIndex)) {
                            memcpy(m_conversionCache.data() + (pixelIndex * pixelSize) + (channelIndex * channelSize),
                                   m_patchPixels.data() + (pixelIndex * pixelSize) + (channelIndex * channelSize),
                                   channelSize);
                        }
                        else {
                            memset(m_conversionCache.data() + (pixelIndex * pixelSize) + (channelIndex * channelSize), 0, channelSize);
                        }
                    }
                }

            }

            m_conversionCache.swap(m_patchPixels);
        }

    }

    void convertTo(const KoColorSpace* dstCS,
                   KoColorConversionTransformation::Intent renderingIntent,
                   KoColorConversionTransformation::ConversionFlags conversionFlags)
    {
        if (dstCS == m_patchColorSpace && conversionFlags == KoColorConversionTransformation::Empty) return;

        if (m_patchRect.isValid()) {
            const qint32 numPixels = m_patchRect.width() * m_patchRect.height();
            const quint32 conversionCacheLength = numPixels * dstCS->pixelSize();

            m_conversionCache.ensureNotSmaller(conversionCacheLength);
            m_patchColorSpace->convertPixelsTo(m_patchPixels.data(), m_conversionCache.data(), dstCS, numPixels, renderingIntent, conversionFlags);

            m_patchColorSpace = dstCS;
            m_conversionCache.swap(m_patchPixels);
            m_patchPixelsLength = conversionCacheLength;
        }
    }

    inline quint8* data() const {
        return m_patchPixels.data();
    }

    inline int patchLevelOfDetail() const {
        return m_patchLevelOfDetail;
    }

    inline QPoint realPatchOffset() const {
        return QPoint(m_patchRect.x() - m_tileRect.x(),
                      m_patchRect.y() - m_tileRect.y());
    }

    inline QSize realPatchSize() const {
        return m_patchRect.size();
    }

    inline QSize realTileSize() const {
        return m_tileRect.size();
    }

    inline bool isTopmost() const {
        return m_originalPatchRect.top() == m_currentImageRect.top();
    }

    inline bool isLeftmost() const {
        return m_originalPatchRect.left() == m_currentImageRect.left();
    }

    inline bool isRightmost() const {
        return m_originalPatchRect.right() == m_currentImageRect.right();
    }

    inline bool isBottommost() const {
        return m_originalPatchRect.bottom() == m_currentImageRect.bottom();
    }

    inline bool isEntireTileUpdated() const {
        return m_patchRect == m_tileRect;
    }

    inline qint32 tileCol() const {
        return m_tileCol;
    }

    inline qint32 tileRow() const {
        return m_tileRow;
    }

    inline quint32 pixelSize() const {
        return m_patchColorSpace->pixelSize();
    }

    inline quint32 patchPixelsLength() const {
        return m_patchPixelsLength;
    }

    inline bool valid() const {
        return m_patchRect.isValid();
    }

private:
    Q_DISABLE_COPY(KisTextureTileUpdateInfo)

private:
    qint32 m_tileCol;
    qint32 m_tileRow;
    QRect m_currentImageRect;
    QRect m_tileRect;
    QRect m_patchRect;
    const KoColorSpace* m_patchColorSpace;
    quint32 m_patchPixelsLength;

    QRect m_realPatchRect;
    QRect m_realPatchOffset;
    QRect m_realTileSize;
    int m_patchLevelOfDetail;

    QRect m_originalPatchRect;
    QRect m_originalTileRect;

    ConversionCache::Buffer m_patchPixels;
    static ConversionCache m_patchPixelsCache;
    static ConversionCache m_conversionCache;
};

#endif /* KIS_TEXTURE_TILE_UPDATE_INFO_H_ */

