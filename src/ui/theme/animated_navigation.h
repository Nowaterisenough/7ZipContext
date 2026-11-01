#pragma once

#include <QFocusEvent>
#include <QFrame>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPointer>
#include <QPushButton>
#include "magpie_motion.h"
#include "typography.h"

namespace desktop {
class NavigationButton final : public QPushButton {
public:
    explicit NavigationButton(const QString &text, bool child, QWidget *parent = nullptr)
        : QPushButton(text, parent), child_(child), color_(IdleColor()), colorAnimation_(this), pressAnimation_(this) {
        setMouseTracking(true);
        setCursor(Qt::ArrowCursor);
        connect(&colorAnimation_, &QVariantAnimation::valueChanged, this, [this](const QVariant &value) { color_ = value.value<QColor>(); update(); });
        connect(&pressAnimation_, &QVariantAnimation::valueChanged, this, [this](const QVariant &value) { scale_ = value.toReal(); update(); });
        connect(this, &QPushButton::toggled, this, [this] { UpdateColor(); });
        connect(this, &QPushButton::pressed, this, [this] { UpdatePress(); });
        connect(this, &QPushButton::released, this, [this] { UpdatePress(); });
    }
    qreal PressScale() const { return scale_; }
    bool KeyboardFocusVisible() const { return keyboardFocus_ && hasFocus(); }
    QSize sizeHint() const override {
        auto f = type::Navigation(child_, theme::UsesTrayPalette(this), false);
        return {QFontMetrics(f).horizontalAdvance(text()) + (icon().isNull() ? 0 : 24) + (child_ ? 16 : 27), child_ ? 28 : 30};
    }
protected:
    void showEvent(QShowEvent *event) override {
        QPushButton::showEvent(event);
        colorAnimation_.stop();
        color_ = isChecked() ? QColor(theme::Text) : IdleColor();
        update();
    }
    void paintEvent(QPaintEvent *) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        if (KeyboardFocusVisible()) {
            p.setPen(QPen(theme::AccentColor(this), 1)); p.setBrush(Qt::NoBrush);
            p.drawRoundedRect(QRectF(rect()).adjusted(1, 2, -1, -2), 5, 5);
        }
        p.translate(QRectF(rect()).center()); p.scale(scale_, scale_); p.translate(-QRectF(rect()).center());
        auto f = type::Navigation(child_, theme::UsesTrayPalette(this), isChecked()); p.setFont(f);
        const int textWidth = QFontMetrics(f).horizontalAdvance(text());
        const int iconWidth = icon().isNull() ? 0 : 24;
        const int x = (width() - textWidth - iconWidth) / 2;
        const QColor ink = isEnabled() ? color_ : QColor(theme::Muted);
        if (iconWidth) {
            auto image = icon().pixmap(QSize(18, 18), devicePixelRatioF());
            QPainter tint(&image); tint.setCompositionMode(QPainter::CompositionMode_SourceIn); tint.fillRect(image.rect(), ink); tint.end();
            p.drawPixmap(QRect(x, (height() - 18) / 2, 18, 18), image);
        }
        p.setPen(ink);
        p.drawText(QRect(x + iconWidth, 0, textWidth, height()), Qt::AlignCenter, text());
    }
    void enterEvent(QEnterEvent *event) override { hovered_ = true; UpdateColor(); QPushButton::enterEvent(event); }
    void leaveEvent(QEvent *event) override { hovered_ = false; UpdateColor(); QPushButton::leaveEvent(event); }
    void mousePressEvent(QMouseEvent *event) override { keyboardFocus_ = false; QPushButton::mousePressEvent(event); UpdatePress(); }
    void mouseMoveEvent(QMouseEvent *event) override { QPushButton::mouseMoveEvent(event); UpdatePress(); }
    void mouseReleaseEvent(QMouseEvent *event) override { QPushButton::mouseReleaseEvent(event); UpdatePress(); }
    void focusInEvent(QFocusEvent *event) override {
        keyboardFocus_ = event->reason() == Qt::TabFocusReason || event->reason() == Qt::BacktabFocusReason || event->reason() == Qt::ShortcutFocusReason;
        QPushButton::focusInEvent(event); update();
    }
    void keyPressEvent(QKeyEvent *event) override { keyboardFocus_ = true; QPushButton::keyPressEvent(event); UpdatePress(); }
    void keyReleaseEvent(QKeyEvent *event) override { QPushButton::keyReleaseEvent(event); UpdatePress(); }
private:
    QColor IdleColor() const { return child_ || theme::UsesTrayPalette(this) ? theme::MutedColor(this) : QColor("#a1a1aa"); }
    void UpdateColor() { motion::Animate(colorAnimation_, color_, isChecked() ? QColor(theme::Text) : hovered_ ? QColor("#b9b9c1") : IdleColor(), motion::TextMs); }
    void UpdatePress() {
        const qreal target = isDown() ? motion::PressScale : 1;
        if (pressAnimation_.endValue().isValid() && pressAnimation_.endValue().toReal() == target) return;
        motion::Animate(pressAnimation_, scale_, target, motion::PressMs);
    }
    bool child_, hovered_ = false, keyboardFocus_ = false;
    QColor color_;
    qreal scale_ = 1;
    QVariantAnimation colorAnimation_, pressAnimation_;
};

