#pragma once

#include <QAbstractListModel>
#include <QObject>
#include <QVector>

// AppLibrary indexes every installed .desktop Application entry and
// exposes them as a filterable list model for the dock manager UI.
// Filtering by display name, appId, or comment is live (per-keystroke).

class AppLibrary : public QAbstractListModel {
    Q_OBJECT
    Q_PROPERTY(QString filter READ filter WRITE setFilter NOTIFY filterChanged)

public:
    enum Roles {
        AppIdRole = Qt::UserRole + 1,
        DisplayNameRole,
        IconNameRole,
        CommentRole,
        CategoryRole,
    };

    explicit AppLibrary(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    QString filter() const;
    void setFilter(const QString &f);

    Q_INVOKABLE void refresh();

signals:
    void filterChanged();

private:
    struct AppEntry {
        QString appId;
        QString displayName;
        QString iconName;
        QString comment;
        QString category;
    };

    void scan();
    void applyFilter();

    // Parsing every .desktop file on the system costs real memory and is only
    // ever needed by the app-picker window, which most sessions never open.
    // Deferred until something actually reads the model.
    void ensureScanned() const;

    mutable bool              m_scanned = false;
    mutable QVector<AppEntry>         m_all;
    mutable QVector<const AppEntry *> m_filtered;
    QString                   m_filter;
};
