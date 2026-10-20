#pragma once

#include <QImage>
#include <QJsonObject>
#include <QRect>
#include <QString>
#include <QVector>

// Backup & restore of the open pins across app restarts. A pin is captured as a
// PinSnapshot — its pixels, transform/group state (PinModel::toJson), the window's
// frame geometry, and any annotation document JSON. save() writes a session.json
// manifest plus one sidecar PNG per pin into a folder; load() reads them back so
// the app can re-create the windows in place. Pure logic (Qt Gui only, no widgets/
// Win32) → headlessly unit-tested (tst_session_store). The on-disk folder is the
// final format — the QuaZip archive is won't-do (Implementation-Plan.md Stage 6).
// Stage 6.
struct PinSnapshot {
    QImage      image;        // the pin's source pixels (sidecar PNG on disk)
    QJsonObject transform;    // PinModel::toJson(): rotation/flip/scale/opacity/group/text
    QRect       geometry;     // window frame geometry, logical px (null → place by cursor)
    QJsonObject annotations;  // AnnotationDocument::toJson() (empty if none)
};

class SessionStore {
public:
    // `dir` is the session folder (created on save). Pass a temp dir in tests.
    explicit SessionStore(const QString& dir);

    QString directory() const { return m_dir; }

    // Persist the given pins (overwrites any previous session). Returns false on
    // I/O failure. Saving an empty list clears the session.
    bool save(const QVector<PinSnapshot>& pins) const;

    // Restore the persisted pins (empty if none / unreadable).
    QVector<PinSnapshot> load() const;

    // Whether a session file exists.
    bool hasSession() const;

    // Remove session.json + all sidecar PNGs.
    void clear() const;

private:
    QString manifestPath() const;
    QString imagePath(int index) const;

    QString m_dir;
};
