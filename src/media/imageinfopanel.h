#ifndef IMAGEINFOPANEL_H
#define IMAGEINFOPANEL_H

#include <QPoint>
#include <QWidget>

#include <opencv2/core.hpp>

QT_BEGIN_NAMESPACE
namespace Ui { class ImageInfoPanel; }
QT_END_NAMESPACE

class ImageInfoPanel : public QWidget
{
    Q_OBJECT
public:
    explicit ImageInfoPanel(QWidget *parent = nullptr);
    ~ImageInfoPanel() override;

    // 把当前图像信息刷新到面板上
    void updateInfo(const cv::Mat &img, const QString &filePath = QString());

    // 鼠标 hover 时更新像素信息 (原图坐标, 由 ImageWindow 转发)
    void updatePixel(const cv::Mat &img, const QPoint &pos);

public slots:
    // 主题变化时刷新样式
    void applyTheme();

private:
    Ui::ImageInfoPanel *ui;
    cv::Mat m_currentImg;   // 缓存最近一次图像, 让 updatePixel 能用
};

#endif // IMAGEINFOPANEL_H
