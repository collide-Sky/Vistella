// SPDX-License-Identifier: MIT
//
// BrushPreset + BrushPresetManager impl - P1.1 (2026-09-15)
//   JSON serialization + the 5 built-in PS-equivalent presets + persistence.

#include "BrushPreset.h"
#include "../../logger/Logger.h"

#include <QBuffer>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QIODevice>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QStandardPaths>

namespace brushes {

// ============================================================================
//  JSON helpers (settings / dynamics / shape)
// ============================================================================

static QJsonObject settingsToJson(const BrushSettings &s) {
    QJsonObject o;
    o["size"]       = s.size;
    o["hardness"]   = s.hardness;
    o["opacity"]    = s.opacity;
    o["flow"]       = s.flow;
    o["spacing"]    = s.spacing;
    o["angle"]      = s.angle;
    o["roundness"]  = s.roundness;
    return o;
}

static BrushSettings settingsFromJson(const QJsonObject &o) {
    BrushSettings s;
    s.size       = o.value("size").toInt(s.size);
    s.hardness   = o.value("hardness").toInt(s.hardness);
    s.opacity    = o.value("opacity").toInt(s.opacity);
    s.flow       = o.value("flow").toInt(s.flow);
    s.spacing    = o.value("spacing").toInt(s.spacing);
    s.angle      = o.value("angle").toInt(s.angle);
    s.roundness  = o.value("roundness").toInt(s.roundness);
    return s;
}

static QJsonObject shapeToJson(const BrushShape &s) {
    QJsonObject o;
    o["type"]          = shapeTypeToString(s.type);
    o["spacingHint"]   = s.spacingHint;
    if (!s.sampledBitmapPng.isEmpty()) {
        o["sampledBitmapPng"] = QString::fromLatin1(s.sampledBitmapPng.toBase64());
    }
    return o;
}

static BrushShape shapeFromJson(const QJsonObject &o) {
    BrushShape s;
    s.type        = shapeTypeFromString(o.value("type").toString());
    s.spacingHint = o.value("spacingHint").toInt(s.spacingHint);
    const auto b64 = o.value("sampledBitmapPng").toString();
    if (!b64.isEmpty()) {
        s.sampledBitmapPng = QByteArray::fromBase64(b64.toLatin1());
    }
    return s;
}

static QJsonObject shapeDynToJson(const ShapeDynamics &d) {
    QJsonObject o;
    o["sizeJitterMin"]   = d.sizeJitterMin;
    o["sizeJitterMax"]   = d.sizeJitterMax;
    o["sizeControl"]     = jitterControlToString(d.sizeControl);
    o["angleJitter"]     = d.angleJitter;
    o["angleControl"]    = jitterControlToString(d.angleControl);
    o["roundnessJitter"] = d.roundnessJitter;
    o["roundnessControl"]= jitterControlToString(d.roundnessControl);
    o["flipX"]           = d.flipX;
    o["flipY"]           = d.flipY;
    return o;
}

static ShapeDynamics shapeDynFromJson(const QJsonObject &o) {
    ShapeDynamics d;
    d.sizeJitterMin    = o.value("sizeJitterMin").toInt();
    d.sizeJitterMax    = o.value("sizeJitterMax").toInt();
    d.sizeControl      = jitterControlFromString(o.value("sizeControl").toString());
    d.angleJitter      = o.value("angleJitter").toInt();
    d.angleControl     = jitterControlFromString(o.value("angleControl").toString());
    d.roundnessJitter  = o.value("roundnessJitter").toInt();
    d.roundnessControl = jitterControlFromString(o.value("roundnessControl").toString());
    d.flipX            = o.value("flipX").toBool();
    d.flipY            = o.value("flipY").toBool();
    return d;
}

static QJsonObject scatterToJson(const ScatteringDynamics &d) {
    QJsonObject o;
    o["scatterX"]      = d.scatterX;
    o["scatterY"]      = d.scatterY;
    o["scatterControl"]= jitterControlToString(d.scatterControl);
    o["count"]         = d.count;
    o["countJitter"]   = d.countJitter;
    o["bothAxes"]      = d.bothAxes;
    return o;
}

static ScatteringDynamics scatterFromJson(const QJsonObject &o) {
    ScatteringDynamics d;
    d.scatterX       = o.value("scatterX").toInt();
    d.scatterY       = o.value("scatterY").toInt();
    d.scatterControl = jitterControlFromString(o.value("scatterControl").toString());
    d.count          = o.value("count").toInt(1);
    d.countJitter    = o.value("countJitter").toInt();
    d.bothAxes       = o.value("bothAxes").toBool(true);
    return d;
}

static QJsonObject textureToJson(const TextureDynamics &d) {
    QJsonObject o;
    o["texturePath"]  = d.texturePath;
    o["scale"]        = d.scale;
    o["mode"]         = jitterControlToString(d.mode);
    o["depthMin"]     = d.depthMin;
    o["depthMax"]     = d.depthMax;
    o["depthControl"] = jitterControlToString(d.depthControl);
    o["invert"]       = d.invert;
    o["brightness"]   = d.brightness;
    o["contrast"]     = d.contrast;
    o["protectTexture"]= d.protectTexture;
    return o;
}

static TextureDynamics textureFromJson(const QJsonObject &o) {
    TextureDynamics d;
    d.texturePath     = o.value("texturePath").toString();
    d.scale           = o.value("scale").toInt(100);
    d.mode            = jitterControlFromString(o.value("mode").toString());
    d.depthMin        = o.value("depthMin").toInt();
    d.depthMax        = o.value("depthMax").toInt(100);
    d.depthControl    = jitterControlFromString(o.value("depthControl").toString());
    d.invert          = o.value("invert").toBool();
    d.brightness      = o.value("brightness").toInt();
    d.contrast        = o.value("contrast").toInt();
    d.protectTexture  = o.value("protectTexture").toBool();
    return d;
}

static QJsonObject dualToJson(const DualBrushDynamics &d) {
    QJsonObject o;
    o["enabled"]         = d.enabled;
    o["secondaryPreset"] = d.secondaryPreset;
    o["size"]            = d.size;
    o["spacing"]         = d.spacing;
    o["scatter"]         = d.scatter;
    o["count"]           = d.count;
    return o;
}

static DualBrushDynamics dualFromJson(const QJsonObject &o) {
    DualBrushDynamics d;
    d.enabled         = o.value("enabled").toBool();
    d.secondaryPreset = o.value("secondaryPreset").toString();
    d.size            = o.value("size").toInt(100);
    d.spacing         = o.value("spacing").toInt(100);
    d.scatter         = o.value("scatter").toInt();
    d.count           = o.value("count").toInt(1);
    return d;
}

static QJsonObject transferToJson(const TransferDynamics &d) {
    QJsonObject o;
    o["opacityJitterMin"] = d.opacityJitterMin;
    o["opacityJitterMax"] = d.opacityJitterMax;
    o["opacityControl"]   = jitterControlToString(d.opacityControl);
    o["flowJitterMin"]    = d.flowJitterMin;
    o["flowJitterMax"]    = d.flowJitterMax;
    o["flowControl"]      = jitterControlToString(d.flowControl);
    o["airbrush"]         = d.airbrush;
    o["perClick"]         = d.perClick;
    return o;
}

static TransferDynamics transferFromJson(const QJsonObject &o) {
    TransferDynamics t;
    t.opacityJitterMin = o.value("opacityJitterMin").toInt();
    t.opacityJitterMax = o.value("opacityJitterMax").toInt();
    t.opacityControl   = jitterControlFromString(o.value("opacityControl").toString());
    t.flowJitterMin    = o.value("flowJitterMin").toInt();
    t.flowJitterMax    = o.value("flowJitterMax").toInt();
    t.flowControl      = jitterControlFromString(o.value("flowControl").toString());
    t.airbrush         = o.value("airbrush").toBool();
    t.perClick         = o.value("perClick").toBool(true);
    return t;
}

static QJsonObject smoothingToJson(const SmoothingDynamics &d) {
    QJsonObject o;
    o["enabled"]          = d.enabled;
    o["amount"]           = d.amount;
    o["strokeStabilizer"] = d.strokeStabilizer;
    o["radius"]           = d.radius;
    return o;
}

static SmoothingDynamics smoothingFromJson(const QJsonObject &o) {
    SmoothingDynamics s;
    s.enabled          = o.value("enabled").toBool(true);
    s.amount           = o.value("amount").toInt(50);
    s.strokeStabilizer = o.value("strokeStabilizer").toBool(true);
    s.radius           = o.value("radius").toInt(5);
    return s;
}

// ============================================================================
//  BrushSettings JSON
// ============================================================================

QString BrushSettings::toJsonString() const {
    return QString::fromUtf8(QJsonDocument(settingsToJson(*this)).toJson(QJsonDocument::Compact));
}

BrushSettings BrushSettings::fromJsonString(const QString &s) {
    QJsonDocument doc = QJsonDocument::fromJson(s.toUtf8());
    if (!doc.isObject()) return {};
    return settingsFromJson(doc.object());
}

// ============================================================================
//  BrushShape JSON
// ============================================================================

QString BrushShape::toJsonString() const {
    return QString::fromUtf8(QJsonDocument(shapeToJson(*this)).toJson(QJsonDocument::Compact));
}

BrushShape BrushShape::fromJsonString(const QString &s) {
    QJsonDocument doc = QJsonDocument::fromJson(s.toUtf8());
    if (!doc.isObject()) return {};
    return shapeFromJson(doc.object());
}

// ============================================================================
//  BrushDynamics JSON
// ============================================================================

QString BrushDynamics::toJsonString() const {
    QJsonObject o;
    o["shape"]      = shapeDynToJson(shape);
    o["scattering"] = scatterToJson(scattering);
    o["texture"]    = textureToJson(texture);
    o["dual"]       = dualToJson(dual);
    o["transfer"]   = transferToJson(transfer);
    o["smoothing"]  = smoothingToJson(smoothing);
    return QString::fromUtf8(QJsonDocument(o).toJson(QJsonDocument::Compact));
}

BrushDynamics BrushDynamics::fromJsonString(const QString &s) {
    QJsonDocument doc = QJsonDocument::fromJson(s.toUtf8());
    if (!doc.isObject()) return {};
    QJsonObject o = doc.object();
    BrushDynamics d;
    d.shape      = shapeDynFromJson(o.value("shape").toObject());
    d.scattering = scatterFromJson(o.value("scattering").toObject());
    d.texture    = textureFromJson(o.value("texture").toObject());
    d.dual       = dualFromJson(o.value("dual").toObject());
    d.transfer   = transferFromJson(o.value("transfer").toObject());
    d.smoothing  = smoothingFromJson(o.value("smoothing").toObject());
    return d;
}

// ============================================================================
//  BrushPreset
// ============================================================================

QString BrushPreset::toJsonString() const {
    QJsonObject o;
    o["name"]        = name;
    o["description"] = description;
    o["kind"]        = kindToString(kind);   // P7.9.2: 13-kinds taxonomy
    o["groupName"]   = groupName;
    o["settings"]    = settingsToJson(settings);
    o["shape"]       = shapeToJson(shape);
    o["dynamics"]    = QJsonDocument::fromJson(dynamics.toJsonString().toUtf8()).object();
    if (!thumbnail.isNull()) {
        QByteArray ba;
        QBuffer buf(&ba);
        buf.open(QIODevice::WriteOnly);
        thumbnail.save(&buf, "PNG");
        o["thumbnailPng"] = QString::fromLatin1(ba.toBase64());
    }
    if (!sourcePath.isEmpty()) o["sourcePath"] = sourcePath;
    return QString::fromUtf8(QJsonDocument(o).toJson(QJsonDocument::Compact));
}

BrushPreset BrushPreset::fromJsonString(const QString &s) {
    QJsonDocument doc = QJsonDocument::fromJson(s.toUtf8());
    QJsonObject o = doc.object();
    BrushPreset p;
    p.name        = o.value("name").toString();
    p.description = o.value("description").toString();
    p.kind        = kindFromString(o.value("kind").toString());  // P7.9.2
    p.groupName   = o.value("groupName").toString(QStringLiteral("Default"));
    p.settings    = settingsFromJson(o.value("settings").toObject());
    p.shape       = shapeFromJson(o.value("shape").toObject());
    p.dynamics    = BrushDynamics::fromJsonString(
                        QString::fromUtf8(QJsonDocument(o.value("dynamics").toObject()).toJson(QJsonDocument::Compact)));
    const auto tb64 = o.value("thumbnailPng").toString();
    if (!tb64.isEmpty()) {
        p.thumbnail.loadFromData(QByteArray::fromBase64(tb64.toLatin1()), "PNG");
    }
    p.sourcePath  = o.value("sourcePath").toString();
    return p;
}

bool BrushPreset::saveToFile(const QString &filePath, QString *err) const {
    QFile f(filePath);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        if (err) *err = f.errorString();
        return false;
    }
    QJsonObject root;
    root["format"]    = QStringLiteral("vbrush");
    root["version"]   = 1;
    root["preset"]    = QJsonDocument::fromJson(toJsonString().toUtf8()).object();
    f.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    return f.error() == QFile::NoError;
}

