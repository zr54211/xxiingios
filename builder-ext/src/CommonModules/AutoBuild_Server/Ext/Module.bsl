
// AutoBuild_Server
//
// Headless "import configuration file + build mobile application + export artifacts" pipeline,
// triggered from the command line as:
//
//   1cv8c ENTERPRISE /F"<infobase path>" /C"autobuild;<path to 1cema.zip>;<configuration code or name>"
//
// The startup hook that calls RunAutoBuild() lives in Ext/ManagedApplicationModule.bsl
// (patches the base configuration's OnStart event). See README.md next to this extension
// for the full description, the exact base-configuration procedures this module relies on,
// and diagnostics (log file / marker files).
//
// This module intentionally re-implements (instead of reusing) the client-side plumbing of
// Catalogs.MobileConfigurations.ItemForm and CommonForms.MakeApplicationProccess, because that
// plumbing is built around NotifyDescription callbacks and an interactive form/timer and cannot
// be called headlessly. It calls straight into the lowest-level server procedures those forms
// eventually call (MobileCfgsAndAppsServer, StoredFileManagement, MobileApplicationBuilding,
// BuildProcessManagement, ServiceClientServer) - see README.md for exact references.

#Region Private

// Two hours by default: Android + iOS multi-architecture builds on a modest dev machine can take a while.
// Adjust here if needed - there is no command-line parameter for this on purpose (keep the
// command line format fixed: "autobuild;<zip path>;<configuration code or name>").
Function BuildTimeoutSeconds()

	Return 7200;

EndFunction

// Parses the LaunchParameter value.
//
// Returns:
//  Structure - with fields ZipFile, ConfigurationID, if StartupParameter is a well-formed
//              "autobuild;<zip path>;<configuration code or name>" string.
//  Undefined - if StartupParameter does not request an autobuild run.
//
Function ParseParams(Val StartupParameter)

	Parts = StrSplit(StartupParameter, ";", False);
	If Parts.Count() < 3 Then
		Return Undefined;
	EndIf;
	If Upper(TrimAll(Parts[0])) <> "AUTOBUILD" Then
		Return Undefined;
	EndIf;

	Result = New Structure("ZipFile, ConfigurationID");
	Result.ZipFile = TrimAll(Parts[1]);
	Result.ConfigurationID = TrimAll(Parts[2]);

	If IsBlankString(Result.ZipFile) Or IsBlankString(Result.ConfigurationID) Then
		Return Undefined;
	EndIf;

	Return Result;

EndFunction

Procedure LogLine(Val Log, Val Text)

	If Log = Undefined Then
		Return;
	EndIf;
	Log.WriteLine(Format(CurrentSessionDate(), "DF=HH:mm:ss") + "  " + Text);

EndProcedure

Procedure WriteMarkerFile(Val FileName, Val Text)

	Writer = New TextWriter(FileName, TextEncoding.UTF8, , False);
	Writer.WriteLine(Text);
	Writer.Close();

EndProcedure

Procedure DeleteMarkerFile(Val FileName)

	MarkerFile = New File(FileName);
	If MarkerFile.Exists() Then
		DeleteFiles(FileName);
	EndIf;

EndProcedure

// Finds the Catalog.MobileConfigurations item to overwrite.
//
// Id can be either the Code or the Description of an existing (non-folder) configuration item,
// or the Code/Description of its parent group - in which case the existing item of the matching
// ConfigurationType inside that group is reused (mirrors MobileCfgsAndAppsServer.SelectConfiguration,
// the same lookup Catalogs.MobileConfigurations.ItemForm relies on for "load into the same item").
// AutoBuild never creates new items or groups - see README.md.
//
Function FindConfigurationItem(Val Id)

	Query = New Query;
	Query.Text =
		"SELECT TOP 1
		|	MobileConfigurations.Ref AS Ref,
		|	MobileConfigurations.IsFolder AS IsFolder
		|FROM
		|	Catalog.MobileConfigurations AS MobileConfigurations
		|WHERE
		|	MobileConfigurations.DeletionMark = FALSE
		|	AND (MobileConfigurations.Code = &Id
		|		OR MobileConfigurations.Description = &Id)
		|
		|ORDER BY
		|	MobileConfigurations.IsFolder ASC";
	Query.SetParameter("Id", Id);
	Selection = Query.Execute().Select();
	If Not Selection.Next() Then
		Raise "Configuration/group '" + Id + "' was not found in Catalog.MobileConfigurations"
			+ " (searched by Code and Description).";
	EndIf;

	If Not Selection.IsFolder Then
		Return Selection.Ref;
	EndIf;

	GroupRef = Selection.Ref;
	AppType = MobileCfgsAndAppsServer.ConfigurationType(GroupRef);
	ItemRef = MobileCfgsAndAppsServer.SelectConfiguration(GroupRef, AppType);
	If ItemRef.IsEmpty() Then
		Raise "Group '" + Id + "' has no existing configuration item to overwrite."
			+ " AutoBuild only updates an existing item, it does not create new ones.";
	EndIf;

	Return ItemRef;

