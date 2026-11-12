// SPDX-License-Identifier: MIT
//
// VstProject implementation
//
//   See VstProject.h for the format description and the why.
#include "VstProject.h"

#include "../imageworker/layers/LayerStack.h"
#include "../imageworker/layers/Layer.h"
#include "../imageworker/layers/LayerMask.h"

#include <QFile>
#include <QFileInfo>
#include <QBuffer>
#include <QDataStream>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPainterPath>
#include <QSaveFile>

#include <algorithm>
#include <cstring>

namespace project {

const char* const kVstMagic = "VSTPROJ\0";

// The header layout (32 bytes):
//   0  .. 7   magic ("VSTPROJ\0")
//   8  ..11   manifestOffset  (uint32, little endian)
//   12 ..15   manifestLength  (uint32)
//   16 ..19   payloadOffset   (uint32)
//   20 ..23   payloadLength   (uint32)
//   24 ..27   magic2          (uint32, the first 4 bytes of the magic)
//   28 ..31   formatVersion   (uint32)
constexpr int kHeaderSize = 32;

namespace {

void putU32(QByteArray& b, quint32 v)
{
    b.append(static_cast<char>(v & 0xFF));
    b.append(static_cast<char>((v >> 8) & 0xFF));
    b.append(static_cast<char>((v >> 16) & 0xFF));
    b.append(static_cast<char>((v >> 24) & 0xFF));
}

quint32 getU32(const QByteArray& b, int offset)
{
    if (offset < 0 || offset + 4 > b.size()) return 0;
    const auto* p = reinterpret_cast<const uchar*>(b.constData() + offset);
    return quint32(p[0]) | (quint32(p[1]) << 8)
         | (quint32(p[2]) << 16) | (quint32(p[3]) << 24);
}

// ===== the bulk encoders =====

// The raw bytes of a cv::Mat, plus the shape needed to rebuild it.
QByteArray encodeMat(const cv::Mat& m)
{
    if (m.empty()) return QByteArray();
    QByteArray out;
    out.reserve(static_cast<int>(m.total() * m.elemSize()));
    for (int y = 0; y < m.rows; ++y) {
        const uchar* row = m.ptr<uchar>(y);
        out.append(reinterpret_cast<const char*>(row),
                   static_cast<int>(m.cols * m.elemSize()));
    }
    return out;
}

cv::Mat decodeMat(const QByteArray& bytes, int rows, int cols, int type)
{
    if (bytes.isEmpty() || rows <= 0 || cols <= 0) return cv::Mat();
    const size_t need = static_cast<size_t>(rows) * cols * CV_ELEM_SIZE(type);
    if (static_cast<size_t>(bytes.size()) < need) return cv::Mat();

    cv::Mat m(rows, cols, type);
    for (int y = 0; y < rows; ++y) {
        uchar* row = m.ptr<uchar>(y);
        memcpy(row, bytes.constData() + static_cast<ptrdiff_t>(y) * cols * CV_ELEM_SIZE(type),
               static_cast<size_t>(cols) * CV_ELEM_SIZE(type));
    }
    return m;
}

// A QPainterPath as a QDataStream byte blob.
//
//   QPainterPath has no public element-wise accessor in Qt 6: the
//   element list, writeSvg() and readSvg() are all private API
//   (QtGui/qpainterpath_p.h), and QPainterPathIterator was removed.
//   The SVG route was tried and dropped for that reason.
//
//   What IS public is the QDataStream operator:
//
//       friend Q_GUI_EXPORT QDataStream &operator<<(QDataStream &,
//                                                    const QPainterPath &);
//       friend Q_GUI_EXPORT QDataStream &operator>>(QDataStream &,
//                                                    QPainterPath &);
//
//   Qt writes the path's element list through it, so it is an exact,
//   lossless round trip -- the control points and the doubles survive
//   as they are. That is better than going through a text format,
//   which would round the coordinates.
//
//   The blob is stored base64 in the manifest so the manifest stays
//   valid JSON.
QByteArray pathToBytes(const QPainterPath& p)
{
    if (p.isEmpty()) return QByteArray();
    QByteArray bytes;
    {
        QDataStream out(&bytes, QIODevice::WriteOnly);
        out.setVersion(QDataStream::Qt_6_0);
        out << p;
    }
    return bytes.toBase64();
}

QPainterPath pathFromBytes(const QByteArray& base64)
{
    if (base64.isEmpty()) return QPainterPath();
    QByteArray bytes = QByteArray::fromBase64(base64);
    if (bytes.isEmpty()) return QPainterPath();
    QDataStream in(&bytes, QIODevice::ReadOnly);
    in.setVersion(QDataStream::Qt_6_0);
    QPainterPath p;
    in >> p;
    return p;
}

QJsonArray colorsToJson(const QVector<QColor>& colors)
{
    QJsonArray arr;
    for (const QColor& c : colors) {
        QJsonObject o;
        o["r"] = c.red();
        o["g"] = c.green();
        o["b"] = c.blue();
        o["a"] = c.alpha();
        arr.append(o);
    }
    return arr;
}

QVector<QColor> colorsFromJson(const QJsonArray& arr)
{
    QVector<QColor> out;
    out.reserve(arr.size());
    for (const QJsonValue& v : arr) {
        const QJsonObject o = v.toObject();
        out.append(QColor(o["r"].toInt(), o["g"].toInt(),
                          o["b"].toInt(), o["a"].toInt(255)));
    }
    return out;
}

QJsonArray qrealArrayToJson(const QVector<qreal>& v)
{
    QJsonArray arr;
    for (qreal x : v) arr.append(x);
    return arr;
}

QVector<qreal> qrealArrayFromJson(const QJsonArray& arr)
{
    QVector<qreal> out;
    out.reserve(arr.size());
    for (const QJsonValue& v : arr) arr.isEmpty() ? out.append(0.0) : out.append(v.toDouble());
    return out;
}

} // namespace

// ===== one layer =====

// layerIndex is the layer's position in the stack as it is written into
// this file. The blob names are built from it, NOT from layer.stableId:
// stableId comes from a process-global counter and is restored verbatim
// from the file, so two sessions can hand out the same id for two
// different layers. The file-internal index cannot collide -- it is the
// position in THIS file, unique by construction.
QJsonObject layerToJson(const layers::Layer& layer, int layerIndex,
                       QList<VstBlob>& blobs)
{
    QJsonObject o;
    o["layerIndex"] = layerIndex;

    o["stableId"] = static_cast<double>(layer.stableId);
    o["kind"] = static_cast<int>(layer.kind);
    o["blend"] = static_cast<int>(layer.blend);
    o["name"] = layer.name;
    o["visible"] = layer.visible;
    o["locked"] = layer.locked;
    o["linked"] = layer.isLinked;
    o["opacity"] = layer.opacity;
    o["zOrder"] = layer.zOrder;

    // The pixels.
    if (!layer.image.empty()) {
        const QString name = QStringLiteral("layer%1_pixels").arg(layerIndex);
        blobs.append(VstBlob{name, encodeMat(layer.image)});
        o["pixels"] = name;
        o["pixelsRows"] = layer.image.rows;
        o["pixelsCols"] = layer.image.cols;
        o["pixelsType"] = layer.image.type();
    }

    // The mask.
    if (layer.mask.kind != layers::LayerMask::None) {
        o["maskKind"] = static_cast<int>(layer.mask.kind);
        o["maskEnabled"] = layer.mask.enabled;
        o["maskDensity"] = layer.mask.density;
        o["maskFeather"] = layer.mask.feather;
        o["maskInvert"] = layer.mask.invert;
        if (layer.mask.kind == layers::LayerMask::Pixel && !layer.mask.pixel.empty()) {
            const QString name = QStringLiteral("layer%1_mask").arg(layerIndex);
            blobs.append(VstBlob{name, encodeMat(layer.mask.pixel)});
            o["maskPixels"] = name;
            o["maskRows"] = layer.mask.pixel.rows;
            o["maskCols"] = layer.mask.pixel.cols;
        } else if (layer.mask.kind == layers::LayerMask::Vector) {
            QJsonArray paths;
            for (const QPainterPath& p : layer.mask.vectorPaths) {
                paths.append(QString::fromLatin1(pathToBytes(p)));
            }
            o["maskVectorPaths"] = paths;
        }
    }

    // The text payload.
    if (layer.kind == layers::Layer::Text) {
        o["text"] = layer.text;
        o["fontSize"] = layer.fontSize;
        o["textColor"] = QStringLiteral("#%1%2%3")
                             .arg(layer.textColor.red(), 2, 16, QChar('0'))
                             .arg(layer.textColor.green(), 2, 16, QChar('0'))
                             .arg(layer.textColor.blue(), 2, 16, QChar('0'));
        o["fontFamily"] = layer.fontFamily;
    }

    // The vector payload.
    if (layer.kind == layers::Layer::Vector) {
        QJsonArray paths;
        for (const QPainterPath& p : layer.vectorPaths) paths.append(QString::fromLatin1(pathToBytes(p)));
        o["vectorPaths"] = paths;
        o["vectorFills"] = colorsToJson(layer.vectorFillColors);
        o["vectorStrokeWidths"] = qrealArrayToJson(layer.vectorStrokeWidths);
        o["vectorStrokeColors"] = colorsToJson(layer.vectorStrokeColors);
    }

    // The smart object payload.
    if (layer.kind == layers::Layer::SmartObject) {
        o["sourceFilePath"] = layer.sourceFilePath;
        o["sourceEmbedded"] = layer.sourceEmbedded;
        if (layer.hasTransform) {
            const QTransform& t = layer.transform;
            o["transform"] = QJsonArray{
                t.m11(), t.m12(), t.m13(),
                t.m21(), t.m22(), t.m23(),
                t.m31(), t.m32(), t.m33()
            };
        }
    }

    // The smart filter payload.
    if (layer.kind == layers::Layer::SmartFilter) {
        o["filterType"] = layer.filterType;
        o["filterStrength"] = layer.filterStrength;
        o["parentSmartId"] = static_cast<double>(layer.parentSmartId);
        o["filterSlotIndex"] = layer.filterSlotIndex;
    }

    // The adjustment payload.
    if (layer.kind == layers::Layer::Adjustment) {
        o["adjustmentType"] = layer.adjustmentType;
        if (!layer.adjustmentLut.empty()) {
            const QString name = QStringLiteral("layer%1_lut").arg(layerIndex);
            blobs.append(VstBlob{name, encodeMat(layer.adjustmentLut)});
            o["adjustmentLut"] = name;
            o["lutRows"] = layer.adjustmentLut.rows;
            o["lutCols"] = layer.adjustmentLut.cols;
            o["lutType"] = layer.adjustmentLut.type();
        }

        const layers::AdjustmentParams& p = layer.adjustmentParams;
        QJsonObject ap;
        ap["kind"] = static_cast<int>(p.kind);
        ap["lightness"] = p.lightness;
        ap["cmMonochrome"] = p.cmMonochrome;
        QJsonArray hue, sat, bw, cm;
        for (int v : p.hueShifts) hue.append(v);
        for (int v : p.satShifts) sat.append(v);
        for (double v : p.bwMixers) bw.append(v);
        for (int i = 0; i < 9; ++i) cm.append(p.cmMatrix[i]);
        ap["hueShifts"] = hue;
        ap["satShifts"] = sat;
        ap["bwMixers"] = bw;
        ap["cmMatrix"] = cm;
        o["adjustmentParams"] = ap;

        if (!layer.adjustmentChannelLuts.isEmpty()) {
            QJsonArray luts;
            for (int i = 0; i < layer.adjustmentChannelLuts.size(); ++i) {
                const cv::Mat& lut = layer.adjustmentChannelLuts[i];
                if (lut.empty()) { luts.append(QString()); continue; }
                const QString name = QStringLiteral("layer%1_clut%2").arg(layerIndex).arg(i);
                blobs.append(VstBlob{name, encodeMat(lut)});
                QJsonObject lo;
                lo["name"] = name;
                lo["rows"] = lut.rows;
                lo["cols"] = lut.cols;
                lo["type"] = lut.type();
                luts.append(lo);
            }
            o["channelLuts"] = luts;
        }
    }

    return o;
}

layers::Layer layerFromJson(const QJsonObject& o,
                            const QHash<QString, QByteArray>& blobs)
{
    layers::Layer l;

    l.stableId = static_cast<layers::LayerId>(o["stableId"].toDouble());
    l.kind = static_cast<layers::Layer::LayerKind>(o["kind"].toInt());
    l.blend = static_cast<layers::Layer::BlendMode>(o["blend"].toInt());
    l.name = o["name"].toString();
    l.visible = o["visible"].toBool(true);
    l.locked = o["locked"].toBool(false);
    l.isLinked = o["linked"].toBool(false);
    l.opacity = static_cast<float>(o["opacity"].toDouble(1.0));
    l.zOrder = o["zOrder"].toInt();

    if (o.contains("pixels")) {
        const QString name = o["pixels"].toString();
        const QByteArray bytes = blobs.value(name);
        l.image = decodeMat(bytes, o["pixelsRows"].toInt(), o["pixelsCols"].toInt(),
                            o["pixelsType"].toInt(CV_8UC4));
    }

    if (o.contains("maskKind")) {
        l.mask.kind = static_cast<layers::LayerMask::Kind>(o["maskKind"].toInt());
        l.mask.enabled = o["maskEnabled"].toBool(false);
        l.mask.density = o["maskDensity"].toDouble(1.0);
        l.mask.feather = o["maskFeather"].toDouble(0.0);
        l.mask.invert = o["maskInvert"].toBool(false);
        if (o.contains("maskPixels")) {
            l.mask.pixel = decodeMat(blobs.value(o["maskPixels"].toString()),
                                     o["maskRows"].toInt(), o["maskCols"].toInt(),
                                     CV_8UC1);
        } else if (o.contains("maskVectorPaths")) {
            const QJsonArray paths = o["maskVectorPaths"].toArray();
            for (const QJsonValue& v : paths) {
                l.mask.vectorPaths.append(pathFromBytes(v.toString().toLatin1()));
            }
        }
    }

    if (o.contains("text")) {
        l.text = o["text"].toString();
        l.fontSize = o["fontSize"].toInt(24);
        l.fontFamily = o["fontFamily"].toString(QStringLiteral("Arial"));
        const QColor c(o["textColor"].toString(QStringLiteral("#ffffff")));
        l.textColor = c.isValid() ? c : QColor(Qt::white);
    }

    if (o.contains("vectorPaths")) {
        const QJsonArray paths = o["vectorPaths"].toArray();
        for (const QJsonValue& v : paths) {
            l.vectorPaths.append(pathFromBytes(v.toString().toLatin1()));
        }
        l.vectorFillColors = colorsFromJson(o["vectorFills"].toArray());
        l.vectorStrokeWidths = qrealArrayFromJson(o["vectorStrokeWidths"].toArray());
        l.vectorStrokeColors = colorsFromJson(o["vectorStrokeColors"].toArray());
    }

    if (o.contains("sourceFilePath")) {
        l.sourceFilePath = o["sourceFilePath"].toString();
        l.sourceEmbedded = o["sourceEmbedded"].toBool(false);
        if (o.contains("transform")) {
            const QJsonArray a = o["transform"].toArray();
            if (a.size() == 9) {
                l.transform = QTransform(a[0].toDouble(), a[1].toDouble(), a[2].toDouble(),
                                         a[3].toDouble(), a[4].toDouble(), a[5].toDouble(),
                                         a[6].toDouble(), a[7].toDouble(), a[8].toDouble());
                l.hasTransform = true;
            }
        }
    }

    if (o.contains("filterType")) {
        l.filterType = o["filterType"].toString();
        l.filterStrength = o["filterStrength"].toDouble(1.0);
        l.parentSmartId = static_cast<layers::LayerId>(o["parentSmartId"].toDouble());
        l.filterSlotIndex = o["filterSlotIndex"].toInt(-1);
    }

    if (o.contains("adjustmentType")) {
        l.adjustmentType = o["adjustmentType"].toString();
        if (o.contains("adjustmentLut")) {
            l.adjustmentLut = decodeMat(blobs.value(o["adjustmentLut"].toString()),
                                         o["lutRows"].toInt(), o["lutCols"].toInt(),
                                         o["lutType"].toInt(CV_8UC1));
        }
        const QJsonObject ap = o["adjustmentParams"].toObject();
        l.adjustmentParams.kind =
            static_cast<layers::AdjustKind>(ap["kind"].toInt());
        l.adjustmentParams.lightness = ap["lightness"].toInt(0);
        l.adjustmentParams.cmMonochrome = ap["cmMonochrome"].toBool(false);
        for (const QJsonValue& v : ap["hueShifts"].toArray()) {
            l.adjustmentParams.hueShifts.append(v.toInt());
        }
        for (const QJsonValue& v : ap["satShifts"].toArray()) {
            l.adjustmentParams.satShifts.append(v.toInt());
        }
        for (const QJsonValue& v : ap["bwMixers"].toArray()) {
            l.adjustmentParams.bwMixers.append(v.toDouble());
        }
        const QJsonArray cm = ap["cmMatrix"].toArray();
        for (int i = 0; i < 9 && i < cm.size(); ++i) {
            l.adjustmentParams.cmMatrix[i] = cm[i].toDouble();
        }
        for (const QJsonValue& v : o["channelLuts"].toArray()) {
            const QJsonObject lo = v.toObject();
            l.adjustmentChannelLuts.append(
                decodeMat(blobs.value(lo["name"].toString()),
                          lo["rows"].toInt(), lo["cols"].toInt(),
                          lo["type"].toInt(CV_8UC1)));
        }
    }

    return l;
}

// ===== the whole stack =====

VstResult saveVstToBytes(QByteArray& out, const layers::LayerStack* stack)
{
    VstResult res;
    if (!stack) {
        res.error = QStringLiteral("no layer stack to save");
        return res;
    }

    // 1) The blobs.
    QList<VstBlob> blobs;
    QJsonArray layersJson;
    for (int i = 0; i < stack->count(); ++i) {
        layers::LayerPtr l = stack->at(i);
        if (!l) continue;
        // The index is the position in THIS file's layer array, so it is
        // stable and unique even if two layers share a stableId.
        layersJson.append(layerToJson(*l, layersJson.size(), blobs));
    }

    // 2) The manifest.
    QJsonObject manifest;
    manifest["version"] = static_cast<int>(kVstFormatVersion);
    // The DOCUMENT's canvas size, not the base layer's size: they
    // differ after a crop, and a project saved after a crop has to
    // come back with the cropped size.
    manifest["canvasWidth"] = stack->documentCanvasSize().width;
    manifest["canvasHeight"] = stack->documentCanvasSize().height;
    manifest["layers"] = layersJson;

    const QByteArray manifestBytes =
        QJsonDocument(manifest).toJson(QJsonDocument::Compact);

    // 3) The payload.
    QByteArray payload;
    for (const VstBlob& b : blobs) {
        payload.append(b.bytes);
    }

    // 4) The header.
    const quint32 manifestOffset = kHeaderSize;
    const quint32 payloadOffset = manifestOffset + static_cast<quint32>(manifestBytes.size());

    out.clear();
    out.append(kVstMagic, 8);
    putU32(out, manifestOffset);
    putU32(out, static_cast<quint32>(manifestBytes.size()));
    putU32(out, payloadOffset);
    putU32(out, static_cast<quint32>(payload.size()));
    putU32(out, 0x56535450);   // "VSTP" -- the first four bytes of the magic
    putU32(out, kVstFormatVersion);
    out.append(manifestBytes);
    out.append(payload);

    res.ok = true;
    return res;
}

VstResult loadVstFromBytes(const QByteArray& bytes, layers::LayerStack* stack)
{
    VstResult res;
    if (!stack) {
        res.error = QStringLiteral("no layer stack to load into");
        return res;
    }
    if (bytes.size() < kHeaderSize) {
        res.error = QStringLiteral("file is too short to be a .vst");
        return res;
    }
    if (std::memcmp(bytes.constData(), kVstMagic, 8) != 0) {
        res.error = QStringLiteral("not a .vst file (bad magic)");
        return res;
    }
    if (getU32(bytes, 24) != 0x56535450) {
        res.error = QStringLiteral("not a .vst file (bad magic copy)");
        return res;
    }

    const quint32 manifestOffset = getU32(bytes, 8);
    const quint32 manifestLength = getU32(bytes, 12);
    const quint32 payloadOffset = getU32(bytes, 16);
    const quint32 payloadLength = getU32(bytes, 20);

    if (static_cast<qint64>(manifestOffset) + manifestLength > bytes.size()) {
        res.error = QStringLiteral("the manifest extends past the end of the file");
        return res;
    }
    if (static_cast<qint64>(payloadOffset) + payloadLength > bytes.size()) {
        res.error = QStringLiteral("the payload extends past the end of the file");
        return res;
    }

    QJsonParseError err;
    const QJsonDocument doc = QJsonDocument::fromJson(
        bytes.mid(static_cast<int>(manifestOffset), static_cast<int>(manifestLength)), &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject()) {
        res.error = QStringLiteral("the manifest is not valid JSON: %1").arg(err.errorString());
        return res;
    }

    const QJsonObject manifest = doc.object();

    // The blobs are addressed by their offset inside the payload. The
    // manifest stores the blobs' SIZES so the reader can slice them
    // out -- the sizes live next to the names in the layer objects.
    const QByteArray payload = bytes.mid(static_cast<int>(payloadOffset),
                                          static_cast<int>(payloadLength));

    // Rebuild the blob table by walking the layers and their blob
    // references. The reader needs the size of each blob, and the
    // writer stores it in the layer objects.
    QHash<QString, QByteArray> blobTable;

    // The layer objects are read in order, and each blob is appended to
    // the payload in the same order, so the reader can walk the payload
    // with one cursor.
    int payloadCursor = 0;
    const QJsonArray layersJson = manifest["layers"].toArray();

    // The sizes are collected first: the writer wrote them next to the
    // names.
    auto takeBlob = [&](const QString& name, int size) -> QByteArray {
        if (name.isEmpty() || size <= 0) return QByteArray();
        if (payloadCursor + size > payload.size()) return QByteArray();
        const QByteArray b = payload.mid(payloadCursor, size);
        payloadCursor += size;
        blobTable.insert(name, b);
        return b;
    };

    // Pass 1: read the blob sizes out of the layer objects.
    QHash<QString, int> blobSizes;
    for (const QJsonValue& v : layersJson) {
        const QJsonObject o = v.toObject();
        if (o.contains("pixels")) {
            blobSizes.insert(o["pixels"].toString(),
                             o["pixelsRows"].toInt() * o["pixelsCols"].toInt()
                             * CV_ELEM_SIZE(o["pixelsType"].toInt(CV_8UC4)));
        }
        if (o.contains("maskPixels")) {
            blobSizes.insert(o["maskPixels"].toString(),
                             o["maskRows"].toInt() * o["maskCols"].toInt());
        }
        if (o.contains("adjustmentLut")) {
            blobSizes.insert(o["adjustmentLut"].toString(),
                             o["lutRows"].toInt() * o["lutCols"].toInt()
                             * CV_ELEM_SIZE(o["lutType"].toInt(CV_8UC1)));
        }
        for (const QJsonValue& lv : o["channelLuts"].toArray()) {
            const QJsonObject lo = lv.toObject();
            blobSizes.insert(lo["name"].toString(),
                             lo["rows"].toInt() * lo["cols"].toInt()
                             * CV_ELEM_SIZE(lo["type"].toInt(CV_8UC1)));
        }
    }

    // Pass 2: slice the payload in the SAME order the writer wrote it.
    //   The writer appends the blobs per layer in this order: pixels,
    //   mask, lut, channel luts.
    for (const QJsonValue& v : layersJson) {
        const QJsonObject o = v.toObject();
        for (const QString& key : {QStringLiteral("pixels"), QStringLiteral("maskPixels"),
                                   QStringLiteral("adjustmentLut")}) {
            if (o.contains(key)) {
                const QString name = o[key].toString();
                takeBlob(name, blobSizes.value(name));
            }
        }
        for (const QJsonValue& lv : o["channelLuts"].toArray()) {
            const QJsonObject lo = lv.toObject();
            const QString name = lo["name"].toString();
            takeBlob(name, blobSizes.value(name));
        }
    }

    // 3) Rebuild the layers.
    stack->clear();
    for (const QJsonValue& v : layersJson) {
        const QJsonObject o = v.toObject();
        layers::Layer l = layerFromJson(o, blobTable);
        stack->addLayer(l);
    }

    const int w = manifest["canvasWidth"].toInt(0);
    const int h = manifest["canvasHeight"].toInt(0);
    if (w > 0 && h > 0) {
        stack->setCanvasSize(cv::Size(w, h));
    }

    res.ok = true;
    return res;
}

VstResult saveVst(const QString& path, const layers::LayerStack* stack)
{
    VstResult res;
    QByteArray bytes;
    res = saveVstToBytes(bytes, stack);
    if (!res.ok) return res;

    // QSaveFile writes to a temporary file and renames it at the end,
    // so a crash mid-write cannot leave a half-written project.
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        res.error = QStringLiteral("cannot open %1 for writing: %2")
                        .arg(path, file.errorString());
        return res;
    }
    if (file.write(bytes) != bytes.size()) {
        res.error = QStringLiteral("cannot write %1: %2").arg(path, file.errorString());
        return res;
    }
    if (!file.commit()) {
        res.error = QStringLiteral("cannot commit %1: %2").arg(path, file.errorString());
        return res;
    }
    res.ok = true;
    return res;
}

VstResult loadVst(const QString& path, layers::LayerStack* stack)
{
    VstResult res;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        res.error = QStringLiteral("cannot open %1: %2").arg(path, file.errorString());
        return res;
    }
    const QByteArray bytes = file.readAll();
    file.close();
    return loadVstFromBytes(bytes, stack);
}

} // namespace project
