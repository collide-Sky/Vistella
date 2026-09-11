// SPDX-License-Identifier: MIT
//
// DialogFactory implementation - F-K (2026-09-09)
//
#include "DialogFactory.h"
#include "AdjustDialogBase.h"
#include "HslAdjustDialog.h"
#include "logger.h"

#include <QLabel>
#include <QVBoxLayout>

namespace dialogs {

// ---- 4 个 stub (F-K.3 待实装) ----
//   AdjustDialogBase 是抽象类 (pure virtual setupUi), 所以 stub 必须 override
namespace {
class CurvesStub : public AdjustDialogBase {
public:
    explicit CurvesStub(const QVariantMap& args, QWidget* parent)
        : AdjustDialogBase("Curves (F-K.3 stub)", parent)
    {
        setInitialArgs(args);
        init();  // F-K (2026-09-09): base ctor 不调 setupUi, derived 调 init() 触发
    }
protected:
    void setupUi(QVBoxLayout* body) override {
        body->addWidget(new QLabel("Curves (F-K.3 stub, TODO: 实现曲线图)", this));
    }
};
class LevelsStub : public AdjustDialogBase {
public:
    explicit LevelsStub(const QVariantMap& args, QWidget* parent)
        : AdjustDialogBase("Levels (F-K.3 stub)", parent)
    {
        setInitialArgs(args);
        init();
    }
protected:
    void setupUi(QVBoxLayout* body) override {
        body->addWidget(new QLabel("Levels (F-K.3 stub, TODO: 实现直方图+滑块)", this));
    }
};
class BnWStub : public AdjustDialogBase {
public:
    explicit BnWStub(const QVariantMap& args, QWidget* parent)
        : AdjustDialogBase("B&W (F-K.3 stub)", parent)
    {
        setInitialArgs(args);
        init();
    }
protected:
    void setupUi(QVBoxLayout* body) override {
        body->addWidget(new QLabel("B&W (F-K.3 stub, TODO: 实现黑白调整)", this));
    }
};
class ChannelMixerStub : public AdjustDialogBase {
public:
    explicit ChannelMixerStub(const QVariantMap& args, QWidget* parent)
        : AdjustDialogBase("ChannelMixer (F-K.3 stub)", parent)
    {
        setInitialArgs(args);
        init();
    }
protected:
    void setupUi(QVBoxLayout* body) override {
        body->addWidget(new QLabel("ChannelMixer (F-K.3 stub, TODO: 实现通道混合)", this));
    }
};
}

static AdjustDialogBase* createCurvesStub(const QVariantMap& args, QWidget* parent) {
    return new CurvesStub(args, parent);
}
static AdjustDialogBase* createLevelsStub(const QVariantMap& args, QWidget* parent) {
    return new LevelsStub(args, parent);
}
static AdjustDialogBase* createBnWStub(const QVariantMap& args, QWidget* parent) {
    return new BnWStub(args, parent);
}
static AdjustDialogBase* createChannelMixerStub(const QVariantMap& args, QWidget* parent) {
    return new ChannelMixerStub(args, parent);
}

QHash<QString, AdjustDialogBase*(*)(const QVariantMap&, QWidget*)>& DialogFactory::creators()
{
    static QHash<QString, AdjustDialogBase*(*)(const QVariantMap&, QWidget*)> map = {
        {"HSL",          &HslAdjustDialog::create},
        {"Curves",       &createCurvesStub},
        {"Levels",       &createLevelsStub},
        {"B&W",          &createBnWStub},
        {"ChannelMixer", &createChannelMixerStub},
    };
    return map;
}

AdjustDialogBase* DialogFactory::create(const QString& dialogId, const QVariantMap& args, QWidget* parent)
{
    auto& map = creators();
    auto it = map.find(dialogId);
    if (it == map.end()) {
        LOG_WARN("[DialogFactory] unknown dialogId: {}", dialogId.toStdString());
        return nullptr;
    }
    AdjustDialogBase* dlg = it.value()(args, parent);
    if (dlg) dlg->setInitialArgs(args);
    return dlg;
}

QStringList DialogFactory::knownIds()
{
    auto& map = creators();
    QStringList ids;
    ids.reserve(map.size());
    for (auto it = map.begin(); it != map.end(); ++it) {
        ids << it.key();
    }
    return ids;
}

} // namespace dialogs
