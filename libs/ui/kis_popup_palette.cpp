/* This file is part of the KDE project
   Copyright 2009 Vera Lukman <shicmap@gmail.com>
   Copyright 2011 Sven Langkamp <sven.langkamp@gmail.com>

   This library is free software; you can redistribute it and/or
   modify it under the terms of the GNU Library General Public
   License version 2 as published by the Free Software Foundation.

   This library is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   Library General Public License for more details.

   You should have received a copy of the GNU Library General Public License
   along with this library; see the file COPYING.LIB.  If not, write to
   the Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
   Boston, MA 02110-1301, USA.

*/

#include "kis_config.h"
#include "kis_popup_palette.h"
#include "kis_paintop_box.h"
#include "kis_favorite_resource_manager.h"
#include "kis_icon_utils.h"
#include <brushengine/kis_paintop_preset.h>
#include "kis_resource_server_provider.h"
#include <KoTriangleColorSelector.h>
#include "KoColorSpaceRegistry.h"
#include <kis_types.h>
#include <QtGui>
#include <kis_debug.h>
#include <QQueue>
#include <QMenu>
#include <QWhatsThis>
#include <QDebug>
#include <math.h>
#include "kis_signal_compressor.h"
#include <QApplication>


#define colorInnerRadius 72.0
#define colorOuterRadius 92.0
#define maxbrushRadius 42.0
#define widgetSize (colorOuterRadius*2+maxbrushRadius*4)

class PopupColorTriangle : public KoTriangleColorSelector
{
public:
    PopupColorTriangle(const KoColorDisplayRendererInterface *displayRenderer, QWidget* parent)
        : KoTriangleColorSelector(displayRenderer, parent)
        , m_dragging(false) {
    }

    virtual ~PopupColorTriangle() {}

    void tabletEvent(QTabletEvent* event) {
        event->accept();
        QMouseEvent* mouseEvent = 0;
        switch (event->type()) {
        case QEvent::TabletPress:
            mouseEvent = new QMouseEvent(QEvent::MouseButtonPress, event->pos(),
                                         Qt::LeftButton, Qt::LeftButton, event->modifiers());
            m_dragging = true;
            mousePressEvent(mouseEvent);
            break;
        case QEvent::TabletMove:
            mouseEvent = new QMouseEvent(QEvent::MouseMove, event->pos(),
                                         (m_dragging) ? Qt::LeftButton : Qt::NoButton,
                                         (m_dragging) ? Qt::LeftButton : Qt::NoButton, event->modifiers());
            mouseMoveEvent(mouseEvent);
            break;
        case QEvent::TabletRelease:
            mouseEvent = new QMouseEvent(QEvent::MouseButtonRelease, event->pos(),
                                         Qt::LeftButton,
                                         Qt::LeftButton,
                                         event->modifiers());
            m_dragging = false;
            mouseReleaseEvent(mouseEvent);
            break;
        default: break;
        }
        delete mouseEvent;
    }

private:
    bool m_dragging;
};