BrushPreset BrushPreset::loadFromFile(const QString &filePath, QString *err) {
    QFile f(filePath);
    if (!f.open(QIODevice::ReadOnly)) {
        if (err) *err = f.errorString();
        return {};
    }
    QJsonParseError perr;
    QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &perr);
    if (perr.error != QJsonParseError::NoError) {
        if (err) *err = perr.errorString();
        return {};
    }
    QJsonObject root = doc.object();
    if (root.value("format").toString() != QStringLiteral("vbrush")) {
        if (err) *err = QStringLiteral("not a vbrush file");
        return {};
    }
    BrushPreset p = BrushPreset::fromJsonString(
        QString::fromUtf8(QJsonDocument(root.value("preset").toObject()).toJson(QJsonDocument::Compact)));
    p.sourcePath = filePath;
    return p;
}

// ============================================================================
//  13 built-in presets (P7.9.2: 8 Basic + 3 Wet + 2 Special)
//
//  Defaults are PS-equivalent values: size in px, hardness/opacity/flow in %,
//  spacing in % of diameter, angle/roundness as documented.
//
//  Group Name: "Built-in" for all (manager exposes via builtIns()).
// ============================================================================

BrushPreset BrushPreset::makeBuiltInHardRound() {
    BrushPreset p;
    p.name        = QStringLiteral("硬边圆笔");
    p.description = QStringLiteral("PS: Hard Round - sharp edge round brush, default Photoshop brush");
    p.kind        = BrushKind::HardRound;
    p.groupName   = QStringLiteral("Built-in");
    p.settings    = BrushSettings{ 32, 100, 100, 100, 25, 0, 100 };
    p.shape.type  = ShapeType::Round;
    // Thumbnail generation deferred to engine (CPaintEngine)
    return p;
}

