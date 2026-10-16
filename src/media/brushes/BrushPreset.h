// SPDX-License-Identifier: MIT
//
// BrushPreset + BrushPresetManager - P1.1 (2026-09-15) Brush full implementation
//   BrushPreset: name + settings + dynamics + shape + thumbnail (for picker UI).
//   BrushPresetManager: built-in 5 presets (Hard Round / Soft Round / Airbrush /
//   Chalk / Charcoal) at startup + JSON load/save for user presets + ABR import.
//
//   Serialization format: JSON file (.vbrush extension, mime-type text/plain).
//   Schema matches Photoshop CS6+ preset structure for round-trip friendliness
//   when user pastes preset exports from internet.

#pragma once

#include <QImage>
#include <QList>
#include <QObject>
#include <QString>
#include <QStringList>

#include "BrushSettings.h"
#include "BrushShape.h"
#include "BrushDynamics.h"
#include "BrushKind.h"

namespace brushes {

struct BrushPreset {
    QString           name;
    QString           description;            // optional tooltip / picker description
    BrushKind         kind = BrushKind::HardRound;  // P7.9.2: 13-kinds taxonomy
    BrushSettings     settings;
    BrushShape        shape;
    BrushDynamics     dynamics;
    QImage            thumbnail;              // 64x64 picker icon (PNG-style alpha preview)
    QString           sourcePath;             // file path if imported from ABR (empty for built-in)
    QString           groupName = QStringLiteral("Default");

    bool              isBuiltIn() const { return sourcePath.isEmpty(); }

    // JSON serialization (QSettings QJsonDocument underneath)
    static BrushPreset fromJsonString(const QString &s);
    QString            toJsonString() const;

    // File I/O
    bool saveToFile(const QString &filePath, QString *err = nullptr) const;
    static BrushPreset loadFromFile(const QString &filePath, QString *err = nullptr);

    // Factory: the 13 built-in presets (PS equivalent names).
    // P7.9.2: extended 5 -> 13 to cover Basic 8 + Wet 3 + Special 2.
    // Order in this list mirrors BrushKind::allKinds() for stable indexing.
    static BrushPreset makeBuiltInHardRound();
    static BrushPreset makeBuiltInSoftRound();
    static BrushPreset makeBuiltInPencil();
    static BrushPreset makeBuiltInMarker();
    static BrushPreset makeBuiltInAirbrush();
    static BrushPreset makeBuiltInChalk();
    static BrushPreset makeBuiltInCharcoal();
    static BrushPreset makeBuiltInCalligraphic();
    static BrushPreset makeBuiltInWatercolor();
    static BrushPreset makeBuiltInOil();
    static BrushPreset makeBuiltInAcrylic();
    static BrushPreset makeBuiltInSmudge();
    static BrushPreset makeBuiltInEraser();
};

class BrushPresetManager : public QObject {
    Q_OBJECT
public:
    explicit BrushPresetManager(QObject *parent = nullptr);

    // Returns the 5 built-in presets (always present, not persisted).
    QList<BrushPreset> builtIns() const;

    // All presets (built-ins + user-loaded).
    QList<BrushPreset> all() const;

    // User presets only.
    QList<BrushPreset> userPresets() const;

    // Add a user preset (replaces by name if exists).
    void addUserPreset(const BrushPreset &p);

    // Remove user preset by name. Returns true if removed.
    bool removeUserPreset(const QString &name);

    // Find by name (searches user first then built-in).
    BrushPreset findByName(const QString &name) const;

    // Persist user presets to disk (default location).
    bool saveUserPresets(QString *err = nullptr);

    // Load user presets from disk.
    bool loadUserPresets(QString *err = nullptr);

    // Default user-presets directory under AppLocalDataLocation.
    static QString defaultUserDir();

signals:
    void presetsChanged();

private:
    QList<BrushPreset> m_userPresets;

    void seedBuiltIns();
};

} // namespace brushes