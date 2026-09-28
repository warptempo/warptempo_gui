#include "git_repo.h"

#include "device_config.h"  // device_config_path — the deploy key sits beside it

#include <git2.h>

#include <cstdio>
#include <filesystem>
#include <memory>
#include <utility>

namespace {

// THE NETWORK TIME BOUND, both halves at one value: libgit2's connect timeout
// and its read/write timeout on the server connection, which reaches the SSH
// session itself (libssh2's own session timeout), so a black-holed route or a
// server that stops answering mid-exchange fails the fetch or the push instead
// of holding it. Thirty seconds is chosen against the two network steps — a
// few kilobytes over SSH, well under a second on a working link — so the bound
// can only fire on something that is not going to finish. Both run on the
// checkpoint worker, off the GUI thread (GuiHistoryCommitWorker): a hang holds
// a checkpoint's join at quit for at most the bound, and a CHECK's not at all
// (the worker abandons a check in flight). Every other git question this
// product asks is local and needs no bound.
constexpr int kServerTimeoutMs = 30000;

// THE DEPLOY KEY (architect 2026-09-27): the GUI fetches and pushes with a key
// of its own,
// registered on the projects repository with write access, never the account
// key an interactive ssh would offer. Two files BESIDE THE DEVICE CONFIG — the
// directory device_config_path() names, which is `$XDG_CONFIG_HOME/
// warptempo_gui/` on every backend — an OpenSSH private key with NO
// PASSPHRASE (nothing could type one) and its public half. No agent, no
// ~/.ssh/config and no permission check: the file is the whole credential.
constexpr const char* kDeployKeyName       = "deploy_key";
constexpr const char* kDeployKeyPublicName = "deploy_key.pub";

// GITHUB'S HOST KEYS, PINNED (architect 2026-09-27): the three keys GitHub
// publishes ("GitHub's SSH key fingerprints", docs.github.com), each checked
// against that page and the laptop's ~/.ssh/known_hosts on the day, in
// OpenSSH's SHA256 fingerprint spelling. ALL THREE, KEYED BY TYPE, because the
// key the server presents is a negotiation this program cannot steer: libgit2
// orders libssh2's host-key preference from $HOME/.ssh/known_hosts, so a home
// that lists GitHub's ed25519 key negotiates ed25519 and an empty one (the
// tablet's) negotiates ECDSA. LIBGIT2'S OWN VALIDITY VERDICT — that same
// known_hosts file — IS IGNORED, so the file can neither widen nor narrow the
// pin and the answer is the same on every device.
//
// TWO HOST NAMES, ONE SERVER (architect 2026-09-27): `github.com` on port 22,
// and `ssh.github.com`, GitHub's SSH over port 443 — the same service with the
// same three keys and the same deploy keys, published for networks that block
// port 22 (docs.github.com, "Using SSH over the HTTPS port"), which is the
// remote both clones name (ssh://git@ssh.github.com:443/...). Measured on the
// day: `ssh-keyscan -p 443 ssh.github.com` presents exactly the three
// fingerprints below. The name is libgit2's parsed host, without the port.
struct PinnedHostKey {
    git_cert_ssh_raw_type_t type;
    const char*             sha256;  // base64, unpadded
};
constexpr PinnedHostKey kGitHubHostKeys[] = {
    {GIT_CERT_SSH_RAW_TYPE_KEY_ED25519,
     "+DiY3wvvV6TuJJhbpZisF/zLDA0zPMSvHdkr4UvCOqU"},
    {GIT_CERT_SSH_RAW_TYPE_KEY_ECDSA_256,
     "p2QAMXNIC1TJYWeIOttrVc98/R1BUFWu3/LiyKgUfQM"},
    {GIT_CERT_SSH_RAW_TYPE_RSA,
     "uNiVztksCsDhcc0u9e8BujQXVUpKZIDTMczCvj3tD2s"},
};
constexpr std::string_view kGitHubHosts[] = {"github.com", "ssh.github.com"};

// ---------------------------------------------------------------------------
// ownership
// ---------------------------------------------------------------------------

struct GitFree {
    void operator()(git_repository* p) const { git_repository_free(p); }
    void operator()(git_commit* p) const { git_commit_free(p); }
    void operator()(git_tree* p) const { git_tree_free(p); }
    void operator()(git_tree_entry* p) const { git_tree_entry_free(p); }
    void operator()(git_blob* p) const { git_blob_free(p); }
    void operator()(git_diff* p) const { git_diff_free(p); }
    void operator()(git_revwalk* p) const { git_revwalk_free(p); }
    void operator()(git_reference* p) const { git_reference_free(p); }
    void operator()(git_index* p) const { git_index_free(p); }
    void operator()(git_signature* p) const { git_signature_free(p); }
    void operator()(git_remote* p) const { git_remote_free(p); }
    void operator()(git_config* p) const { git_config_free(p); }
    void operator()(git_status_list* p) const { git_status_list_free(p); }
};
template <typename T>
using Owned = std::unique_ptr<T, GitFree>;

struct OwnedBuf {
    git_buf buf = GIT_BUF_INIT;
    OwnedBuf() = default;
    OwnedBuf(const OwnedBuf&)            = delete;
    OwnedBuf& operator=(const OwnedBuf&) = delete;
    ~OwnedBuf() { git_buf_dispose(&buf); }
};

// libgit2's own account of the last failure on this thread.
std::string last_error(const char* fallback) {
    const git_error* e = git_error_last();
    if (e != nullptr && e->message != nullptr && e->message[0] != '\0') {
        return e->message;
    }
    return fallback;
}

std::string oid_hex(const git_oid& oid) {
    char buf[GIT_OID_MAX_HEXSIZE + 1];
    git_oid_tostr(buf, sizeof(buf), &oid);
    return buf;
}

// A FULL object name only: libgit2's parser pads a short spelling with zeros,
// and a padded prefix is not the object the caller named.
bool parse_full_oid(const std::string& sha, git_oid& oid) {
    if (sha.size() != GIT_OID_SHA1_HEXSIZE) return false;
    return git_oid_fromstrn(&oid, sha.data(), sha.size()) == 0;
}

Owned<git_commit> lookup_commit(git_repository* repo, const std::string& sha) {
    git_oid oid;
    if (!parse_full_oid(sha, oid)) return nullptr;
    git_commit* c = nullptr;
    if (git_commit_lookup(&c, repo, &oid) < 0) return nullptr;
    return Owned<git_commit>(c);
}

Owned<git_tree> tree_of(const git_commit* commit) {
    git_tree* t = nullptr;
    if (git_commit_tree(&t, commit) < 0) return nullptr;
    return Owned<git_tree>(t);
}

// THE ACCEPTED PATHS ONE TREE-TO-TREE DIFF CHANGED (`old_tree` null = the
// empty tree), in the diff's own path order. Rename detection is never asked
// for (git_diff_find_similar is not called), so a move is its two raw halves,
// whatever the clone's `diff.renames` says. A deletion reports its old path.
bool diff_accepted_paths(git_repository* repo, git_tree* old_tree,
                         git_tree* new_tree, const GuiGitPathFilter& accept,
                         std::vector<std::string>& out) {
    out.clear();
    // The init FUNCTIONS, not the *_INIT macros, here and at the status read:
    // the macros spell a partial brace initializer, which the NDK's clang
    // reports under -Wextra (missing-field-initializers) where GCC does not.
    git_diff_options opts;
    git_diff_options_init(&opts, GIT_DIFF_OPTIONS_VERSION);
    opts.flags |= GIT_DIFF_SKIP_BINARY_CHECK;
    git_diff* raw = nullptr;
    if (git_diff_tree_to_tree(&raw, repo, old_tree, new_tree, &opts) < 0) {
        return false;
    }
    Owned<git_diff> diff(raw);
    const std::size_t n = git_diff_num_deltas(diff.get());
    for (std::size_t i = 0; i < n; ++i) {
        const git_diff_delta* d = git_diff_get_delta(diff.get(), i);
        const char* path = (d->status == GIT_DELTA_DELETED) ? d->old_file.path
                                                            : d->new_file.path;
        if (path != nullptr && accept(path)) out.emplace_back(path);
    }
    return true;
}

// THE PER-PARENT DIFFS OF ONE COMMIT, one accepted-path list per parent (one
// list against the empty tree for a root commit). Both readers — the walk's
// keep rule and the touched-path answer — are derived from these lists.
bool per_parent_accepted_paths(git_repository* repo, const git_commit* commit,
                               const GuiGitPathFilter&                accept,
                               std::vector<std::vector<std::string>>& out) {
    out.clear();
    Owned<git_tree> tree = tree_of(commit);
    if (!tree) return false;
    const unsigned int parents = git_commit_parentcount(commit);
    if (parents == 0) {
        out.emplace_back();
        return diff_accepted_paths(repo, nullptr, tree.get(), accept,
                                   out.back());
    }
    for (unsigned int i = 0; i < parents; ++i) {
        git_commit* p_raw = nullptr;
        if (git_commit_parent(&p_raw, commit, i) < 0) return false;
        Owned<git_commit> parent(p_raw);
        Owned<git_tree>   parent_tree = tree_of(parent.get());
        if (!parent_tree) return false;
        out.emplace_back();
        if (!diff_accepted_paths(repo, parent_tree.get(), tree.get(), accept,
                                 out.back())) {
            return false;
        }
    }
    return true;
}

// ---------------------------------------------------------------------------
// the remote session's callbacks (the fetch's and the push's)
// ---------------------------------------------------------------------------

// ONE REMOTE SESSION'S STATE, shared by the fetch and the push: the deploy
// key's two paths, what the callbacks learned about a failure, and the
// caller's cancel token (a fetch's alone; a push is never cancelled).
struct RemoteState {
    std::string public_key;
    std::string private_key;
    int         credential_asks = 0;
    bool        credential_refused = false;
    bool        key_not_offered    = false;  // the server takes no SSH key
    bool        host_key_refused   = false;
    // GITHUB'S HOST KEY WAS ACCEPTED: the connection stands, so a failure
    // after it that libgit2 files as a local class is this clone's own
    // (local_fetch_failure).
    bool        connected          = false;
    std::string rejected;  // a server-side refusal of the ref, verbatim
    const std::atomic<bool>* cancel = nullptr;
    bool cancelled() const {
        return cancel != nullptr && cancel->load(std::memory_order_relaxed);
    }
    // GitHub declined this device — the refusals no retry fixes.
    bool refused() const {
        return credential_refused || key_not_offered || host_key_refused;
    }
};

std::string base64_unpadded(const unsigned char* bytes, std::size_t n) {
    static constexpr char kAlphabet[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    for (std::size_t i = 0; i < n; i += 3) {
        const unsigned int b0 = bytes[i];
        const unsigned int b1 = (i + 1 < n) ? bytes[i + 1] : 0u;
        const unsigned int b2 = (i + 2 < n) ? bytes[i + 2] : 0u;
        const unsigned int v  = (b0 << 16) | (b1 << 8) | b2;
        out += kAlphabet[(v >> 18) & 63u];
        out += kAlphabet[(v >> 12) & 63u];
        if (i + 1 < n) out += kAlphabet[(v >> 6) & 63u];
        if (i + 2 < n) out += kAlphabet[v & 63u];
    }
    return out;
}

// THE CREDENTIAL IS THE DEPLOY KEY AND NOTHING ELSE. SSH key authentication
// only (an https remote never reaches here — open_remote_session refuses it
// first), and ONE OFFER: libgit2 asks again when the server refuses a key, and
// a second answer would be the same key refused forever, so the second ask
// ends the fetch or the push.
int remote_credentials(git_credential** out, const char* /*url*/,
                       const char* username_from_url, unsigned int allowed_types,
                       void* payload) {
    RemoteState& state = *static_cast<RemoteState*>(payload);
    if (state.cancelled()) return GIT_EUSER;
    if ((allowed_types & GIT_CREDENTIAL_SSH_KEY) == 0) {
        state.key_not_offered = true;
        return GIT_EUSER;
    }
    if (state.credential_asks++ > 0) {
        state.credential_refused = true;
        return GIT_EUSER;
    }
    const char* user = (username_from_url != nullptr && username_from_url[0])
                           ? username_from_url
                           : "git";
    return git_credential_ssh_key_new(out, user, state.public_key.c_str(),
                                      state.private_key.c_str(), nullptr);
}

// THE HOST-KEY PIN: the server must be GitHub — one of kGitHubHosts —
// presenting the published key of the type it negotiated, compared by SHA-256
// fingerprint. `valid` (libgit2's known_hosts verdict) is never read —
// kGitHubHostKeys owns why.
bool is_github_host(const char* host) {
    if (host == nullptr) return false;
    for (std::string_view h : kGitHubHosts) {
        if (std::string_view(host) == h) return true;
    }
    return false;
}

int remote_certificate_check(git_cert* cert, int /*valid*/, const char* host,
                             void* payload) {
    RemoteState& state = *static_cast<RemoteState*>(payload);
    if (state.cancelled()) return GIT_EUSER;
    bool match = false;
    if (cert != nullptr && cert->cert_type == GIT_CERT_HOSTKEY_LIBSSH2 &&
        is_github_host(host)) {
        const auto* hk = reinterpret_cast<const git_cert_hostkey*>(cert);
        if ((hk->type & GIT_CERT_SSH_SHA256) != 0 &&
            (hk->type & GIT_CERT_SSH_RAW) != 0) {
            const std::string seen =
                base64_unpadded(hk->hash_sha256, sizeof(hk->hash_sha256));
            for (const PinnedHostKey& k : kGitHubHostKeys) {
                if (k.type == hk->raw_type && seen == k.sha256) match = true;
            }
        }
    }
    if (match) {
        state.connected = true;
        return 0;
    }
    state.host_key_refused = true;
    return GIT_ECERTIFICATE;
}

// A SERVER-SIDE REFUSAL (a protected branch, a remote hook) arrives HERE and
// only here: git_remote_push itself returns success for it, so without this
// callback a rejected push would read as published.
int push_update_reference(const char* refname, const char* status,
                          void* payload) {
    RemoteState& state = *static_cast<RemoteState*>(payload);
    if (status != nullptr && state.rejected.empty()) {
        state.rejected = std::string(refname != nullptr ? refname : "?") +
                         " rejected: " + status;
    }
    return 0;
}

// THE FETCH'S CANCEL POINTS: every progress report and every ref update asks
// the token, so a cancelled fetch stops at the next thing libgit2 tells it
// (a ref update is reported after it is written, so the answer there stops
// the ones after it).
int fetch_sideband(const char* /*str*/, int /*len*/, void* payload) {
    return static_cast<RemoteState*>(payload)->cancelled() ? GIT_EUSER : 0;
}
int fetch_transfer_progress(const git_indexer_progress* /*stats*/,
                            void* payload) {
    return static_cast<RemoteState*>(payload)->cancelled() ? GIT_EUSER : 0;
}
int fetch_update_refs(const char* /*refname*/, const git_oid* /*a*/,
                      const git_oid* /*b*/, git_refspec* /*spec*/,
                      void* payload) {
    return static_cast<RemoteState*>(payload)->cancelled() ? GIT_EUSER : 0;
}

// THE SESSION'S PRELUDE, the fetch's and the push's alike: SSH ONLY
// (architect 2026-09-27) — the deploy key is the one credential this program
// holds and the host-key pin the one proof of the server, and neither exists on
// any other transport: an http(s) remote would need a token this program has
// not, and `git://` is unauthenticated end to end. The transport is PARSED
// (gui_git_is_ssh_url), never inferred from what the spelling is not: both SSH
// spellings pass, scp-style `git@github.com:...` and the URL form
// `ssh://git@ssh.github.com:443/...` both clones name (kGitHubHosts), and
// everything else refuses here, before a remote is looked up — the one ask,
// the last point before libgit2 chooses a transport from the spelling (the
// guard compares identities only). NO KEY IS A REFUSAL BEFORE ANY CONNECTION,
// never a crash,
// the path going to `diag`. Fills the key paths into `state`; false with
// `diag` set.
bool prepare_remote_session(const std::string& url, RemoteState& state,
                            std::string& diag) {
    if (!gui_git_is_ssh_url(url)) {
        diag = "'" + url + "' is not an SSH remote; only SSH is supported";
        return false;
    }
    const std::filesystem::path config = device_config_path();
    if (config.empty()) {
        diag = "no config home for the deploy key";
        return false;
    }
    const std::filesystem::path dir = config.parent_path();
    state.private_key = (dir / kDeployKeyName).string();
    state.public_key  = (dir / kDeployKeyPublicName).string();
    std::error_code ec;
    if (!std::filesystem::is_regular_file(state.private_key, ec)) {
        diag = "no deploy key at " + state.private_key;
        return false;
    }
    if (!std::filesystem::is_regular_file(state.public_key, ec)) {
        diag = "no deploy key at " + state.public_key;
        return false;
    }
    return true;
}

// The callbacks' account of a failed session, in the push's long-standing
// words.
std::string remote_failure(const RemoteState& state, const char* fallback) {
    if (state.host_key_refused) {
        return "GitHub's host key did not match the pinned key";
    }
    if (state.key_not_offered) {
        return "the remote offers no SSH key authentication";
    }
    if (state.credential_refused) {
        return "the deploy key at " + state.private_key + " was refused";
    }
    return last_error(fallback);
}

// A FETCH THAT FAILED IN THIS CLONE, NOT ON THE WIRE (GuiGitFetch::LocalFailed;
// codex round 6 over the git arc, 2026-09-28): ONCE GITHUB'S HOST KEY HAS BEEN
// ACCEPTED, a failure libgit2 files under a local class — the filesystem, a
// ref, the object database, the repository or its configuration. A ref's
// lock standing is GIT_ERROR_OS (probed 2026-09-28), and so is a full disk
// writing the fetched pack (indexer.c's "cannot extend packfile"). The
// connection is the condition because libgit2 files ONE transport failure
// under GIT_ERROR_OS, the socket's own "failed to connect" (streams/socket.c),
// and that can only happen before the host key is seen; after it, the wire's
// failures are GIT_ERROR_NET / GIT_ERROR_SSH and a received pack that does not
// index is GIT_ERROR_INDEXER, all left to Unreachable.
bool local_fetch_failure(const RemoteState& state) {
    const git_error* e = git_error_last();
    if (!state.connected || e == nullptr) return false;
    switch (e->klass) {
    case GIT_ERROR_OS:
    case GIT_ERROR_FILESYSTEM:
    case GIT_ERROR_REFERENCE:
    case GIT_ERROR_ODB:
    case GIT_ERROR_REPOSITORY:
    case GIT_ERROR_CONFIG:
        return true;
    default:
        return false;
    }
}

}  // namespace

// ---------------------------------------------------------------------------
// the transport (git_repo.h owns the contract)
// ---------------------------------------------------------------------------

// libgit2 picks the transport from the spelling: a known scheme prefix, else
// ANY colon makes it SSH (its scp-style reading), else a local directory. So
// the two admitted shapes are the ones it reads as SSH, and each is parsed
// strictly enough that nothing it would read another way gets through.
bool gui_git_is_ssh_url(std::string_view url) {
    if (url.empty()) return false;
    for (const char c : url) {
        const auto u = static_cast<unsigned char>(c);
        if (u <= 0x20 || u == 0x7f) return false;
    }
    constexpr std::string_view kSshScheme = "ssh://";
    if (url.starts_with(kSshScheme)) {
        // ssh://[user@]host[:port]/path — a host and a path both named.
        const std::string_view rest  = url.substr(kSshScheme.size());
        const std::size_t      slash = rest.find('/');
        if (slash == std::string_view::npos || slash + 1 == rest.size()) {
            return false;
        }
        std::string_view  host = rest.substr(0, slash);
        const std::size_t at   = host.rfind('@');
        if (at != std::string_view::npos) host = host.substr(at + 1);
        return !host.empty() && host.front() != ':';
    }
    // Every other scheme, and git's `<transport>::<address>` helper syntax.
    if (url.find("://") != std::string_view::npos) return false;
    if (url.find("::") != std::string_view::npos) return false;
    // scp-style user@host:path — a colon with no slash before it (a slash
    // first is a local path), a user and a host before it, a path after it.
    const std::size_t colon = url.find(':');
    if (colon == std::string_view::npos || colon + 1 == url.size()) {
        return false;
    }
    const std::size_t slash = url.find('/');
    if (slash != std::string_view::npos && slash < colon) return false;
    const std::string_view authority = url.substr(0, colon);
    const std::size_t      at        = authority.find('@');
    return at != std::string_view::npos && at > 0 &&
           at + 1 < authority.size();
}

// ---------------------------------------------------------------------------
// process-wide
// ---------------------------------------------------------------------------

void gui_git_init() {
    git_libgit2_init();
    // DURABLE OBJECT WRITES: git's own `core.fsync=committed` (the laptop's
    // ~/.gitconfig sets it) is a key libgit2 does not read, so the durability
    // it asks for is this process-wide switch instead.
    git_libgit2_opts(GIT_OPT_ENABLE_FSYNC_GITDIR, 1);
    // The network time bound (kServerTimeoutMs owns the reasoning).
    git_libgit2_opts(GIT_OPT_SET_SERVER_CONNECT_TIMEOUT, kServerTimeoutMs);
    git_libgit2_opts(GIT_OPT_SET_SERVER_TIMEOUT, kServerTimeoutMs);
    // OWNER VALIDATION OFF, on both devices (architect 2026-09-27). libgit2
    // refuses a clone whose work tree or .git is owned by anyone but this
    // process's user (or root) — git's safe.directory rule, whose premise
    // (CVE-2022-24765) is a HOSTILE USER's repository above a victim's working
    // directory: the owner is taken to be the only trusted writer. That
    // premise does not hold on the tablet, whose clone lives on external
    // storage with TWO LEGITIMATE WRITER UIDS BY DESIGN — adb's shell places
    // the audio and the clone itself, the app writes the sidecars — so its
    // shell-owned .git would read "not owned by current user" and every git
    // question would fail as "History is unavailable", and any later adb push
    // under .git would do the same again. On the laptop, a single-user
    // machine, there is no hostile user for the rule to guard against. The
    // clone opened is always the one derived from the loaded source
    // (resolve_repo_root_for_source), never a directory the process wandered
    // into.
    git_libgit2_opts(GIT_OPT_SET_OWNER_VALIDATION, 0);
}

GuiGitRoot gui_git_discover_root(const std::string& dir, std::string& root,
                                 std::string& diag) {
    root.clear();
    diag.clear();
    git_repository* raw = nullptr;
    const int rc = git_repository_open_ext(&raw, dir.c_str(), 0, nullptr);
    if (rc == GIT_ENOTFOUND) return GuiGitRoot::NotAClone;
    if (rc < 0) {
        diag = last_error("libgit2 could not search for a repository");
        return GuiGitRoot::CouldNotAsk;
    }
    Owned<git_repository> repo(raw);
    const char* workdir = git_repository_workdir(repo.get());
    if (workdir == nullptr) return GuiGitRoot::NotAClone;  // bare: no work tree
    root = workdir;
    // libgit2 spells a work tree with a trailing slash; the root is a
    // directory name, never a prefix.
    while (root.size() > 1 && root.back() == '/') root.pop_back();
    return GuiGitRoot::Found;
}

// ---------------------------------------------------------------------------
// the handle
// ---------------------------------------------------------------------------

std::optional<GuiGitRepo> GuiGitRepo::open(const std::string& root,
                                           std::string&       diag) {
    diag.clear();
    git_repository* raw = nullptr;
    if (git_repository_open_ext(&raw, root.c_str(),
                                GIT_REPOSITORY_OPEN_NO_SEARCH, nullptr) < 0) {
        diag = last_error("libgit2 could not open the repository");
        return std::nullopt;
    }
    return GuiGitRepo(raw);
}

GuiGitRepo::GuiGitRepo(GuiGitRepo&& other) noexcept
    : repo_(std::exchange(other.repo_, nullptr)) {}

GuiGitRepo& GuiGitRepo::operator=(GuiGitRepo&& other) noexcept {
    if (this != &other) {
        if (repo_ != nullptr) git_repository_free(repo_);
        repo_ = std::exchange(other.repo_, nullptr);
    }
    return *this;
}

GuiGitRepo::~GuiGitRepo() {
    if (repo_ != nullptr) git_repository_free(repo_);
}

std::string GuiGitRepo::head_commit() const {
    git_oid oid;
    if (git_reference_name_to_id(&oid, repo_, "HEAD") < 0) return {};
    return oid_hex(oid);
}

std::string GuiGitRepo::head_branch() const {
    if (git_repository_head_detached(repo_) != 0) return {};
    git_reference* raw = nullptr;
    if (git_repository_head(&raw, repo_) < 0) return {};  // unborn or unreadable
    Owned<git_reference> head(raw);
    if (git_reference_is_branch(head.get()) == 0) return {};
    const char* name = git_reference_shorthand(head.get());
    return (name != nullptr) ? std::string(name) : std::string();
}

bool GuiGitRepo::origin_fetch_url(std::string& url) const {
    url.clear();
    git_config* raw = nullptr;
    if (git_repository_config(&raw, repo_) < 0) return false;
    Owned<git_config> cfg(raw);
    OwnedBuf value;
    if (git_config_get_string_buf(&value.buf, cfg.get(), "remote.origin.url") <
        0) {
        return false;
    }
    url.assign(value.buf.ptr, value.buf.size);
    return true;
}

bool GuiGitRepo::origin_push_urls(std::vector<std::string>& urls) const {
    urls.clear();
    git_config* raw = nullptr;
    if (git_repository_config(&raw, repo_) < 0) return false;
    Owned<git_config> cfg(raw);
    const int rc = git_config_get_multivar_foreach(
        cfg.get(), "remote.origin.pushurl", nullptr,
        [](const git_config_entry* entry, void* payload) -> int {
            static_cast<std::vector<std::string>*>(payload)->emplace_back(
                entry->value != nullptr ? entry->value : "");
            return 0;
        },
        &urls);
    if (rc < 0 && rc != GIT_ENOTFOUND) return false;
    if (!urls.empty()) return true;
    std::string fetch;
    if (!origin_fetch_url(fetch)) return false;
    urls.push_back(std::move(fetch));
    return true;
}

bool GuiGitRepo::walk_head(const GuiGitPathFilter&   accept,
                           std::vector<std::string>& shas,
                           std::string&              diag) const {
    shas.clear();
    diag.clear();
    git_revwalk* w_raw = nullptr;
    if (git_revwalk_new(&w_raw, repo_) < 0) {
        diag = last_error("could not start a revision walk");
        return false;
    }
    Owned<git_revwalk> walk(w_raw);
    git_revwalk_sorting(walk.get(), GIT_SORT_NONE);
    if (git_revwalk_push_head(walk.get()) < 0) {
        diag = last_error("could not read HEAD");
        return false;
    }
    git_oid oid;
    int     rc = 0;
    std::vector<std::vector<std::string>> per_parent;
    while ((rc = git_revwalk_next(&oid, walk.get())) == 0) {
        git_commit* c_raw = nullptr;
        if (git_commit_lookup(&c_raw, repo_, &oid) < 0) {
            diag = last_error("could not read a commit");
            return false;
        }
        Owned<git_commit> commit(c_raw);
        if (!per_parent_accepted_paths(repo_, commit.get(), accept,
                                       per_parent)) {
            diag = last_error("could not diff a commit");
            return false;
        }
        bool differs_from_every_parent = true;
        for (const std::vector<std::string>& paths : per_parent) {
            if (paths.empty()) differs_from_every_parent = false;
        }
        if (differs_from_every_parent) shas.push_back(oid_hex(oid));
    }
    if (rc != GIT_ITEROVER) {
        diag = last_error("the revision walk did not finish");
        return false;
    }
    return true;
}

bool GuiGitRepo::changed_paths(const std::string&        sha,
                               const GuiGitPathFilter&   accept,
                               std::vector<std::string>& paths) const {
    paths.clear();
    Owned<git_commit> commit = lookup_commit(repo_, sha);
    if (!commit) return false;
    std::vector<std::vector<std::string>> per_parent;
    if (!per_parent_accepted_paths(repo_, commit.get(), accept, per_parent)) {
        return false;
    }
    // One parent (or none): its list. A merge: the paths every parent's list
    // carries, in the first parent's order.
    for (const std::string& p : per_parent.front()) {
        bool in_every = true;
        for (std::size_t i = 1; i < per_parent.size(); ++i) {
            bool found = false;
            for (const std::string& q : per_parent[i]) {
                if (q == p) { found = true; break; }
            }
            if (!found) { in_every = false; break; }
        }
        if (in_every) paths.push_back(p);
    }
    return true;
}

bool GuiGitRepo::tree_paths(const std::string&        sha,
                            const GuiGitPathFilter&   accept,
                            std::vector<std::string>& paths) const {
    paths.clear();
    Owned<git_commit> commit = lookup_commit(repo_, sha);
    if (!commit) return false;
    Owned<git_tree> tree = tree_of(commit.get());
    if (!tree) return false;
    struct Walk {
        const GuiGitPathFilter*   accept;
        std::vector<std::string>* paths;
    } walk{&accept, &paths};
    const int rc = git_tree_walk(
        tree.get(), GIT_TREEWALK_PRE,
        [](const char* root, const git_tree_entry* entry, void* payload) -> int {
            if (git_tree_entry_type(entry) != GIT_OBJECT_BLOB) return 0;
            Walk& w = *static_cast<Walk*>(payload);
            std::string path = std::string(root) + git_tree_entry_name(entry);
            if ((*w.accept)(path)) w.paths->push_back(std::move(path));
            return 0;
        },
        &walk);
    return rc == 0;
}

bool GuiGitRepo::read_blob(const std::string& sha, const std::string& path,
                           std::string& bytes) const {
    bytes.clear();
    Owned<git_commit> commit = lookup_commit(repo_, sha);
    if (!commit) return false;
    Owned<git_tree> tree = tree_of(commit.get());
    if (!tree) return false;
    git_tree_entry* e_raw = nullptr;
    if (git_tree_entry_bypath(&e_raw, tree.get(), path.c_str()) < 0) {
        return false;
    }
    Owned<git_tree_entry> entry(e_raw);
    if (git_tree_entry_type(entry.get()) != GIT_OBJECT_BLOB) return false;
    git_blob* b_raw = nullptr;
    if (git_blob_lookup(&b_raw, repo_, git_tree_entry_id(entry.get())) < 0) {
        return false;
    }
    Owned<git_blob> blob(b_raw);
    const auto* data = static_cast<const char*>(git_blob_rawcontent(blob.get()));
    const auto  size = static_cast<std::size_t>(git_blob_rawsize(blob.get()));
    if (size > 0) bytes.assign(data, size);
    return true;
}

bool GuiGitRepo::is_commit(const std::string& sha) const {
    return static_cast<bool>(lookup_commit(repo_, sha));
}

GuiGitPathStatus GuiGitRepo::status_of(const std::vector<std::string>& paths,
                                       const std::string& branch,
                                       bool&              publication_owed,
                                       std::string&       diag) const {
    publication_owed = false;
    diag.clear();

    std::vector<char*> specs;
    specs.reserve(paths.size());
    for (const std::string& p : paths) specs.push_back(const_cast<char*>(p.c_str()));
    git_status_options opts;
    git_status_options_init(&opts, GIT_STATUS_OPTIONS_VERSION);
    opts.show  = GIT_STATUS_SHOW_INDEX_AND_WORKDIR;
    // Untracked paths count (a sidecar new to its folder is exactly that) —
    // and a path inside an UNTRACKED FOLDER (a new piece's) is reported as
    // itself, not folded into its folder's entry, which no literal path would
    // match. Every path is its own literal spelling, never a glob.
    opts.flags = GIT_STATUS_OPT_INCLUDE_UNTRACKED |
                 GIT_STATUS_OPT_RECURSE_UNTRACKED_DIRS |
                 GIT_STATUS_OPT_DISABLE_PATHSPEC_MATCH;
    opts.pathspec.strings = specs.data();
    opts.pathspec.count   = specs.size();
    git_status_list* s_raw = nullptr;
    if (git_status_list_new(&s_raw, repo_, &opts) < 0) {
        diag = last_error("could not read the status");
        return GuiGitPathStatus::Unavailable;
    }
    Owned<git_status_list> list(s_raw);
    bool dirty = false;
    const std::size_t n = git_status_list_entrycount(list.get());
    for (std::size_t i = 0; i < n; ++i) {
        const git_status_entry* e = git_status_byindex(list.get(), i);
        if (e != nullptr && e->status != GIT_STATUS_CURRENT) dirty = true;
    }

    // THE PUBLICATION READING, compare_with_origin's own: the captured
    // branch AHEAD of `refs/remotes/origin/<branch>` owes a push. No branch
    // (a detached or unborn HEAD) owes nothing.
    if (!branch.empty()) {
        const GuiGitUpstream up = compare_with_origin(branch);
        if (up.reading != GuiGitUpstream::Reading::Compared) {
            diag = last_error("could not compare with origin's branch");
            return GuiGitPathStatus::Unavailable;
        }
        publication_owed = up.ahead > 0;
    }
    return dirty ? GuiGitPathStatus::Dirty : GuiGitPathStatus::Clean;
}

// ---------------------------------------------------------------------------
// THE FENCE — the five mutations and their three callers (git_repo.h)
// ---------------------------------------------------------------------------

bool GuiGitRepo::stage_paths(const std::vector<std::string>& paths,
                             std::string&                    diag) {
    diag.clear();
    git_index* raw = nullptr;
    if (git_repository_index(&raw, repo_) < 0) {
        diag = last_error("could not open the index");
        return false;
    }
    Owned<git_index> index(raw);
    if (git_index_read(index.get(), 0) < 0) {
        diag = last_error("could not read the index");
        return false;
    }
    for (const std::string& p : paths) {
        if (git_index_add_bypath(index.get(), p.c_str()) < 0) {
            diag = last_error("could not stage a path");
            return false;
        }
    }
    if (git_index_write(index.get()) < 0) {
        diag = last_error("could not write the index");
        return false;
    }
    return true;
}

bool GuiGitRepo::commit_paths(const std::vector<std::string>& paths,
                              const std::string& title, std::string& diag) {
    diag.clear();
    auto fail = [&diag](const char* fallback) {
        diag = last_error(fallback);
        return false;
    };

    git_reference* h_raw = nullptr;
    if (git_repository_head(&h_raw, repo_) < 0) return fail("could not read HEAD");
    Owned<git_reference> head(h_raw);
    git_object* c_obj = nullptr;
    if (git_reference_peel(&c_obj, head.get(), GIT_OBJECT_COMMIT) < 0) {
        return fail("HEAD is not a commit");
    }
    Owned<git_commit> head_commit(reinterpret_cast<git_commit*>(c_obj));
    Owned<git_tree>   head_tree = tree_of(head_commit.get());
    if (!head_tree) return fail("could not read HEAD's tree");

    // THE ONLY TREE: HEAD's, with the index's entries for `paths` laid over it,
    // built in a scratch index so nothing else the real index holds can ride.
    git_index* i_raw = nullptr;
    if (git_repository_index(&i_raw, repo_) < 0) {
        return fail("could not open the index");
    }
    Owned<git_index> index(i_raw);
    git_index* m_raw = nullptr;
    if (git_index_new(&m_raw) < 0) return fail("could not build an index");
    Owned<git_index> only(m_raw);
    if (git_index_read_tree(only.get(), head_tree.get()) < 0) {
        return fail("could not read HEAD's tree into an index");
    }
    for (const std::string& p : paths) {
        const git_index_entry* e = git_index_get_bypath(index.get(), p.c_str(), 0);
        if (e == nullptr) {
            diag = "'" + p + "' is not staged";
            return false;
        }
        if (git_index_add(only.get(), e) < 0) return fail("could not add a path");
    }
    git_oid tree_oid;
    if (git_index_write_tree_to(&tree_oid, only.get(), repo_) < 0) {
        return fail("could not write the tree");
    }
    if (git_oid_equal(&tree_oid, git_tree_id(head_tree.get())) != 0) {
        diag = "no changes added to commit";
        return false;
    }
    git_tree* t_raw = nullptr;
    if (git_tree_lookup(&t_raw, repo_, &tree_oid) < 0) {
        return fail("could not read the new tree");
    }
    Owned<git_tree> tree(t_raw);

    // `git commit -m`'s own cleanup: trailing whitespace off every line, runs
    // of blank lines collapsed, blank lines at either end dropped, one final
    // newline; a `#` line is kept (git strips comments only from an edited
    // message).
    OwnedBuf message;
    if (git_message_prettify(&message.buf, title.c_str(), 0, '#') < 0) {
        return fail("could not clean up the message");
    }

    git_signature* a_raw = nullptr;
    git_signature* m_sig = nullptr;
    if (git_signature_default_from_env(&a_raw, &m_sig, repo_) < 0) {
        return fail("no commit identity (user.name / user.email)");
    }
    Owned<git_signature> author(a_raw);
    Owned<git_signature> committer(m_sig);

    // "HEAD" moves the checked-out branch, and libgit2 refuses when that
    // branch no longer points at the parent this tree was built on.
    const git_commit* parents[] = {head_commit.get()};
    git_oid commit_oid;
    if (git_commit_create(&commit_oid, repo_, "HEAD", author.get(),
                          committer.get(), nullptr, message.buf.ptr, tree.get(),
                          1, parents) < 0) {
        return fail("could not create the commit");
    }
    return true;
}

bool GuiGitRepo::push_branch(const std::string& branch,
                             const std::string& destination_url,
                             std::string&       diag) {
    diag.clear();
    RemoteState state;
    if (!prepare_remote_session(destination_url, state, diag)) return false;
    if (branch.empty()) {
        diag = "no branch to push";
        return false;
    }
    // THE NAMED `origin`, so libgit2's post-push tip update moves
    // `refs/remotes/origin/<branch>`; its URL for this push alone is the
    // guard's, set on the instance (never written to the configuration).
    git_remote* r_raw = nullptr;
    if (git_remote_lookup(&r_raw, repo_, "origin") < 0) {
        diag = last_error("no remote 'origin'");
        return false;
    }
    Owned<git_remote> remote(r_raw);
    if (git_remote_set_instance_pushurl(remote.get(),
                                        destination_url.c_str()) < 0) {
        diag = last_error("could not set the push URL");
        return false;
    }

    git_push_options opts;
    git_push_options_init(&opts, GIT_PUSH_OPTIONS_VERSION);
    opts.callbacks.credentials           = remote_credentials;
    opts.callbacks.certificate_check     = remote_certificate_check;
    opts.callbacks.push_update_reference = push_update_reference;
    opts.callbacks.payload               = &state;

    const std::string ref  = "refs/heads/" + branch;
    std::string       spec = ref + ":" + ref;
    char*             spec_ptr = spec.data();
    const git_strarray refspecs{&spec_ptr, 1};

    if (git_remote_push(remote.get(), &refspecs, &opts) < 0) {
        diag = remote_failure(state, "the push failed");
        return false;
    }
    if (!state.rejected.empty()) {
        diag = state.rejected;
        return false;
    }
    return true;
}

GuiGitFetch GuiGitRepo::fetch_origin(const std::string&       source_url,
                                     const std::string&       branch,
                                     const std::atomic<bool>* cancel,
                                     std::string&             diag) {
    diag.clear();
    RemoteState state;
    state.cancel = cancel;
    if (!prepare_remote_session(source_url, state, diag)) {
        return GuiGitFetch::Refused;
    }
    if (branch.empty()) {
        diag = "no branch to fetch";
        return GuiGitFetch::Refused;
    }
    // AN UNNAMED REMOTE ON THE GUARD'S URL (git_repo.h), SKIP_INSTEADOF so
    // the URL is used verbatim.
    git_remote_create_options create;
    git_remote_create_options_init(&create, GIT_REMOTE_CREATE_OPTIONS_VERSION);
    create.repository = repo_;
    create.name       = nullptr;
    create.flags      = GIT_REMOTE_CREATE_SKIP_INSTEADOF;
    git_remote* r_raw = nullptr;
    if (git_remote_create_with_opts(&r_raw, source_url.c_str(), &create) < 0) {
        diag = last_error("could not set the fetch URL");
        return GuiGitFetch::LocalFailed;
    }
    Owned<git_remote> remote(r_raw);

    git_fetch_options opts;
    git_fetch_options_init(&opts, GIT_FETCH_OPTIONS_VERSION);
    opts.download_tags     = GIT_REMOTE_DOWNLOAD_TAGS_NONE;
    opts.update_fetchhead  = 0;
    opts.prune             = GIT_FETCH_NO_PRUNE;
    opts.callbacks.credentials       = remote_credentials;
    opts.callbacks.certificate_check = remote_certificate_check;
    opts.callbacks.sideband_progress = fetch_sideband;
    opts.callbacks.transfer_progress = fetch_transfer_progress;
    opts.callbacks.update_refs       = fetch_update_refs;
    opts.callbacks.payload           = &state;

    // THE ONE REFSPEC: origin's `<branch>` into the one ref every ahead/behind
    // reads (compare_with_origin), forced like git's default refspec.
    const std::string source  = "refs/heads/" + branch;
    const std::string tracked = "refs/remotes/origin/" + branch;
    std::string       spec    = "+" + source + ":" + tracked;
    char*             spec_ptr = spec.data();
    const git_strarray refspecs{&spec_ptr, 1};
    if (git_remote_fetch(remote.get(), &refspecs, &opts, nullptr) < 0) {
        if (state.cancelled()) {
            diag = "cancelled";
            return GuiGitFetch::Cancelled;
        }
        diag = remote_failure(state, "the fetch failed");
        if (state.refused()) return GuiGitFetch::Refused;
        return local_fetch_failure(state) ? GuiGitFetch::LocalFailed
                                          : GuiGitFetch::Unreachable;
    }
    if (state.cancelled()) {
        diag = "cancelled";
        return GuiGitFetch::Cancelled;
    }
    return GuiGitFetch::Fetched;
}

GuiGitUpstream GuiGitRepo::compare_with_origin(
        const std::string& branch) const {
    GuiGitUpstream out;
    const std::string local_ref = "refs/heads/" + branch;
    git_oid local;
    if (git_reference_name_to_id(&local, repo_, local_ref.c_str()) < 0) {
        return out;  // Unreadable
    }
    const std::string tracked = "refs/remotes/origin/" + branch;
    git_oid up;
    if (git_reference_name_to_id(&up, repo_, tracked.c_str()) < 0) {
        return out;  // Unreadable
    }
    if (git_graph_ahead_behind(&out.ahead, &out.behind, repo_, &local, &up) <
        0) {
        return out;
    }
    out.upstream_sha = oid_hex(up);
    out.reading      = GuiGitUpstream::Reading::Compared;
    return out;
}

bool GuiGitRepo::paths_changed_between(const std::string&        from_sha,
                                       const std::string&        to_sha,
                                       const GuiGitPathFilter&   accept,
                                       std::vector<std::string>& paths) const {
    paths.clear();
    Owned<git_commit> from = lookup_commit(repo_, from_sha);
    Owned<git_commit> to   = lookup_commit(repo_, to_sha);
    if (!from || !to) return false;
    Owned<git_tree> from_tree = tree_of(from.get());
    Owned<git_tree> to_tree   = tree_of(to.get());
    if (!from_tree || !to_tree) return false;
    return diff_accepted_paths(repo_, from_tree.get(), to_tree.get(), accept,
                               paths);
}

GuiGitFastForward GuiGitRepo::fast_forward(
        const std::string& branch, const std::string& from_sha,
        const std::string& to_sha, const std::vector<std::string>& exclude,
        const std::vector<std::string>& force, std::string& conflict_path,
        std::string& diag) {
    conflict_path.clear();
    diag.clear();
    git_oid from_oid, to_oid;
    if (!parse_full_oid(from_sha, from_oid) || !parse_full_oid(to_sha, to_oid)) {
        diag = "not a full commit name";
        return GuiGitFastForward::NotStarted;
    }
    Owned<git_commit> to = lookup_commit(repo_, to_sha);
    if (!to) {
        diag = last_error("could not read the commit to move to");
        return GuiGitFastForward::NotStarted;
    }
    Owned<git_tree> to_tree = tree_of(to.get());
    if (!to_tree) {
        diag = last_error("could not read its tree");
        return GuiGitFastForward::NotStarted;
    }
    std::vector<std::string> changed;
    if (!paths_changed_between(from_sha, to_sha,
                               [](std::string_view) { return true; },
                               changed)) {
        diag = last_error("could not diff the two commits");
        return GuiGitFastForward::NotStarted;
    }
    auto listed = [](const std::vector<std::string>& list,
                     const std::string& p) {
        for (const std::string& q : list) {
            if (q == p) return true;
        }
        return false;
    };
    std::vector<std::string> safe;
    for (const std::string& p : changed) {
        if (!listed(exclude, p) && !listed(force, p)) safe.push_back(p);
    }

    // (1) SAFE: libgit2 compares every named path's working-tree file and
    // index entry against the baseline — the checked-out tree, `from_sha`'s —
    // and refuses the WHOLE checkout, before writing anything, when one differs
    // or an untracked file stands in the way. The notify callback names the
    // first such path. Paths are literal (no pathspec matching).
    if (!safe.empty()) {
        std::vector<char*> specs;
        for (std::string& p : safe) specs.push_back(p.data());
        git_checkout_options co;
        git_checkout_options_init(&co, GIT_CHECKOUT_OPTIONS_VERSION);
        co.checkout_strategy =
            GIT_CHECKOUT_SAFE | GIT_CHECKOUT_DISABLE_PATHSPEC_MATCH;
        co.paths.strings = specs.data();
        co.paths.count   = specs.size();
        co.notify_flags  = GIT_CHECKOUT_NOTIFY_CONFLICT;
        co.notify_cb = [](git_checkout_notify_t, const char* path,
                          const git_diff_file*, const git_diff_file*,
                          const git_diff_file*, void* payload) -> int {
            std::string& first = *static_cast<std::string*>(payload);
            if (first.empty() && path != nullptr) first = path;
            return 0;
        };
        co.notify_payload = &conflict_path;
        const int rc = git_checkout_tree(
            repo_, reinterpret_cast<const git_object*>(to_tree.get()), &co);
        if (rc == GIT_ECONFLICT || (rc < 0 && !conflict_path.empty())) {
            diag = last_error("the checkout met local changes");
            return GuiGitFastForward::Conflict;
        }
        if (rc < 0) {
            diag = last_error("the checkout failed");
            return GuiGitFastForward::FilesFailed;
        }
    }
    // (2) FORCED, whatever the working tree holds.
    if (!force.empty()) {
        std::vector<std::string> owned = force;
        std::vector<char*>       specs;
        for (std::string& p : owned) specs.push_back(p.data());
        git_checkout_options co;
        git_checkout_options_init(&co, GIT_CHECKOUT_OPTIONS_VERSION);
        co.checkout_strategy =
            GIT_CHECKOUT_FORCE | GIT_CHECKOUT_DISABLE_PATHSPEC_MATCH;
        co.paths.strings = specs.data();
        co.paths.count   = specs.size();
        if (git_checkout_tree(
                repo_, reinterpret_cast<const git_object*>(to_tree.get()),
                &co) < 0) {
            diag = last_error("the forced checkout failed");
            return GuiGitFastForward::FilesFailed;
        }
    }
    // (3) THE INDEX IS THE NEW TREE, whole.
    git_index* i_raw = nullptr;
    if (git_repository_index(&i_raw, repo_) < 0) {
        diag = last_error("could not open the index");
        return GuiGitFastForward::IndexFailed;
    }
    Owned<git_index> index(i_raw);
    if (git_index_read_tree(index.get(), to_tree.get()) < 0 ||
        git_index_write(index.get()) < 0) {
        diag = last_error("could not write the index");
        return GuiGitFastForward::IndexFailed;
    }
    // (4) THE BRANCH MOVES, only from the commit the pull was planned on.
    git_reference* moved = nullptr;
    const std::string ref = "refs/heads/" + branch;
    if (git_reference_create_matching(&moved, repo_, ref.c_str(), &to_oid,
                                      /*force=*/1, &from_oid,
                                      "pull: fast-forward") < 0) {
        diag = last_error("could not move the branch");
        return GuiGitFastForward::BranchFailed;
    }
    git_reference_free(moved);
    return GuiGitFastForward::Done;
}

void GuiGitRepo::clear_stale_locks(const std::string& branch) {
    namespace fs = std::filesystem;
    // The index is the work tree's own (gitdir); refs, packed-refs and the
    // object store are shared (commondir) — one directory in an ordinary
    // clone.
    const fs::path gitdir = git_repository_path(repo_);
    const fs::path common = git_repository_commondir(repo_);
    std::vector<fs::path> stale = {gitdir / "index.lock"};
    if (!branch.empty()) {
        stale.push_back(common / "refs" / "heads" / (branch + ".lock"));
        stale.push_back(common / "refs" / "remotes" / "origin" /
                        (branch + ".lock"));
    }
    // THE FETCH'S INDEXER (libgit2 1.9.7, indexer.c): the pack streams into a
    // temporary `pack_git2_<16 hex>` (git_futils_mktmp) and its index is
    // written through a lock `pack_git2_<12 hex>idx.lock`, both renamed only
    // once whole; the prefix is libgit2's alone (git's own are `tmp_pack_*`).
    std::error_code ec;
    for (fs::directory_iterator it(common / "objects" / "pack", ec), end;
         !ec && it != end; it.increment(ec)) {
        if (it->path().filename().string().rfind("pack_git2_", 0) == 0) {
            stale.push_back(it->path());
        }
    }
    for (const fs::path& p : stale) {
        std::error_code sec;
        if (!fs::is_regular_file(fs::symlink_status(p, sec))) continue;
        if (fs::remove(p, sec)) {
            std::fprintf(stderr,
                         "warptempo_gui: removed '%s', left by an earlier run "
                         "that ended mid-write\n",
                         p.c_str());
        } else if (sec) {
            std::fprintf(stderr,
                         "warptempo_gui: could not remove '%s', left by an "
                         "earlier run that ended mid-write (%s)\n",
                         p.c_str(), sec.message().c_str());
        }
    }
}
