"""Break self-check for the M2-7b file tree (QFileSystemModel + proxy).

Injects a deliberate bug and verifies that the regression test goes RED.
Then restores the file byte for byte.

Four bugs are injected, one after the other, because they cover the four
independent halves of the feature:

  1. "the extension filter is gone"
     -- every file passes the filter again, so the unsupported files show
        up in the tree.  Caught by unsupportedFilesAreHidden() and
        unsupportedFilesAreHiddenInEveryLevel().

  2. "the tree is navigated by a cached proxy index"
     -- InfoTreeDock caches the proxy index of a row instead of taking it
        fresh from the path.  A proxy index does not survive a change of
        the source model, and the read of a directory IS such a change:
        the cached index then points at freed memory and the next use of
        it crashes with
            ASSERT: "it != source_index_mapping.constEnd()"
        Caught by the whole test binary (the crash aborts it).

  3. "an empty directory keeps its expand arrow"
     -- hasChildren() answers "yes" for every directory, which is the
        defect the hand written model had (UI-07).  Caught by
        emptyDirectoryHasNoExpandArrow().

  4. "the directories that have been read are forgotten"
     -- the set of loaded directories is never filled, so the arrow of
        every directory stays optimistic.  Caught by
        emptyDirectoryHasNoExpandArrow() and
        nonEmptyDirectoryKeepsItsExpandArrow().

The files are restored byte for byte afterwards (hash comparison is
printed).
"""
import hashlib
import io
import os
import shutil
import subprocess
import sys
import tempfile

ROOT = r"D:\Collide\MyCode\QT6Projects\MultiDoc"
BUILD = os.path.join(ROOT, "build", "Desktop_Qt_6_11_1_MSVC2022_64bit_Debug")
QT_BIN = r"D:\QT6\6.11.1\msvc2022_64\bin"

PROXY = os.path.join(ROOT, "src", "ui", "FileTreeProxyModel.cpp")
DOCK = os.path.join(ROOT, "src", "ui", "infotreedock.cpp")

TREE_EXE = os.path.join(BUILD, "tst_M2_7_FileTree.exe")


# ---------------------------------------------------------------------------
# Bug 1: the extension filter is gone.
# ---------------------------------------------------------------------------
BUG1_OLD = """    // A file only passes when the registry knows its extension.
    return FileExtensionRegistry::isSupportedExtension(path);"""

BUG1_NEW = """    // BREAK SELF-CHECK: the extension filter is dropped, so the files
    // the registry does not know show up in the tree.
    return true;"""


# ---------------------------------------------------------------------------
# Bug 2: the dock caches a CHILD proxy index and uses it after the read.
# ---------------------------------------------------------------------------
BUG2_OLD = """    const QModelIndex rootIdx = m_proxy->indexForPath(m_rootPath);
    ui->tree->setRootIndex(rootIdx);"""

BUG2_NEW = """    // BREAK SELF-CHECK: the proxy index of the FIRST CHILD is cached and
    // used again after the model has changed. A proxy index does not
    // survive a change of the source model, and the read of a directory
    // is exactly such a change -- the cached index then points at freed
    // memory and the next use of it crashes with
    //     ASSERT: "it != source_index_mapping.constEnd()"
    if (m_cachedRoot.isValid()) {
        ui->tree->setRootIndex(m_cachedRoot);
        // The cached CHILD index is read here -- that is the crash.
        const QVariant name = m_cachedRoot.data(Qt::DisplayRole);
        if (!name.isValid()) return;
        ui->tree->setRootIndex(m_proxy->indexForPath(m_rootPath));
        return;
    }
    m_cachedRoot = m_proxy->indexForPath(m_rootPath);
    ui->tree->setRootIndex(m_cachedRoot);"""


# ---------------------------------------------------------------------------
# Bug 3: an empty directory keeps its expand arrow.
# ---------------------------------------------------------------------------
BUG3_OLD = """    if (m_loadedDirs.contains(QDir::cleanPath(path))) {
        // The directory has been read: the arrow shows only when there
        // really is something inside (the count is the FILTERED count,
        // so a directory whose files are all filtered out has no arrow
        // either).
        return QSortFilterProxyModel::rowCount(parent) > 0;
    }"""

BUG3_NEW = """    if (m_loadedDirs.contains(QDir::cleanPath(path))) {
        // BREAK SELF-CHECK: the arrow is always shown, even for a
        // directory that turned out to be empty (UI-07).
        return true;
    }"""


