/*
 *  Copyright (c) 2015 Jouni Pentikäinen <joupent@gmail.com>
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

#ifndef KIS_KEYFRAME_H
#define KIS_KEYFRAME_H

#include <qglobal.h>
#include <qmetatype.h>
#include <QScopedPointer>

#include "kritaimage_export.h"

class KisKeyframeChannel;

class KRITAIMAGE_EXPORT KisKeyframe
{
public:
    KisKeyframe(KisKeyframeChannel *channel, int time, void *data);
    KisKeyframe(KisKeyframeChannel *channel, int time, quint32 value);
    ~KisKeyframe();

    quint32 value() const;
    void *data() const;

    void setValue(quint32 value);
    int time() const;
    void setTime(int time);
    KisKeyframeChannel *channel() const;

private:
    struct Private;
    QScopedPointer<Private> m_d;
};

Q_DECLARE_METATYPE(KisKeyframe*)
#endif
