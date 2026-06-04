#include "AppLibrary.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSettings>
#include <QStandardPaths>
#include <algorithm>

AppLibrary::AppLibrary(QObject *parent)
    : QAbstractListModel(parent)
{
    scan();
}

void AppLibrary::scan()
{
    m_all.clear();

    const QStringList dataDirs = QStandardPaths::standardLocations(
        QStandardPaths::ApplicationsLocation);

    QSet<QString> seen;
    for (const QString &dir : dataDirs) {
        const QFileInfoList files = QDir(dir).entryInfoList(
            {QStringLiteral("*.desktop")}, QDir::Files);

        for (const QFileInfo &fi : files) {
            const QString appId = fi.completeBaseName();
            if (seen.contains(appId)) continue;

            QSettings ini(fi.absoluteFilePath(), QSettings::IniFormat);
            ini.beginGroup(QStringLiteral("Desktop Entry"));

            if (ini.value(QStringLiteral("Type")).toString()
                    != QStringLiteral("Application")) { ini.endGroup(); continue; }
            if (ini.value(QStringLiteral("NoDisplay"), false).toBool())
                { ini.endGroup(); continue; }
            if (ini.value(QStringLiteral("Hidden"), false).toBool())
                { ini.endGroup(); continue; }

            AppEntry e;
            e.appId       = appId;
            e.displayName = ini.value(QStringLiteral("Name"), appId).toString();
            e.iconName    = ini.value(QStringLiteral("Icon"), appId).toString();
            e.comment     = ini.value(QStringLiteral("Comment")).toString();
            e.category    = ini.value(QStringLiteral("Categories"))
                                .toString()
                                .split(QChar(';'), Qt::SkipEmptyParts)
                                .value(0);
            ini.endGroup();

            m_all.append(std::move(e));
            seen.insert(appId);
        }
    }

    std::sort(m_all.begin(), m_all.end(), [](const AppEntry &a, const AppEntry &b) {
        return a.displayName.compare(b.displayName, Qt::CaseInsensitive) < 0;
    });

    applyFilter();
}

void AppLibrary::applyFilter()
{
    m_filtered.clear();
    for (const AppEntry &e : std::as_const(m_all)) {
        if (m_filter.isEmpty()
                || e.displayName.contains(m_filter, Qt::CaseInsensitive)
                || e.appId.contains(m_filter, Qt::CaseInsensitive)
                || e.comment.contains(m_filter, Qt::CaseInsensitive)) {
            m_filtered.append(&e);
        }
    }
}

int AppLibrary::rowCount(const QModelIndex &parent) const
{
    if (parent.isValid()) return 0;
    return m_filtered.size();
}

QVariant AppLibrary::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() >= m_filtered.size()) return {};
    const AppEntry &e = *m_filtered.at(index.row());
    switch (role) {
    case AppIdRole:       return e.appId;
    case DisplayNameRole: return e.displayName;
    case IconNameRole:    return e.iconName;
    case CommentRole:     return e.comment;
    case CategoryRole:    return e.category;
    }
    return {};
}

QHash<int, QByteArray> AppLibrary::roleNames() const
{
    return {
        {AppIdRole,       "appId"},
        {DisplayNameRole, "displayName"},
        {IconNameRole,    "iconName"},
        {CommentRole,     "comment"},
        {CategoryRole,    "category"},
    };
}

QString AppLibrary::filter() const { return m_filter; }

void AppLibrary::setFilter(const QString &f)
{
    if (m_filter == f) return;
    m_filter = f;
    emit filterChanged();
    beginResetModel();
    applyFilter();
    endResetModel();
}

void AppLibrary::refresh()
{
    beginResetModel();
    scan();
    endResetModel();
}
