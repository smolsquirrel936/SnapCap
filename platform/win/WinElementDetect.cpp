#include "platform/win/WinElementDetect.h"
#include "platform/win/WinMonitorTable.h"

#ifndef WIN32_LEAN_AND_MEAN
#  define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#  define NOMINMAX
#endif
#include <windows.h>
#include <objbase.h>
#include <dwmapi.h>
#include <uiautomation.h>
#include <oleacc.h>

#include <QByteArray>
#include <QDebug>
#include <QElapsedTimer>
#include <QVector>

namespace {

// UIAutomation COM identifiers, declared locally so we don't depend on the
// platform uuid import library being present in the MinGW kit.
const GUID kCLSID_CUIAutomation =
    {0xff48dba4, 0x60ef, 0x4201, {0xaa, 0x87, 0x54, 0x10, 0x3e, 0xef, 0x59, 0x4e}};
const GUID kIID_IUIAutomation =
    {0x30cbe57d, 0xd9d0, 0x452a, {0xab, 0x13, 0x7a, 0xc5, 0xac, 0x48, 0x25, 0xee}};
const GUID kIID_IAccessible =
    {0x618736e0, 0x3c3d, 0x11cf, {0x81, 0x0c, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71}};

// DIAGNOSTIC (temporary, browser element-selection investigation): set
// SNAPCAP_ELEMENT_DIAG=1 to log the UIA descent path plus two comparison
// strategies (Raw View / MSAA) for every hover. Off by default so normal runs
// pay nothing. Geometry only — never element names, values, URLs or page text.
bool diagEnabled()
{
    static const bool on = !qgetenv("SNAPCAP_ELEMENT_DIAG").isEmpty()
                           && qgetenv("SNAPCAP_ELEMENT_DIAG") != "0";
    return on;
}

QString fmt(const RECT& r)
{
    return QStringLiteral("[%1,%2 %3x%4]").arg(r.left).arg(r.top)
               .arg(r.right - r.left).arg(r.bottom - r.top);
}

// The QScreen<->HMONITOR pairing + logical<->physical mapping lives in the shared
// winmon:: table (platform/win/WinMonitorTable) so it isn't duplicated here.
bool rectContains(const RECT& r, long x, long y)
{
    return x >= r.left && x < r.right && y >= r.top && y < r.bottom;
}

// Visible frame of a top-level window, preferring the DWM extended frame bounds
// (excludes the invisible resize border / drop shadow) over GetWindowRect.
RECT visibleFrame(HWND h)
{
    RECT r{};
    if (SUCCEEDED(DwmGetWindowAttribute(h, DWMWA_EXTENDED_FRAME_BOUNDS, &r, sizeof(r)))
        && r.right > r.left && r.bottom > r.top)
        return r;
    GetWindowRect(h, &r);
    return r;
}

// --- Top-level window Z-order walk (windowAt) ------------------------------
struct WalkCtx { POINT pt; DWORD ownPid; HWND found; };

// Is `h` a real, on-screen, foreign top-level window? Shared skip policy for the
// Z-order hit test and for platform::foregroundWindowRect(), so both agree on what
// counts as a window. `out` receives its physical GetWindowRect.
bool capturableWindow(HWND h, DWORD ownPid, RECT& out)
{
    if (!IsWindowVisible(h) || IsIconic(h))
        return false;
    DWORD pid = 0;
    GetWindowThreadProcessId(h, &pid);
    if (pid == ownPid)                          // skip our own overlay windows
        return false;
    BOOL cloaked = FALSE;                        // skip DWM-cloaked (e.g. ghost UWP)
    if (SUCCEEDED(DwmGetWindowAttribute(h, DWMWA_CLOAKED, &cloaked, sizeof(cloaked))) && cloaked)
        return false;
    return GetWindowRect(h, &out) && out.right > out.left && out.bottom > out.top;
}

BOOL CALLBACK pickTopWindow(HWND h, LPARAM lp)
{
    auto* c = reinterpret_cast<WalkCtx*>(lp);
    RECT r{};
    if (!capturableWindow(h, c->ownPid, r))
        return TRUE;
    if (rectContains(r, c->pt.x, c->pt.y)) {
        c->found = h;                           // EnumWindows is top->bottom Z order
        return FALSE;
    }
    return TRUE;
}

// Same walk with no point test: the topmost foreign window, whatever it is.
BOOL CALLBACK pickFirstWindow(HWND h, LPARAM lp)
{
    auto* c = reinterpret_cast<WalkCtx*>(lp);
    RECT r{};
    if (!capturableWindow(h, c->ownPid, r))
        return TRUE;
    c->found = h;
    return FALSE;
}

// Topmost foreign top-level window under a global logical point (or null).
HWND topWindowAt(const QVector<winmon::Mon>& table, const QPoint& globalPos)
{
    WalkCtx c{winmon::logicalToPhysical(table, globalPos), GetCurrentProcessId(), nullptr};
    EnumWindows(&pickTopWindow, reinterpret_cast<LPARAM>(&c));
    return c.found;
}

// True if a UIA element's bounding rect contains the physical point.
bool elementContains(IUIAutomationElement* el, const POINT& p, RECT& outRect)
{
    if (!el || FAILED(el->get_CurrentBoundingRectangle(&outRect)))
        return false;
    return rectContains(outRect, p.x, p.y);
}

// Per-level record of a descent, for the diagnostic log only.
struct DescentTrace {
    QString path;             // one "d<depth>:ct<type><rect>" token per level
    int     rejectedByArea = 0;   // children that contained the point but were
                                  // dropped by the area<=parentArea guard
    const char* stop = "?";   // why the walk ended
};

// Descend from `root` to the deepest element whose bounds contain `p`; among
// siblings that contain it, prefer the smallest (most specific). Returns the
// chosen rect (== root's rect when nothing deeper matched).
//
// `enforceParentArea` and `maxDepth` are parameters purely so the diagnostic can
// replay the same walk with its guards removed; the
// production call passes the historical values (true / 12).
RECT descend(IUIAutomationTreeWalker* walker, IUIAutomationElement* root, const POINT& p,
             int maxDepth, bool enforceParentArea, DescentTrace* trace,
             QVector<RECT>* levels = nullptr)
{
    RECT bestRect{};
    root->get_CurrentBoundingRectangle(&bestRect);
    qint64 parentArea = qint64(bestRect.right - bestRect.left)
                      * qint64(bestRect.bottom - bestRect.top);
    IUIAutomationElement* cur = root;                // owned; released as we descend
    int depth = 0;
    for (; depth < maxDepth; ++depth) {
        IUIAutomationElement* child = nullptr;
        if (FAILED(walker->GetFirstChildElement(cur, &child)) || !child) {
            if (trace) trace->stop = "no-children";
            break;
        }

        IUIAutomationElement* chosen = nullptr;      // smallest containing sibling (owned)
        RECT chosenRect{};
        qint64 chosenArea = 0;
        while (child) {
            RECT cr{};
            bool keep = false;
            if (elementContains(child, p, cr)) {
                const qint64 area = qint64(cr.right - cr.left) * qint64(cr.bottom - cr.top);
                // Ignore bogus rects: zero/negative area, or larger than the parent
                // (a more-specific child can't be bigger — guards against the stale/
                // virtualized rects some providers, e.g. Chromium, report).
                const bool areaOk = area > 0 && (!enforceParentArea || area <= parentArea);
                if (trace && area > 0 && enforceParentArea && area > parentArea)
                    ++trace->rejectedByArea;
                if (areaOk && (!chosen || area < chosenArea)) {
                    if (chosen) chosen->Release();
                    chosen = child;                  // take ownership of this candidate
                    chosenRect = cr;
                    chosenArea = area;
                    keep = true;
                }
            }
            IUIAutomationElement* next = nullptr;
            walker->GetNextSiblingElement(child, &next);   // child still valid here
            if (!keep)
                child->Release();
            child = next;
        }

        if (!chosen) {
            if (trace) trace->stop = "no-containing-child";
            break;                                   // no child contains the point
        }
        if (cur != root)
            cur->Release();
        cur = chosen;                                // descend into the chosen element
        bestRect = chosenRect;
        parentArea = chosenArea;
        if (levels)
            levels->push_back(bestRect);             // outermost-first; caller reverses

        if (trace) {
            CONTROLTYPEID ct = 0;
            cur->get_CurrentControlType(&ct);
            trace->path += QStringLiteral(" d%1:ct%2%3")
                               .arg(depth).arg(int(ct)).arg(fmt(bestRect));
        }
    }
    if (trace && depth >= maxDepth)
        trace->stop = "depth-cap";

    if (cur != root)
        cur->Release();
    return bestRect;
}

// MSAA/IAccessible provider hit test, as a comparison. Used
// by the diagnostic only (log-only; it does not drive selection yet). Returns
// false when the provider yields no usable rect.
// `outLeaf` (optional) receives the resolved leaf object on success — the caller
// then owns it and must Release. Used to walk accParent for the hierarchy chain.
bool msaaHitTest(HWND target, const POINT& p, RECT& out, int& depthOut, QString& how,
                 IAccessible** outLeaf = nullptr)
{
    IAccessible* acc = nullptr;
    if (FAILED(AccessibleObjectFromWindow(target, OBJID_CLIENT, kIID_IAccessible,
                                          reinterpret_cast<void**>(&acc)))
        || !acc) {
        how = QStringLiteral("no-root");
        return false;
    }

    VARIANT childId;
    VariantInit(&childId);
    childId.vt = VT_I4;
    childId.lVal = CHILDID_SELF;
    bool ok = false;
    int depth = 0;
    for (; depth < 64; ++depth) {
        VARIANT hit;
        VariantInit(&hit);
        if (FAILED(acc->accHitTest(p.x, p.y, &hit))) { how = QStringLiteral("hittest-failed"); break; }
        if (hit.vt == VT_DISPATCH && hit.pdispVal) {
            IAccessible* next = nullptr;
            hit.pdispVal->QueryInterface(kIID_IAccessible, reinterpret_cast<void**>(&next));
            VariantClear(&hit);
            if (!next) { how = QStringLiteral("dispatch-no-iaccessible"); break; }
            acc->Release();
            acc = next;
            VariantClear(&childId);
            childId.vt = VT_I4;
            childId.lVal = CHILDID_SELF;
            continue;                                 // hit-test the deeper object
        }
        if (hit.vt == VT_I4) {
            const long id = hit.lVal;
            VariantClear(&hit);
            if (id == CHILDID_SELF) { ok = true; how = QStringLiteral("self"); break; }
            IDispatch* d = nullptr;
            VARIANT v;
            VariantInit(&v);
            v.vt = VT_I4;
            v.lVal = id;
            if (SUCCEEDED(acc->get_accChild(v, &d)) && d) {
                IAccessible* next = nullptr;
                d->QueryInterface(kIID_IAccessible, reinterpret_cast<void**>(&next));
                d->Release();
                if (next) {
                    acc->Release();
                    acc = next;
                    VariantClear(&childId);
                    childId.vt = VT_I4;
                    childId.lVal = CHILDID_SELF;
                    VariantClear(&v);
                    continue;
                }
            }
            VariantClear(&v);
            VariantClear(&childId);                   // simple child: keep parent + id
            childId.vt = VT_I4;
            childId.lVal = id;
            ok = true;
            how = QStringLiteral("child-id");
            break;
        }
        VariantClear(&hit);                           // VT_EMPTY and friends
        how = QStringLiteral("empty");
        break;
    }

    if (ok) {
        long x = 0, y = 0, w = 0, h = 0;
        if (SUCCEEDED(acc->accLocation(&x, &y, &w, &h, childId)) && w > 0 && h > 0)
            out = RECT{x, y, x + w, y + h};
        else
            { ok = false; how += QStringLiteral("+no-location"); }
    }
    depthOut = depth;
    VariantClear(&childId);
    if (ok && outLeaf)
        *outLeaf = acc;                               // ownership transfers to the caller
    else
        acc->Release();
    return ok;
}

// One log line per distinct hover result, comparing the production walk against
// two alternative strategies. Geometry only.
void logDiagnostics(IUIAutomation* uia, HWND target, const QPoint& globalPos, const POINT& p,
                    const QVector<winmon::Mon>& table, const RECT& result,
                    const DescentTrace& trace)
{
    QElapsedTimer timer;
    timer.start();

    wchar_t cls[128] = {};
    GetClassNameW(target, cls, 128);
    DWORD pid = 0;
    GetWindowThreadProcessId(target, &pid);

    // Which monitor entry mapped the cursor — the scale/origin actually applied.
    QString mon = QStringLiteral("none");
    for (const winmon::Mon& m : table) {
        if (m.logGeo.contains(globalPos)) {
            mon = QStringLiteral("log%1,%2 %3x%4 dpr%5 phys%6")
                      .arg(m.logGeo.left()).arg(m.logGeo.top())
                      .arg(m.logGeo.width()).arg(m.logGeo.height())
                      .arg(m.dpr).arg(fmt(m.phys));
            break;
        }
    }

    // Comparison A: Raw View, no area guard, deep budget.
    QString rawStr = QStringLiteral("n/a");
    IUIAutomationTreeWalker* rawWalker = nullptr;
    if (SUCCEEDED(uia->get_RawViewWalker(&rawWalker)) && rawWalker) {
        IUIAutomationElement* rawRoot = nullptr;
        if (SUCCEEDED(uia->ElementFromHandle(target, &rawRoot)) && rawRoot) {
            DescentTrace rawTrace;
            const RECT rr = descend(rawWalker, rawRoot, p, 64, /*enforceParentArea=*/false,
                                    &rawTrace);
            rawStr = QStringLiteral("%1 stop=%2 path=%3")
                         .arg(fmt(rr), QString::fromLatin1(rawTrace.stop), rawTrace.path);
            rawRoot->Release();
        }
        rawWalker->Release();
    }

    // Comparison C: UIA's global ElementFromPoint. UIA has no subtree-scoped hit
    // test, so this is the only "ask the provider" option it offers — and it is
    // global, which is the open question: does it resolve the target window, or
    // our own topmost frozen overlay?
    QString efpStr = QStringLiteral("n/a");
    IUIAutomationElement* efp = nullptr;
    if (SUCCEEDED(uia->ElementFromPoint(p, &efp)) && efp) {
        RECT er{};
        efp->get_CurrentBoundingRectangle(&er);
        CONTROLTYPEID ct = 0;
        efp->get_CurrentControlType(&ct);
        int epid = 0;
        efp->get_CurrentProcessId(&epid);
        efpStr = QStringLiteral("%1 ct%2 pid=%3%4")
                     .arg(fmt(er)).arg(int(ct)).arg(epid)
                     .arg(DWORD(epid) == GetCurrentProcessId()
                              ? QStringLiteral(" <-- OUR OWN OVERLAY") : QString());
        efp->Release();
    }

    // Comparison B: MSAA provider hit test.
    QString msaaStr;
    RECT mr{};
    int msaaDepth = 0;
    QString how;
    if (msaaHitTest(target, p, mr, msaaDepth, how)) {
        const QRect ml = winmon::physRectToLogical(table, mr);
        msaaStr = QStringLiteral("%1 how=%2 depth=%3 contains=%4 logical=[%5,%6 %7x%8]")
                      .arg(fmt(mr), how).arg(msaaDepth)
                      .arg(rectContains(mr, p.x, p.y) ? "Y" : "N")
                      .arg(ml.left()).arg(ml.top()).arg(ml.width()).arg(ml.height());
    } else {
        msaaStr = QStringLiteral("FAIL how=%1").arg(how);
    }

    const QRect logical = winmon::physRectToLogical(table, result);
    const bool guardFired = !logical.contains(globalPos);

    // Collapse the ~30 Hz hover stream: only log when the picture changes.
    const QString sig = QStringLiteral("%1|%2|%3|%4").arg(fmt(result), rawStr, msaaStr, efpStr);
    static QString lastSig;
    if (sig == lastSig)
        return;
    lastSig = sig;

    qDebug().noquote()
        << "[elemdiag] hwnd=" << QString::number(quintptr(target), 16)
        << "cls=" << QString::fromWCharArray(cls) << "pid=" << pid
        << "\n  cursor: logical=" << QStringLiteral("(%1,%2)").arg(globalPos.x()).arg(globalPos.y())
        << "physical=" << QStringLiteral("(%1,%2)").arg(p.x).arg(p.y) << "mon=" << mon
        << "\n  window: " << fmt(visibleFrame(target))
        << "\n  ctrlview(production): " << fmt(result)
        << "stop=" << QString::fromLatin1(trace.stop)
        << "areaRejects=" << trace.rejectedByArea
        << "sanityGuardFired=" << (guardFired ? "YES->windowAt" : "no")
        << "logical=" << QStringLiteral("[%1,%2 %3x%4]").arg(logical.left()).arg(logical.top())
                             .arg(logical.width()).arg(logical.height())
        << "\n    path:" << trace.path
        << "\n  rawview(noguard,d64): " << rawStr
        << "\n  uia ElementFromPoint: " << efpStr
        << "\n  msaa: " << msaaStr
        << "\n  diagCost=" << timer.elapsed() << "ms";
}

// Accumulates an innermost-first hierarchy chain in physical px.
//
// Only strictly-growing rects are kept. Providers hand back long runs of wrapper
// nodes sharing identical bounds (very common in web content), and a chain with
// duplicates would spend wheel ticks on steps that visibly change nothing. Each
// entry is clamped to the target window and must still contain the cursor, so a
// scrolled-out-of-view ancestor can't produce a rect that excludes the point.
class ChainBuilder {
public:
    ChainBuilder(const RECT& frame, const POINT& p) : m_frame(frame), m_p(p) {}