BrushPreset BrushPreset::makeBuiltInSoftRound() {
    BrushPreset p;
    p.name        = QStringLiteral("柔边圆笔");
    p.description = QStringLiteral("PS: Soft Round - Gaussian falloff brush, airbrush feel");
    p.kind        = BrushKind::SoftRound;
    p.groupName   = QStringLiteral("Built-in");
    p.settings    = BrushSettings{ 64, 20, 100, 100, 15, 0, 100 };
    p.shape.type  = ShapeType::SoftRound;
    return p;
}

BrushPreset BrushPreset::makeBuiltInPencil() {
    BrushPreset p;
    p.name        = QStringLiteral("铅笔");
    p.description = QStringLiteral("PS: Pencil - small diameter, high hardness, tight spacing");
    p.kind        = BrushKind::Pencil;
    p.groupName   = QStringLiteral("Built-in");
    p.settings    = BrushSettings{ 4, 85, 80, 100, 5, 0, 80 };
    p.shape.type  = ShapeType::Round;
    return p;
}

BrushPreset BrushPreset::makeBuiltInMarker() {
    BrushPreset p;
    p.name        = QStringLiteral("马克笔");
    p.description = QStringLiteral("PS: Marker - flat oval, 50% opacity, large spacing");
    p.kind        = BrushKind::Marker;
    p.groupName   = QStringLiteral("Built-in");
    p.settings    = BrushSettings{ 20, 70, 50, 100, 10, 0, 30 };
    p.shape.type  = ShapeType::Flat;
    return p;
}

