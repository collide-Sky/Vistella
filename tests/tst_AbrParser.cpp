// SPDX-License-Identifier: MIT
//
// tst_AbrParser - P1.1 (2026-09-15) Brush full implementation
//   Unit tests for AbrParser (ABRv6 computed brush).
//
//   Build a minimal valid ABRv6 binary in memory, verify parseBuffer extracts
//   the right fields, name (UTF-16 BE), size/hardness/spacing/etc.
//   Negative tests: bad magic, unsupported version, truncated body.

#include <QTest>
#include <QByteArray>

#include "../src/media/brushes/AbrParser.h"
#include "../src/media/brushes/BrushPreset.h"

#include <cstring>

using namespace brushes;

namespace {

// Helper: append a big-endian u16 to a byte array.
void appendU16BE(QByteArray &out, quint16 v) {
    out.append(char((v >> 8) & 0xff));
    out.append(char(v & 0xff));
}
void appendU32BE(QByteArray &out, quint32 v) {
    out.append(char((v >> 24) & 0xff));
    out.append(char((v >> 16) & 0xff));
    out.append(char((v >>  8) & 0xff));
    out.append(char(v & 0xff));
}
void appendUtf16BE(QByteArray &out, const QString &s) {
    const QChar *u = s.unicode();
    for (int i = 0; i < s.length(); ++i) {
        const char16_t c16 = u[i].unicode();
        out.append(char((c16 >> 8) & 0xff));
        out.append(char(c16 & 0xff));
    }
}

// Build a minimal ABRv6 buffer with one computed brush.
QByteArray buildSingleComputedBrushAbr(const QString &name, int spacing, int diameter, int hardness)
{
    QByteArray body;
    // u32 unknown1 = 1
    appendU32BE(body, 1);
    // u32 nameLen (incl 2-byte prefix) - little quirk: nameLen is count of UTF-16 code units, NOT bytes
    const int nameLen = name.length();
    appendU32BE(body, quint32(nameLen));
    appendUtf16BE(body, name);
    // u32 shortForm = 1 (short form: spacing + diameter + hardness only)
    appendU32BE(body, 1);
    // u32 spacing
    appendU32BE(body, quint32(spacing));
    // u32 diameter
    appendU32BE(body, quint32(diameter));
    // u32 hardness
    appendU32BE(body, quint32(hardness));

    QByteArray brush;
    // tag 'm' = 0x6D 0x00 0x00 0x00 (BE u32)
    appendU32BE(brush, 0x6D000000u);
    // u32 length of body
    appendU32BE(brush, quint32(body.size()));
    brush.append(body);

    QByteArray hdr;
    // magic 0x3842 BE
    appendU16BE(hdr, 0x3842);
    // version 6
    appendU16BE(hdr, 6);
    // u32 unknown 0x00000100
    appendU32BE(hdr, 0x00000100);
    // u32 count = 1
    appendU32BE(hdr, 1);

    return hdr + brush;
}

} // namespace

class tst_AbrParser : public QObject
{
    Q_OBJECT

private slots:
    void parses_single_computed_brush();
    void parses_multiple_brushes();
    void rejects_bad_magic();
    void rejects_unsupported_version();
    void handles_truncated_body();
    void round_trip_preset_name();
};

void tst_AbrParser::parses_single_computed_brush()
{
    QByteArray buf = buildSingleComputedBrushAbr(QStringLiteral("My Test Brush"), 30, 64, 75);
    auto r = AbrParser::parseBuffer(buf.constData(), buf.size(), QStringLiteral("test.abr"));
    QVERIFY2(r.ok, qPrintable(r.errorMessage));
    QCOMPARE(r.version, 6);
    QCOMPARE(r.presets.size(), 1);
    QCOMPARE(r.presets[0].name, QStringLiteral("My Test Brush"));
    QCOMPARE(r.presets[0].settings.spacing,  30);
    QCOMPARE(r.presets[0].settings.size,     64);
    QCOMPARE(r.presets[0].settings.hardness, 75);
    QCOMPARE(r.presets[0].sourcePath, QStringLiteral("test.abr"));
    QCOMPARE(r.presets[0].shape.type, ShapeType::Round);
}

