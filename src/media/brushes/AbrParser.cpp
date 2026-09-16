// SPDX-License-Identifier: MIT
//
// AbrParser impl - P1.1 (2026-09-15)

#include "AbrParser.h"

#include <QByteArray>
#include <QFile>
#include <QFileInfo>

#include <cstring>

namespace brushes {

namespace {

class BitReader {
public:
    BitReader(const char *data, int size)
        : m_data(reinterpret_cast<const uchar*>(data)), m_size(size), m_pos(0) {}

    bool    eof()       const { return m_pos >= m_size; }
    int     remaining() const { return m_size - m_pos; }
    qint64  pos()       const { return m_pos; }
    void    seek(qint64 p) { m_pos = p; }
    void    skip(int n) { m_pos += n; }
    // Peek 1 byte at offset n from current position (no advance).
    uchar   peek(int n = 0) const { return m_data[m_pos + n]; }

    // Big-endian readers
    bool readU16(quint16 &out) {
        if (remaining() < 2) return false;
        out = (quint16(m_data[m_pos]) << 8) | quint16(m_data[m_pos+1]);
        m_pos += 2;
        return true;
    }
    bool readU32(quint32 &out) {
        if (remaining() < 4) return false;
        out = (quint32(m_data[m_pos])   << 24) | (quint32(m_data[m_pos+1]) << 16)
            | (quint32(m_data[m_pos+2]) << 8 ) |  quint32(m_data[m_pos+3]);
        m_pos += 4;
        return true;
    }
    // Little-endian readers (ABRv9 descriptors)
    bool readU16LE(quint16 &out) {
        if (remaining() < 2) return false;
        out = quint16(m_data[m_pos]) | (quint16(m_data[m_pos+1]) << 8);
        m_pos += 2;
        return true;
    }
    bool readU32LE(quint32 &out) {
        if (remaining() < 4) return false;
        out = quint32(m_data[m_pos]) | (quint32(m_data[m_pos+1]) << 8)
            | (quint32(m_data[m_pos+2]) << 16) | (quint32(m_data[m_pos+3]) << 24);
        m_pos += 4;
        return true;
    }

