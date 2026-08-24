// The OTG probe -- the highest-value open question in the research doc (§5.6,
// §5.10): does All-files access actually permit a plain POSIX write under
// /storage/XXXX-XXXX, or is SAF unavoidable?
//
// The probe answers it the blunt way: enumerate /storage, pick out every volume
// whose name is an FS-UUID (XXXX-XXXX), and attempt open()+write()+fsync()+close()
// of a small file on each, reporting errno by name. Two controls ride along so a
// failure can be read: the app's own internal dir (must always succeed) and
// Environment.isExternalStorageManager() (the permission's live state).
//
// NO JAVA SLIVER IS NEEDED. isExternalStorageManager is a STATIC method on
// android.os.Environment, a framework class on the boot classpath, so JNI
// FindClass resolves it from a hasCode="false" APK. The sliver the research doc
// plans for is owed to onActivityResult, which this spike does not use.
#pragma once

#include <android/native_activity.h>

#include <string>
#include <vector>

struct SpikeStorageReport {
    // Ready to paint, one line each.
    std::vector<std::string> lines;
    bool ran = false;
};

// Runs synchronously; called from the glue thread on a tap. Attaches that thread
// to the JVM for the isExternalStorageManager read and detaches again.
void spike_storage_probe(ANativeActivity* activity, SpikeStorageReport& out);
