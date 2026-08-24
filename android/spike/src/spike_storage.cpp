#include "spike_storage.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <jni.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cctype>
#include <cstdarg>
#include <cstdio>
#include <cstring>

#include "spike_log.h"

namespace {

std::string fmt(const char* f, ...) {
    char buf[512];
    va_list ap;
    va_start(ap, f);
    vsnprintf(buf, sizeof buf, f, ap);
    va_end(ap);
    return std::string(buf);
}

bool is_fs_uuid(const char* name) {
    // FAT32/exFAT volumes are mounted at /storage/<FS-UUID>, e.g. /storage/1234-5678.
    if (std::strlen(name) != 9) return false;
    for (int i = 0; i < 9; ++i) {
        if (i == 4) {
            if (name[i] != '-') return false;
        } else if (!std::isxdigit(static_cast<unsigned char>(name[i]))) {
            return false;
        }
    }
    return true;
}

// open() + write() + fsync() + close(), reporting the errno of whichever step
// failed first. The file is left in place -- seeing it from a desktop is half the
// evidence.
std::string write_probe(const std::string& dir) {
    const std::string path = dir + "/warptempo_spike_probe.txt";
    const char payload[] = "warptempo M2 spike OTG write probe\n";

    const int fd = ::open(path.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) return fmt("open   FAILED errno=%d %s", errno, std::strerror(errno));

    const ssize_t n = ::write(fd, payload, sizeof payload - 1);
    if (n != static_cast<ssize_t>(sizeof payload - 1)) {
        const int e = errno;
        ::close(fd);
        return fmt("write  FAILED n=%zd errno=%d %s", n, e, std::strerror(e));
    }
    if (::fsync(fd) != 0) {
        const int e = errno;
        ::close(fd);
        return fmt("fsync  FAILED errno=%d %s", e, std::strerror(e));
    }
    if (::close(fd) != 0) return fmt("close  FAILED errno=%d %s", errno, std::strerror(errno));

    struct stat st {};
    if (::stat(path.c_str(), &st) != 0) {
        return fmt("OK but stat FAILED errno=%d %s", errno, std::strerror(errno));
    }
    return fmt("OK  wrote %lld bytes, stat size %lld", static_cast<long long>(n),
               static_cast<long long>(st.st_size));
}

// Environment.isExternalStorageManager() -- static, framework, boot classpath.
// Returns -1 if the call could not be made at all, else 0/1.
int is_external_storage_manager(ANativeActivity* activity) {
    if (!activity || !activity->vm) return -1;
    JNIEnv* env = nullptr;
    const jint attached = activity->vm->AttachCurrentThread(&env, nullptr);
    if (attached != JNI_OK || env == nullptr) return -1;

    int result = -1;
    jclass cls = env->FindClass("android/os/Environment");
    if (cls != nullptr) {
        jmethodID mid = env->GetStaticMethodID(cls, "isExternalStorageManager", "()Z");
        if (mid != nullptr) {
            const jboolean v = env->CallStaticBooleanMethod(cls, mid);
            if (env->ExceptionCheck()) {
                env->ExceptionDescribe();
                env->ExceptionClear();
            } else {
                result = v ? 1 : 0;
            }
        } else if (env->ExceptionCheck()) {
            env->ExceptionClear();
        }
        env->DeleteLocalRef(cls);
    } else if (env->ExceptionCheck()) {
        env->ExceptionClear();
    }

    activity->vm->DetachCurrentThread();
    return result;
}

}  // namespace

void spike_storage_probe(ANativeActivity* activity, SpikeStorageReport& out) {
    out.lines.clear();
    out.ran = true;

    const int mgr = is_external_storage_manager(activity);
    out.lines.push_back(fmt("MANAGE_EXTERNAL_STORAGE granted: %s",
                            mgr < 0 ? "UNKNOWN (JNI call failed)" : (mgr ? "YES" : "no")));

    // Control 1: app-internal storage. If this fails the probe itself is broken.
    if (activity && activity->internalDataPath) {
        out.lines.push_back(fmt("internal %s", activity->internalDataPath));
        out.lines.push_back("   " + write_probe(activity->internalDataPath));
    }

    // Control 2: can /storage even be enumerated?
    DIR* d = ::opendir("/storage");
    if (!d) {
        out.lines.push_back(fmt("opendir /storage FAILED errno=%d %s", errno, std::strerror(errno)));
        return;
    }

    std::vector<std::string> all;
    std::vector<std::string> volumes;
    while (dirent* e = ::readdir(d)) {
        if (std::strcmp(e->d_name, ".") == 0 || std::strcmp(e->d_name, "..") == 0) continue;
        all.push_back(e->d_name);
        if (is_fs_uuid(e->d_name)) volumes.push_back(e->d_name);
    }
    ::closedir(d);

    std::string listing;
    for (size_t i = 0; i < all.size(); ++i) {
        if (i) listing += " ";
        listing += all[i];
    }
    out.lines.push_back(fmt("opendir /storage OK, %zu entries: %s", all.size(),
                            listing.empty() ? "(none visible)" : listing.c_str()));

    if (volumes.empty()) {
        out.lines.push_back("no XXXX-XXXX volume mounted -- plug the OTG stick in and tap again");
        return;
    }

    for (const std::string& v : volumes) {
        const std::string dir = "/storage/" + v;
        out.lines.push_back("volume " + dir);

        DIR* vd = ::opendir(dir.c_str());
        if (!vd) {
            out.lines.push_back(fmt("   opendir FAILED errno=%d %s", errno, std::strerror(errno)));
        } else {
            int count = 0;
            while (::readdir(vd)) ++count;
            ::closedir(vd);
            out.lines.push_back(fmt("   opendir OK, %d dirents", count));
        }
        out.lines.push_back("   " + write_probe(dir));
    }
}