    bool readBytes(int n, QByteArray &out) {
        if (remaining() < n) return false;
        out = QByteArray(reinterpret_cast<const char*>(m_data + m_pos), n);
        m_pos += n;
        return true;
    }

private:
    const uchar* m_data;
    int          m_size;
    qint64       m_pos;
};

bool parseComputedV6(BitReader &r, BrushPreset &out, QString &err) {
    // Skip u32 unknown1
    quint32 unk1 = 0;
    if (!r.readU32(unk1)) { err = QStringLiteral("EOF before unknown1"); return false; }

    // Name length (u32 big-endian): number of UTF-16 code units (chars).
    quint32 nameLen = 0;
    if (!r.readU32(nameLen)) { err = QStringLiteral("EOF before nameLen"); return false; }
    if (nameLen < 1 || nameLen > 1000) { err = QStringLiteral("invalid nameLen"); return false; }

    // Read UTF-16 BE chars explicitly (don't use QString::fromUtf16 which uses native endianness).
    QString name;
    name.reserve(int(nameLen));
    for (quint32 i = 0; i < nameLen; ++i) {
        if (r.remaining() < 2) { err = QStringLiteral("EOF in name"); return false; }
        const quint16 c = (quint16(r.peek(0)) << 8) | quint16(r.peek(1));
        r.skip(2);
        name.append(QChar(c));
    }
    out.name = name;

    // short form flag
    quint32 shortForm = 0;
    if (!r.readU32(shortForm)) { err = QStringLiteral("EOF before shortForm"); return false; }

    // spacing, diameter, hardness
    quint32 spacing = 25, diameter = 32, hardness = 80;
    if (!r.readU32(spacing)) return false;
    if (!r.readU32(diameter)) return false;
    if (!r.readU32(hardness)) return false;

    out.settings.spacing  = std::min(1000, int(spacing));
    out.settings.size     = std::max(1, std::min(2000, int(diameter)));
    out.settings.hardness = std::min(100, int(hardness));

    // Long form: angle, roundness, etc.
    if (!shortForm) {
        quint32 angle = 0, roundness = 100;
        if (!r.readU32(angle)) return false;
        if (!r.readU32(roundness)) return false;
        out.settings.angle     = std::min(359, int(angle));
        out.settings.roundness = std::min(100, int(roundness));
    }
    return true;
}

} // namespace

AbrParser::Result AbrParser::parseFile(const QString &filePath)
{
    Result res;
    QFile f(filePath);
    if (!f.open(QIODevice::ReadOnly)) {
        res.errorMessage = f.errorString();
        return res;
    }
    QByteArray buf = f.readAll();
    f.close();
    QFileInfo fi(filePath);
    Result parsed = parseBuffer(buf.constData(), buf.size(), fi.fileName());
    parsed.presets;  // copy
    return parsed;
}

AbrParser::Result AbrParser::parseBuffer(const char *data, int size, const QString &sourceName)
{
    Result res;
    BitReader r(data, size);

    // Magic (u16 BE 0x3842)
    quint16 magic = 0, version = 0;
    if (!r.readU16(magic) || magic != 0x3842) {
        res.errorMessage = QStringLiteral("not an ABR file (magic 0x%1)")
                                .arg(magic, 4, 16, QLatin1Char('0'));
        return res;
    }
    if (!r.readU16(version)) {
        res.errorMessage = QStringLiteral("EOF reading version");
        return res;
    }
    if (version != 6 && version != 7 && version != 9) {
        res.errorMessage = QStringLiteral("unsupported ABR version: %1 (only 6/7/9)").arg(version);
        return res;
    }
    res.version = int(version);

    // u32 unknown + u32 count
    quint32 count = 0;
    r.readU32(count);  // unknown -- skip result
    if (!r.readU32(count)) {
        res.errorMessage = QStringLiteral("EOF reading brush count");
        return res;
    }
    if (count > 10000) {
        res.errorMessage = QStringLiteral("brush count too large: %1").arg(count);
        return res;
    }

    // For ABRv9 the body is little-endian descriptor; we only support v6/v7 here.
    if (version == 9) {
        res.errorMessage = QStringLiteral("ABRv9 parsing not implemented (P1.1 follow-up); use ABRv6 files");
        return res;
    }

    for (quint32 i = 0; i < count; ++i) {
        quint32 tag = 0, length = 0;
        if (!r.readU32(tag)) {
            res.errorMessage = QStringLiteral("EOF at brush %1 tag").arg(i);
            return res;
        }
        if (!r.readU32(length)) {
            res.errorMessage = QStringLiteral("EOF at brush %1 length").arg(i);
            return res;
        }
        const qint64 brushStart = r.pos();
        const qint64 brushEnd = brushStart + length;

        if (tag == 0x6D000000u || tag == 0x0000006Du) {
            // 'm' computed brush
            BrushPreset p;
            QString perr;
            if (!parseComputedV6(r, p, perr)) {
                res.errorMessage = QStringLiteral("brush %1: %2").arg(i).arg(perr);
                return res;
            }
            p.shape.type = ShapeType::Round;
            p.sourcePath = sourceName;
            res.presets.append(p);
        } else if (tag == 0x74000000u || tag == 0x00000074u) {
            // 't' sampled brush -- skip for now
            r.seek(brushEnd);
        } else {
            // Unknown tag, skip
            r.seek(brushEnd);
        }
        // Safety: enforce end position
        if (r.pos() < brushEnd) r.seek(brushEnd);
        else if (r.pos() > brushEnd) {
            res.errorMessage = QStringLiteral("brush %1 overread").arg(i);
            return res;
        }
    }

    res.ok = true;
    return res;
}

} // namespace brushes