BrushPreset BrushPreset::makeBuiltInAirbrush() {
    BrushPreset p;
    p.name        = QStringLiteral("空气笔");
    p.description = QStringLiteral("PS: Airbrush - very low hardness + Airbrush mode");
    p.kind        = BrushKind::Airbrush;
    p.groupName   = QStringLiteral("Built-in");
    p.settings    = BrushSettings{ 80, 0, 50, 50, 5, 0, 100 };
    p.shape.type  = ShapeType::Airbrush;
    p.dynamics.transfer.airbrush = true;   // Airbrush mode (paint doesn't decay)
    p.dynamics.transfer.perClick = false;
    return p;
}

BrushPreset BrushPreset::makeBuiltInChalk() {
    BrushPreset p;
    p.name        = QStringLiteral("粉笔");
    p.description = QStringLiteral("PS: Chalk - noisy edges with reduced opacity");
    p.kind        = BrushKind::Chalk;
    p.groupName   = QStringLiteral("Built-in");
    p.settings    = BrushSettings{ 48, 60, 80, 80, 10, 0, 100 };
    p.shape.type  = ShapeType::Chalk;
    // Chalk-specific: scatter 5%, count 2
    p.dynamics.scattering.scatterX = 5;
    p.dynamics.scattering.scatterY = 5;
    p.dynamics.scattering.count    = 2;
    return p;
}

