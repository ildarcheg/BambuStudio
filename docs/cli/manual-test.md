# bambu-cli -- manual smoke test recipe (v1)

This recipe exercises the full v1 surface end-to-end against the canonical
local-realistic fixtures. Run it before every release. Each step documents the
expected exit code and output. Steps that interact with Bambu Studio are marked
**[BS layer-2 check]** -- the human performs those; the CLI steps can be
automated.

---

## Prerequisites

- `bambu-cli.exe` built at `build\src\cli\Release\bambu-cli.exe` (set `$cli`
  below to the actual path if different).
- Canonical fixtures committed under `tests\cli\fixtures\local\` in this repo:
  - `temp_project_for_bambu_studio.3mf` -- pre-configured reference
    (1 plate, 4 filament slots, AMS configured).
  - `stls\000_01_test_cube.stl`
  - `stls\000_01_test_cylinder.stl`
  - `stls\000_01_test_cone.stl`
  - `stls\000_01_test_bambu_cube.stl`
- Bambu Studio installed (used for layer-2 visual checks only).

Run this recipe from the repo root. Set these variables at the top of your
PowerShell session:

```powershell
$cli     = ".\build\src\cli\Release\bambu-cli.exe"
$out     = "$env:TEMP\bcli_smoke.3mf"
$ref     = ".\tests\cli\fixtures\local\temp_project_for_bambu_studio.3mf"
$stldir  = ".\tests\cli\fixtures\local\stls"
```

---

## Step 1: Verify the binary

```powershell
& $cli --version
& $cli --help
```

**Expected:**
- `--version` prints `bambu-cli 0.1.0` (exit 0).
- `--help` prints the subcommand list. The first line of the description reads
  `bambu-cli -- compose .3mf project files for Bambu Studio` (two hyphens, no
  em-dash). Exit 0.

---

## Step 2: project init (clone-and-verify)

```powershell
& $cli project init $out --template $ref
Write-Host "exit: $LASTEXITCODE"
```

**Expected:** exit 0. Stdout:
```
project init: clone-and-verify succeeded -> <path to $out>
```

**[BS layer-2 check]** Open `$out` in Bambu Studio. Verify it looks identical
to the reference template (same plate name, same filament configuration in the
AMS panel). Close BS.

---

## Step 3: plate add -- add a second plate

```powershell
& $cli plate add $out --name "Plate-2"
Write-Host "exit: $LASTEXITCODE"

& $cli plate list $out
Write-Host "exit: $LASTEXITCODE"
```

**Expected:**
- `plate add` exits 0. Stdout: `plate added: Plate-2 -> <path>`
- `plate list` exits 0. Stdout lists both plate names (one per line with index),
  e.g.:
  ```
  1  <original plate name>
  2  Plate-2
  ```

**[BS layer-2 check]** Open `$out` in BS. The plate selector at the top shows
two plates. Close BS.

---

## Step 4: object add -- place objects on Plate-2 with filament, transforms

```powershell
& $cli object add $out --plate "Plate-2" `
    --stl "$stldir\000_01_test_cube.stl" `
    --filament 1 --translate 30,30
Write-Host "exit: $LASTEXITCODE"

& $cli object add $out --plate "Plate-2" `
    --stl "$stldir\000_01_test_cylinder.stl" `
    --filament 2 --translate 80,30
Write-Host "exit: $LASTEXITCODE"

& $cli object add $out --plate "Plate-2" `
    --stl "$stldir\000_01_test_cone.stl" `
    --filament 3 --translate 30,80
Write-Host "exit: $LASTEXITCODE"

& $cli object add $out --plate "Plate-2" `
    --stl "$stldir\000_01_test_bambu_cube.stl" `
    --filament 4 --translate 80,80 --rotate 0,0,45 --scale 1.2
