#pragma once

#include <QObject>
#include <QImage>
#include <QPoint>
#include <QRect>
#include <QSize>
#include <QVector>
#include <deque>
#include <memory>
#include <vector>

class PinModel;

// Owns every live pin's model plus the bounded "recover ring" of recently closed
// pins (close → recoverable by the paste hotkey; destroy → permanent), and the
// hide-all/show-all + magnetic-snap logic. Pure bookkeeping (Qt Core only, no
// widgets) → unit-tested (tst_recover_ring); the app layer connects added()/
// removed() to create/tear-down the actual PinWindows, keeping core UI-free. Stage 5.
class PinManager : public QObject {
    Q_OBJECT
public:
    explicit PinManager(QObject* parent = nullptr);
    ~PinManager() override;

    void setMaxRecoverable(int n) { m_maxRecover = qMax(0, n); }
    int  maxRecoverable() const { return m_maxRecover; }
    void setMagneticThreshold(int px) { m_magnetic = qMax(0, px); }
    int  magneticThreshold() const { return m_magnetic; }

    // Create + own a pin from an image (returns a borrowed pointer). Emits added().
    // sourceDpr>0 fixes the pin's logical size to image-px/dpr on every monitor (the
    // capture DPR for a snip); 0 leaves it to the display's dpr (clipboard images).
    PinModel* addImage(const QImage& image, const QString& sourceText = QString(),
                       qreal sourceDpr = 0.0);

    // Take ownership of an already-built model (e.g. restored from SessionStore,
    // with its transforms already applied) and surface it. Emits added(). Stage 6.
    PinModel* adopt(std::unique_ptr<PinModel> model);

    void close(PinModel* m);       // → recover ring (evicting the oldest past max)
    void destroy(PinModel* m);     // permanent
    PinModel* recoverOldest();     // bring back the oldest still-recoverable pin (or nullptr)

    int activeCount() const { return int(m_active.size()); }
    int recoverableCount() const { return int(m_ring.size()); }
    QVector<PinModel*> activePins() const;

    void setAllHidden(bool hidden);
    bool allHidden() const { return m_hidden; }

    // Snap a moving pin's proposed top-left to align its edges with nearby pins'
    // edges (within the magnetic threshold). `others` are the other pins' geometries.
    QPoint snapPosition(const QPoint& proposedTopLeft, const QSize& size,
                        const QVector<QRect>& others) const;

signals:
    void added(PinModel* m);       // app should create a window for this model
    void removed(PinModel* m);     // app should tear down this model's window
    void hiddenChanged(bool hidden);

private:
    int indexOfActive(PinModel* m) const;

    std::vector<std::unique_ptr<PinModel>> m_active;   // live pins
    std::deque<std::unique_ptr<PinModel>>  m_ring;     // closed, recoverable (bounded)
    int m_maxRecover = 10;
    int m_magnetic = 20;
    bool m_hidden = false;
};