KisPopupPalette::KisPopupPalette(KisFavoriteResourceManager* manager, const KoColorDisplayRendererInterface *displayRenderer, QWidget *parent)
    : QWidget(parent, Qt::FramelessWindowHint)
    , m_resourceManager(manager)
    , m_triangleColorSelector(0)
    , m_timer(0)
    , m_displayRenderer(displayRenderer)
    , m_colorChangeCompressor(new KisSignalCompressor(50, KisSignalCompressor::POSTPONE))
{
    
    const int borderWidth = 3;
    m_triangleColorSelector  = new PopupColorTriangle(displayRenderer, this);
    m_triangleColorSelector->move(widgetSize/2-colorInnerRadius+borderWidth, widgetSize/2-colorInnerRadius+borderWidth);
    m_triangleColorSelector->resize(colorInnerRadius*2-borderWidth*2, colorInnerRadius*2-borderWidth*2);
    m_triangleColorSelector->setVisible(true);

    QRegion maskedRegion(0, 0, m_triangleColorSelector->width(), m_triangleColorSelector->height(), QRegion::Ellipse );
    m_triangleColorSelector->setMask(maskedRegion);

    setAttribute(Qt::WA_ContentsPropagated, true);
    //setAttribute(Qt::WA_TranslucentBackground, true);

    connect(m_triangleColorSelector, SIGNAL(realColorChanged(KoColor)),
            m_colorChangeCompressor.data(), SLOT(start()));
    connect(m_colorChangeCompressor.data(), SIGNAL(timeout()),
            SLOT(slotEmitColorChanged()));

    connect(m_resourceManager, SIGNAL(sigChangeFGColorSelector(KoColor)),
            SLOT(slotExternalFgColorChanged(KoColor)));
    connect(this, SIGNAL(sigChangefGColor(KoColor)),
            m_resourceManager, SIGNAL(sigSetFGColor(KoColor)));


    connect(this, SIGNAL(sigChangeActivePaintop(int)), m_resourceManager, SLOT(slotChangeActivePaintop(int)));
    connect(this, SIGNAL(sigUpdateRecentColor(int)), m_resourceManager, SLOT(slotUpdateRecentColor(int)));

    connect(m_resourceManager, SIGNAL(setSelectedColor(int)), SLOT(slotSetSelectedColor(int)));
    connect(m_resourceManager, SIGNAL(updatePalettes()), SLOT(slotUpdate()));
    connect(m_resourceManager, SIGNAL(hidePalettes()), SLOT(slotHide()));

    // This is used to handle a bug:
    // If pop up palette is visible and a new colour is selected, the new colour
    // will be added when the user clicks on the canvas to hide the palette
    // In general, we want to be able to store recent color if the pop up palette
    // is not visible
    m_timer = new QTimer(this);
    m_timer->setSingleShot(true);
    connect(this, SIGNAL(sigTriggerTimer()), this, SLOT(slotTriggerTimer()));
    connect(m_timer, SIGNAL(timeout()), this, SLOT(slotEnableChangeFGColor()));
    connect(this, SIGNAL(sigEnableChangeFGColor(bool)), m_resourceManager, SIGNAL(sigEnableChangeColor(bool)));

    setCursor(Qt::ArrowCursor);
    setMouseTracking(true);
    setHoveredPreset(-1);
    setHoveredColor(-1);
    setSelectedColor(-1);

    setVisible(true);
    setVisible(false);
}

void KisPopupPalette::slotExternalFgColorChanged(const KoColor &color)
{
    m_triangleColorSelector->setRealColor(color);
}

void KisPopupPalette::slotEmitColorChanged()
{
    if (isVisible()) {
        update();
        emit sigChangefGColor(m_triangleColorSelector->realColor());
    }
}

//setting KisPopupPalette properties
int KisPopupPalette::hoveredPreset() const
{
    return m_hoveredPreset;
}

void KisPopupPalette::setHoveredPreset(int x)
{
    m_hoveredPreset = x;
}

int KisPopupPalette::hoveredColor() const
{
    return m_hoveredColor;
}

void KisPopupPalette::setHoveredColor(int x)
{
    m_hoveredColor = x;
}

int KisPopupPalette::selectedColor() const
{
    return m_selectedColor;
}

void KisPopupPalette::setSelectedColor(int x)
{
    m_selectedColor = x;
}

void KisPopupPalette::slotTriggerTimer()
{
    m_timer->start(750);
}

void KisPopupPalette::slotEnableChangeFGColor()
{
    emit sigEnableChangeFGColor(true);
}

void KisPopupPalette::showPopupPalette(const QPoint &p)
{
    if (!isVisible() && parentWidget())  {
        QSize parentSize(parentWidget()->size());
        QPoint pointPalette(p.x() - width() / 2, p.y() - height() / 2);

        //setting offset point in case the widget is shown outside of canvas region
        int offsetX = 0, offsetY = 0;

        if ((offsetX = pointPalette.x() + width() - parentSize.width()) > 0 || (offsetX = pointPalette.x()) < 0) {
            pointPalette.setX(pointPalette.x() - offsetX);
        }

        if ((offsetY = pointPalette.y() + height() - parentSize.height()) > 0 || (offsetY = pointPalette.y()) < 0) {
            pointPalette.setY(pointPalette.y() - offsetY);
        }

        move(pointPalette);

    }
    showPopupPalette(!isVisible());

}

