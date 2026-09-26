# Final release packaging and reviewed publication

Proposed immutable tag: `usvfs-0.5.7.2-rs.2`. Never move or reuse it.
The existing `usvfs-0.5.7.2-rs.1` tag remains immutable and must not be retargeted.

`source-approval.json` freezes the 170 recipe files, 66 source assets, 137 notice
files, upstream baseline, and exact issue #33 proxy blob/diff from reviewed candidate run
36245638937. Source and notice review statuses follow successful x86/x64 archive
rebuilding in that run. The reviewed run URL and fork revision remain audit evidence.

Approval covers these exact library source inputs, not an unknowable future
archive checksum. Every fresh release run must build both architectures, verify that both staged
proxies terminate non-modally without shared logging, collect matching sources,
rebuild that same archive with origin fallback blocked, match all approved
source/recipe/notice hashes, and match the packaging checkout to the fork revision
in the build evidence. `package-release.ps1` then records the actual source and
native archive checksums. It never pairs artifacts from separate runs.

The manual workflow remains contents:read by default. Its publication job is
literal-false until a separate reviewed change enables explicit dispatch approval,
immutable tag checks, draft asset uploads, uploaded checksum verification, and
publication only with both matching archives present. Grant contents:write only
to publication. Do not enable it merely to bypass a failed build or source check.

Package locally on the validated Windows runner with:

```powershell
./packaging/package-release.ps1 -StageDirectory <stage> -RebuildDirectory <rebuild> -OutputDirectory <new-output>
```

The ZIP contains bin/ with four matched DLL/proxy outputs, the compatible upstream
baseline marker, and artifacts.json with the exact fork revision and approved native
delta identity; licenses, notices and release evidence are separate. The source tar
is the exact archive rebuilt by this run. Its candidate
README records its pre-approval state; release evidence and the release notes
record the subsequent audit and successful rebuild. No compiled Rust binary is
redistributed. Rust bindings remain a Cargo Git source package.

Windows 11 and both VC++ redistributables are external runtime prerequisites.
The source archive's reconstruction instructions describe external build tools;
this is not a fully offline toolchain or a bit-reproducible PE claim.

Windows packaging still needs validation. Keep publication disabled until
ZIP layout, manifest integrity, source checks and both rebuilds pass.
