#pragma once
#include "core/MixedDataSession.h"
#include <QObject>
#include <QFutureWatcher>
#include <QTimer>
#include <atomic>
#include <memory>
#include <optional>

// At most one running query and one latest pending query. Workers own only
// metadata snapshots and return results; they never touch widgets or UI data.
class MixedLoadController : public QObject {
    Q_OBJECT
public:
    explicit MixedLoadController(QObject* parent = nullptr);
    ~MixedLoadController() override;
    void Request(const pat::MixedDataSession& session, QVector<int> indices,
                 pat::MixedReadWindow window, QVector<pat::MixedAlignment> alignments = {});
    void Reset();
    bool Busy() const { return running_ || pending_.has_value(); }
signals:
    void Loaded(const pat::MixedDataSession& session);
    void Failed(const QString& error);
private:
    struct Query {
        quint64 revision = 0;
        qsizetype budget = 0;
        QVector<int> indices;
        pat::MixedReadWindow window;
        QVector<pat::MixedAlignment> alignments;
        bool operator==(const Query&) const = default;
    };
    struct Work { Query query; pat::MixedDataSession session; quint64 generation = 0; };
    struct Result { pat::MixedDataSession session; QString error; quint64 generation = 0; bool ok = false; };
    void StartPending();
    QTimer debounce_;
    QFutureWatcher<Result> watcher_;
    std::optional<Query> latest_;
    std::optional<Work> pending_;
    std::shared_ptr<std::atomic_bool> cancel_;
    quint64 generation_ = 0;
    bool running_ = false;
};
