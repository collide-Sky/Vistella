// SPDX-License-Identifier: MIT
//
// tst_BrushDataLayer - P1.1 (2026-09-15) Brush full implementation
//   Unit tests for the data layer:
//     - BrushSettings / BrushDynamics / BrushPreset JSON round-trip
//     - 5 built-in presets exist + sane defaults
//     - BrushPresetManager add/remove/find + load/save persistence

#include <QTest>
#include <QSignalSpy>
#include <QTemporaryDir>

#include "../src/media/brushes/BrushSettings.h"
#include "../src/media/brushes/BrushDynamics.h"
#include "../src/media/brushes/BrushShape.h"
#include "../src/media/brushes/BrushPreset.h"

using namespace brushes;

class tst_BrushDataLayer : public QObject
{
    Q_OBJECT

private slots:
    void settings_roundtrip_preserves_all_fields();
    void dynamics_roundtrip_preserves_all_fields();
    void preset_roundtrip_preserves_all_fields();
    void five_builtins_are_distinct();
    void manager_add_remove_find();
    void manager_save_load_persistence();
    void manager_persistence_signals();
    void jitter_control_string_roundtrip();
    void shape_type_string_roundtrip();
    void preset_file_io();
};

void tst_BrushDataLayer::settings_roundtrip_preserves_all_fields()
{
    BrushSettings a;
    a.size = 87;
    a.hardness = 42;
    a.opacity = 73;
    a.flow = 91;
    a.spacing = 18;
    a.angle = 234;
    a.roundness = 56;
    const QString s = a.toJsonString();
    QVERIFY(!s.isEmpty());
    const BrushSettings b = BrushSettings::fromJsonString(s);
    QCOMPARE(b.size, a.size);
    QCOMPARE(b.hardness, a.hardness);
    QCOMPARE(b.opacity, a.opacity);
    QCOMPARE(b.flow, a.flow);
    QCOMPARE(b.spacing, a.spacing);
    QCOMPARE(b.angle, a.angle);
    QCOMPARE(b.roundness, a.roundness);
}

void tst_BrushDataLayer::dynamics_roundtrip_preserves_all_fields()
{
    BrushDynamics d;
    d.shape.sizeJitterMin   = 10;
    d.shape.sizeJitterMax   = 60;
    d.shape.sizeControl     = JitterControl::PenPressure;
    d.shape.angleJitter     = 45;
    d.shape.angleControl    = JitterControl::Rotation;
    d.shape.flipX           = true;
    d.shape.flipY           = false;
    d.scattering.scatterX   = 25;
    d.scattering.scatterY   = 50;
    d.scattering.scatterControl = JitterControl::Fade;
    d.scattering.count      = 4;
    d.scattering.countJitter = 30;
    d.texture.scale         = 150;
    d.texture.depthMin      = 25;
    d.texture.depthMax      = 75;
    d.texture.invert        = true;
    d.texture.brightness    = -10;
    d.texture.contrast      = 25;
    d.texture.protectTexture= true;
    d.dual.enabled          = true;
    d.dual.size             = 75;
    d.dual.spacing          = 200;
    d.dual.scatter          = 15;
    d.dual.count            = 3;
    d.transfer.opacityJitterMin = 5;
    d.transfer.opacityJitterMax = 30;
    d.transfer.opacityControl = JitterControl::PenTilt;
    d.transfer.flowJitterMin = 0;
    d.transfer.flowJitterMax = 50;
    d.transfer.flowControl   = JitterControl::StylusWheel;
    d.transfer.airbrush      = true;
    d.transfer.perClick      = false;
    d.smoothing.enabled      = true;
    d.smoothing.amount       = 70;
    d.smoothing.strokeStabilizer = true;
    d.smoothing.radius       = 10;

    const QString s = d.toJsonString();
    QVERIFY(!s.isEmpty());
    const BrushDynamics b = BrushDynamics::fromJsonString(s);

    QCOMPARE(b.shape.sizeJitterMin,    d.shape.sizeJitterMin);
    QCOMPARE(b.shape.sizeJitterMax,    d.shape.sizeJitterMax);
    QCOMPARE(b.shape.sizeControl,      d.shape.sizeControl);
    QCOMPARE(b.shape.angleJitter,      d.shape.angleJitter);
    QCOMPARE(b.shape.angleControl,     d.shape.angleControl);
    QCOMPARE(b.shape.flipX,            d.shape.flipX);
    QCOMPARE(b.shape.flipY,            d.shape.flipY);
    QCOMPARE(b.scattering.scatterX,    d.scattering.scatterX);
    QCOMPARE(b.scattering.scatterY,    d.scattering.scatterY);
    QCOMPARE(b.scattering.scatterControl, d.scattering.scatterControl);
    QCOMPARE(b.scattering.count,       d.scattering.count);
    QCOMPARE(b.scattering.countJitter, d.scattering.countJitter);
    QCOMPARE(b.texture.scale,          d.texture.scale);
    QCOMPARE(b.texture.depthMin,       d.texture.depthMin);
    QCOMPARE(b.texture.depthMax,       d.texture.depthMax);
    QCOMPARE(b.texture.invert,         d.texture.invert);
    QCOMPARE(b.texture.brightness,     d.texture.brightness);
    QCOMPARE(b.texture.contrast,       d.texture.contrast);
    QCOMPARE(b.texture.protectTexture, d.texture.protectTexture);
    QCOMPARE(b.dual.enabled,           d.dual.enabled);
    QCOMPARE(b.dual.size,              d.dual.size);
    QCOMPARE(b.dual.spacing,           d.dual.spacing);
    QCOMPARE(b.dual.scatter,           d.dual.scatter);
    QCOMPARE(b.dual.count,             d.dual.count);
    QCOMPARE(b.transfer.opacityJitterMin, d.transfer.opacityJitterMin);
    QCOMPARE(b.transfer.opacityJitterMax, d.transfer.opacityJitterMax);
    QCOMPARE(b.transfer.opacityControl, d.transfer.opacityControl);
    QCOMPARE(b.transfer.flowJitterMin,  d.transfer.flowJitterMin);
    QCOMPARE(b.transfer.flowJitterMax,  d.transfer.flowJitterMax);
    QCOMPARE(b.transfer.flowControl,    d.transfer.flowControl);
    QCOMPARE(b.transfer.airbrush,       d.transfer.airbrush);
    QCOMPARE(b.transfer.perClick,       d.transfer.perClick);
    QCOMPARE(b.smoothing.enabled,       d.smoothing.enabled);
    QCOMPARE(b.smoothing.amount,        d.smoothing.amount);
    QCOMPARE(b.smoothing.strokeStabilizer, d.smoothing.strokeStabilizer);
    QCOMPARE(b.smoothing.radius,        d.smoothing.radius);
}

