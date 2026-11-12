"""Break self-check for the M2-1 .vst project format and the auto-save.

Injects a deliberate bug and verifies that the EXPECTED regression test
goes RED. Then restores the file byte for byte.

Every injection targets the CAUSE of an effect the test asserts on, and
every injection names the test that must go red:

  bug 1 (the save keeps only the pixels)
     The layer loop writes only the FIRST layer's pixels and drops the
     rest -- the "flat picture with a different extension" defect that
     the whole format exists to prevent ->
     theSaveKeepsMoreThanThePixels.

  bug 2 (the mask is not saved)
     The mask block is skipped, so the read-back layer has no mask ->
     savesAndLoadsAPixelMask.

  bug 3 (the text is not saved)
     The text payload is skipped -> savesAndLoadsATextLayer.

  bug 4 (the vector paths are not saved)
     The vector payload is skipped -> savesAndLoadsAVectorLayer.

  bug 5 (the layer order is not preserved)
     The layers are written in REVERSE order, so the read-back stack is
     the wrong way round -> savesAndLoadsTheLayerOrder.

  bug 6 (a load merges into the old layers)
     The stack is not cleared before the load, so the old layers stay ->
     loadReplacesTheOldLayers.

  bug 7 (the auto-save writes even when the document is clean)
     The dirty check is ignored -> autoSaveDoesNothingWhenTheDocumentIsClean.

  bug 8 (the recovery copy outlives the save)
     removeRecovery does nothing -> theRecoveryCopyIsRemovedAfterASave.

  bug 9 (the blob names collide on a shared stableId)
     The pixel blob is named after layer.stableId instead of the
     file-internal layer index, so two layers that share a stableId share
     a pixel blob and one overwrites the other ->
     layersThatShareAStableIdKeepTheirOwnPixels.
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

VST = os.path.join(ROOT, "src", "media", "project", "VstProject.cpp")
AUTOSAVE = os.path.join(ROOT, "src", "media", "project", "AutoSave.cpp")

TEST_EXE = os.path.join(BUILD, "tst_M2_1_Project.exe")


# ---------------------------------------------------------------------------
# Bug 1: the save keeps only the pixels (the flat-picture defect).
#
# The layer loop is broken after the first layer, so the other layers
# are dropped from the manifest.
# ---------------------------------------------------------------------------
BUG1_OLD = """    for (int i = 0; i < stack->count(); ++i) {
        layers::LayerPtr l = stack->at(i);
        if (!l) continue;
        // The index is the position in THIS file's layer array, so it is
        // stable and unique even if two layers share a stableId.
        layersJson.append(layerToJson(*l, layersJson.size(), blobs));
    }"""

BUG1_NEW = """    // BREAK SELF-CHECK: only the first layer is written, so the save
    // keeps just the pixels and drops the structure.
    for (int i = 0; i < stack->count(); ++i) {
        layers::LayerPtr l = stack->at(i);
        if (!l) continue;
        layersJson.append(layerToJson(*l, layersJson.size(), blobs));
        break;
    }"""


# ---------------------------------------------------------------------------
# Bug 2: the mask is not saved.
# ---------------------------------------------------------------------------
BUG2_OLD = """    if (layer.mask.kind != layers::LayerMask::None) {
        o["maskKind"] = static_cast<int>(layer.mask.kind);"""

BUG2_NEW = """    // BREAK SELF-CHECK: the mask is not saved at all.
    if (false) {
        o["maskKind"] = static_cast<int>(layer.mask.kind);"""


# ---------------------------------------------------------------------------
# Bug 3: the text is not saved.
# ---------------------------------------------------------------------------
BUG3_OLD = """    if (layer.kind == layers::Layer::Text) {
        o["text"] = layer.text;"""

BUG3_NEW = """    // BREAK SELF-CHECK: the text payload is not saved.
    if (false) {
        o["text"] = layer.text;"""


# ---------------------------------------------------------------------------
# Bug 4: the vector paths are not saved.
# ---------------------------------------------------------------------------
BUG4_OLD = """    if (layer.kind == layers::Layer::Vector) {
        QJsonArray paths;"""

BUG4_NEW = """    // BREAK SELF-CHECK: the vector payload is not saved.
    if (false) {
        QJsonArray paths;"""


# ---------------------------------------------------------------------------
# Bug 5: the layer order is not preserved.
#
# The layers are written in reverse order.
# ---------------------------------------------------------------------------
BUG5_OLD = """    QJsonArray layersJson;
    for (int i = 0; i < stack->count(); ++i) {
        layers::LayerPtr l = stack->at(i);
        if (!l) continue;
        // The index is the position in THIS file's layer array, so it is
        // stable and unique even if two layers share a stableId.
        layersJson.append(layerToJson(*l, layersJson.size(), blobs));
    }"""

BUG5_NEW = """    QJsonArray layersJson;
    for (int i = stack->count() - 1; i >= 0; --i) {
        layers::LayerPtr l = stack->at(i);
        if (!l) continue;
        layersJson.append(layerToJson(*l, layersJson.size(), blobs));
    }"""


# ---------------------------------------------------------------------------
# Bug 6: a load merges into the old layers.
#
# The stack is not cleared before the layers are added.
# ---------------------------------------------------------------------------
BUG6_OLD = """    // 3) Rebuild the layers.
    stack->clear();
    for (const QJsonValue& v : layersJson) {"""

BUG6_NEW = """    // 3) Rebuild the layers.
    // BREAK SELF-CHECK: the old layers are not cleared, so a load
    // merges into whatever was there.
    for (const QJsonValue& v : layersJson) {"""


# ---------------------------------------------------------------------------
# Bug 7: the auto-save writes even when the document is clean.
# ---------------------------------------------------------------------------
BUG7_OLD = """void AutoSave::onTick()
{
    if (!m_isDirty) return;
    if (!m_isDirty()) return;"""

BUG7_NEW = """void AutoSave::onTick()
{
    if (!m_isDirty) return;
    // BREAK SELF-CHECK: the dirty check is skipped, so a clean
    // document still gets a recovery copy."""


# ---------------------------------------------------------------------------
# Bug 8: the recovery copy outlives the save.
# ---------------------------------------------------------------------------
BUG8_OLD = """void AutoSave::removeRecovery(const QString& documentPath)
{
    const QString path = recoveryPathFor(documentPath);
    QFile::remove(path);
}"""

BUG8_NEW = """void AutoSave::removeRecovery(const QString& documentPath)
{
    // BREAK SELF-CHECK: the recovery copy is never removed, so it
    // outlives the save it was made for.
    Q_UNUSED(documentPath);
}"""


# ---------------------------------------------------------------------------
# Bug 9: the blob names are built from stableId again.
#
# stableId comes from a process-global counter and is restored verbatim
# from the file, so two DIFFERENT layers can end up sharing one id. The
# blob table is keyed by name, so the two layers' pixel blobs collide and
# one layer silently overwrites the other. The blob names have to be
# built from the file-internal layer index instead.
# ---------------------------------------------------------------------------
BUG9_OLD = """        const QString name = QStringLiteral("layer%1_pixels").arg(layerIndex);"""

BUG9_NEW = """        // BREAK SELF-CHECK: the blob name is built from stableId again, so
        // two layers that share a stableId share a pixel blob.
        const QString name = QStringLiteral("layer%1_pixels")
                                 .arg(static_cast<qulonglong>(layer.stableId));"""


BUGS = [
    ("bug 1 (the save keeps only the pixels)",
     VST, BUG1_OLD, BUG1_NEW,
     "theSaveKeepsMoreThanThePixels"),
    ("bug 2 (the mask is not saved)",
     VST, BUG2_OLD, BUG2_NEW,
     "savesAndLoadsAPixelMask"),
    ("bug 3 (the text is not saved)",
     VST, BUG3_OLD, BUG3_NEW,
     "savesAndLoadsATextLayer"),
    ("bug 4 (the vector paths are not saved)",
     VST, BUG4_OLD, BUG4_NEW,
     "savesAndLoadsAVectorLayer"),
    ("bug 5 (the layer order is not preserved)",
     VST, BUG5_OLD, BUG5_NEW,
     "savesAndLoadsTheLayerOrder"),
    ("bug 6 (a load merges into the old layers)",
     VST, BUG6_OLD, BUG6_NEW,
     "loadReplacesTheOldLayers"),
    ("bug 7 (the auto-save writes even when the document is clean)",
     AUTOSAVE, BUG7_OLD, BUG7_NEW,
     "autoSaveDoesNothingWhenTheDocumentIsClean"),
    ("bug 8 (the recovery copy outlives the save)",
     AUTOSAVE, BUG8_OLD, BUG8_NEW,
     "theRecoveryCopyIsRemovedAfterASave"),
    ("bug 9 (the blob names collide on a shared stableId)",
     VST, BUG9_OLD, BUG9_NEW,
     "layersThatShareAStableIdKeepTheirOwnPixels"),
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


def inject(path, old, new):
    src = read(path)
    nl = "\r\n" if "\r\n" in src else "\n"
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


def expect_red(label, expected_test, out, returncode):
    print("[break] %s -- test output:" % label)
    for line in out.splitlines():
        if ("FAIL!" in line or "Totals:" in line or "Actual" in line
                or "Expected" in line or "ASSERT" in line):
            print("   " + line.strip())

    crashed = (returncode != 0) and ("Totals:" not in out)

    # The expected test has to appear on a FAIL line.
    #
    #   A plain "expected_test in out" also matches the PASS line --
    #   Qt Test prints the test name either way -- so that check reports
    #   OK on a GREEN run. The name is therefore looked for on the
    #   "FAIL!" lines only.
    failed_lines = [ln for ln in out.splitlines() if "FAIL!" in ln]

    for line in failed_lines:
        if expected_test in line:
            print("[break] OK: the expected test (%s) went red" % expected_test)
            return True

    if crashed:
        print("[break] OK: the test binary crashed (exit=%d) with the injected bug"
              % returncode)
        return True

    if failed_lines:
        print("[break] FAIL: the tests went red, but NOT the expected one (%s)"
              % expected_test)
        for line in failed_lines:
            print("   " + line.strip())
        return False

    if "FAIL!" in out:
        print("[break] FAIL: the tests went red, but NOT the expected one (%s)"
              % expected_test)
        return False

    print("[break] FAIL: the tests stayed GREEN with the injected bug -- "
          "they do not detect it")
    return False


def main():
    if not os.path.exists(TEST_EXE):
        print("FAIL: the test binary does not exist: " + TEST_EXE)
        return 2

    touched = sorted(set(b[1] for b in BUGS))
    hash_before = {p: sha(p) for p in touched}
    backups = {}
    for p in touched:
        tf = tempfile.NamedTemporaryFile(delete=False, suffix=".bak")
        with io.open(p, "rb") as f:
            tf.write(f.read())
        tf.close()
        backups[p] = tf.name

    ok = True
    try:
        for label, path, old, new, expected_test in BUGS:
            inject(path, old, new)
            print("[break] injected: " + label)
            if not build():
                ok = False
            else:
                out, rc = run_test(TEST_EXE)
                if not expect_red(label, expected_test, out, rc):
                    ok = False
            shutil.copyfile(backups[path], path)

    finally:
        for p, b in backups.items():
            shutil.copyfile(b, p)
            os.unlink(b)
        for p in touched:
            same = sha(p) == hash_before[p]
            print("[restore] %s restored, byte identical: %s"
                  % (os.path.basename(p), same))
            if not same:
                return 2

    if not ok:
        return 1

    print("[break] rebuilding the restored tree")
    if not build():
        return 2
    out, _ = run_test(TEST_EXE)
    green = "Totals:" in out and " 0 failed" in out
    for line in out.splitlines():
        if "Totals:" in line:
            print("   " + os.path.basename(TEST_EXE) + ": " + line.strip())
    print("[break] restored tree is green:", green)
    return 0 if green else 1


if __name__ == "__main__":
    sys.exit(main())