EndFunction

// Re-implements the import step of Catalogs.MobileConfigurations.ItemForm
// (ImportConfigurationAtServer + the CfgParams branch of OnWriteAtServer), so an existing
// configuration item can be overwritten headlessly, without opening the form.
// See README.md for the exact original procedure references this mirrors.
//
Procedure ImportConfigurationFile(Val ConfigRef, Val ZipFilePath, Val Log)

	ZipFile = New File(ZipFilePath);
	If Not ZipFile.Exists() Then
		Raise "File not found: " + ZipFilePath;
	EndIf;

	StorageUUID = New UUID;
	BinaryData = New BinaryData(ZipFilePath);
	Addr = PutToTempStorage(BinaryData, StorageUUID);

	CfgParams = MobileCfgsAndAppsServer.AnalyzeDownloadedFile(Addr, StorageUUID);

	ExpectedType = MobileCfgsAndAppsServer.ConfigurationType(ConfigRef.Parent);
	If ExpectedType <> CfgParams.configType Then
		Raise "Configuration type mismatch: the target group expects " + String(ExpectedType)
			+ ", the uploaded file contains " + String(CfgParams.configType) + ".";
	EndIf;

	ConfigObject = ConfigRef.GetObject();
	ImportWarnings = MobileCfgsAndAppsServer.FillCfgObject(ConfigObject, CfgParams);
	If Not IsBlankString(ImportWarnings) Then
		LogLine(Log, "Import warnings: " + ImportWarnings);
	EndIf;

	IsApplication = (ConfigObject.ConfigurationType = Enums.MakeType.Application);
	If IsApplication Then
		StoredFileManagement.PutObject(
			CfgParams.addrCfg, ConfigObject.Ref, Enums.FileTypes.MobileConfiguration, "1cema.xml");
	Else
		StoredFileManagement.PutObject(
			CfgParams.addrCfg, ConfigObject.Ref, Enums.FileTypes.MobileConfigurationClient, "1cemca.xml");
	EndIf;
	DeleteFromTempStorage(CfgParams.addrCfg);
	CfgParams.addrCfg = Undefined;

	If IsApplication And ConfigObject.IsDB Then
		If IsBlankString(CfgParams.addrStartDB) Then
			Raise "The uploaded file has no 1Cv8.1CM start database, but the configuration requires one (IsDB = True).";
		EndIf;
		StoredFileManagement.PutObject(
			CfgParams.addrStartDB, ConfigObject.Ref, Enums.FileTypes.MobileConfigurationDB, "1Cv8.1CM");
		DeleteFromTempStorage(CfgParams.addrStartDB);
		CfgParams.addrStartDB = Undefined;
	EndIf;

	MobileCfgsAndAppsServer.SaveExternalComponentToDB(ConfigObject.Ref, CfgParams);

	ConfigObject.Write();

EndProcedure

// Creates a new application build item, exactly the way the interactive flow does:
// LoadAndBuild's "createApp" action opens a new Catalog.MobileApplications item with
// FillingValues (Parent = application group), and the item's Filling handler copies the
// group settings and picks the latest configuration of every source group
// (MobileCfgsAndAppsServer.SelectConfiguration). The application group is found by its
// ConfigurationSources table referencing the configuration's parent group.
//
Function CreateApplicationBuild(Val ConfigRef, Val Log)

	Query = New Query;
	Query.Text =
		"SELECT DISTINCT
		|	ConfigurationSources.Ref AS Ref
		|FROM
		|	Catalog.MobileApplications.ConfigurationSources AS ConfigurationSources
		|WHERE
		|	ConfigurationSources.Source = &ConfigGroup
		|	AND ConfigurationSources.Ref.IsFolder = TRUE
		|	AND ConfigurationSources.Ref.DeletionMark = FALSE
		|
		|ORDER BY
		|	ConfigurationSources.Ref.Code";
	Query.SetParameter("ConfigGroup", ConfigRef.Parent);
	Selection = Query.Execute().Select();

	AppGroups = New Array;
	AppGroupNames = New Array;
	While Selection.Next() Do
		AppGroups.Add(Selection.Ref);
		AppGroupNames.Add(String(Selection.Ref));
	EndDo;

	If AppGroups.Count() = 0 Then
		Raise "No application group in Catalog.MobileApplications references configuration group '"
			+ String(ConfigRef.Parent) + "' in its ConfigurationSources table."
			+ " Create the application group once interactively before running AutoBuild.";
	EndIf;

	If AppGroups.Count() > 1 Then
		LogLine(Log, "WARNING: several application groups reference this configuration group,"
			+ " using the first one: " + StrConcat(AppGroupNames, "; "));
	EndIf;

	FillingData = New Structure;
	FillingData.Insert("Parent", AppGroups[0]);

	NewBuild = Catalogs.MobileApplications.CreateItem();
	NewBuild.Fill(FillingData);
	NewBuild.Write();
	LogLine(Log, "Created application build item: " + String(NewBuild.Ref));

	Return NewBuild.Ref;

