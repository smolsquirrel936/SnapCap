#include "ui/WheelGuard.h"

#include <QAbstractSpinBox>
#include <QComboBox>
#include <QEvent>
#include <QFocusEvent>
#include <QPointer>
#include <QWidget>

namespace {

// One filter object shared by every guarded widget in the process; it only ever
// remembers which single widget is currently "armed" (the last one clicked).
class Guard : public QObject
{
public:
    static Guard* instance()
    {
        static Guard g;
        return &g;
    }

protected:
    bool eventFilter(QObject* obj, QEvent* ev) override
    {
        switch (ev->type()) {
        case QEvent::FocusIn:
            // Arm on a click only. A spin box's own line edit swallows the mouse
            // press, so watching for MouseButtonPress here would miss clicks in the
            // text field — but the line edit's focus proxy is the spin box, so the
            // click always surfaces as FocusIn/MouseFocusReason. Tab focus arrives
            // as TabFocusReason and deliberately does not arm.
            if (static_cast<QFocusEvent*>(ev)->reason() == Qt::MouseFocusReason)
                m_armed = obj;
            break;
        case QEvent::FocusOut:
            // A combo box loses focus to its own drop-down; that must not disarm it.
            if (obj == m_armed && static_cast<QFocusEvent*>(ev)->reason() != Qt::PopupFocusReason)
                m_armed = nullptr;
            break;
        case QEvent::Wheel:
            if (obj != m_armed) {
                // ignore() + true: we swallow it here, but leaving the event
                // unaccepted makes QApplication propagate it up the parent chain,
                // so an enclosing QScrollArea still scrolls the page.
                ev->ignore();
                return true;
            }
            break;
        default:
            break;
        }
        return QObject::eventFilter(obj, ev);
    }

private:
    QPointer<QObject> m_armed;
};

void guard(QWidget* w)
{
    // Both classes default to Qt::WheelFocus: without this a wheel could focus
    // the widget on its own, which is exactly what we are trying to prevent.
    w->setFocusPolicy(Qt::StrongFocus);
    w->installEventFilter(Guard::instance());
}

}   // namespace

namespace WheelGuard {

void install(QWidget* root)
{
    for (QAbstractSpinBox* sb : root->findChildren<QAbstractSpinBox*>())
        guard(sb);
    for (QComboBox* cb : root->findChildren<QComboBox*>())
        guard(cb);
}

}   // namespace WheelGuard