Write-Host "exit: $LASTEXITCODE"
```

**Expected:** each command exits 0. Stdout per command:
```
object added: <stem name> -> <path>
```

Verify with object list:

```powershell
& $cli --json object list $out
Write-Host "exit: $LASTEXITCODE"
```

**Expected:** exit 0. JSON contains `"object_count":4` and all four object names
in the `"objects"` array. Each object has an `"extruder"` field matching the
`--filament` value passed (1, 2, 3, 4 respectively).

**[BS layer-2 check]** Open `$out` in BS. Switch to Plate-2. Four objects are
visible, each rendered in its distinct AMS slot color. The bambu-cube is
rotated 45 degrees and visibly larger (1.2x scale). Close BS.

---

## Step 5: multi-copy object add (--count)

```powershell
& $cli object add $out --plate "Plate-2" `
    --stl "$stldir\000_01_test_cube.stl" `
    --filament 1 --count 3
Write-Host "exit: $LASTEXITCODE"

& $cli --json object list $out
Write-Host "exit: $LASTEXITCODE"
```

**Expected:** `object add` exits 0. The subsequent `object list` reports
`"object_count":7` (4 from step 4 + 3 copies from this step). All three new
copies are named `000_01_test_cube`.

---

## Step 6: config set -- project-level override

```powershell
& $cli config set $out --key line_width --value 0.5
Write-Host "exit: $LASTEXITCODE"

& $cli --json config list $out --changed-only
Write-Host "exit: $LASTEXITCODE"
```

**Expected:**
- `config set` exits 0. Stdout: `config set: project line_width=0.5 -> <path>`
- `config list --changed-only` exits 0. JSON output contains `"key":"line_width"`
  and `"value":"0.5"` in the `"entries"` array.

**[BS layer-2 check]** Open `$out` in BS. In the process settings panel, Line
width should show 0.5 mm as a project override.

---

## Step 7: config set --object -- per-object override

```powershell
& $cli config set $out --object "000_01_test_cube" --key line_width --value 0.4
Write-Host "exit: $LASTEXITCODE"
```

**Expected:** exit 0. Stdout:
```
config set: object '000_01_test_cube' line_width=0.4 -> <path>
```

**[BS layer-2 check]** Open `$out` in BS. Select the cube object on Plate-2.
In Object Settings, Line width shows 0.4 mm as a per-object override.

---

## Step 8: object set-filament -- retrofit existing object

```powershell
& $cli object set-filament $out --name "000_01_test_cylinder" --filament 4
Write-Host "exit: $LASTEXITCODE"

& $cli --json object list $out --plate "Plate-2"
Write-Host "exit: $LASTEXITCODE"
```

**Expected:**
- `set-filament` exits 0. Stdout: `set-filament: 000_01_test_cylinder -> 4`
- `object list` JSON shows `"extruder":4` for the cylinder entry.

---

## Step 9: object remove (with group-by-name)

```powershell
# Remove the cone (single object).
& $cli object remove $out --name "000_01_test_cone"
Write-Host "exit: $LASTEXITCODE"

# Remove all 3 copies of the cube added in step 5.
# group-by-name removes all ModelObjects sharing that name.
& $cli object remove $out --name "000_01_test_cube"
Write-Host "exit: $LASTEXITCODE"

& $cli --json object list $out
Write-Host "exit: $LASTEXITCODE"
```

**Expected:**
- Both `object remove` commands exit 0.
- The final `object list` reports `"object_count":2` (cylinder and bambu_cube
  remain on Plate-2). No entry for `000_01_test_cone` or `000_01_test_cube`.

---

## Step 10: plate rename

```powershell
& $cli plate rename $out --from "Plate-2" --to "FinalLayout"
Write-Host "exit: $LASTEXITCODE"

& $cli plate list $out
Write-Host "exit: $LASTEXITCODE"
```

**Expected:**
- `plate rename` exits 0. Stdout: `plate renamed: Plate-2 -> FinalLayout in <path>`
- `plate list` shows "FinalLayout" in place of "Plate-2".

---

## Step 11: plate remove (remove an empty plate)

First add a throwaway plate, then remove it:

```powershell
& $cli plate add $out --name "Throwaway"
Write-Host "exit: $LASTEXITCODE"

& $cli plate remove $out --name "Throwaway"
Write-Host "exit: $LASTEXITCODE"