EndFunction

// Runs the build and waits for it to finish. Mirrors the server-side logic of
// CommonForms.MakeApplicationProccess (OnCreateAtServer + ProcessStep), inlining the loop that
// BuildProcessManagement.StartBuildQueue itself runs. AutoBuild deliberately does NOT call
// StartBuildQueue as a nested background job (that call raises for file infobases, see
// BuildProcessManagement.StartBuildQueue) - it runs the same loop directly in this ordinary
// (non-background) server call instead, which works for both file and client/server infobases.
// See README.md for exact procedure references.
//
Procedure BuildApplicationAndWait(Val AppRef, Val Log)

	If BuildProcessManagement.IsBuildInProgress(AppRef) Then
		Raise "A build for application " + String(AppRef) + " is already in progress"
			+ " (InformationRegister.BuildProcess is not empty). AutoBuild refuses to start a second one.";
	EndIf;

	LogLine(Log, "Calling MobileApplicationBuilding.BuildApplication...");
	TaskArray = MobileApplicationBuilding.BuildApplication(AppRef);
	If TaskArray = Undefined Then
		Raise "MobileApplicationBuilding.BuildApplication returned Undefined. This means the builder cache folder"
			+ " contains a debug ""<user>_sequential_build.mab"" marker file"
			+ " (see MobileApplicationBuilding.IsNeedSequentialBuild) - a mode explicitly documented as unsuitable"
			+ " for production use. Remove the marker file and retry.";
	EndIf;

	ResultDataAddr = PutToTempStorage("", New UUID);
	BuildProcessManagement.BuildPrepare(AppRef, TaskArray, ResultDataAddr);
	LogLine(Log, "Build queue prepared: " + TaskArray.Count() + " task(s).");

	StartMoment = CurrentSessionDate();
	While True Do

		TasksLeft = BuildProcessManagement.BuildTasksStateUpdate(AppRef);
		If TasksLeft = 0 Then
			Break;
		EndIf;

		If CurrentSessionDate() - StartMoment > BuildTimeoutSeconds() Then
			BuildProcessManagement.BuildInterrupt(AppRef);
			Raise "Build timeout exceeded (" + BuildTimeoutSeconds() + " sec), the build was interrupted.";
		EndIf;

		BuildProcessManagement.BuildSchedule(AppRef);
		JobArray = BuildProcessManagement.GetJobsArray(AppRef);
		If JobArray.Count() > 0 Then
			BackgroundJobs.WaitForExecutionCompletion(JobArray, 2);
		EndIf;

	EndDo;

	// Диагностика заданий до BuildComplete (он удаляет записи регистра).
	TaskList = BuildProcessManagement.ReadBuildTasks(AppRef);
	For Each Task In TaskList Do

		LogLine(Log, "Task " + String(Task.FileVariant)
			+ ": JobState=" + Task.JobState + ", BuildState=" + Task.BuildState);

		If ValueIsFilled(Task.TaskID) Then
			Job = BackgroundJobs.FindByUUID(Task.TaskID);
			If Job <> Undefined And Job.ErrorInfo <> Undefined Then
				LogLine(Log, "  Job error: " + DetailErrorDescription(Job.ErrorInfo));
			EndIf;
		EndIf;

		If Task.BuildState = 3 And ValueIsFilled(Task.LogVariant) Then
			// Выгрузим лог упавшей сборки для диагностики.
			BuildLogName = TempFilesDir() + "autobuild-task.log";
			GetLogResult = StoredFileManagement.GetObject(AppRef, Task.LogVariant, BuildLogName);
			LogLine(Log, "  Build log exported: " + BuildLogName + " (Completed=" + GetLogResult.Completed + ")");
		EndIf;

	EndDo;

	ErrorText = BuildProcessManagement.BuildComplete(AppRef);
	If Not IsBlankString(ErrorText) Then
		Raise "The build completed with errors: " + ErrorText;
	EndIf;
	LogLine(Log, "Build completed without errors.");