void tst_BrushDataLayer::preset_roundtrip_preserves_all_fields()
{
    BrushPreset a;
    a.name = "My Test Brush";
    a.description = "test";
    a.groupName = "CustomGroup";
    a.settings.size = 50;
    a.settings.hardness = 30;
    a.shape.type = ShapeType::SoftRound;
    a.dynamics.scattering.scatterX = 15;
    a.dynamics.transfer.airbrush = true;

    const QString s = a.toJsonString();
    QVERIFY(!s.isEmpty());
    const BrushPreset b = BrushPreset::fromJsonString(s);
    QCOMPARE(b.name, a.name);
    QCOMPARE(b.description, a.description);
    QCOMPARE(b.groupName, a.groupName);
    QCOMPARE(b.settings.size, a.settings.size);
    QCOMPARE(b.settings.hardness, a.settings.hardness);
    QCOMPARE(b.shape.type, a.shape.type);
    QCOMPARE(b.dynamics.scattering.scatterX, a.dynamics.scattering.scatterX);
    QCOMPARE(b.dynamics.transfer.airbrush, a.dynamics.transfer.airbrush);
}

void tst_BrushDataLayer::five_builtins_are_distinct()
{
    const QList<BrushPreset> b = {
        BrushPreset::makeBuiltInHardRound(),
        BrushPreset::makeBuiltInSoftRound(),
        BrushPreset::makeBuiltInAirbrush(),
        BrushPreset::makeBuiltInChalk(),
        BrushPreset::makeBuiltInCharcoal(),
    };
    QCOMPARE(b.size(), 5);
    QSet<QString> names;
    for (const auto &p : b) {
        QVERIFY(!p.name.isEmpty());
        QVERIFY(!names.contains(p.name));
        names.insert(p.name);
        // 5 built-ins must have sane defaults
        QVERIFY(p.settings.size > 0);
        QVERIFY(p.settings.size <= 500);
        QVERIFY(p.settings.hardness >= 0);
        QVERIFY(p.settings.hardness <= 100);
        QVERIFY(p.settings.opacity >= 0);
        QVERIFY(p.settings.opacity <= 100);
        QVERIFY(p.settings.flow >= 0);
        QVERIFY(p.settings.flow <= 100);
        QVERIFY(p.isBuiltIn());
    }
}