BrushPreset BrushPreset::makeBuiltInCharcoal() {
    BrushPreset p;
    p.name        = QStringLiteral("炭笔");
    p.description = QStringLiteral("PS: Charcoal - heavy grain texture, low flow");
    p.kind        = BrushKind::Charcoal;
    p.groupName   = QStringLiteral("Built-in");
    p.settings    = BrushSettings{ 56, 40, 100, 30, 12, 0, 100 };
    p.shape.type  = ShapeType::Charcoal;
    // Charcoal-specific: size jitter 20% with pressure, transfer flow low
    p.dynamics.shape.sizeJitterMin = 0;
    p.dynamics.shape.sizeJitterMax = 20;
    p.dynamics.shape.sizeControl   = JitterControl::PenPressure;
    return p;
}

BrushPreset BrushPreset::makeBuiltInCalligraphic() {
    BrushPreset p;
    p.name        = QStringLiteral("书法笔");
    p.description = QStringLiteral("PS: Calligraphic - angle/roundness-driven, flat oval");
    p.kind        = BrushKind::Calligraphic;
    p.groupName   = QStringLiteral("Built-in");
    p.settings    = BrushSettings{ 15, 80, 100, 100, 10, 45, 30 };
    p.shape.type  = ShapeType::Flat;
    // Calligraphic: roundness jitter with pressure for stroke feel
    p.dynamics.shape.roundnessJitter  = 30;
    p.dynamics.shape.roundnessControl = JitterControl::PenPressure;
    return p;
}