# ---------------------------------------------------------------------------
# Bug 4: the loaded directories are forgotten.
# ---------------------------------------------------------------------------
BUG4_OLD = """    m_loadedDirs.insert(QDir::cleanPath(path));"""

BUG4_NEW = """    // BREAK SELF-CHECK: the directory is not remembered as read, so the
    // arrow of every directory stays optimistic.
    // m_loadedDirs.insert(QDir::cleanPath(path));"""


BUGS = [
    ("bug 1 (the extension filter is gone)", PROXY, BUG1_OLD, BUG1_NEW),
    ("bug 2 (the tree is navigated by a cached proxy index)",
     DOCK, BUG2_OLD, BUG2_NEW),
    ("bug 3 (an empty directory keeps its expand arrow)",
     PROXY, BUG3_OLD, BUG3_NEW),
    ("bug 4 (the loaded directories are forgotten)",
     PROXY, BUG4_OLD, BUG4_NEW),
]


def read(path):
    with io.open(path, "r", encoding="utf-8", newline="") as f:
        return f.read()


def write(path, text):
    with io.open(path, "w", encoding="utf-8", newline="") as f:
        f.write(text)


def sha(path):
    with io.open(path, "rb") as f:
        return hashlib.sha256(f.read()).hexdigest()


def nl_of(text):
    return "\r\n" if "\r\n" in text else "\n"


def inject(path, old, new):
    """Replace `old` with `new` in `path`, keeping the file's newline style."""
    src = read(path)
    nl = nl_of(src)
    o = old.replace("\n", nl)
    n = new.replace("\n", nl)
    if o not in src:
        raise AssertionError("anchor not found in " + path + ": "
                             + old.splitlines()[0])
    write(path, src.replace(o, n, 1))


def build():
    r = subprocess.run(["cmd", "/c", "tools\\build.bat"], cwd=ROOT,
                       capture_output=True, text=True)
    out = (r.stdout or "") + (r.stderr or "")
    if "BUILD OK" not in out:
        print("[break] BUILD FAILED -- the injection does not compile")
        print(out[-3000:])
        return False
    print("[break] build ok")
    return True


def run_test(exe):
    env = dict(os.environ)
    env["PATH"] = QT_BIN + os.pathsep + env["PATH"]
    r = subprocess.run([exe], cwd=BUILD, capture_output=True, text=True, env=env)
    return (r.stdout or "") + (r.stderr or ""), r.returncode


def expect_red(label, out):
    red = ("FAIL!" in out) or ("Totals:" in out and " 0 failed" not in out)
    print("[break] %s -- test output:" % label)
    for line in out.splitlines():
        if ("FAIL!" in line or "Totals:" in line or "Actual" in line
                or "Expected" in line or "ASSERT" in line):
            print("   " + line.strip())
    if not red:
        print("[break] FAIL: the tests stayed GREEN with the injected bug -- "
              "they do not detect it")
        return False
    print("[break] OK: the regression tests went RED with the injected bug")
    return True


def main():
    if not os.path.exists(TREE_EXE):
        print("FAIL: the test binary does not exist: " + TREE_EXE)
        return 2

    touched = (PROXY, DOCK)
    hashes_before = {p: sha(p) for p in touched}

    backups = {}
    for path in touched:
        backup = tempfile.NamedTemporaryFile(delete=False, suffix=".cpp")
        with io.open(path, "rb") as f:
            backup.write(f.read())
        backup.close()
        backups[path] = backup.name

    ok = True
    try:
        for label, path, old, new in BUGS:
            inject(path, old, new)
            print("[break] injected: " + label)
            if not build():
                ok = False
            else:
                out, _ = run_test(TREE_EXE)
                if not expect_red(label, out):
                    ok = False
            # restore before the next injection
            shutil.copyfile(backups[path], path)

    finally:
        for path, backup in backups.items():
            shutil.copyfile(backup, path)
            os.unlink(backup)

        for path in touched:
            same = sha(path) == hashes_before[path]
            print("[restore] %s restored, byte identical: %s"
                  % (os.path.basename(path), same))
            if not same:
                return 2

    if not ok:
        return 1

    # The tree is back to its original state, so rebuild once more and make
    # sure the tests are green again.
    print("[break] rebuilding the restored tree")
    if not build():
        return 2
    out, _ = run_test(TREE_EXE)
    green = "Totals:" in out and " 0 failed" in out
    for line in out.splitlines():
        if "Totals:" in line:
            print("   " + os.path.basename(TREE_EXE) + ": " + line.strip())
    print("[break] restored tree is green:", green)
    return 0 if green else 1


if __name__ == "__main__":
    sys.exit(main())