    void add(const RECT& r)
    {
        const RECT c{qMax(r.left, m_frame.left), qMax(r.top, m_frame.top),
                     qMin(r.right, m_frame.right), qMin(r.bottom, m_frame.bottom)};
        if (c.right <= c.left || c.bottom <= c.top || !rectContains(c, m_p.x, m_p.y))
            return;
        const qint64 area = qint64(c.right - c.left) * qint64(c.bottom - c.top);
        if (!m_out.isEmpty() && area <= m_lastArea)
            return;                                  // same size or shrinking: not a step
        m_out.push_back(c);
        m_lastArea = area;
    }

    // Physical -> Qt global-logical, at the one boundary where that conversion belongs.
    QList<QRect> toLogical(const QVector<winmon::Mon>& table, const QPoint& globalPos) const
    {
        QList<QRect> out;
        for (const RECT& r : m_out) {
            const QRect l = winmon::physRectToLogical(table, r);
            if (l.contains(globalPos))               // drop anything we can't reconcile
                out.push_back(l);
        }
        return out;
    }

    bool isEmpty() const { return m_out.isEmpty(); }

private:
    RECT m_frame;
    POINT m_p;
    QVector<RECT> m_out;
    qint64 m_lastArea = 0;
};

// Walk MSAA parents from the hit object outward, feeding each level's bounds into
// `chain`. Stops at a null parent, a repeated COM identity, or the depth cap.
void msaaAncestors(IAccessible* leaf, ChainBuilder& chain)
{
    // `owned` holds every reference we take; `cur` only ever borrows from it (or
    // from the caller's `leaf`), so each object is released exactly once at the end.
    IAccessible* cur = leaf;
    QVector<IAccessible*> owned;
    for (int depth = 0; depth < 64; ++depth) {
        IDispatch* parent = nullptr;
        if (FAILED(cur->get_accParent(&parent)) || !parent)
            break;
        IAccessible* pa = nullptr;
        parent->QueryInterface(kIID_IAccessible, reinterpret_cast<void**>(&pa));
        parent->Release();
        if (!pa)
            break;
        if (owned.contains(pa)) {                    // cheap cycle guard, with the cap
            pa->Release();
            break;
        }
        owned.push_back(pa);

        VARIANT self;
        VariantInit(&self);
        self.vt = VT_I4;
        self.lVal = CHILDID_SELF;
        long x = 0, y = 0, w = 0, h = 0;
        if (SUCCEEDED(pa->accLocation(&x, &y, &w, &h, self)) && w > 0 && h > 0)
            chain.add(RECT{x, y, x + w, y + h});
        VariantClear(&self);

        cur = pa;
    }
    for (IAccessible* a : owned)
        a->Release();
}

// Replay the historical Control View walk purely to log it, then emit the
// side-by-side comparison. Log-only: nothing here feeds the returned rect.
void runDiagnostics(IUIAutomation* uia, HWND target, const QPoint& globalPos, const POINT& p,
                    const QVector<winmon::Mon>& table)
{
    IUIAutomationElement* root = nullptr;
    if (FAILED(uia->ElementFromHandle(target, &root)) || !root)
        return;
    IUIAutomationTreeWalker* walker = nullptr;
    if (SUCCEEDED(uia->get_ControlViewWalker(&walker)) && walker) {
        DescentTrace trace;
        const RECT result = descend(walker, root, p, 12, /*enforceParentArea=*/true, &trace);
        logDiagnostics(uia, target, globalPos, p, table, result, trace);
        walker->Release();
    }
    root->Release();
}

} // namespace