& $cli plate list $out
Write-Host "exit: $LASTEXITCODE"
```

**Expected:**
- Both commands exit 0.
- Final `plate list` shows only the original plate and "FinalLayout" (no
  "Throwaway").

---

## Step 12: config unset

```powershell
& $cli config unset $out --key line_width
Write-Host "exit: $LASTEXITCODE"

& $cli --json config list $out --changed-only
Write-Host "exit: $LASTEXITCODE"
```

**Expected:**
- `config unset` exits 0. Stdout: `config unset: project line_width -> <path>`
- `config list --changed-only` exits 0. JSON output does NOT contain
  `"key":"line_width"` (the key was removed from the changed set).

---

## Step 13: JSON mode smoke check

```powershell
& $cli --json inspect $out
Write-Host "exit: $LASTEXITCODE"
```

**Expected:** exit 0. JSON stdout Shape A:
```json
{"status":"ok","code":"ok","message":"inspect ok","data":{"plate_count":2,"object_count":2,"filament_count":4}}
```

(Exact counts depend on the steps above. `plate_count` should be 2 --
original plate + FinalLayout. `object_count` should be 2 -- cylinder and
bambu_cube. `filament_count` is determined by the template and should be 4.)

---

## Step 14: error handling smoke check

```powershell
& $cli inspect Z:\no\such\file.3mf
Write-Host "exit: $LASTEXITCODE"
```

**Expected:** exit 2. Stderr contains `file_not_found`.

```powershell
& $cli --json inspect Z:\no\such\file.3mf
Write-Host "exit: $LASTEXITCODE"
```

**Expected:** exit 2. Stderr contains JSON Shape A error:
`{"status":"error","code":"file_not_found","message":"..."}`

---

## Step 15: [BS layer-2 check] Final open + slice

Open `$out` in Bambu Studio:

1. Switch to the "FinalLayout" plate.
2. Verify two objects are present: cylinder and bambu_cube.
3. Cylinder should render in AMS slot 4 (retrofitted in step 8).
4. bambu_cube should render in AMS slot 4, rotated 45 degrees, scaled 1.2x.
5. Click **Slice plate**. Slicing must succeed with no errors.
6. In the preview, verify objects render in the correct AMS slot colors.
7. Save the gcode if desired.

---

## Cleanup

```powershell
Remove-Item $out -Force -ErrorAction SilentlyContinue
```

---

## Known limitations

These are parked gaps documented for future maintainers. They are NOT bugs that
block v1 release; they are design constraints with known workarounds.

### M3: plate-metadata gap (top_file / pick_file / filament_map_mode)

CLI-created plates do not carry `top_file`, `pick_file`, and `filament_map_mode`
metadata that Bambu Studio writes when it creates plates. This means the plate
may show a blank thumbnail icon in the BS plate selector. The plate is otherwise
functional: objects slice and gcode generates correctly. Fix requires reverse-
engineering BS's thumbnail generation pipeline. Parked for v2.

### M7: config list shows changed-vs-system, not changed-vs-default

`config list --changed-only` reports keys present in the `different_settings_to_system`
array, which is the set of keys that differ from the system profile -- not keys
that differ from libslic3r factory defaults. This means some "default" values may
appear as changed if the project's system profile differs from factory defaults.
Adding a `--all` flag or a full default-comparison mode was out of scope for v1.

### M7: key nomenclature (line_width vs specific per-feature width keys)

Bambu Studio splits line width into per-feature keys (e.g.
`outer_wall_line_width`, `inner_wall_line_width`, `sparse_infill_line_width`).
Setting `line_width` as a project-level override sets the base value; BS may
override it with per-feature values during slicing. For precise control, use
the specific per-feature key names. The full list is visible in BS's process
settings panel.

### M3/M6: world stride placement for multi-plate scenarios

When `--count N` places N objects across multiple plates using the grid-stride
algorithm, each copy is offset by the bed width (256 mm for X1C). If your print
profile uses a different bed size, adjust `--translate` manually instead of
relying on `--count` for cross-plate placement.