class NavigationStrip final : public QFrame {
public:
    explicit NavigationStrip(bool header, QWidget *parent = nullptr) : QFrame(parent), header_(header), animation_(this) {
        connect(&animation_, &QVariantAnimation::valueChanged, this, [this](const QVariant &value) { indicator_ = value.toRectF(); update(); });
    }
    void AddButton(QPushButton *button) {
        button->installEventFilter(this);
        connect(button, &QPushButton::toggled, this, [this, button](bool checked) {
            if (!checked) return;
            const auto from = IndicatorRect();
            const bool sameButton = selected_ == button;
            selected_ = button;
            const auto to = Target();
            if (sameButton && animation_.state() == QAbstractAnimation::Running && animation_.endValue().toRectF() == to) return;
            if (!isVisible() || from.isEmpty() || from == to) Snap();
            else motion::Animate(animation_, from, to, motion::SlideMs);
            update();
        });
    }
    QRectF IndicatorRect() const { return animation_.state() == QAbstractAnimation::Running ? indicator_ : Target(); }
    bool SelectionAnimating() const { return animation_.state() == QAbstractAnimation::Running; }
    void SetPlain(bool plain) { plain_ = plain; update(); }
    void ClearSelection() { animation_.stop(); selected_.clear(); indicator_ = {}; update(); }
protected:
    void paintEvent(QPaintEvent *) override {
        QPainter p(this); p.setRenderHint(QPainter::Antialiasing);
        if (!header_ && !plain_) { p.setPen(Qt::NoPen); p.setBrush(theme::RaisedColor(this)); p.drawRoundedRect(QRectF(rect()), 7, 7); }
        if (!IndicatorRect().isEmpty()) {
            if (plain_) {
                p.setPen(Qt::NoPen); p.setBrush(theme::RaisedColor(this));
                p.drawRoundedRect(IndicatorRect(), 6, 6);
            } else motion::Thumb(p, IndicatorRect(), header_, this);
        }
    }
    void resizeEvent(QResizeEvent *event) override { QFrame::resizeEvent(event); Snap(); }
    void showEvent(QShowEvent *event) override { QFrame::showEvent(event); Snap(); }
    void hideEvent(QHideEvent *event) override { QFrame::hideEvent(event); Snap(); }
    bool eventFilter(QObject *object, QEvent *event) override {
        if (object == selected_ && (event->type() == QEvent::Move || event->type() == QEvent::Resize)) Snap();
        return QFrame::eventFilter(object, event);
    }
private:
    QRectF Target() const { return selected_ ? QRectF(selected_->geometry()).adjusted(0, 2, 0, -2) : QRectF{}; }
    void Snap() { animation_.stop(); indicator_ = Target(); update(); }
    bool header_, plain_ = false;
    QPointer<QPushButton> selected_;
    QRectF indicator_;
    QVariantAnimation animation_;
};
} // namespace desktop