WinElementDetect::WinElementDetect() = default;

WinElementDetect::~WinElementDetect()
{
    if (m_uia)
        reinterpret_cast<IUIAutomation*>(m_uia)->Release();
    if (m_comInited)
        CoUninitialize();
}

void WinElementDetect::ensureUia()
{
    if (m_uia)
        return;
    const HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    // S_OK and S_FALSE both require a matching CoUninitialize; RPC_E_CHANGED_MODE
    // means COM is already up in another mode — don't balance it.
    m_comInited = (hr == S_OK || hr == S_FALSE);
    IUIAutomation* uia = nullptr;
    if (SUCCEEDED(CoCreateInstance(kCLSID_CUIAutomation, nullptr, CLSCTX_INPROC_SERVER,
                                   kIID_IUIAutomation, reinterpret_cast<void**>(&uia)))
        && uia)
        m_uia = uia;
}

QRect WinElementDetect::windowAt(const QPoint& globalPos)
{
    const QVector<winmon::Mon> table = winmon::buildTable();
    HWND h = topWindowAt(table, globalPos);
    if (!h)
        return {};
    return winmon::physRectToLogical(table, visibleFrame(h));
}

QRect WinElementDetect::elementAt(const QPoint& globalPos)
{
    return resolve(globalPos, /*wantChain=*/false).value(0);
}