void KisPopupPalette::showPopupPalette(bool show)
{
    if (show) {
        emit sigEnableChangeFGColor(!show);
    } else {
        emit sigTriggerTimer();
    }
    setVisible(show);
}

//redefinition of setVariable function to change the scope to private
void KisPopupPalette::setVisible(bool b)
{
    QWidget::setVisible(b);
}

QSize KisPopupPalette::sizeHint() const
{
    return QSize(widgetSize, widgetSize);
}

void KisPopupPalette::resizeEvent(QResizeEvent*)
{
}

void KisPopupPalette::paintEvent(QPaintEvent* e)
{
    Q_UNUSED(e)

    float rotationAngle = 0.0;

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.translate(width() / 2, height() / 2);

    //painting background color
    QPainterPath bgColor;
    bgColor.addEllipse(QPoint(-width() / 2 + 24, -height() / 2 + 60), 20, 20);
    painter.fillPath(bgColor, m_displayRenderer->toQColor(m_resourceManager->bgColor()));
    painter.drawPath(bgColor);

    //painting foreground color
    QPainterPath fgColor;
    fgColor.addEllipse(QPoint(-width() / 2 + 50, -height() / 2 + 32), 30, 30);
    painter.fillPath(fgColor, m_displayRenderer->toQColor(m_triangleColorSelector->realColor()));
    painter.drawPath(fgColor);


    // create an ellipse for the background that is slightly
    // smaller than the clipping mask. This will prevent aliasing
    QPainterPath backgroundContainer;
    backgroundContainer.addEllipse( -colorOuterRadius, -colorOuterRadius,
                                    colorOuterRadius*2, colorOuterRadius*2 );
    painter.fillPath(backgroundContainer,palette().brush(QPalette::Window));
    painter.drawPath(backgroundContainer);





    //painting favorite brushes
    QList<QImage> images(m_resourceManager->favoritePresetImages());

    //painting favorite brushes pixmap/icon
    QPainterPath path;
    for (int pos = 0; pos < numSlots(); pos++) {
        painter.save();

        path = pathFromPresetIndex(pos);
        
        if(pos < images.size())
        {
            painter.setClipPath(path);

            QRect bounds = path.boundingRect().toAlignedRect();
            painter.drawImage(bounds.topLeft() , images.at(pos).scaled(bounds.size() , Qt::KeepAspectRatioByExpanding));
        }
        else {
            painter.fillPath(path, palette().brush(QPalette::Window));     
        }
        QPen pen = painter.pen();
        pen.setWidth(3);
        painter.setPen(pen);
        painter.drawPath(path);
        
        painter.restore();
    }
    if (hoveredPreset() > -1) {
        path = pathFromPresetIndex(hoveredPreset());
        QPen pen(palette().color(QPalette::Highlight));
        pen.setWidth(3);
        painter.setPen(pen);
        painter.drawPath(path);
    }

    //painting recent colors
    painter.setPen(Qt::NoPen);
    rotationAngle = -360.0 / m_resourceManager->recentColorsTotal();

    KoColor kocolor;

    for (int pos = 0; pos < m_resourceManager->recentColorsTotal(); pos++) {
        QPainterPath path(drawDonutPathAngle(colorInnerRadius, colorOuterRadius, m_resourceManager->recentColorsTotal()));

        //accessing recent color of index pos
        kocolor = m_resourceManager->recentColorAt(pos);
        painter.fillPath(path, m_displayRenderer->toQColor(kocolor));

        painter.drawPath(path);
        painter.rotate(rotationAngle);
    }

    painter.setBrush(Qt::transparent);

    if (m_resourceManager->recentColorsTotal() == 0) {
        QPainterPath path_ColorDonut(drawDonutPathFull(0, 0, colorInnerRadius, colorOuterRadius));
        painter.setPen(QPen(palette().color(QPalette::Window).darker(130), 1, Qt::SolidLine, Qt::FlatCap, Qt::MiterJoin));
        painter.drawPath(path_ColorDonut);
    }

    //painting hovered color
    if (hoveredColor() > -1) {
        painter.setPen(QPen(palette().color(QPalette::Highlight), 2, Qt::SolidLine, Qt::FlatCap, Qt::MiterJoin));

        if (m_resourceManager->recentColorsTotal() == 1) {
            QPainterPath path_ColorDonut(drawDonutPathFull(0, 0, colorInnerRadius, colorOuterRadius));
            painter.drawPath(path_ColorDonut);
        } else {
            painter.rotate((m_resourceManager->recentColorsTotal() + hoveredColor()) *rotationAngle);
            QPainterPath path(drawDonutPathAngle(colorInnerRadius, colorOuterRadius, m_resourceManager->recentColorsTotal()));
            painter.drawPath(path);
            painter.rotate(hoveredColor() * -1 * rotationAngle);
        }
    }

    //painting selected color
    if (selectedColor() > -1) {
        painter.setPen(QPen(palette().color(QPalette::Highlight).darker(130), 2, Qt::SolidLine, Qt::FlatCap, Qt::MiterJoin));

        if (m_resourceManager->recentColorsTotal() == 1) {
            QPainterPath path_ColorDonut(drawDonutPathFull(0, 0, colorInnerRadius, colorOuterRadius));
            painter.drawPath(path_ColorDonut);
        } else {
            painter.rotate((m_resourceManager->recentColorsTotal() + selectedColor()) *rotationAngle);
            QPainterPath path(drawDonutPathAngle(colorInnerRadius, colorOuterRadius, m_resourceManager->recentColorsTotal()));
            painter.drawPath(path);
            painter.rotate(selectedColor() * -1 * rotationAngle);
        }
    }


    //painting configure background, then icon
    QPainterPath configureContainer;
    int side = qMin(width(), height());


    configureContainer.addEllipse( side / 2 - 38 , side / 2 - 38 , 35 , 35 );
    painter.fillPath(configureContainer,palette().brush(QPalette::Window));
    painter.drawPath(configureContainer);


    QPixmap settingIcon = KisIconUtils::loadIcon("configure").pixmap(QSize(22,22));
    painter.drawPixmap(side / 2 - 40 + 9, side / 2 - 40 + 9, settingIcon);
    
}

