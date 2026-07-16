// Patches the base configuration's ManagedApplicationModule.OnStart event (Ext/ManagedApplicationModule.bsl,
// Procedure OnStart() - no parameters, that is the exact signature used by this configuration).
//
// If the infobase was started with:
//   1cv8c ENTERPRISE /F"<infobase path>" /C"autobuild;<path to 1cema.zip>;<configuration code or name>"
// this hook runs the headless import+build pipeline (AutoBuild_Server.RunAutoBuild) after the base
// startup logic has finished, then terminates the session. For any other LaunchParameter value
// (including a normal interactive start with no /C at all) this hook does nothing at all, so the
// builder's regular startup behavior is unchanged.
&Before("OnStart")
Procedure AutoBuild_BeforeOnStart()

	If Not StrStartsWith(Upper(TrimAll(LaunchParameter)), "AUTOBUILD;") Then
		Return;
	EndIf;

	// RunAutoBuild never raises: any error is written to "<zip>.autobuild.log" / ".fail".
	AutoBuild_Server.RunAutoBuild(LaunchParameter);

	Exit(False);

EndProcedure