QList<QRect> WinElementDetect::elementChainAt(const QPoint& globalPos)
{
    return resolve(globalPos, /*wantChain=*/true);
}

QList<QRect> WinElementDetect::resolve(const QPoint& globalPos, bool wantChain)
{
    const QVector<winmon::Mon> table = winmon::buildTable();

    // Resolve the real target window first (our own overlay is excluded by the
    // Z-order walk), then hit-test/descend ITS subtree. Starting from the window
    // root means the overlay is never in scope — no point-exclusion needed.
    HWND target = topWindowAt(table, globalPos);
    if (!target)
        return {};

    const POINT p = winmon::logicalToPhysical(table, globalPos);
    const RECT frame = visibleFrame(target);

    // Diagnostics run BEFORE the strategies below, because those return early —
    // logging from inside them only ever captures whichever one happened to win.
    if (diagEnabled()) {
        ensureUia();
        if (m_uia)
            runDiagnostics(reinterpret_cast<IUIAutomation*>(m_uia), target, globalPos, p, table);
    }

    // Strategy 1 — ask the provider to hit-test the point (MSAA/IAccessible).
    //
    // This is the only strategy that works inside a browser document. A geometric
    // descent cannot: Firefox reports content nodes with their FULL SCROLLABLE
    // bounds (a 7315px-tall body inside a 1278px viewport), so the "a child can't
    // be bigger than its parent" guard throws the document body away and the walk
    // dies at the Document node — and dropping that guard only lands on 6700px-tall
    // container groups instead. accHitTest returns the actual element in one hop.
    // On browser chrome it agrees exactly with the UIA walk, so it is safe to try
    // first; anything it can't resolve falls through to UIA below.
    QElapsedTimer chainTimer;
    if (diagEnabled())
        chainTimer.start();

    // The window frame is always the outermost step, so `windowAt`'s answer is the
    // floor this function degrades to rather than a separate fallback path.
    const QRect windowLogical = winmon::physRectToLogical(table, frame);

    RECT mr{};
    int msaaDepth = 0;
    QString how;
    IAccessible* leaf = nullptr;
    if (msaaHitTest(target, p, mr, msaaDepth, how, &leaf)) {
        ChainBuilder chain(frame, p);
        chain.add(mr);                   // ChainBuilder clamps to the frame for us
        // The ancestor walk is ~40ms PER LEVEL cross-process, so it only runs when a
        // caller actually needs the hierarchy — never on the hover path.
        if (wantChain && !chain.isEmpty())
            msaaAncestors(leaf, chain);  // ...then outward, one step per real size change
        leaf->Release();
        chain.add(frame);                // outermost step

        const QList<QRect> out = chain.toLogical(table, globalPos);
        // Size >= 2 means at least one entry sits strictly inside the window, i.e.
        // MSAA actually resolved an element. A lone entry is just the frame, which
        // tells us nothing the UIA walk can't tell us better — let it try.
        if (out.size() >= 2) {
            if (diagEnabled())
                qDebug().noquote() << "[elemdiag] msaa chain levels=" << out.size()
                                   << "leaf=" << fmt(mr) << "how=" << how
                                   << "depth=" << msaaDepth
                                   << "buildMs=" << chainTimer.elapsed();
            return out;
        }
    }

    // Strategy 2 — UIA subtree descent (native apps; browser chrome fallback).
    ensureUia();
    if (!m_uia)
        return {windowLogical};

    auto* uia = reinterpret_cast<IUIAutomation*>(m_uia);

    IUIAutomationElement* root = nullptr;
    if (FAILED(uia->ElementFromHandle(target, &root)) || !root)
        return {windowLogical};

    IUIAutomationTreeWalker* walker = nullptr;
    if (FAILED(uia->get_ControlViewWalker(&walker)) || !walker) {
        root->Release();
        return {windowLogical};
    }

    // Descend to the deepest element whose bounds contain the point; among
    // siblings that contain it, prefer the smallest (most specific) one. The walk
    // already visits every level, so the chain costs one vector of rects.
    constexpr int kMaxDepth = 12;
    QVector<RECT> levels;
    descend(walker, root, p, kMaxDepth, /*enforceParentArea=*/true, nullptr, &levels);

    walker->Release();
    root->Release();

    ChainBuilder chain(frame, p);
    for (int i = levels.size() - 1; i >= 0; --i)     // descent is outermost-first
        chain.add(levels.at(i));
    chain.add(frame);

    // Entries that don't contain the cursor after mapping back to logical are
    // dropped by the builder, so a provider reporting rects in a space we can't
    // reconcile degrades to the window rect instead of a shifted, wrong highlight.
    const QList<QRect> out = chain.toLogical(table, globalPos);
    if (diagEnabled())
        qDebug().noquote() << "[elemdiag] uia chain levels=" << out.size()
                           << "buildMs=" << chainTimer.elapsed();
    return out.isEmpty() ? QList<QRect>{windowLogical} : out;
}

// --- Foreground window (Snip active window) --------------------------------
// Lives here rather than beside foregroundWindowTitle() in WinPlatform.cpp so it
// can reuse visibleFrame() + capturableWindow() above — duplicating the DWM frame
// logic would let the two drift apart.
namespace platform {

QRect foregroundWindowRect()
{
    const DWORD ownPid = GetCurrentProcessId();
    RECT ignored{};
    HWND h = GetForegroundWindow();
    // A SnapCap pin/dialog may hold focus when the hotkey fires; fall through to the
    // topmost window that isn't ours rather than snipping our own frame.
    if (!h || !capturableWindow(h, ownPid, ignored)) {
        WalkCtx c{POINT{}, ownPid, nullptr};
        EnumWindows(&pickFirstWindow, reinterpret_cast<LPARAM>(&c));
        h = c.found;
    }
    if (!h)
        return {};
    const RECT f = visibleFrame(h);              // physical px == capture px
    return QRect(f.left, f.top, f.right - f.left, f.bottom - f.top);
}

} // namespace platform