// --- Wet 3 ---

BrushPreset BrushPreset::makeBuiltInWatercolor() {
    BrushPreset p;
    p.name        = QStringLiteral("水彩");
    p.description = QStringLiteral("PS: Watercolor - low hardness, low flow, wet edge feel");
    p.kind        = BrushKind::Watercolor;
    p.groupName   = QStringLiteral("Built-in");
    p.settings    = BrushSettings{ 40, 10, 40, 30, 8, 0, 100 };
    p.shape.type  = ShapeType::SoftRound;
    // Watercolor: high scatter (1 stamp per stroke point splatters)
    p.dynamics.scattering.scatterX = 15;
    p.dynamics.scattering.scatterY = 15;
    p.dynamics.scattering.count    = 3;
    return p;
}

BrushPreset BrushPreset::makeBuiltInOil() {
    BrushPreset p;
    p.name        = QStringLiteral("油画");
    p.description = QStringLiteral("PS: Oil - heavy deposit, no flow jitter, dense pigment");
    p.kind        = BrushKind::Oil;
    p.groupName   = QStringLiteral("Built-in");
    p.settings    = BrushSettings{ 24, 60, 85, 100, 20, 0, 100 };
    p.shape.type  = ShapeType::Round;
    // Oil: minimal jitter; opacity/flow steady
    p.dynamics.transfer.flowJitterMin = 0;
    p.dynamics.transfer.flowJitterMax = 0;
    return p;
}

BrushPreset BrushPreset::makeBuiltInAcrylic() {
    BrushPreset p;
    p.name        = QStringLiteral("丙烯");
    p.description = QStringLiteral("PS: Acrylic - medium wet, soft round, high flow");
    p.kind        = BrushKind::Acrylic;
    p.groupName   = QStringLiteral("Built-in");
    p.settings    = BrushSettings{ 30, 70, 90, 85, 15, 0, 100 };
    p.shape.type  = ShapeType::SoftRound;
    return p;
}

// --- Special 2 ---

BrushPreset BrushPreset::makeBuiltInSmudge() {
    BrushPreset p;
    p.name        = QStringLiteral("涂抹");
    p.description = QStringLiteral("PS: Smudge - drags existing pixels, no new color");
    p.kind        = BrushKind::Smudge;
    p.groupName   = QStringLiteral("Built-in");
    p.settings    = BrushSettings{ 30, 50, 100, 50, 20, 0, 100 };
    p.shape.type  = ShapeType::Round;
    // Smudge: tight spacing for finger-paint feel
    return p;
}

BrushPreset BrushPreset::makeBuiltInEraser() {
    BrushPreset p;
    p.name        = QStringLiteral("橡皮");
    p.description = QStringLiteral("PS: Eraser - destination erase, hard round");
    p.kind        = BrushKind::Eraser;
    p.groupName   = QStringLiteral("Built-in");
    p.settings    = BrushSettings{ 20, 80, 100, 100, 25, 0, 100 };
    p.shape.type  = ShapeType::Round;
    return p;
}

// ============================================================================
//  BrushPresetManager
// ============================================================================

BrushPresetManager::BrushPresetManager(QObject *parent)
    : QObject(parent)
{
    seedBuiltIns();
}

void BrushPresetManager::seedBuiltIns() {
    // 5 built-ins are returned by builtIns(); they are not stored in m_userPresets.
    // (No action needed, factory methods are static.)
}