void tst_BrushDataLayer::manager_add_remove_find()
{
    BrushPresetManager mgr;
    QCOMPARE(mgr.userPresets().size(), 0);
    QCOMPARE(mgr.builtIns().size(), 5);

    BrushPreset p;
    p.name = "Custom 1";
    p.settings.size = 60;
    mgr.addUserPreset(p);
    QCOMPARE(mgr.userPresets().size(), 1);
    QCOMPARE(mgr.all().size(), 6);  // 5 built-in + 1 user

    // Replace by same name
    p.settings.size = 80;
    mgr.addUserPreset(p);
    QCOMPARE(mgr.userPresets().size(), 1);
    QCOMPARE(mgr.findByName("Custom 1").settings.size, 80);

    // Remove
    QVERIFY(mgr.removeUserPreset("Custom 1"));
    QCOMPARE(mgr.userPresets().size(), 0);
    QVERIFY(!mgr.removeUserPreset("Custom 1"));  // double-remove returns false

    // Find built-in
    BrushPreset hard = mgr.findByName("硬边圆笔");
    QCOMPARE(hard.name, QStringLiteral("硬边圆笔"));
    QCOMPARE(hard.shape.type, ShapeType::Round);
}

void tst_BrushDataLayer::manager_save_load_persistence()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());

    BrushPresetManager mgr;
    // Redirect user dir to temp by setting QStandardPaths
    qputenv("XDG_DATA_HOME", tmp.path().toUtf8());   // Linux
    qputenv("APPDATA", tmp.path().toUtf8());         // Windows

    BrushPreset p;
    p.name = "Saved Brush";
    p.settings.size = 42;
    p.settings.hardness = 88;
    p.shape.type = ShapeType::Chalk;
    mgr.addUserPreset(p);

    QString err;
    QVERIFY(mgr.saveUserPresets(&err));
    QCOMPARE(err, QString());

    // New manager instance, verify it loads back
    BrushPresetManager mgr2;
    // Force re-load from disk
    QVERIFY(mgr2.loadUserPresets(&err));
    QVERIFY(mgr2.userPresets().size() >= 1);

    BrushPreset loaded = mgr2.findByName("Saved Brush");
    QCOMPARE(loaded.name, QStringLiteral("Saved Brush"));
    QCOMPARE(loaded.settings.size, 42);
    QCOMPARE(loaded.settings.hardness, 88);
    QCOMPARE(loaded.shape.type, ShapeType::Chalk);
}

void tst_BrushDataLayer::manager_persistence_signals()
{
    BrushPresetManager mgr;
    QSignalSpy spy(&mgr, &BrushPresetManager::presetsChanged);

    BrushPreset p;
    p.name = "SignalTest";
    mgr.addUserPreset(p);
    QCOMPARE(spy.count(), 1);

    mgr.removeUserPreset("SignalTest");
    QCOMPARE(spy.count(), 2);

    // remove non-existent: no signal
    mgr.removeUserPreset("does-not-exist");
    QCOMPARE(spy.count(), 2);
}

void tst_BrushDataLayer::jitter_control_string_roundtrip()
{
    const JitterControl vals[] = {
        JitterControl::Off,
        JitterControl::Fade,
        JitterControl::PenPressure,
        JitterControl::PenTilt,
        JitterControl::Rotation,
        JitterControl::StylusWheel,
    };
    for (auto v : vals) {
        const QString s = jitterControlToString(v);
        QVERIFY(!s.isEmpty());
        QCOMPARE(jitterControlFromString(s), v);
    }
    // Unknown string -> Off
    QCOMPARE(jitterControlFromString(QStringLiteral("nonsense")), JitterControl::Off);
}

void tst_BrushDataLayer::shape_type_string_roundtrip()
{
    const ShapeType vals[] = {
        ShapeType::Round, ShapeType::SoftRound, ShapeType::Flat,
        ShapeType::Fan, ShapeType::Airbrush, ShapeType::Chalk,
        ShapeType::Charcoal, ShapeType::Textured, ShapeType::Sampled,
    };
    for (auto v : vals) {
        const QString s = shapeTypeToString(v);
        QVERIFY(!s.isEmpty());
        QCOMPARE(shapeTypeFromString(s), v);
    }
    // Unknown string -> Round
    QCOMPARE(shapeTypeFromString(QStringLiteral("nonsense")), ShapeType::Round);
}

void tst_BrushDataLayer::preset_file_io()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    const QString path = tmp.filePath("test.vbrush.p");

    BrushPreset a;
    a.name = "File IO Brush";
    a.settings.size = 64;
    a.settings.hardness = 75;
    a.shape.type = ShapeType::Airbrush;
    a.dynamics.transfer.airbrush = true;

    QString err;
    QVERIFY(a.saveToFile(path, &err));
    QCOMPARE(err, QString());

    const BrushPreset b = BrushPreset::loadFromFile(path, &err);
    QCOMPARE(err, QString());
    QCOMPARE(b.name, a.name);
    QCOMPARE(b.settings.size, a.settings.size);
    QCOMPARE(b.settings.hardness, a.settings.hardness);
    QCOMPARE(b.shape.type, a.shape.type);
    QCOMPARE(b.dynamics.transfer.airbrush, a.dynamics.transfer.airbrush);
    QCOMPARE(b.sourcePath, path);   // loadFromFile populates sourcePath
}

QTEST_MAIN(tst_BrushDataLayer)
#include "tst_BrushDataLayer.moc"