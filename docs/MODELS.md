# ONNX Model Dependencies (Liquify Face-Aware)

## Why this file exists

The face/landmark detection in Liquify uses **InsightFace buffalo_l** ONNX weights.
We do NOT commit these to git because:
- `det_10g.onnx` is 17 MB, `2d106det.onnx` is 5 MB - total 22 MB
- Combined with the original `buffalo_l.zip` (289 MB) and the **unused** heavier models
  (`w600k_r50.onnx` 174 MB, `1k3d68.onnx` 144 MB), a naive `git push` would exceed
  GitHub's 100 MB per-file limit
- InsightFace releases the canonical bundle upstream - pinning to upstream version
  avoids drift and lets users grab fresh weights

Two previous commits (P1.2.7 v2/v3) explored loading the full buffalo_l pack
(`w600k_r50.onnx` + `1k3d68.onnx`) for higher accuracy, but reverted to the lighter
`det_10g.onnx` + `2d106det.onnx` pair (which already gives 106-point landmark
output). The heavy weights are NOT wired into the codebase.

---

## Required models (used by current code)

| File             | Size   | Purpose                                | Path                                              |
|------------------|--------|----------------------------------------|---------------------------------------------------|
| `det_10g.onnx`   | 17 MB  | Face detection (RetinaFace-10GF)       | `third_party/landmark/det_10g.onnx`               |
| `2d106det.onnx`  | 5 MB   | 106-point landmark detection           | `third_party/landmark/2d106det.onnx`              |

### SHA-256 (for integrity check)

```
det_10g.onnx     5838f7fe053675b1c7a08b633df49e7af5495cee0493c7dcf6697200b85b5b91
2d106det.onnx    f001b856447c413801ef5c42091ed0cd516fcd21f2d6b79635b1e733a7109dbf
```

### Source

Both files come from InsightFace's **`buffalo_l` pack v0.7** release:

- Repository:  https://github.com/deepinsight/insightface
- Release:     https://github.com/deepinsight/insightface/releases/tag/v0.7
- Direct zip:  https://github.com/deepinsight/insightface/releases/download/v0.7/buffalo_l.zip
- HuggingFace mirror: https://huggingface.co/Anyisam/buffalo_l/resolve/main/buffalo_l.zip

The zip contains both files. After extracting `buffalo_l.zip` you get a buffalo_l/
  folder with both ONNX weights inside.

---

## Setup on a fresh checkout

Run either:

### Option A: One-liner download script (PowerShell)

```powershell
.\scripts\download_models.ps1
```

This downloads buffalo_l.zip from the InsightFace GitHub release, verifies the
SHA-256 of the extracted `.onnx` files, and places them under
`third_party/landmark/`.

### Option B: Manual

```powershell
# Download + extract
Invoke-WebRequest -Uri https://github.com/deepinsight/insightface/releases/download/v0.7/buffalo_l.zip `
                  -OutFile third_party\landmark\buffalo_l.zip
Expand-Archive third_party\landmark\buffalo_l.zip -DestinationPath third_party\landmark\ -Force

# Move the two needed files out of buffalo_l/ subfolder
Move-Item -Force third_party\landmark\buffalo_l\det_10g.onnx  third_party\landmark\det_10g.onnx
Move-Item -Force third_party\landmark\buffalo_l\2d106det.onnx third_party\landmark\2d106det.onnx

# Clean up (keep only the two needed ONNX files; ignore the rest)
Remove-Item -Recurse -Force third_party\landmark\buffalo_l
Remove-Item -Force third_party\landmark\buffalo_l.zip

# Verify
Get-FileHash third_party\landmark\det_10g.onnx  -Algorithm SHA256   # expect 5838f7fe...
Get-FileHash third_party\landmark\2d106det.onnx -Algorithm SHA256   # expect f001b856...
```

---

## Not used (kept here for historical reference, NOT loaded by code)

| File                       | Size     | Why unused                              |
|----------------------------|----------|-----------------------------------------|
| `buffalo_l/w600k_r50.onnx` | 174 MB   | v3 plan - replaced by det_10g.onnx      |
| `buffalo_l/1k3d68.onnx`    | 144 MB   | v3 plan - replaced by 2d106det.onnx     |
| `buffalo_l/genderage.onnx` | 1.3 MB   | gender/age not used by Liquify          |
| `buffalo_l.zip`            | 289 MB   | original archive, redundant after extract |

If you want to experiment with the heavier weights, edit
`src/media/filters/liquify/FaceDetector.cpp::loadDefaults()` to point at the
heavier files. The 106-point landmark API surface is unchanged.

---

## Why not commit (vs. Git LFS)

| Aspect           | Document + bootstrap (this file)        | Git LFS                              |
|------------------|------------------------------------------|--------------------------------------|
| `git clone` size | 22 MB total                              | 22 MB (LFS transparent fetch)         |
| Setup step       | 1 command to download + verify           | 0 commands                            |
| Network at clone | First-time 22 MB download                | First-time 22 MB LFS download         |
| Disk after clone | identical                                | identical                             |
| Model updates    | User re-runs bootstrap script             | `git pull` auto-fetches               |
| Repo size growth | none                                     | LFS metadata overhead                 |
| Bandwidth quota  | none                                     | 1 GB/month on GitHub free             |

Document + bootstrap is the lighter approach - the only loss is "one extra
step on first checkout". Given the models are public, version-pinned, and
small (22 MB total), the trade-off favors Documentation for this project.

---

## License

The buffalo_l weights are released under the **InsightFace academic-use license**:
https://github.com/deepinsight/insightface/tree/master/python-package

Commercial use requires a separate license. Contact deepinsight for terms.