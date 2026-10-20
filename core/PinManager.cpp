#include "core/PinManager.h"
#include "core/PinModel.h"

#include <QtGlobal>
#include <QPair>
#include <cstdlib>

PinManager::PinManager(QObject* parent) : QObject(parent) {}
PinManager::~PinManager() = default;

int PinManager::indexOfActive(PinModel* m) const
{
    for (int i = 0; i < int(m_active.size()); ++i)
        if (m_active[i].get() == m)
            return i;
    return -1;
}

QVector<PinModel*> PinManager::activePins() const
{
    QVector<PinModel*> v;
    v.reserve(int(m_active.size()));
    for (const auto& p : m_active)
        v.append(p.get());
    return v;
}

PinModel* PinManager::addImage(const QImage& image, const QString& sourceText, qreal sourceDpr)
{
    if (image.isNull())
        return nullptr;
    setAllHidden(false);             // a new pin is shown → keep "all hidden" state consistent
    m_active.push_back(std::make_unique<PinModel>(image, sourceText));
    PinModel* m = m_active.back().get();
    m->setSourceDpr(sourceDpr);      // before added(): onPinAdded() sizes the window synchronously
    emit added(m);
    return m;
}

PinModel* PinManager::adopt(std::unique_ptr<PinModel> model)
{
    if (!model)
        return nullptr;
    setAllHidden(false);
    m_active.push_back(std::move(model));
    PinModel* m = m_active.back().get();
    emit added(m);
    return m;
}

void PinManager::close(PinModel* m)
{
    const int idx = indexOfActive(m);
    if (idx < 0)
        return;
    emit removed(m);                                   // tear down the window first
    if (m_maxRecover > 0) {
        m_ring.push_back(std::move(m_active[idx]));    // keep the model recoverable
        while (int(m_ring.size()) > m_maxRecover)
            m_ring.pop_front();                        // evict oldest (permanently)
    }
    m_active.erase(m_active.begin() + idx);            // (unique_ptr already moved out / freed)
}

void PinManager::destroy(PinModel* m)
{
    const int idx = indexOfActive(m);
    if (idx < 0)
        return;
    emit removed(m);
    m_active.erase(m_active.begin() + idx);            // permanent — never enters the ring
}

PinModel* PinManager::recoverOldest()
{
    if (m_ring.empty())
        return nullptr;
    setAllHidden(false);
    m_active.push_back(std::move(m_ring.front()));     // oldest-closed comes back first
    m_ring.pop_front();
    PinModel* m = m_active.back().get();
    emit added(m);
    return m;
}

void PinManager::setAllHidden(bool hidden)
{
    if (m_hidden == hidden)
        return;
    m_hidden = hidden;
    emit hiddenChanged(hidden);
}

namespace {
// Best single-axis snap: try aligning each moving edge (lo/hi) to each other edge.
// Returns the adjusted `lo` (top or left) and whether a snap occurred.
int snapAxis(int lo, int extent, const QVector<QPair<int, int>>& otherEdges, int threshold)
{
    const int hi = lo + extent;
    int bestDelta = threshold + 1;
    int bestLo = lo;
    for (const auto& e : otherEdges) {
        for (int oedge : {e.first, e.second}) {
            for (int medge : {lo, hi}) {
                const int d = std::abs(medge - oedge);
                if (d < bestDelta) {
                    bestDelta = d;
                    bestLo = lo + (oedge - medge);
                }
            }
        }
    }
    return bestDelta <= threshold ? bestLo : lo;
}
} // namespace

QPoint PinManager::snapPosition(const QPoint& proposedTopLeft, const QSize& size,
                                const QVector<QRect>& others) const
{
    if (m_magnetic <= 0 || others.isEmpty())
        return proposedTopLeft;

    QVector<QPair<int, int>> xEdges, yEdges;
    xEdges.reserve(others.size());
    yEdges.reserve(others.size());
    for (const QRect& r : others) {
        xEdges.append({r.left(), r.right() + 1});      // right()+1 = exclusive edge
        yEdges.append({r.top(),  r.bottom() + 1});
    }
    const int x = snapAxis(proposedTopLeft.x(), size.width(),  xEdges, m_magnetic);
    const int y = snapAxis(proposedTopLeft.y(), size.height(), yEdges, m_magnetic);
    return QPoint(x, y);
}