void tst_AbrParser::parses_multiple_brushes()
{
    // Build header with count=3 (12 bytes total: u16+u16+u32+u32)
    QByteArray hdr;
    appendU16BE(hdr, 0x3842);
    appendU16BE(hdr, 6);
    appendU32BE(hdr, 0x00000100);
    appendU32BE(hdr, 3);

    // Build 3 individual brushes (each: tag 4 + length 4 + body N)
    QByteArray aBody;
    appendU32BE(aBody, 1);   // unknown1
    appendU32BE(aBody, 1);   // nameLen
    appendUtf16BE(aBody, QStringLiteral("A"));
    appendU32BE(aBody, 1);   // shortForm
    appendU32BE(aBody, 25);  // spacing
    appendU32BE(aBody, 32);  // diameter
    appendU32BE(aBody, 80);  // hardness
    QByteArray brushA;
    appendU32BE(brushA, 0x6D000000u);
    appendU32BE(brushA, quint32(aBody.size()));
    brushA.append(aBody);

    QByteArray bBody;
    appendU32BE(bBody, 1);
    appendU32BE(bBody, 1);
    appendUtf16BE(bBody, QStringLiteral("B"));
    appendU32BE(bBody, 1);
    appendU32BE(bBody, 15);
    appendU32BE(bBody, 16);
    appendU32BE(bBody, 50);
    QByteArray brushB;
    appendU32BE(brushB, 0x6D000000u);
    appendU32BE(brushB, quint32(bBody.size()));
    brushB.append(bBody);

    QByteArray cBody;
    appendU32BE(cBody, 1);
    appendU32BE(cBody, 1);
    appendUtf16BE(cBody, QStringLiteral("C"));
    appendU32BE(cBody, 1);
    appendU32BE(cBody, 35);
    appendU32BE(cBody, 100);
    appendU32BE(cBody, 90);
    QByteArray brushC;
    appendU32BE(brushC, 0x6D000000u);
    appendU32BE(brushC, quint32(cBody.size()));
    brushC.append(cBody);

    QByteArray buf = hdr + brushA + brushB + brushC;
    auto r = AbrParser::parseBuffer(buf.constData(), buf.size());
    QVERIFY2(r.ok, qPrintable(r.errorMessage));
    QCOMPARE(r.presets.size(), 3);
    QCOMPARE(r.presets[0].name, QStringLiteral("A"));
    QCOMPARE(r.presets[1].name, QStringLiteral("B"));
    QCOMPARE(r.presets[2].name, QStringLiteral("C"));
    QCOMPARE(r.presets[2].settings.size, 100);
    QCOMPARE(r.presets[2].settings.hardness, 90);
}

void tst_AbrParser::rejects_bad_magic()
{
    QByteArray buf;
    appendU16BE(buf, 0xDEAD);
    appendU16BE(buf, 6);
    appendU32BE(buf, 0);
    appendU32BE(buf, 0);
    auto r = AbrParser::parseBuffer(buf.constData(), buf.size());
    QVERIFY(!r.ok);
    QVERIFY(r.errorMessage.contains(QStringLiteral("not an ABR")));
}

void tst_AbrParser::rejects_unsupported_version()
{
    QByteArray buf;
    appendU16BE(buf, 0x3842);
    appendU16BE(buf, 3);   // too old
    appendU32BE(buf, 0);
    appendU32BE(buf, 0);
    auto r = AbrParser::parseBuffer(buf.constData(), buf.size());
    QVERIFY(!r.ok);
    QVERIFY(r.errorMessage.contains(QStringLiteral("unsupported")));
}

void tst_AbrParser::handles_truncated_body()
{
    // Header says count=1, but no body follows
    QByteArray buf;
    appendU16BE(buf, 0x3842);
    appendU16BE(buf, 6);
    appendU32BE(buf, 0);
    appendU32BE(buf, 1);
    auto r = AbrParser::parseBuffer(buf.constData(), buf.size());
    QVERIFY(!r.ok);
}

void tst_AbrParser::round_trip_preset_name()
{
    // Various characters including non-ASCII to validate UTF-16 BE decoding
    QByteArray buf = buildSingleComputedBrushAbr(QStringLiteral("画笔-12px"), 25, 12, 100);
    auto r = AbrParser::parseBuffer(buf.constData(), buf.size());
    QVERIFY2(r.ok, qPrintable(r.errorMessage));
    QCOMPARE(r.presets[0].name, QStringLiteral("画笔-12px"));
}

QTEST_MAIN(tst_AbrParser)
#include "tst_AbrParser.moc"