// THE GIT ROAD WITHOUT LIBGIT2 — git_repo.h's every declaration answered as
// "could not ask" (architect 2026-10-02). Linked IN PLACE OF git_repo.cpp when
// WARPTEMPO_GUI_GIT is OFF (CMakeLists.txt states the swap, at its one site).
//
// A BUILD-HOST CONVENIENCE FOR GRAPHICAL WORK, NEVER A PRODUCT CONFIGURATION:
// it exists so a host that cannot provide libgit2 at the version the program
// is built against can still build and run the GUI to paint it. THE TWO
// DEVICES BUILD WITH THE SWITCH ON — the laptop's build/ and the APK both link
// git_repo.cpp — and nothing here is ever on a device the architect authors
// with.
//
// EVERY GIT ACT FAILS THROUGH THE PROGRAM'S EXISTING OUTCOMES, with no new UI
// text. The root derivation is the one question the product asks first, and
// this file answers it CouldNotAsk with this build's reason as libgit2's words
// would ride — so the `h` view's entry refuses its commit walk with the one
// stderr line it always prints ("History is unavailable: could not ask git
// which clone holds <folder> (git is not available in this build)") and opens
// on the local walk, Save greyed, the walk lamp greyed, no GitHub check
// dispatched and no pull reachable (history_remote_walk_available, app_state.h,
// gates all three). CouldNotAsk rather than NotAClone because it is the true
// answer: the folder may well be in a clone, and this build cannot ask.
//
// NO GuiGitRepo IS EVER CONSTRUCTED: open() refuses, and the constructor that
// takes a handle is private. Every method below is therefore unreachable in
// practice, and is defined only because its callers link against it; each
// still answers its vocabulary's "local, could not ask" value with one stderr
// line, never a crash and never a silent success.

#include "git_repo.h"

#include <cstdio>
#include <utility>

namespace {

constexpr const char* kNoGit = "git is not available in this build";

void say_no_git() {
    std::fprintf(stderr, "warptempo_gui: git: not available in this build\n");
}

}  // namespace

// ---------------------------------------------------------------------------
// process-wide
// ---------------------------------------------------------------------------

void gui_git_init() {}

// The callers' own stderr line carries `diag` (GuiHistoryDiff::init), so this
// prints nothing of its own: the derivation is asked several times per visit
// (the staleness tip, the prefetch header, the entry), and the one line is the
// entry's.
GuiGitRoot gui_git_discover_root(const std::string& /*dir*/, std::string& root,
                                 std::string& diag) {
    root.clear();
    diag = kNoGit;
    return GuiGitRoot::CouldNotAsk;
}

// ---------------------------------------------------------------------------
// the handle
// ---------------------------------------------------------------------------

// Silent for discover_root's reason: every caller composes `diag` into its own
// failure. Unreachable in practice — no root is ever discovered to open.
std::optional<GuiGitRepo> GuiGitRepo::open(const std::string& /*root*/,
                                           std::string&       diag) {
    diag = kNoGit;
    return std::nullopt;
}

GuiGitRepo::GuiGitRepo(GuiGitRepo&& other) noexcept
    : repo_(std::exchange(other.repo_, nullptr)) {}

GuiGitRepo& GuiGitRepo::operator=(GuiGitRepo&& other) noexcept {
    if (this != &other) repo_ = std::exchange(other.repo_, nullptr);
    return *this;
}

GuiGitRepo::~GuiGitRepo() = default;

// ---- reads ------------------------------------------------------------------

std::string GuiGitRepo::head_commit() const {
    say_no_git();
    return std::string();
}

std::string GuiGitRepo::head_branch() const {
    say_no_git();
    return std::string();
}

bool GuiGitRepo::origin_fetch_url(std::string& /*url*/) const {
    say_no_git();
    return false;
}

bool GuiGitRepo::origin_push_urls(std::vector<std::string>& /*urls*/) const {
    say_no_git();
    return false;
}

bool GuiGitRepo::walk_head(const GuiGitPathFilter& /*accept*/,
                           std::vector<std::string>& /*shas*/,
                           std::string& diag) const {
    say_no_git();
    diag = kNoGit;
    return false;
}

bool GuiGitRepo::changed_paths(const std::string& /*sha*/,
                               const GuiGitPathFilter& /*accept*/,
                               std::vector<std::string>& /*paths*/) const {
    say_no_git();
    return false;
}

bool GuiGitRepo::tree_paths(const std::string& /*sha*/,
                            const GuiGitPathFilter& /*accept*/,
                            std::vector<std::string>& /*paths*/) const {
    say_no_git();
    return false;
}

bool GuiGitRepo::read_blob(const std::string& /*sha*/,
                           const std::string& /*path*/,
                           std::string& /*bytes*/) const {
    say_no_git();
    return false;
}

bool GuiGitRepo::is_commit(const std::string& /*sha*/) const {
    say_no_git();
    return false;
}

GuiGitPathStatus GuiGitRepo::status_of(
        const std::vector<std::string>& /*paths*/,
        const std::string& /*branch*/, bool& publication_owed,
        std::string& diag) const {
    say_no_git();
    publication_owed = false;
    diag = kNoGit;
    return GuiGitPathStatus::Unavailable;
}

GuiGitUpstream GuiGitRepo::compare_with_origin(
        const std::string& /*branch*/) const {
    say_no_git();
    return GuiGitUpstream{};  // Reading::Unreadable
}

bool GuiGitRepo::paths_changed_between(
        const std::string& /*from_sha*/, const std::string& /*to_sha*/,
        const GuiGitPathFilter& /*accept*/,
        std::vector<std::string>& /*paths*/) const {
    say_no_git();
    return false;
}

// ---- the five mutations -----------------------------------------------------

bool GuiGitRepo::stage_paths(const std::vector<std::string>& /*paths*/,
                             std::string& diag) {
    say_no_git();
    diag = kNoGit;
    return false;
}

bool GuiGitRepo::commit_paths(const std::vector<std::string>& /*paths*/,
                              const std::string& /*title*/,
                              std::string& diag) {
    say_no_git();
    diag = kNoGit;
    return false;
}

bool GuiGitRepo::push_branch(const std::string& /*branch*/,
                             const std::string& /*destination_url*/,
                             std::string& diag) {
    say_no_git();
    diag = kNoGit;
    return false;
}

GuiGitFetch GuiGitRepo::fetch_origin(const std::string& /*source_url*/,
                                     const std::string& /*branch*/,
                                     const std::atomic<bool>* /*cancel*/,
                                     std::string& diag) {
    say_no_git();
    diag = kNoGit;
    return GuiGitFetch::LocalFailed;
}

GuiGitFastForward GuiGitRepo::fast_forward(
        const std::string& /*branch*/, const std::string& /*from_sha*/,
        const std::string& /*to_sha*/,
        const std::vector<std::string>& /*exclude*/,
        const std::vector<std::string>& /*force*/,
        std::string& /*conflict_path*/, std::string& diag) {
    say_no_git();
    diag = kNoGit;
    return GuiGitFastForward::NotStarted;
}

// ---- the cold-start lock recovery ---------------------------------------------

void GuiGitRepo::clear_stale_locks(const std::string& /*branch*/) {
    say_no_git();
}
