/*
 *  Copyright (C) 2011 Silvio Heinrich <plassy@web.de>
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

#include "kis_colorsmudgeop.h"

#include <cmath>
#include <memory>
#include <QRect>

#include <KoColorSpaceRegistry.h>
#include <KoColor.h>
#include <KoColorProfile.h>
#include <KoCompositeOpRegistry.h>

#include <kis_brush.h>
#include <kis_global.h>
#include <kis_paint_device.h>
#include <kis_painter.h>
#include <kis_image.h>
#include <kis_selection.h>
#include <kis_brush_based_paintop_settings.h>
#include <kis_cross_device_color_picker.h>
#include <kis_fixed_paint_device.h>
#include <kis_lod_transform.h>


KisColorSmudgeOp::KisColorSmudgeOp(const KisBrushBasedPaintOpSettings* settings, KisPainter* painter, KisNodeSP node, KisImageSP image)
    : KisBrushBasedPaintOp(settings, painter)
    , m_firstRun(true)
    , m_image(image)
    , m_tempDev(painter->device()->createCompositionSourceDevice())
    , m_backgroundPainter(new KisPainter(m_tempDev))
    , m_smudgePainter(new KisPainter(m_tempDev))
    , m_colorRatePainter(new KisPainter(m_tempDev))
    , m_smudgeRateOption()
    , m_colorRateOption("ColorRate", KisPaintOpOption::GENERAL, false)
    , m_smudgeRadiusOption()
{
    Q_UNUSED(node);

    Q_ASSERT(settings);
    Q_ASSERT(painter);

    m_sizeOption.readOptionSetting(settings);
    m_opacityOption.readOptionSetting(settings);
    m_spacingOption.readOptionSetting(settings);
    m_smudgeRateOption.readOptionSetting(settings);
    m_colorRateOption.readOptionSetting(settings);
    m_smudgeRadiusOption.readOptionSetting(settings);
    m_overlayModeOption.readOptionSetting(settings);
    m_rotationOption.readOptionSetting(settings);
    m_scatterOption.readOptionSetting(settings);
    m_gradientOption.readOptionSetting(settings);

    m_sizeOption.resetAllSensors();
    m_opacityOption.resetAllSensors();
    m_spacingOption.resetAllSensors();
    m_smudgeRateOption.resetAllSensors();
    m_colorRateOption.resetAllSensors();
    m_smudgeRadiusOption.resetAllSensors();
    m_rotationOption.resetAllSensors();
    m_scatterOption.resetAllSensors();
    m_gradientOption.resetAllSensors();

    m_gradient = painter->gradient();

    m_backgroundPainter->setCompositeOp(COMPOSITE_COPY);
    // Smudge Painter works in default COMPOSITE_OVER mode
    m_colorRatePainter->setCompositeOp(painter->compositeOp()->id());

    m_rotationOption.applyFanCornersInfo(this);

    /**
     * Disable handling of the subpixel precision. In the smudge op we
     * should read from the aligned areas of the image, so having
     * additional internal offsets, created by the subpixel precision,
     * will worsen the quality (at least because
     * QRectF(m_dstDabRect).center() will not point to the real center
     * of the brush anymore).
     */
    m_dabCache->disableSubpixelPrecision();
}

KisColorSmudgeOp::~KisColorSmudgeOp()
{
    delete m_backgroundPainter;
    delete m_colorRatePainter;
    delete m_smudgePainter;
}

void KisColorSmudgeOp::updateMask(const KisPaintInformation& info, double scale, double rotation, const QPointF &cursorPoint)
{
    static const KoColorSpace *cs = KoColorSpaceRegistry::instance()->alpha8();
    static KoColor color(Qt::black, cs);

    m_maskDab = m_dabCache->fetchDab(cs,
                                     color,
                                     cursorPoint,
                                     scale, scale,
                                     rotation,
                                     info,
                                     1.0,
                                     &m_dstDabRect);

    // sanity check
    KIS_ASSERT_RECOVER_NOOP(m_dstDabRect.size() == m_maskDab->bounds().size());
}

inline void KisColorSmudgeOp::getTopLeftAligned(const QPointF &pos, const QPointF &hotSpot, qint32 *x, qint32 *y)
{
    QPointF topLeft = pos - hotSpot;

    qreal xFraction, yFraction; // will not be used
    splitCoordinate(topLeft.x(), x, &xFraction);
    splitCoordinate(topLeft.y(), y, &yFraction);
}