QList<BrushPreset> BrushPresetManager::builtIns() const {
    // P7.9.2: 5 -> 13 built-in presets, ordered per BrushKind::allKinds().
    //   Basic 8: HardRound / SoftRound / Pencil / Marker / Airbrush / Chalk / Charcoal / Calligraphic
    //   Wet 3:   Watercolor / Oil / Acrylic
    //   Special 2: Smudge / Eraser
    return {
        BrushPreset::makeBuiltInHardRound(),
        BrushPreset::makeBuiltInSoftRound(),
        BrushPreset::makeBuiltInPencil(),
        BrushPreset::makeBuiltInMarker(),
        BrushPreset::makeBuiltInAirbrush(),
        BrushPreset::makeBuiltInChalk(),
        BrushPreset::makeBuiltInCharcoal(),
        BrushPreset::makeBuiltInCalligraphic(),
        BrushPreset::makeBuiltInWatercolor(),
        BrushPreset::makeBuiltInOil(),
        BrushPreset::makeBuiltInAcrylic(),
        BrushPreset::makeBuiltInSmudge(),
        BrushPreset::makeBuiltInEraser(),
    };
}

QList<BrushPreset> BrushPresetManager::all() const {
    QList<BrushPreset> r = builtIns();
    r.append(m_userPresets);
    return r;
}

QList<BrushPreset> BrushPresetManager::userPresets() const {
    return m_userPresets;
}

void BrushPresetManager::addUserPreset(const BrushPreset &p) {
    // Reject if name collides with a built-in (PS: built-ins are read-only)
    for (const auto &b : builtIns()) {
        if (b.name == p.name) return;
    }
    for (int i = 0; i < m_userPresets.size(); ++i) {
        if (m_userPresets[i].name == p.name) {
            m_userPresets[i] = p;
            emit presetsChanged();
            return;
        }
    }
    m_userPresets.append(p);
    emit presetsChanged();
}

bool BrushPresetManager::removeUserPreset(const QString &name) {
    for (int i = 0; i < m_userPresets.size(); ++i) {
        if (m_userPresets[i].name == name) {
            m_userPresets.removeAt(i);
            emit presetsChanged();
            return true;
        }
    }
    return false;
}

BrushPreset BrushPresetManager::findByName(const QString &name) const {
    for (const auto &p : m_userPresets) if (p.name == name) return p;
    for (const auto &p : builtIns()) if (p.name == name) return p;
    return {};
}

QString BrushPresetManager::defaultUserDir() {
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation)
                      + QStringLiteral("/brushes");
    QDir().mkpath(dir);
    return dir;
}

bool BrushPresetManager::saveUserPresets(QString *err) {
    const QString dir = defaultUserDir();
    QJsonArray arr;
    for (const auto &p : m_userPresets) {
        arr.append(QJsonDocument::fromJson(p.toJsonString().toUtf8()).object());
    }
    QJsonObject root;
    root["format"]  = QStringLiteral("vbrush-set");
    root["version"] = 1;
    root["presets"] = arr;

    QFile f(dir + QStringLiteral("/user-presets.json"));
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        if (err) *err = f.errorString();
        return false;
    }
    f.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    return f.error() == QFile::NoError;
}

bool BrushPresetManager::loadUserPresets(QString *err) {
    const QString path = defaultUserDir() + QStringLiteral("/user-presets.json");
    QFile f(path);
    if (!f.exists()) return true;   // ok, empty user set
    if (!f.open(QIODevice::ReadOnly)) {
        if (err) *err = f.errorString();
        return false;
    }
    QJsonParseError perr;
    QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &perr);
    if (perr.error != QJsonParseError::NoError) {
        if (err) *err = perr.errorString();
        return false;
    }
    QJsonObject root = doc.object();
    if (root.value("format").toString() != QStringLiteral("vbrush-set")) {
        if (err) *err = QStringLiteral("not a vbrush-set file");
        return false;
    }
    m_userPresets.clear();
    const QJsonArray arr = root.value("presets").toArray();
    for (const auto &v : arr) {
        BrushPreset p = BrushPreset::fromJsonString(
            QString::fromUtf8(QJsonDocument(v.toObject()).toJson(QJsonDocument::Compact)));
        if (!p.name.isEmpty()) m_userPresets.append(p);
    }
    emit presetsChanged();
    return true;
}

} // namespace brushes