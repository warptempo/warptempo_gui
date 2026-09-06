#pragma once

#include "history_diff.h"

#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <deque>
#include <mutex>
#include <optional>
#include <string>
#include <thread>

// THE HISTORY WALK'S PREFETCH STORE AND ITS WORKER (architect 2026-08-07).
//
// The `h` view's walk is LOAD-GATED — membership is the strict whole-set load
// itself (history_diff.h) — so building it costs a `rev-list --count` and a
// `git log`, then a `rev-parse` + a `show` for the touched directory + `ls-tree`
// + three `show`s for the blobs + three strict parses PER CANDIDATE (six
// children each since 2026-08-09, when the touched-directory evidence read
// joined; two where that evidence refuses). That ran
// synchronously at every `h`, which is what made the entry stall and what the
// ruled depth of 20 was really buying. The architect's answer is this class: the
// whole git half runs ONCE AT STARTUP on a background thread, UNCAPPED, and
// STREAMS its members to the main thread one at a time, so the view opens
// instantly and fills in.
//
// SHAPED LIKE GuiHistoryCommitWorker, which is shaped like GuiAsyncRenderer: own
// std::thread, a condition variable for the wake, and an eventfd the platform
// run loop polls, whose POLLIN makes the GUI thread call drain(). The one
// difference is the direction of the traffic — a checkpoint is one job with one
// verdict, a scan is one job with MANY results — so what crosses is a QUEUE of
// messages behind the mutex, and the eventfd is a plain counter whose signals
// coalesce freely: a POLLIN means "there is something in the queue", never "one
// thing", and the drain empties it.
//
// THE STORE IS MAIN-THREAD STATE. Everything a reader can see — the header, the
// members, the done flag, the counts, the tip — is written only by drain() and
// kick(), both of which run on the GUI thread. The worker never touches it; it
// only pushes messages. So GuiHistoryDiff can hold a pointer to it and read it
// from paint and input code with no lock anywhere.
//
// GENERATIONS ARE HOW A RESTART IS SAFE. Every run carries the generation it was
// kicked with; a new kick bumps the counter, clears the store and asks the
// running run to abandon at its next candidate boundary. Messages are TAGGED,
// and the drain drops on the floor anything that does not carry the current
// generation — so the superseded run's remaining members, and its DONE, can
// arrive whenever they like and change nothing.
//
// CONCURRENCY WITH THE CHECKPOINT WORKER IS ACCEPTED, deliberately and with no
// mechanism: every call this class makes is a git READ (`rev-parse`, `log`,
// `ls-tree`, `show`) and the checkpoint act's `add`/`commit`/`push` may be
// running beside it. A read that races a mutation sees the repository partway
// through — an older `log`, a commit not yet listed — and the answer to that is
// the RE-WARM rather than a lock: the act's completion kicks a fresh run for
// every outcome that MAY have committed — three of the five, everything but the
// two that provably run no commit — so whatever raced is rebuilt from the
// settled repository a moment later. That kick SUPERSEDES this run rather than
// queueing behind it (the generation bump above is the whole mechanism), which
// is what keeps a scan begun against the pre-commit tip from outliving it. (The `h` entry is refused outright while a
// checkpoint publishes, so no VIEW can be reading a half-mutated walk either.)
class GuiHistoryPrefetch {
public:
    GuiHistoryPrefetch();
    ~GuiHistoryPrefetch();

    GuiHistoryPrefetch(const GuiHistoryPrefetch&)            = delete;
    GuiHistoryPrefetch& operator=(const GuiHistoryPrefetch&) = delete;

    // Create the eventfd and spawn the worker thread. False if the eventfd
    // could not be created (one stderr line of its own). Must run before any
    // kick.
    bool init();

    // Stop the worker and close the eventfd. Idempotent, safe after a failed
    // init, and called from the destructor. A run in flight is ABANDONED at its
    // next candidate boundary rather than waited out — a scan writes nothing
    // anywhere, so there is nothing to leave half-done.
    void shutdown();

    // The eventfd the platform layer polls. -1 before init().
    int completion_fd() const { return completion_fd_; }

    // -- The main thread's side -------------------------------------------

    // START A FRESH RUN against this source and this projects_repo, superseding
    // whatever is running. Clears the store, bumps the generation.
    //
    // THREE KICKERS, and the inventory is here because there is nowhere better
    // (membership re-derived 2026-08-09): the startup load's tail (main.cpp,
    // once the source has settled), the checkpoint act's completion for every
    // outcome that MAY have committed (three of the five —
    // GuiInputHandler::on_history_checkpoint_complete owns that derivation), and
    // the `h` entry when the store is STALE. All three reach this through
    // GuiInputHandler::kick_history_prefetch, whose definition carries the
    // proof that NONE of the three can fire with an `h` visit standing — the
    // property a visit needs, its indices naming this store's deque.
    void kick(std::string source_audio_path, std::string projects_repo);