QPainterPath KisPopupPalette::drawDonutPathFull(int x, int y, int inner_radius, int outer_radius)
{
    QPainterPath path;
    path.addEllipse(QPointF(x, y), outer_radius, outer_radius);
    path.addEllipse(QPointF(x, y), inner_radius, inner_radius);
    path.setFillRule(Qt::OddEvenFill);

    return path;
}

QPainterPath KisPopupPalette::drawDonutPathAngle(int inner_radius, int outer_radius, int limit)
{
    QPainterPath path;
    path.moveTo(-0.999 * outer_radius * sin(M_PI / limit), 0.999 * outer_radius * cos(M_PI / limit));
    path.arcTo(-1 * outer_radius, -1 * outer_radius, 2 * outer_radius, 2 * outer_radius, -90.0 - 180.0 / limit,
               360.0 / limit);
    path.arcTo(-1 * inner_radius, -1 * inner_radius, 2 * inner_radius, 2 * inner_radius, -90.0 + 180.0 / limit,
               - 360.0 / limit);
    path.closeSubpath();

    return path;
}

void KisPopupPalette::mouseMoveEvent(QMouseEvent* event)
{
    QPointF point = event->posF();
    event->accept();

    QPainterPath pathColor(drawDonutPathFull(width() / 2, height() / 2, colorInnerRadius, colorOuterRadius));

    setToolTip("");
    setHoveredPreset(-1);
    setHoveredColor(-1);

    {
        int pos = calculatePresetIndex(point, m_resourceManager->numFavoritePresets());

        if (pos >= 0 && pos < m_resourceManager->numFavoritePresets()) {
            setToolTip(m_resourceManager->favoritePresetList().at(pos).data()->name());
            setHoveredPreset(pos);
        }
    }
    if (pathColor.contains(point)) {
        int pos = calculateIndex(point, m_resourceManager->recentColorsTotal());

        if (pos >= 0 && pos < m_resourceManager->recentColorsTotal()) {
            setHoveredColor(pos);
        }
    }
    update();
}

