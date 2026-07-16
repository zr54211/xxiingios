# AutoBuild — headless mobile app build extension for "MobileApplicationBuilder"

Configuration extension for the "Сборщик" infobase (`C:\Bases\Сборщик`, configuration
`MobileApplicationBuilder`, platform 8.5.1.1423, `ScriptVariant = English`,
`ConfigurationExtensionCompatibilityMode = Version8_5_1`).

It automates the cycle that is normally done by hand in the interactive client:

1. load a `1cema.zip` mobile configuration export into an existing `Catalog.MobileConfigurations`
   item (overwriting it, same as re-importing into the same item from the item form);
2. create a new `Catalog.MobileApplications` build item ("сборка N") under the applications group
   via the standard `Filling` handler - the same thing the interactive "Load and build" form does;
3. build it and save the resulting artifacts to disk next to the source `1cema.zip`;
4. terminate the session.

Triggered from the command line via the standard `/C` launch parameter:

```
1cv8c ENTERPRISE /F"C:\Bases\Сборщик" /C"autobuild;<path to 1cema.zip>;<configuration code or name>"
```

For any other launch parameter (including a normal interactive start with **no** `/C` at all,
which is what the desktop shortcut / everyday development use) the extension does **nothing** -
the builder's regular startup and UI behave exactly as before.

## Source layout

```
builder-ext/
  src/
    Configuration.xml                                  extension root (Name=AutoBuild, prefix AutoBuild_)
    Ext/ManagedApplicationModule.bsl                    &After("OnStart") hook, reads LaunchParameter, calls Exit()
    CommonModules/AutoBuild_Server.xml                  common module descriptor (Server + ServerCall)
    CommonModules/AutoBuild_Server/Ext/Module.bsl        the actual import+build+export pipeline
  README.md                                             this file
```

## How to load the extension

The base doesn't have this extension yet, so it must be created once (extension metadata objects
cannot be materialized purely by `/LoadConfigFromFiles` unless the extension already exists on
some platform versions - creating it once interactively is the safe path):

1. Open **Configurator** on `C:\Bases\Сборщик` → *Configuration → Configuration extensions*
   → *Add* → Name `AutoBuild`, Name prefix `AutoBuild_`, Purpose `Customization`. Save and close.
2. Load the sources into that extension from the command line:

```
1cv8.exe DESIGNER /F"C:\Bases\Сборщик" /N"<user>" /P"<password>" /DisableStartupDialogs ^
    /LoadConfigFromFiles "C:\Source\addin-barcode-zxing\builder-ext\src" -Extension "AutoBuild" ^
    /UpdateDBCfg -Extension "AutoBuild" ^
    /Out "C:\Source\addin-barcode-zxing\builder-ext\load.log"
```

   (adjust `1cv8.exe` path / credentials for the local install; drop `/N`/`/P` if the base has
   no password-protected users). Check `load.log` for errors, in particular metadata-check
   failures if the extension's compatibility mode or object prefix was set differently in step 1.

3. Re-running step 2 later (after editing the `.bsl` files) reloads/updates the same extension in
   place - no need to repeat step 1.

If the platform in use supports creating the extension straight from `/LoadConfigFromFiles
-Extension "AutoBuild"` without step 1, that also works; step 1 is only there as the guaranteed
fallback if the extension object does not exist yet.

## Running an autobuild

```
1cv8c ENTERPRISE /F"C:\Bases\Сборщик" /N"<user>" /P"<password>" ^
    /C"autobuild;C:\Path\To\export\1cema.zip;Тест сканера ВК"
```

Parameter format (semicolon-separated, matches `StrSplit(LaunchParameter, ";", False)`):

```
autobuild;<path to 1cema.zip>;<configuration code or name>
```

- `autobuild` - literal, case-insensitive.
- `<path to 1cema.zip>` - absolute path on the machine the base's server process runs on (this is
  a file infobase, so that is simply the local disk). Can point to a `.zip` (with add-ins) or a
  bare `.xml` export - both are accepted, exactly like the item form's "Import configuration" file
  dialog filter.
- `<configuration code or name>` - either the `Code` or the `Description` of the target
  `Catalog.MobileConfigurations` **item** to overwrite, or of its parent **group** (in which case
  the existing item of the matching type inside that group is reused, e.g. `Тест сканера ВК`).

AutoBuild **never creates** new `MobileConfigurations` items/groups - it only overwrites an
existing configuration item. For `MobileApplications` it creates a **new build item** on every run
(numbered "сборка N" like the interactive builder) inside the applications **group** whose
`ConfigurationSources.Source` points to the resolved configurations group; that group must be set
up once, interactively, before the first autobuild run.

## Environment gotcha

The build background job inherits the environment of the process that launched `1cv8c`. If
`NoDefaultCurrentDirectoryInExePath` is set (some sandboxed shells set it), Windows stops looking
for executables in the current directory and the builder's `cmd /c make.bat` / `call gradlew` die
instantly with "is not recognized" (`BuildState=3`, empty build log). Remove the variable before
launching - `package/build-and-deploy.ps1` (the full ВК → APK → device pipeline wrapper) does this
automatically.