    // What a drain did. `members_appended` is how many walk members arrived (0
    // for a drain that carried only a header or a DONE), `became_done` true on
    // the drain that ended the current run.
    struct DrainResult {
        std::size_t members_appended = 0;
        bool        became_done      = false;
    };

    // Take everything the worker has queued into the store. Called from the
    // platform's completion hook (which read()s the eventfd counter first, as it
    // does for the other three workers).
    DrainResult drain();

    // -- What the store knows (main thread only) ---------------------------

    unsigned long long generation() const { return generation_seen_; }
    bool               has_header() const { return header_seen_; }
    const GuiHistoryWalkHeader& header() const { return header_; }

    // The walk, newest first, exactly as GuiHistoryDiff indexes it. A DEQUE
    // because it grows by append under live readers and nothing may move.
    const std::deque<GuiHistoryCommitSidecars>& members() const {
        return members_;
    }

    // True once the current run has reported DONE — the term that separates a
    // walk that is merely still streaming from one that is FINISHED, which is
    // what the head-delta measurement reads to answer "there is everything to
    // checkpoint" (GuiHistoryDiff::walk_finished_empty).
    bool run_done() const { return done_; }

    // AND WHETHER THAT FINISH WAS AN ANSWER. A run whose `git log` capture could
    // not run ends DONE and NOT ok, carrying the one line the mode prints when
    // it refuses: an unread history is not an empty one, and with an empty walk
    // now OPENING the view and telling Save and commit there is everything to
    // checkpoint, the two had to stop being the same state (GuiHistoryScanResult,
    // history_diff.h, owns the ruling). Both are reset at kick(), so a later run
    // that answers clears the failure with nothing to remember.
    //
    // TWO READERS, and they are the two halves of the same refusal:
    // GuiHistoryDiff::init, which returns UNAVAILABLE with this reason when the
    // run it bound to failed, and GuiHistoryDiff::walk_finished_empty, which
    // answers false so the head delta keeps its conservative greyed face.
    bool               run_failed() const { return failed_; }
    const GuiFailure&  scan_failure_reason() const { return failure_reason_; }

    // A run is in flight for the current generation (kicked, no DONE drained
    // yet). It is what makes the staleness question answerable before the
    // worker's header has arrived: a run already scanning for this subject was
    // started against a tip we have not read yet, so it counts as fresh.
    bool running() const { return running_; }

    // THE STALENESS KEY'S OTHER TWO TERMS — what the current run was kicked
    // FOR. A store built for another source, or under another projects_repo, is
    // stale whatever the tip says. (Both are recorded at kick time, so they are
    // answerable the instant a run starts.)
    const std::string& subject_source_path() const { return subject_path_; }
    const std::string& subject_projects_repo() const { return subject_repo_; }

    // The branch tip the current run was built against, empty until the run's
    // header arrives. The third staleness term: an entry compares it against a
    // live read_history_branch_tip_sha().
    const std::string& tip_sha() const { return tip_sha_; }

private:
    // What crosses the queue. One struct rather than a variant: the three kinds
    // are small, they are produced a handful of times per run, and one shape
    // keeps the drain a single switch.
    struct Message {
        enum class Kind { Header, Member, Done } kind = Kind::Done;
        unsigned long long       generation          = 0;
        GuiHistoryWalkHeader     header;
        std::string              tip_sha;
        GuiHistoryCommitSidecars member;
        GuiHistoryScanResult     result;
    };

    struct Run {
        unsigned long long generation = 0;
        std::string        source_audio_path;
        std::string        projects_repo;
    };

    void worker_loop();
    void push_message(Message m);
    void signal_ready();

    std::thread             worker_;
    std::mutex              mtx_;
    std::condition_variable cv_;

    // Shared with the worker, all under mtx_ except the two atomics.
    std::deque<Message> queue_;
    std::optional<Run>  pending_run_;
    std::atomic<unsigned long long> generation_{0};
    std::atomic<bool>               stop_worker_{false};

    // THE STORE — main thread only, no lock.
    unsigned long long                   generation_seen_ = 0;
    bool                                 header_seen_     = false;
    GuiHistoryWalkHeader                 header_;
    std::deque<GuiHistoryCommitSidecars> members_;
    bool                                 done_    = false;
    bool                                 running_ = false;
    bool                                 failed_  = false;
    GuiFailure                           failure_reason_;
    int                                  hidden_  = 0;
    std::string                          tip_sha_;
    std::string                          subject_path_;
    std::string                          subject_repo_;

    int completion_fd_ = -1;
};