void KisPopupPalette::mousePressEvent(QMouseEvent* event)
{
    QPointF point = event->posF();
    event->accept();

    if (event->button() == Qt::LeftButton) {

        //in favorite brushes area
        int pos = calculateIndex(point, m_resourceManager->numFavoritePresets());
        if (pos >= 0 && pos < m_resourceManager->numFavoritePresets()
                && isPointInPixmap(point, pos)) {
            //setSelectedBrush(pos);
            update();
        }

        int side = qMin(width(), height());
        QPainterPath settingCircle;
        settingCircle.addEllipse(width() / 2 + side / 2 - 40, height() / 2 + side / 2 - 40, 40, 40);
        if (settingCircle.contains(point)) {                
            KisPaintOpPresetResourceServer* rServer = KisResourceServerProvider::instance()->paintOpPresetServer();
            QStringList tags = rServer->tagNamesList();
            qSort(tags);

            if (!tags.isEmpty()) {
                QMenu menu;
                Q_FOREACH (const QString& tag, tags) {
                    menu.addAction(tag);
                }
                QAction* action = menu.exec(event->globalPos());
                if (action) {
                    m_resourceManager->setCurrentTag(action->text());
                }
            } else {
                QWhatsThis::showText(event->globalPos(), 
                                        i18n("There are no tags available to show in this popup. To add presets, you need to tag them and then select the tag here."));
            }
        }
    }
}



void KisPopupPalette::tabletEvent(QTabletEvent* /*event*/) {
}


void KisPopupPalette::mouseReleaseEvent(QMouseEvent * event)
{
    QPointF point = event->posF();
    event->accept();

    if (event->button() == Qt::LeftButton || event->button() == Qt::RightButton) {
        QPainterPath pathColor(drawDonutPathFull(width() / 2, height() / 2, colorInnerRadius, colorOuterRadius));

        //in favorite brushes area
        if (hoveredPreset() > -1) {
            //setSelectedBrush(hoveredBrush());
            emit sigChangeActivePaintop(hoveredPreset());
        }
        if (pathColor.contains(point)) {
            int pos = calculateIndex(point, m_resourceManager->recentColorsTotal());

            if (pos >= 0 && pos < m_resourceManager->recentColorsTotal()) {
                emit sigUpdateRecentColor(pos);
            }
        }
    }
}

int KisPopupPalette::calculateIndex(QPointF point, int n)
{
    calculatePresetIndex(point, n);
    //translate to (0,0)
    point.setX(point.x() - width() / 2);
    point.setY(point.y() - height() / 2);

    //rotate
    float smallerAngle = M_PI / 2 + M_PI / n - atan2(point.y(), point.x());
    float radius = sqrt((float)point.x() * point.x() + point.y() * point.y());
    point.setX(radius * cos(smallerAngle));
    point.setY(radius * sin(smallerAngle));

    //calculate brush index
    int pos = floor(acos(point.x() / radius) * n / (2 * M_PI));
    if (point.y() < 0) pos =  n - pos - 1;

    return pos;
}

bool KisPopupPalette::isPointInPixmap(QPointF& point, int pos)
{
    if (pathFromPresetIndex(pos).contains(point + QPointF(-width() / 2, -height() / 2))) {
        return true;
    }
    return false;
}

KisPopupPalette::~KisPopupPalette()
{
}

QPainterPath KisPopupPalette::pathFromPresetIndex(int index)
{
    qreal angleLength = 360.0 / numSlots() / 180 * M_PI;
    qreal angle = index * angleLength;

    qreal r = colorOuterRadius * sin(angleLength/2) / ( 1 - sin(angleLength/2));
    
    QPainterPath path;
    path.addEllipse((colorOuterRadius+r) * cos(angle)-r, -(colorOuterRadius+r) * sin(angle)-r, 2*r, 2*r);
    path.closeSubpath();
    return path;
}

int KisPopupPalette::calculatePresetIndex(QPointF point, int /*n*/)
{
    for(int i = 0; i < numSlots(); i++)
    {
        QPointF adujustedPoint = point - QPointF(width()/2, height()/2);
        if(pathFromPresetIndex(i).contains(adujustedPoint))
        {
            return i;
        }
    }
    return -1;
}

int KisPopupPalette::numSlots()
{
    KisConfig config;
    return qMax(config.favoritePresets(), 10);
}