## Diagnostics

Everything is written next to the source zip file (`<ZipFile>` = the path passed on the command
line), so a wrapper script never has to inspect Windows Event Log / infobase Event Log:

| File | Meaning |
|------|---------|
| `<ZipFile>.autobuild.log` | Full run log (import warnings, build progress, exported files, or the fatal error with `DetailErrorDescription`). Overwritten on every run. |
| `<ZipFile>.autobuild.ok` | Created only if the run finished successfully (import + build + at least one exported artifact). |
| `<ZipFile>.autobuild.fail` | Created only if the run failed; contains the same error text as the log. |

A wrapper script should poll for the process to exit, then check for `.autobuild.ok` /
`.autobuild.fail` - **do not** rely on the 1C process exit code, `AutoBuild_Server.RunAutoBuild`
never lets an exception escape (it always calls `Exit(False)` afterwards, on both success and
failure) so that the session terminates cleanly either way.

Build artifacts (APK etc.) are written to the same directory as `<ZipFile>`, named exactly like
the interactive "Get application" command names them
(`ServiceClientServer.ApplicationFileName`), e.g. `pb.mobilesn.com.onecvn.scannertest-arm64.apk`.

Default build timeout: 7200 seconds (2 hours), hardcoded in
`AutoBuild_Server.BuildTimeoutSeconds()` - edit that function if a different value is needed
(deliberately not exposed as a 4th `/C` parameter, to keep the command-line format fixed).

## Exact base-configuration procedures this extension relies on

