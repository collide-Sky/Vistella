// SPDX-License-Identifier: MIT
//
// IccProfile implementation - P0-8.3 (2026-09-15)
//
#include "IccProfile.h"
#include "logger.h"

#include <lcms2.h>

#include <QFile>
#include <QFileInfo>

namespace media {
namespace icc {

Profile::Profile(cmsHPROFILE h) : m_handle(h) {}

Profile::~Profile()
{
    if (m_handle) {
        cmsCloseProfile(m_handle);
        m_handle = nullptr;
    }
}

QSharedPointer<Profile> Profile::load(const QString& path, QString* err)
{
    if (!QFile::exists(path)) {
        if (err) *err = QStringLiteral("ICC profile file not found: %1").arg(path);
        return nullptr;
    }
    cmsHPROFILE h = cmsOpenProfileFromFile(path.toLocal8Bit().constData(), "r");
    if (!h) {
        if (err) *err = QStringLiteral("LCMS2 failed to open ICC profile: %1").arg(path);
        return nullptr;
    }
    auto p = QSharedPointer<Profile>(new Profile(h));
    LOG_INFO("[IccProfile] loaded: {} (desc='{}')",
             QFileInfo(path).fileName().toStdString(),
             p->description().toStdString());
    return p;
}

QSharedPointer<Profile> Profile::createSRgb()
{
    cmsHPROFILE h = cmsCreate_sRGBProfile();
    if (!h) {
        LOG_WARN("[IccProfile] createSRgb failed");
        return nullptr;
    }
    return QSharedPointer<Profile>(new Profile(h));
}

QSharedPointer<Profile> Profile::createAdobeRgb()
{
    // LCMS2 2.18 没有 cmsCreateAdobeRGBProfile (跟 2.9 之前不同)
    //   用 cmsCreateRGBProfile 手工构造 (Adobe RGB primaries + D65 white point)
    //   Adobe RGB 1998 primaries: R(0.64, 0.33) G(0.21, 0.71) B(0.15, 0.06)
    //   D65: (0.3127, 0.3290)
    cmsCIExyYTRIPLE adobeRgbPrimaries = {
        { 0.64, 0.33, 1.0 },   // R
        { 0.21, 0.71, 1.0 },   // G
        { 0.15, 0.06, 1.0 }    // B
    };
    cmsCIExyY d65WhitePoint = { 0.3127, 0.3290, 1.0 };
    cmsToneCurve* gamma22[3] = {
        cmsBuildGamma(nullptr, 2.2),
        cmsBuildGamma(nullptr, 2.2),
        cmsBuildGamma(nullptr, 2.2)
    };
    cmsHPROFILE h = cmsCreateRGBProfile(
        &d65WhitePoint, &adobeRgbPrimaries, gamma22);
    cmsFreeToneCurve(gamma22[0]);
    cmsFreeToneCurve(gamma22[1]);
    cmsFreeToneCurve(gamma22[2]);
    if (!h) {
        LOG_WARN("[IccProfile] createAdobeRgb failed");
        return nullptr;
    }
    return QSharedPointer<Profile>(new Profile(h));
}

QString Profile::description() const
{
    if (!m_handle) return QString();
    wchar_t buf[256] = {0};
    cmsGetProfileInfo(m_handle, cmsInfoDescription,
                      "en", "US", buf, sizeof(buf));
    return QString::fromWCharArray(buf);
}

QString Profile::manufacturer() const
{
    if (!m_handle) return QString();
    wchar_t buf[256] = {0};
    cmsGetProfileInfo(m_handle, cmsInfoManufacturer,
                      "en", "US", buf, sizeof(buf));
    return QString::fromWCharArray(buf);
}

QString Profile::model() const
{
    if (!m_handle) return QString();
    wchar_t buf[256] = {0};
    cmsGetProfileInfo(m_handle, cmsInfoModel,
                      "en", "US", buf, sizeof(buf));
    return QString::fromWCharArray(buf);
}

QString Profile::copyright() const
{
    if (!m_handle) return QString();
    wchar_t buf[256] = {0};
    cmsGetProfileInfo(m_handle, cmsInfoCopyright,
                      "en", "US", buf, sizeof(buf));
    return QString::fromWCharArray(buf);
}

QByteArray Profile::toByteArray() const
{
    if (!m_handle) return QByteArray();
    // LCMS2 用 cmsGetProfileInfoSerialNumber / cmsSaveProfileToMem
    cmsUInt32Number bytesNeeded = 0;
    cmsSaveProfileToMem(m_handle, nullptr, &bytesNeeded);
    if (bytesNeeded == 0) return QByteArray();
    QByteArray buf(static_cast<int>(bytesNeeded), '\0');
    if (!cmsSaveProfileToMem(m_handle, buf.data(), &bytesNeeded)) {
        return QByteArray();
    }
    buf.resize(static_cast<int>(bytesNeeded));
    return buf;
}

void Profile::applyTo(cv::Mat& img) const
{
    if (!m_handle || img.empty() || img.depth() != CV_8U) return;

    // OpenCV 默认 BGR (3ch) / BGRA (4ch). 转换: BGR -> RGB -> profile
    // source: sRGB built-in
    cmsHPROFILE srcProfile = cmsCreate_sRGBProfile();
    cmsHTRANSFORM xform = cmsCreateTransform(
        srcProfile, (img.channels() == 3) ? TYPE_BGR_8 : TYPE_BGRA_8,
        m_handle,   (img.channels() == 3) ? TYPE_BGR_8 : TYPE_BGRA_8,
        INTENT_PERCEPTUAL, 0);
    if (!xform) {
        cmsCloseProfile(srcProfile);
        LOG_WARN("[IccProfile] applyTo: cmsCreateTransform failed");
        return;
    }
    // LCMS2 transform 整图 in-place
    //   cmsDoTransformLineStride 签名: PixelsPerLine, LineCount, BytesPerLineIn/Out, BytesPerPlaneIn/Out
    //   BGR/BGRA 8bit: BytesPerLine = cols * channels, BytesPerPlane = cols
    const cmsUInt32Number channels = static_cast<cmsUInt32Number>(img.channels());
    const cmsUInt32Number bytesPerLine = static_cast<cmsUInt32Number>(img.cols * channels);
    cmsDoTransformLineStride(xform, img.data, img.data,
                             static_cast<cmsUInt32Number>(img.cols),
                             static_cast<cmsUInt32Number>(img.rows),
                             bytesPerLine, bytesPerLine,
                             bytesPerLine, bytesPerLine);
    cmsDeleteTransform(xform);
    cmsCloseProfile(srcProfile);
}

} // namespace icc
} // namespace media