KisSpacingInformation KisColorSmudgeOp::paintAt(const KisPaintInformation& info)
{
    KisBrushSP brush = m_brush;

    // Simple error catching
    if (!painter()->device() || !brush || !brush->canPaintFor(info)) {
        return KisSpacingInformation(1.0);
    }
    
    //if precision
    KoColor colorSpaceChanger = painter()->paintColor();
    const KoColorSpace* preciseColorSpace = colorSpaceChanger.colorSpace();
    /*if (colorSpaceChanger.colorSpace()->colorDepthId().id() == "U8") {
	preciseColorSpace = KoColorSpaceRegistry::instance()->colorSpace(colorSpaceChanger.colorSpace()->colorModelId().id(), "U16", colorSpaceChanger.profile() );
        colorSpaceChanger.convertTo(preciseColorSpace);
    }
    painter()->setPaintColor(colorSpaceChanger);*/

    // get the scaling factor calculated by the size option
    qreal scale    = m_sizeOption.apply(info);
    scale *= KisLodTransform::lodToScale(painter()->device());
    qreal rotation = m_rotationOption.apply(info);

    if (checkSizeTooSmall(scale)) return KisSpacingInformation();

    setCurrentScale(scale);
    setCurrentRotation(rotation);

    QPointF scatteredPos =
        m_scatterOption.apply(info,
                              brush->maskWidth(scale, rotation, 0, 0, info),
                              brush->maskHeight(scale, rotation, 0, 0, info));

    QPointF hotSpot = brush->hotSpot(scale, scale, rotation, info);

    /**
     * Update the brush mask.
     *
     * Upon leaving the function:
     *   o m_maskDab stores the new mask
     *   o m_maskBounds stores the extents of the mask paint device
     *   o m_dstDabRect stores the destination rect where the mask is going
     *     to be written to
     */
    updateMask(info, scale, rotation, scatteredPos);

    QPointF newCenterPos = QRectF(m_dstDabRect).center();
    /**
     * Save the center of the current dab to know where to read the
     * data during the next pass. We do not save scatteredPos here,
     * because it may differ slightly from the real center of the
     * brush (due to rounding effects), which will result in a
     * really weird quality.
     */
    QRect srcDabRect = m_dstDabRect.translated((m_lastPaintPos - newCenterPos).toPoint());

    m_lastPaintPos = newCenterPos;

    KisSpacingInformation spacingInfo =
        effectiveSpacing(scale, rotation,
                         m_spacingOption, info);

    if (m_firstRun) {
        m_firstRun = false;
        return spacingInfo;
    }

    // save the old opacity value and composite mode
    quint8  oldOpacity = painter()->opacity();
    QString oldCompositeOpId = painter()->compositeOp()->id();
    qreal   fpOpacity  = (qreal(oldOpacity) / 255.0) * m_opacityOption.getOpacityf(info);

    if (m_image && m_overlayModeOption.isChecked()) {
        m_image->blockUpdates();
        m_backgroundPainter->bitBlt(QPoint(), m_image->projection(), srcDabRect);
        m_image->unblockUpdates();
    }
    else {
        // IMPORTANT: clear the temporary painting device to color black with zero opacity:
        //            it will only clear the extents of the brush.
        m_tempDev->clear(QRect(QPoint(), m_dstDabRect.size()));
    }

    if (m_smudgeRateOption.getMode() == KisSmudgeOption::SMEARING_MODE) {
        m_smudgePainter->bitBlt(QPoint(), painter()->device(), srcDabRect);
    } else if (m_smudgeRateOption.getMode() == KisSmudgeOption::DULLING_MODE) {
        QPoint pt = (srcDabRect.topLeft() + hotSpot).toPoint();

        if (m_smudgeRadiusOption.isChecked()) {
            qreal effectiveSize = 0.5 * (m_dstDabRect.width() + m_dstDabRect.height());
            m_smudgeRadiusOption.apply(*m_smudgePainter, info, effectiveSize, pt.x(), pt.y(), painter()->device());

            KoColor color2 = m_smudgePainter->paintColor();
            m_smudgePainter->fill(0, 0, m_dstDabRect.width(), m_dstDabRect.height(), color2);

        } else {
            KoColor color = painter()->paintColor();

            // get the pixel on the canvas that lies beneath the hot spot
            // of the dab and fill  the temporary paint device with that color

            KisCrossDeviceColorPickerInt colorPicker(painter()->device(), color);
            colorPicker.pickColor(pt.x(), pt.y(), color.data());

            m_smudgePainter->fill(0, 0, m_dstDabRect.width(), m_dstDabRect.height(), color);
        }
    } else if (m_smudgeRateOption.getMode() == KisSmudgeOption::YUV_MODE){
        QPoint pt = (srcDabRect.topLeft() + hotSpot).toPoint();
        KoColor smudgeColor;
        if (m_smudgeRadiusOption.isChecked()) {
            qreal effectiveSize = 0.5 * (m_dstDabRect.width() + m_dstDabRect.height());
            m_smudgeRadiusOption.apply(*m_smudgePainter, info, effectiveSize, pt.x(), pt.y(), painter()->device());

            smudgeColor = m_smudgePainter->paintColor();
        } else {
            smudgeColor = painter()->paintColor();
            // get the pixel on the canvas that lies beneath the hot spot
            // of the dab and fill  the temporary paint device with that color

            KisCrossDeviceColorPickerInt colorPicker(painter()->device(), smudgeColor);
            colorPicker.pickColor(pt.x(), pt.y(), smudgeColor.data());
        }
            KoColor paintColor = painter()->paintColor();
            m_gradientOption.apply(paintColor, m_gradient, info);
            
            if (paintColor.colorSpace()->colorModelId().id() == "CMYKA" || colorSpaceChanger.colorSpace()->colorModelId().id() == "XYZA") {
                preciseColorSpace = KoColorSpaceRegistry::instance()->lab16();
                paintColor.convertTo(preciseColorSpace);
            }
            smudgeColor.convertTo(paintColor.colorSpace());
            
            int channelnumber = abs(paintColor.colorSpace()->channelCount());
            QVector <double> channelValues(channelnumber);
            QVector <float> channelValuesF(channelnumber);
            paintColor.colorSpace()->normalisedChannelsValue(smudgeColor.data(), channelValuesF);
            for (int i=0;i<channelnumber;i++){
                channelValues[i]=channelValuesF[i];
            }
            paintColor.colorSpace()->profile()->LinearizeFloatValue(channelValues);
            qreal Sy, Su, Sv = 0.0;
            paintColor.colorSpace()->toYUV(channelValues, &Sy, &Su, &Sv);
            smudgeColor.colorSpace()->normalisedChannelsValue(paintColor.data(), channelValuesF);
            for (int i=0;i<channelnumber;i++){
                channelValues[i]=channelValuesF[i];
            }
            paintColor.colorSpace()->profile()->LinearizeFloatValue(channelValues);
            qreal Py, Pu, Pv = 0.0;
            paintColor.colorSpace()->toYUV(channelValues, &Py, &Pu, &Pv);
            
            qreal maxColorRate = qMax<qreal>(1.0 - m_smudgeRateOption.getRate(), 0.2);
            m_colorRateOption.apply(*m_colorRatePainter, info, 0.0, maxColorRate, fpOpacity);
            
            //now first apply gamma
            if (paintColor.colorSpace()->profile()->hasTRC()){
                Sy = pow(Sy, 1/2.2);
                Py = pow(Py, 1/2.2);
            }
            //then avarage
            qreal Fy, Fu, Fv = 0.0;
            if (smudgeColor.opacityF()>0){
                Fy = ( (Sy*(255-m_colorRatePainter->opacity()) ) + (Py*m_colorRatePainter->opacity()) ) / 255.0;
                Fu = ( (Su*(255-m_colorRatePainter->opacity()) ) + (Pu*m_colorRatePainter->opacity()) ) / 255.0;
                Fv = ( (Sv*(255-m_colorRatePainter->opacity()) ) + (Pv*m_colorRatePainter->opacity()) ) / 255.0;
            } else {
                Fy = Py;
                Fu = Pu;
                Fv = Pv;
            }
            qreal alpha = (smudgeColor.opacityF()+paintColor.opacityF())*0.5;
            //delinearize
            if (paintColor.colorSpace()->profile()->hasTRC()){
                Fy = pow(Fy, 2.2);
            }
            channelValues = paintColor.colorSpace()->fromYUV(&Fy, &Fu, &Fv);
            paintColor.colorSpace()->profile()->DelinearizeFloatValue(channelValues);
            for (int i=0;i<channelnumber;i++){
                channelValuesF[i]=channelValues[i];
                if (paintColor.colorSpace()->colorDepthId().id() == "U16") {
                channelValuesF[i] = qBound(0.0,channelValues[i],1.0);//no clue why but 16bit gives odd results otherwise...
                }
            }
            paintColor.colorSpace()->fromNormalisedChannelsValue(paintColor.data(), channelValuesF);
            paintColor.setOpacity(alpha);

            m_smudgePainter->fill(0, 0, m_dstDabRect.width(), m_dstDabRect.height(), paintColor);
    } else {
            QPoint pt = (srcDabRect.topLeft() + hotSpot).toPoint();
            KoColor smudgeColor;
        if (m_smudgeRadiusOption.isChecked()) {
            qreal effectiveSize = 0.5 * (m_dstDabRect.width() + m_dstDabRect.height());
            m_smudgeRadiusOption.apply(*m_smudgePainter, info, effectiveSize, pt.x(), pt.y(), painter()->device());

            smudgeColor = m_smudgePainter->paintColor();
        } else {
            smudgeColor = painter()->paintColor();
            // get the pixel on the canvas that lies beneath the hot spot
            // of the dab and fill  the temporary paint device with that color

            KisCrossDeviceColorPickerInt colorPicker(painter()->device(), smudgeColor);
            colorPicker.pickColor(pt.x(), pt.y(), smudgeColor.data());
        }
            KoColor paintColor = painter()->paintColor();
            m_gradientOption.apply(paintColor, m_gradient, info);
            
            if (paintColor.colorSpace()->colorModelId().id() == "CMYKA" || colorSpaceChanger.colorSpace()->colorModelId().id() == "XYZA") {
                preciseColorSpace = KoColorSpaceRegistry::instance()->lab16();
                paintColor.convertTo(preciseColorSpace);
            }
            smudgeColor.convertTo(paintColor.colorSpace());
            
            int channelnumber = abs(paintColor.colorSpace()->channelCount());
            QVector <double> channelValues(channelnumber);
            QVector <float> channelValuesF(channelnumber);
            paintColor.colorSpace()->normalisedChannelsValue(smudgeColor.data(), channelValuesF);
            for (int i=0;i<channelnumber;i++){
                channelValues[i]=channelValuesF[i];
            }
            paintColor.colorSpace()->profile()->LinearizeFloatValue(channelValues);
            qreal Sh, Ss, Sy = 0.0;
            paintColor.colorSpace()->toHSY(channelValues, &Sh, &Ss, &Sy);
            smudgeColor.colorSpace()->normalisedChannelsValue(paintColor.data(), channelValuesF);
            for (int i=0;i<channelnumber;i++){
                channelValues[i]=channelValuesF[i];
            }
            paintColor.colorSpace()->profile()->LinearizeFloatValue(channelValues);
            qreal Ph, Ps, Py = 0.0;
            paintColor.colorSpace()->toHSY(channelValues, &Ph, &Ps, &Py);
            
            qreal maxColorRate = qMax<qreal>(1.0 - m_smudgeRateOption.getRate(), 0.2);
            m_colorRateOption.apply(*m_colorRatePainter, info, 0.0, maxColorRate, fpOpacity);
            
            //now first apply gamma
            if (paintColor.colorSpace()->profile()->hasTRC()){
                Sy = pow(Sy, 1/2.2);
                Py = pow(Py, 1/2.2);
            }
            //then avarage
            //hue is a special case. We want <60 and > 240 to behave differently.
            qreal Fh, Fs, Fy;
            
            if (smudgeColor.opacityF()>0){ 
                if (qMin(Ps, Ss) > 0.0){
                    if (qMax(Ph, Sh)-qMin(Ph, Sh)>0.5) {
                        qreal difference = qMax(Ph, Sh)-qMin(Ph, Sh);
                        difference = 1.0-difference;
                        difference = (difference*m_colorRatePainter->opacity()) / 255.0;
                        Fh = qMax(Ph, Sh)+difference;
                        if (Fh>1.0){Fh-=1.0;}
                    } else {
                        Fh = ( (Sh*(255-m_colorRatePainter->opacity()) ) + (Ph*m_colorRatePainter->opacity()) ) / 255.0;
                    }
                } else {
                    if (Ps>0.0) {
                        Fh = Ph;
                    } else {
                        Fh = Sh;
                    }
                }
                Fh = qBound(0.0,Fh,1.0);
                Fs = ( (Ss*(255-m_colorRatePainter->opacity()) ) + (Ps*m_colorRatePainter->opacity()) ) / 255.0;
                Fy = ( (Sy*(255-m_colorRatePainter->opacity()) ) + (Py*m_colorRatePainter->opacity()) ) / 255.0;
            } else {
                Fh = Ph;
                Fs = Ps;
                Fy = Py;
            }
            
            qreal alpha = (smudgeColor.opacityF()+paintColor.opacityF())*0.5;
            //delinearize
            if (paintColor.colorSpace()->profile()->hasTRC()){
                Fy = pow(Fy, 2.2);
            }
            channelValues = paintColor.colorSpace()->fromHSY(&Fh, &Fs, &Fy);
            paintColor.colorSpace()->profile()->DelinearizeFloatValue(channelValues);
            for (int i=0;i<channelnumber;i++){
                channelValuesF[i]=channelValues[i];
                if (paintColor.colorSpace()->colorDepthId().id() == "U16") {
                channelValuesF[i] = qBound(0.0,channelValues[i],1.0);//no clue why but 16bit gives odd results otherwise...
                }
            }
            paintColor.colorSpace()->fromNormalisedChannelsValue(paintColor.data(), channelValuesF);
            paintColor.setOpacity(alpha);

            m_smudgePainter->fill(0, 0, m_dstDabRect.width(), m_dstDabRect.height(), paintColor);
    }

    // if the user selected the color smudge option,
    // we will mix some color into the temporary painting device (m_tempDev)
    if (m_colorRateOption.isChecked() && m_smudgeRateOption.getMode() != KisSmudgeOption::YUV_MODE && m_smudgeRateOption.getMode() != KisSmudgeOption::HSY_MODE) {
        // this will apply the opacity (selected by the user) to copyPainter
        // (but fit the rate inbetween the range 0.0 to (1.0-SmudgeRate))
        qreal maxColorRate = qMax<qreal>(1.0 - m_smudgeRateOption.getRate(), 0.2);
        m_colorRateOption.apply(*m_colorRatePainter, info, 0.0, maxColorRate, fpOpacity);

        // paint a rectangle with the current color (foreground color)
        // or a gradient color (if enabled)
        // into the temporary painting device and use the user selected
        // composite mode
        KoColor color = painter()->paintColor();
        m_gradientOption.apply(color, m_gradient, info);
        m_colorRatePainter->fill(0, 0, m_dstDabRect.width(), m_dstDabRect.height(), color);
    }

    // if color is disabled (only smudge) and "overlay mode" is enabled
    // then first blit the region under the brush from the image projection
    // to the painting device to prevent a rapid build up of alpha value
    // if the color to be smudged is semi transparent.
    if (m_image && m_overlayModeOption.isChecked() && !m_colorRateOption.isChecked()) {
        painter()->setCompositeOp(COMPOSITE_COPY);
        painter()->setOpacity(OPACITY_OPAQUE_U8);
        m_image->blockUpdates();
        painter()->bitBlt(m_dstDabRect.topLeft(), m_image->projection(), m_dstDabRect);
        m_image->unblockUpdates();
    }


    // set opacity calculated by the rate option
    m_smudgeRateOption.apply(*painter(), info, 0.0, 1.0, fpOpacity);

    // then blit the temporary painting device on the canvas at the current brush position
    // the alpha mask (maskDab) will be used here to only blit the pixels that are in the area (shape) of the brush
    if (m_smudgeRateOption.getMode() != KisSmudgeOption::YUV_MODE && m_smudgeRateOption.getMode() != KisSmudgeOption::HSY_MODE) {
        painter()->setCompositeOp(COMPOSITE_COPY);
    }
    painter()->bitBltWithFixedSelection(m_dstDabRect.x(), m_dstDabRect.y(), m_tempDev, m_maskDab, m_dstDabRect.width(), m_dstDabRect.height());
    painter()->renderMirrorMaskSafe(m_dstDabRect, m_tempDev, 0, 0, m_maskDab, !m_dabCache->needSeparateOriginal());

    // restore orginal opacy and composite mode values
    painter()->setOpacity(oldOpacity);
    painter()->setCompositeOp(oldCompositeOpId);

    return spacingInfo;
}