All line numbers refer to the XML export under
`...\scratchpad\builder-src\` used while writing this extension - re-check them against the
actual base if the builder configuration has since changed.

| Module | Procedure/Function | Signature | Line |
|---|---|---|---|
| `CommonModules\MobileCfgsAndAppsServer` | `ConfigurationType` | `(refGroup) Export` - returns `refGroup.ConfigurationType` | 18 |
| `CommonModules\MobileCfgsAndAppsServer` | `SelectConfiguration` | `(ParentRef, AppType) Export` - latest non-deleted item of `AppType` directly under `ParentRef` | 44 |
| `CommonModules\MobileCfgsAndAppsServer` | `AnalyzeDownloadedFile` | `(Addr, FormUUID) Export` - `Addr` is a **temp storage address** (not a file path); returns the `CfgParams` structure | 368 |
| `CommonModules\MobileCfgsAndAppsServer` | `FillCfgObject` | `(Object, CfgParams) Export` - fills attributes/tabular sections of a `CatalogObject.MobileConfigurations` (works on a plain object, not only a form's `Object`); returns import warnings as text | 838 |
| `CommonModules\MobileCfgsAndAppsServer` | `SaveExternalComponentToDB` | `(CfgObjectRef, CfgParams) Export` - creates/updates `Catalog.ExternalComponents` items and stores their files | 958 |
| `CommonModules\StoredFileManagement` | `PutObject` | `(Val DataSource, Val OwnerRef, Val FileTypeRef, Val UniqueFileName, Val CalculateHashWithExtracting = False) Export` - `DataSource` accepts a temp storage address, `BinaryData`, **or a plain file-name string** | 197 |
| `CommonModules\StoredFileManagement` | `GetObject` | `(Val OwnerRef, Val FileTypeRef, Val FullFileName = Undefined, Val UUID = Undefined) Export` - pass `FullFileName` to save straight to disk, skipping temp storage | 289 |
| `CommonModules\MobileApplicationBuilding` | `BuildApplication` | `(ApplicationToBuild) Export` - returns `Array` of background-job task descriptors, or `Undefined` only in the debug "sequential build" mode (see `IsNeedSequentialBuild`, line 6466) | 10 |
| `CommonModules\BuildProcessManagement` | `BuildPrepare` | `(refApplication, TaskArray, TempAddr = "") Export` - writes the task list to `InformationRegister.BuildProcess` | 58 |
| `CommonModules\BuildProcessManagement` | `BuildTasksStateUpdate` | `(refApplication) Export` - syncs job states from `BackgroundJobs`, returns remaining task count | 97 |
| `CommonModules\BuildProcessManagement` | `BuildSchedule` | `(refApplication) Export` - starts as many queued `BackgroundJobs.Execute(...)` build tasks as free "slots" allow | 127 |
| `CommonModules\BuildProcessManagement` | `GetJobsArray` | `(refApplication) Export` - `Array` of currently-active `BackgroundJob` objects for the app | 300 |
| `CommonModules\BuildProcessManagement` | `BuildComplete` | `(refApplication) Export` - fills `MobileApplications.BuildResults`, clears the task queue, returns error text (blank = success) | 168 |
| `CommonModules\BuildProcessManagement` | `BuildInterrupt` | `(refApplication, RootQueue = Undefined) Export` - cancels jobs/artifacts, used on our own timeout | 213 |
| `CommonModules\BuildProcessManagement` | `IsBuildInProgress` | `(refApplication) Export` - `Boolean` | 407 |
| `CommonModules\BuildProcessManagement` | `StartBuildQueue` | `(refApplication) Export` - **not called by this extension**; raises for file infobases ("Parallel build in file version does not work"). We inline its own loop (`BuildTasksStateUpdate` → `BuildSchedule` → `BackgroundJobs.WaitForExecutionCompletion`) directly in an ordinary server call instead, which is not subject to that restriction | 12 |
| `CommonModules\ServiceClientServer` | `ApplicationFileName` | `(Val refApplication, Val FileType) Export` - builds the on-disk file name (`ApplicationID + suffix`, e.g. `...-arm64.apk`) | 1021 |
| `CommonModules\ServiceClientServer` | `ApplicationFileSuffix` | `(Val FileType) Export` - the `FileType → "-arm64.apk"`-style suffix map | 1157 |
| `Catalogs\MobileConfigurations\Forms\ItemForm\Ext\Form\Module.bsl` | `ImportConfigurationAtServer`, `OnWriteAtServer` | interactive flow this extension re-implements headlessly (see below) | 135, 28 |
| `Catalogs\MobileApplications\Forms\ItemForm\Ext\Form\Module.bsl` | `GetApplication` | interactive "download built app to disk" command whose naming/`StoredFileManagement.GetObject` call this extension mirrors | 556 |
| `CommonForms\MakeApplicationProccess\Ext\Form\Module.bsl` | `OnCreateAtServer`, `ProcessStep` | interactive build-progress flow this extension re-implements headlessly | 1, 153 |
| `Ext\ManagedApplicationModule.bsl` (base configuration) | `OnStart` | `Procedure OnStart()` - **no parameters**, this is the exact event name/signature patched by `&After("OnStart")` | 1 |

Metadata relied on: `Catalog.MobileApplications.Configurations` tabular section (`Use = ForItem`,
attribute `Configuration: CatalogRef.MobileConfigurations`) is how an application item is linked
to the configuration item(s) it builds from; `Enums.FileTypes.MobileConfiguration` /
`MobileConfigurationClient` / `MobileConfigurationDB` / `ExternalComponent`; `Enums.MakeType.Application`
/ `Client`.

## What could not be reused as-is, and why

`Catalogs.MobileConfigurations.ItemForm` and `CommonForms.MakeApplicationProccess` drive the
import and the build entirely through client-side code (`BeginPutFileToServer`/
`NotifyDescription` callbacks, and a `AttachIdleHandler`-driven 1-second poll timer respectively).
None of that can be invoked headlessly, so `AutoBuild_Server` calls the lowest-level server
procedures those forms eventually reach (listed above) directly, in the same order and with the
same parameters the forms use, instead of trying to drive the forms themselves.

## Known risks / assumptions to double-check

- **Extension load mechanics for `Ext/ManagedApplicationModule.bsl`** were not verified against
  the live `C:\Bases\Сборщик` base (no EDT/Configurator session against that base was available
  while writing this) - only against confirmed real extension `Configuration.xml`/module examples
  found locally and the task's own description of the intended mechanism. Load the extension and
  check the Configurator's metadata-check output before relying on it.
- **`ObjectBelonging=Adopted` / `ConfigurationExtensionPurpose` / `ConfigurationExtensionCompatibilityMode`**
  values in `Configuration.xml` were taken from two real extension exports found on this machine,
  not from the "Сборщик" project itself - re-verify `ConfigurationExtensionCompatibilityMode` does
  not exceed whatever `C:\Bases\Сборщик`'s own `ConfigurationExtensionCompatibilityMode` ceiling is
  (it was `Version8_5_1` in the export used for research).
- **Matching a `MobileApplications` item to a `MobileConfigurations` item** is done by scanning
  `Catalog.MobileApplications.Configurations` for a row whose `Configuration` equals the resolved
  configuration ref. If several application items reference the same configuration, the extension
  picks the first one (by `Code`) and logs a warning - it does not try to guess which one you meant.
- **The debug "sequential build" mode** (`MobileApplicationBuilding.IsNeedSequentialBuild`, a
  `<user>_sequential_build.mab` marker file in the builder cache folder) is explicitly unsupported
  by this extension (it raises a clear error) since the platform's own comment marks that mode
  "not suitable for industrial use".
- **Build timeout** (2 hours) will call `BuildProcessManagement.BuildInterrupt`, which deletes any
  partial artifacts already produced for that run - a timed-out run has nothing to export.
- Everything runs under `SetPrivilegedMode(True)` for the duration of the import+build (bracketed,
  reset to `False` on every exit path) since this mirrors `CommonForms.MakeApplicationProccess`
  and background build jobs that need to see `InformationRegister.BuildProcess` regardless of the
  interactive user's own role assignments - acceptable for a local, single-developer base as
  requested, but worth revisiting if this base is ever shared.
