#include "ui/MixedLoadController.h"
#include <QtConcurrent>
#include <algorithm>

MixedLoadController::MixedLoadController(QObject* parent) : QObject(parent) {
    debounce_.setSingleShot(true);
    debounce_.setInterval(100);
    connect(&debounce_, &QTimer::timeout, this, &MixedLoadController::StartPending);
    connect(&watcher_, &QFutureWatcher<Result>::finished, this, [this] {
        const auto result = watcher_.result();
        running_ = false;
        if (result.generation == generation_) {
            if (result.ok) emit Loaded(result.session);
            else emit Failed(result.error);
        }
        if (pending_ && !debounce_.isActive()) StartPending();
    });
}
MixedLoadController::~MixedLoadController() {
    Reset();
    watcher_.waitForFinished();
}
void MixedLoadController::Reset() {
    ++generation_;
    if (cancel_) cancel_->store(true);
    debounce_.stop();
    pending_.reset();
    latest_.reset();
}
void MixedLoadController::Request(const pat::MixedDataSession& session,
                                   QVector<int> indices, pat::MixedReadWindow window, QVector<pat::MixedAlignment> alignments) {
    std::sort(indices.begin(), indices.end());
    indices.erase(std::unique(indices.begin(), indices.end()), indices.end());
    Query query{session.Revision(), session.CacheBudgetBytes(), indices, window, alignments};
    if (latest_ && *latest_ == query) return;
    latest_ = query;
    ++generation_;
    if (cancel_) cancel_->store(true);
    pending_ = Work{query, session.MetadataSnapshot(), generation_};
    debounce_.start();
}
void MixedLoadController::StartPending() {
    if (running_ || !pending_) return;
    auto work = std::move(*pending_);
    pending_.reset();
    cancel_ = std::make_shared<std::atomic_bool>(false);
    running_ = true;
    watcher_.setFuture(QtConcurrent::run([work = std::move(work), cancel = cancel_]() mutable {
        Result result;
        result.generation = work.generation;
        result.ok = work.session.LoadSignals(work.query.indices, result.error,
            work.query.window, [cancel] { return cancel->load(); }, work.query.alignments);
        result.session = std::move(work.session);
        return result;
    }));
}