EndProcedure

// Saves every completed BuildResults row to OutputDir, using the same naming
// (ServiceClientServer.ApplicationFileName) the interactive "Get application" command uses.
//
Function ExportBuildResults(Val AppRef, Val OutputDir, Val Log)

	ExportedCount = 0;
	For Each Row In AppRef.BuildResults Do

		If Not Row.Completed Then
			LogLine(Log, "Skipped (build did not complete): " + String(Row.FileVariant));
			Continue;
		EndIf;

		FileName = ServiceClientServer.ApplicationFileName(AppRef, Row.FileVariant);
		FullFileName = OutputDir + FileName;
		GetResult = StoredFileManagement.GetObject(AppRef, Row.FileVariant, FullFileName);

		If GetResult.Completed And Not GetResult.Removed Then
			LogLine(Log, "Exported: " + FullFileName);
			ExportedCount = ExportedCount + 1;
		Else
			LogLine(Log, "FAILED to export " + String(Row.FileVariant) + " (Removed = " + GetResult.Removed + ")");
		EndIf;

	EndDo;

	If ExportedCount = 0 Then
		Raise "No build artifacts were exported - BuildResults is empty or every file failed to save to disk.";
	EndIf;

	Return ExportedCount;

EndFunction

#EndRegion

#Region Public

// Entry point called from Ext/ManagedApplicationModule.bsl (After OnStart).
// Runs the whole "import configuration file -> build application -> export artifacts" pipeline.
//
// Never raises: every error is written to "<ZipFile>.autobuild.log" and marked with a
// "<ZipFile>.autobuild.ok" or "<ZipFile>.autobuild.fail" file, so the caller can always safely
// exit the session afterwards regardless of the outcome. If StartupParameter does not match the
// "autobuild;<zip path>;<configuration code or name>" format, this procedure does nothing.
//
// Parameters:
//  StartupParameter - String - value of the global LaunchParameter.
//
Procedure RunAutoBuild(Val StartupParameter) Export

	Params = ParseParams(StartupParameter);
	If Params = Undefined Then
		Return;
	EndIf;

	OkMarker = Params.ZipFile + ".autobuild.ok";
	FailMarker = Params.ZipFile + ".autobuild.fail";
	Log = Undefined;

	Try
		// Best-effort cleanup/setup: a locked leftover marker file or a bad log path must not
		// stop the run - the caller only needs Log <> Undefined to be checked from here on.
		DeleteMarkerFile(OkMarker);
		DeleteMarkerFile(FailMarker);
		Log = New TextWriter(Params.ZipFile + ".autobuild.log", TextEncoding.UTF8, , False);
	Except
		Log = Undefined;
	EndTry;

	Try

		LogLine(Log, "=== AutoBuild started " + Format(CurrentSessionDate(), "DF=yyyy-MM-dd HH:mm:ss") + " ===");
		LogLine(Log, "Zip file: " + Params.ZipFile);
		LogLine(Log, "Configuration code/name: " + Params.ConfigurationID);

		SetPrivilegedMode(True);

		ConfigRef = FindConfigurationItem(Params.ConfigurationID);
		LogLine(Log, "Target configuration item: " + String(ConfigRef));

		ImportConfigurationFile(ConfigRef, Params.ZipFile, Log);
		LogLine(Log, "Configuration file imported.");

		AppRef = CreateApplicationBuild(ConfigRef, Log);
		LogLine(Log, "Target application: " + String(AppRef));

		BuildApplicationAndWait(AppRef, Log);

		ZipFileInfo = New File(Params.ZipFile);
		OutputDir = ZipFileInfo.Path;
		ExportedCount = ExportBuildResults(AppRef, OutputDir, Log);

		SetPrivilegedMode(False);
		LogLine(Log, "=== AutoBuild finished OK, " + ExportedCount + " file(s) exported to " + OutputDir + " ===");
		WriteMarkerFile(OkMarker, "OK");

	Except

		SetPrivilegedMode(False);
		ErrorText = DetailErrorDescription(ErrorInfo());
		LogLine(Log, "FATAL ERROR: " + ErrorText);
		LogLine(Log, "=== AutoBuild finished WITH ERRORS ===");
		Try
			// The .log file above already has the full error text; the marker file is a
			// convenience for the caller and must not turn a build failure into an unhandled one.
			WriteMarkerFile(FailMarker, ErrorText);
		Except
		EndTry;

	EndTry;

	Try
		// This procedure must return normally no matter what, so the client-side caller can
		// always call Exit() afterwards - closing the log stream must not raise past this point.
		If Log <> Undefined Then
			Log.Close();
		EndIf;
	Except
	EndTry;

EndProcedure

#EndRegion
