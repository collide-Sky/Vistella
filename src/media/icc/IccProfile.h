// SPDX-License-Identifier: MIT
//
// IccProfile - P0-8.3 (2026-09-15)
//
// LCMS2-based ICC profile wrapper:
//   - load .icc / .icm 文件
//   - 内置 profile: sRGB / Adobe RGB
//   - toByteArray() 给 PNG/JPEG/TIFF embed 用
//   - applyTo(cv::Mat) 做色彩空间转换 (sRGB -> profile)
//
// LCMS2 (Little CMS) 是 MIT-licensed color management engine
//   vendored as third_party/lcms2-2.18
//
#pragma once

#include <QString>
#include <QByteArray>
#include <QSharedPointer>
#include <opencv2/core.hpp>

#include <lcms2.h>

namespace media {
namespace icc {

class Profile
{
public:
    // 加载 .icc / .icm 文件 (PS 同款 File > Save For Web > ICC profile)
    static QSharedPointer<Profile> load(const QString& path, QString* err = nullptr);

    // LCMS2 内置 profile (不需要文件, 直接生成)
    static QSharedPointer<Profile> createSRgb();
    static QSharedPointer<Profile> createAdobeRgb();

    ~Profile();

    Profile(const Profile&) = delete;
    Profile& operator=(const Profile&) = delete;

    // Profile 信息 (LCMS2 GetProfileInfo)
    QString description() const;
    QString manufacturer() const;
    QString model() const;
    QString copyright() const;

    // raw bytes (PNG iCCP / JPEG APP2 / TIFF ICCProfile tag 用)
    QByteArray toByteArray() const;

    // 把 cv::Mat sRGB -> 此 profile (in-place 像素转换)
    //   8UC3 BGR (OpenCV 默认) -> RGB -> profile space
    //   8UC4 BGRA 同样处理
    void applyTo(cv::Mat& img) const;

    // LCMS2 handle (给 export embed 用)
    cmsHPROFILE handle() const { return m_handle; }

private:
    explicit Profile(cmsHPROFILE h);
    cmsHPROFILE m_handle = nullptr;
};

} // namespace icc
} // namespace media