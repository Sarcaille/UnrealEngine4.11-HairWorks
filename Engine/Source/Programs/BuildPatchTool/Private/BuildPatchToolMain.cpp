// Copyright 1998-2015 Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	BuildPatchToolMain.cpp: Implements the BuildPatchTool application's main loop.

	The tool can be used in two modes of operation:
	(1) to generate patch data (manifest, chunks, files) for build images given the existing cloud data; or
	(2) to "compactify" a cloud directory by removing all orphaned chunks, not referenced by any manifest file.

	In order to trigger compactify functionality, the -compactify commandline argument should be specified.


	GENERATE PATCH DATA MODE

	The tool supports generating chunk based patches or simple file based patches. Chunk based patch data will be generated by default
	but you can switch to file based patch data by adding the -nochunks commandline argument.

	File based patch data is only recommended for small build images that probably wouldn't benefit from chunking. I.e. Updates are commonly only a few small changed files.

	Required arguments:
	-BuildRoot=""			Specifies in quotes the directory containing the build image to be read.
	-CloudDir=""			Specifies in quotes the cloud directory where existing data will be recognised from, and new data added to.
	-AppID=123456			Specifies without quotes, the ID number for the app
	-AppName=""				Specifies in quotes, the name of the app
	-BuildVersion=""		Specifies in quotes, the version string for the build image
	-AppLaunch=""			Specifies in quotes, the path to the app executable, must be relative to, and inside of BuildRoot.
	-AppArgs=""				Specifies in quotes, the commandline to send to the app on launch.

	Optional arguments:
	-stdout					Adds stdout logging to the app.
	-nochunks				Creates file based patch data instead of chunk based patch data. Should not be used with builds that have large files.
	-FileIgnoreList=""		Specifies in quotes, the path to a text file containing BuildRoot relative files, separated by \r\n line endings, to not be included in the build.
	-FileAttributeList=""	Specifies in quotes, the path to a text file containing quoted BuildRoot relative files followed by optional attribute keywords readonly compressed executable, separated by \r\n line endings. These attribute will be applied when build is installed client side.
	-PrereqName=""			Specifies in quotes, the display name for the prerequisites installer
	-PrereqPath=""			Specifies in quotes, the prerequisites installer to launch on successful product install
	-PrereqArgs=""			Specifies in quotes, the commandline to send to prerequisites installer on launch
	-DataAgeThreshold=12.5	Specified the maximum age (in days) of existing patch data which can be reused in the generated manifest

	NB: -DataAgeThreshold can also be present in the [PatchGeneration] section of BuildPatchTool.ini in the cloud directory
	NB: If -DataAgeThreshold is not supplied, either on the command-line or in BuildPatchTool.ini, then all existing data is eligible for reuse in the generated manifest

	Example command lines:
	-BuildRoot="D:\Builds\ExampleGame_[07-02_03.00]" -FileIgnoreList="D:\Builds\ExampleGame_[07-02_03.00]\Manifest_DebugFiles.txt" -CloudDir="D:\BuildPatchCloud" -AppID=123456 -AppName="Example" -BuildVersion="ExampleGame_[07-02_03.00]" -AppLaunch=".\ExampleGame\Binaries\Win32\ExampleGame.exe" -AppArgs="-pak -nosteam" -DataAgeThreshold=12
	-BuildRoot="C:\Program Files (x86)\Community Portal" -CloudDir="E:\BuildPatchCloud" -AppID=0 -AppName="Community Portal" -BuildVersion="++depot+UE4-CL-1791234" -AppLaunch=".\Engine\Binaries\Win32\CommunityPortal.exe" -AppArgs="" -nochunks -DataAgeThreshold=12

	-custom="field=value"	 	Adds a custom string field to the build manifest
	-customint="field=number"	Adds a custom int64 field to the build manifest 
	-customfloat="field=number"	Adds a custom double field to the build manifest

	COMPACTIFY MODE

	Required arguments:
	-CloudDir=""		Specifies in quotes the cloud directory where manifest files and chunks to be compactified can be found.
	-compactify			Must be specified to launch the tool in compactify mode

	Optional arguments:
	-stdout				Adds stdout logging to the app.
	-preview			Log all the actions it will take to update internal structures, but don't actually execute them.
	-nopatchdelete		When specified, will only set the modified date of referenced files to the current time, but will NOT delete any patch data.
	-ManifestsList=""			Specifies in quotes, the list of manifest filenames to keep following the operation. If omitted, all manifests are kept.
	-ManifestsFile=""			Specifies in quotes, the name of the file (relative to CloudDir) which contains a list of manifests to keep, one manifest filename per line
	-DataAgeThreshold=14.25		The maximum age in days of chunk files that will be retained. All older chunks will be deleted.

	NB: If -ManifestsList is specified, then -ManifestsFile is ignored.
	NB: -DataAgeThreshold can also be present in the [Compactify] section of BuildPatchTool.ini in the cloud directory
	NB: If -DataAgeThreshold is not supplied, either on the command-line or in BuildPatchTool.ini, then all unreferenced existing data is eligible for deletion by the compactify process

	Example command lines:
	-CloudDir="E:\BuildPatchCloud" -compactify -DataAgeThreshold=14
	-CloudDir="E:\BuildPatchCloud" -compactify -DataAgeThreshold=14 -ManifestsList="Example_V1.manifest,Example_V2.manifest"
	-CloudDir="E:\BuildPatchCloud" -compactify -DataAgeThreshold=14 -ManifestsFile="preserve_manifests.txt"

	DATA ENUMERATE MODE

	Required arguments:
	-ManifestFile=""	Specifies in quotes the file path to the manifest to enumerate from
	-OutputFile=""		Specifies in quotes the file path to a file where the list will be saved out, \r\n separated cloud relative paths.
	-includesizes		When specified, the size of each file (in bytes) will be output following the filename and a tab.  I.e. /path/to/chunk\t1233
	-dataenumerate		Must be specified to launch the tool in cloud seeder mode

=============================================================================*/

#include "RequiredProgramMainCPPInclude.h"

#include "BuildPatchServices.h"

#include "CoreUObject.h"

// Ensure compiled with patch generation code
#if WITH_BUILDPATCHGENERATION
#else
#error BuildPatchTool must define WITH_BUILDPATCHGENERATION
#endif

IMPLEMENT_APPLICATION( BuildPatchTool, "BuildPatchTool" );

struct FCommandLineMatcher
{
	const FString Command;

	FCommandLineMatcher(FString InCommand)
		: Command(MoveTemp(InCommand))
	{}

	FORCEINLINE bool operator()(const FString& ToMatch) const
	{
		if (ToMatch.StartsWith(Command, ESearchCase::CaseSensitive))
		{
			return true;
		}

		return false;
	}
};

class FBuildPatchOutputDevice : public FOutputDevice
{
public:
	virtual void Serialize( const TCHAR* V, ELogVerbosity::Type Verbosity, const class FName& Category ) override
	{
#if PLATFORM_USE_LS_SPEC_FOR_WIDECHAR
		printf( "\n%ls", *FOutputDevice::FormatLogLine( Verbosity, Category, V, GPrintLogTimes ) );
#else
		wprintf( TEXT( "\n%s" ), *FOutputDevice::FormatLogLine( Verbosity, Category, V, GPrintLogTimes ) );
#endif
		fflush( stdout );
	}
};

namespace EBuildPatchToolMode
{
	enum Type
	{
		PatchGeneration,
		Compactify,
		DataEnumeration,

		Unknown
	};
}


int32 BuildPatchToolMain( const TCHAR* CommandLine )
{
	// Initialize the command line
	FCommandLine::Set(CommandLine);

	// Add log devices
	if (FParse::Param(FCommandLine::Get(), TEXT("stdout")))
	{
		GLog->AddOutputDevice(new FBuildPatchOutputDevice());
	}
	if (FPlatformMisc::IsDebuggerPresent())
	{
		GLog->AddOutputDevice(new FOutputDeviceDebug());
	}

	GLog->Logf(TEXT("BuildPatchToolMain ran with: %s"), CommandLine);

	FPlatformProcess::SetCurrentWorkingDirectoryToBaseDir();

	bool bSuccess = false;

	FString RootDirectory;
	FString CloudDirectory;
	uint32  AppID=0;
	FString AppName;
	FString BuildVersion;
	FString LaunchExe;
	FString LaunchCommand;
	FString IgnoreListFile;
	FString AttributeListFile;
	FString PrereqName;
	FString PrereqPath;
	FString PrereqArgs;
	FString ManifestsList;
	FString ManifestsFile;
	float DataAgeThreshold = 0.0f;
	FString IniFile;
	TMap<FString, FVariant> CustomFields;
	FString ManifestFile;
	FString OutputFile;

	EBuildPatchToolMode::Type ToolMode = EBuildPatchToolMode::Unknown;
	bool bPreview = false;
	bool bNoPatchDelete = false;
	bool bPatchWithReuseAgeThreshold = true;
	bool bIncludeSizes = false;

	// Collect all the info from the CommandLine
	TArray< FString > Tokens, Switches;
	FCommandLine::Parse(FCommandLine::Get(), Tokens, Switches);
	if (Switches.Num() > 0)
	{
		int32 BuildRootIdx;
		int32 CloudDirIdx;
		int32 AppIDIdx;
		int32 AppNameIdx;
		int32 BuildVersionIdx;
		int32 AppLaunchIdx;
		int32 AppArgsIdx;
		int32 FileIgnoreListIdx;
		int32 FileAttributeListIdx;
		int32 PrereqNameIdx;
		int32 PrereqPathIdx;
		int32 PrereqArgsIdx;
		int32 ManifestsListIdx;
		int32 ManifestsFileIdx;
		int32 DataAgeThresholdIdx;
		int32 ManifestFileIdx;
		int32 OutputFileIdx;
		bSuccess = true;

		if (Switches.IndexOfByPredicate(FCommandLineMatcher(TEXT("compactify"))) != INDEX_NONE)
		{
			ToolMode = EBuildPatchToolMode::Compactify;
		}
		else if (Switches.IndexOfByPredicate(FCommandLineMatcher(TEXT("dataenumerate"))) != INDEX_NONE)
		{
			ToolMode = EBuildPatchToolMode::DataEnumeration;
		}
		else
		{
			ToolMode = EBuildPatchToolMode::PatchGeneration;
		}
		bPreview = Switches.IndexOfByPredicate(FCommandLineMatcher(TEXT("preview"))) != INDEX_NONE;
		bNoPatchDelete = Switches.IndexOfByPredicate(FCommandLineMatcher(TEXT("nopatchdelete"))) != INDEX_NONE;
		bIncludeSizes = Switches.IndexOfByPredicate(FCommandLineMatcher(TEXT("includesizes"))) != INDEX_NONE;
		BuildRootIdx = Switches.IndexOfByPredicate(FCommandLineMatcher(TEXT("BuildRoot")));
		CloudDirIdx = Switches.IndexOfByPredicate(FCommandLineMatcher(TEXT("CloudDir")));
		AppIDIdx = Switches.IndexOfByPredicate(FCommandLineMatcher(TEXT("AppID")));
		AppNameIdx = Switches.IndexOfByPredicate(FCommandLineMatcher(TEXT("AppName")));
		BuildVersionIdx = Switches.IndexOfByPredicate(FCommandLineMatcher(TEXT("BuildVersion")));
		AppLaunchIdx = Switches.IndexOfByPredicate(FCommandLineMatcher(TEXT("AppLaunch")));
		AppArgsIdx = Switches.IndexOfByPredicate(FCommandLineMatcher(TEXT("AppArgs")));
		FileIgnoreListIdx = Switches.IndexOfByPredicate(FCommandLineMatcher(TEXT("FileIgnoreList")));
		FileAttributeListIdx = Switches.IndexOfByPredicate(FCommandLineMatcher(TEXT("FileAttributeList")));
		PrereqNameIdx = Switches.IndexOfByPredicate(FCommandLineMatcher(TEXT("PrereqName")));
		PrereqPathIdx = Switches.IndexOfByPredicate(FCommandLineMatcher(TEXT("PrereqPath")));
		PrereqArgsIdx = Switches.IndexOfByPredicate(FCommandLineMatcher(TEXT("PrereqArgs")));
		ManifestsListIdx = Switches.IndexOfByPredicate(FCommandLineMatcher(TEXT("ManifestsList")));
		ManifestsFileIdx = Switches.IndexOfByPredicate(FCommandLineMatcher(TEXT("ManifestsFile")));
		DataAgeThresholdIdx = Switches.IndexOfByPredicate(FCommandLineMatcher(TEXT("DataAgeThreshold")));
		ManifestFileIdx = Switches.IndexOfByPredicate(FCommandLineMatcher(TEXT("ManifestFile")));
		OutputFileIdx = Switches.IndexOfByPredicate(FCommandLineMatcher(TEXT("OutputFile")));

		// Check required param indexes
		switch (ToolMode)
		{
		case EBuildPatchToolMode::PatchGeneration:
		bSuccess = bSuccess && CloudDirIdx != INDEX_NONE;
			bSuccess = bSuccess && BuildRootIdx != INDEX_NONE;
			bSuccess = bSuccess && AppIDIdx != INDEX_NONE;
			bSuccess = bSuccess && AppNameIdx != INDEX_NONE;
			bSuccess = bSuccess && BuildVersionIdx != INDEX_NONE;
			bSuccess = bSuccess && AppLaunchIdx != INDEX_NONE;
			bSuccess = bSuccess && AppArgsIdx != INDEX_NONE;
			break;
		case EBuildPatchToolMode::Compactify:
			bSuccess = bSuccess && CloudDirIdx != INDEX_NONE;
			break;
		case EBuildPatchToolMode::DataEnumeration:
			bSuccess = bSuccess && ManifestFileIdx != INDEX_NONE;
			bSuccess = bSuccess && OutputFileIdx != INDEX_NONE;
			break;
		}

		// Get required param values
		switch (ToolMode)
		{
		case EBuildPatchToolMode::PatchGeneration:
		bSuccess = bSuccess && FParse::Value( *Switches[CloudDirIdx], TEXT( "CloudDir=" ), CloudDirectory );
			bSuccess = bSuccess && FParse::Value(*Switches[BuildRootIdx], TEXT("BuildRoot="), RootDirectory);
			bSuccess = bSuccess && FParse::Value(*Switches[AppIDIdx], TEXT("AppID="), AppID);
			bSuccess = bSuccess && FParse::Value(*Switches[AppNameIdx], TEXT("AppName="), AppName);
			bSuccess = bSuccess && FParse::Value(*Switches[BuildVersionIdx], TEXT("BuildVersion="), BuildVersion);
			bSuccess = bSuccess && FParse::Value(*Switches[AppLaunchIdx], TEXT("AppLaunch="), LaunchExe);
			bSuccess = bSuccess && FParse::Value(*Switches[AppArgsIdx], TEXT("AppArgs="), LaunchCommand);
			break;
		case EBuildPatchToolMode::Compactify:
			bSuccess = bSuccess && FParse::Value(*Switches[CloudDirIdx], TEXT("CloudDir="), CloudDirectory);
			break;
		case EBuildPatchToolMode::DataEnumeration:
			bSuccess = bSuccess && FParse::Value(*Switches[ManifestFileIdx], TEXT("ManifestFile="), ManifestFile);
			bSuccess = bSuccess && FParse::Value(*Switches[OutputFileIdx], TEXT("OutputFile="), OutputFile);
			break;
		}

		// Get optional param values
		if( FileIgnoreListIdx != INDEX_NONE )
		{
			FParse::Value( *Switches[ FileIgnoreListIdx ], TEXT( "FileIgnoreList=" ), IgnoreListFile );
		}

		if( FileAttributeListIdx != INDEX_NONE )
		{
			FParse::Value( *Switches[ FileAttributeListIdx ], TEXT( "FileAttributeList=" ), AttributeListFile );
		}

		if( PrereqNameIdx != INDEX_NONE )
		{
			FParse::Value( *Switches[ PrereqNameIdx ], TEXT( "PrereqName=" ), PrereqName );
		}

		if( PrereqPathIdx != INDEX_NONE )
		{
			FParse::Value( *Switches[ PrereqPathIdx ], TEXT( "PrereqPath=" ), PrereqPath );
		}

		if( PrereqArgsIdx != INDEX_NONE )
		{
			FParse::Value( *Switches[ PrereqArgsIdx ], TEXT( "PrereqArgs=" ), PrereqArgs );
		}

		if (ManifestsListIdx != INDEX_NONE)
		{
			bool bShouldStopOnComma = false;
			FParse::Value(*Switches[ManifestsListIdx], TEXT("ManifestsList="), ManifestsList, bShouldStopOnComma);
		}
		else if (ManifestsFileIdx != INDEX_NONE)
		{
			FParse::Value( *Switches[ ManifestsFileIdx ], TEXT( "ManifestsFile=" ), ManifestsFile);
		}

		FString CustomValue;
		FString Left;
		FString Right;
		for (const auto& Switch : Switches)
		{
			if (FParse::Value(*Switch, TEXT("custom="), CustomValue))
			{
				if (CustomValue.Split(TEXT("="), &Left, &Right))
				{
					Left.Trim();
					Left.TrimTrailing();
					Right.Trim();
					Right.TrimTrailing();
					CustomFields.Add(Left, FVariant(Right));
				}
			}
			else if (FParse::Value(*Switch, TEXT("customfloat="), CustomValue))
			{
				if (CustomValue.Split(TEXT("="), &Left, &Right))
				{
					Left.Trim();
					Left.TrimTrailing();
					Right.Trim();
					Right.TrimTrailing();
					if (!Right.IsNumeric())
					{
						GLog->Log(ELogVerbosity::Error, TEXT("An error occurred processing token -customfloat. Non Numeric character found right of ="));
						bSuccess = false;
					}
					CustomFields.Add(Left, FVariant(TCString<TCHAR>::Atod(*Right)));
				}
			}
			else if (FParse::Value(*Switch, TEXT("customint="), CustomValue))
			{
				if (CustomValue.Split(TEXT("="), &Left, &Right))
				{
					Left.Trim();
					Left.TrimTrailing();
					Right.Trim();
					Right.TrimTrailing();
					if (!Right.IsNumeric())
					{
						GLog->Log(ELogVerbosity::Error, TEXT("An error occurred processing token -customint. Non Numeric character found right of ="));
						bSuccess = false;
					}
					CustomFields.Add(Left, FVariant(TCString<TCHAR>::Atoi64(*Right)));
				}
			}
		}

		FPaths::NormalizeDirectoryName( RootDirectory );
		FPaths::NormalizeDirectoryName( CloudDirectory );

		if (bSuccess)
		{
			// Initialize the configuration system, we can only do this reliably if we have CloudDirectory (i.e. bSuccess is true)
			IniFile = CloudDirectory / TEXT("BuildPatchTool.ini");
			GConfig->InitializeConfigSystem();

			if (DataAgeThresholdIdx != INDEX_NONE)
			{
				FParse::Value(*Switches[DataAgeThresholdIdx], TEXT("DataAgeThreshold="), DataAgeThreshold);
			}
			else
			{
				switch (ToolMode)
				{
				case EBuildPatchToolMode::Compactify:
					// For compactification, if we don't pass in DataAgeThreshold, and it's not in BuildPatchTool.ini,
					// then we set it to zero, to indicate that any unused chunks are valid for deletion
					if (!GConfig->GetFloat(TEXT("Compactify"), TEXT("DataAgeThreshold"), DataAgeThreshold, IniFile))
					{
						GLog->Log(ELogVerbosity::Warning, TEXT("DataAgeThreshold not supplied, so all unreferenced data is eliglble for deletion. Note that this process is NOT compatible with any concurrently running patch generaiton processes"));
						DataAgeThreshold = 0.0f;
					}
					break;
				case EBuildPatchToolMode::PatchGeneration:
					// For patch generation, if we don't pass in DataAgeThreshold, and it's not specified in BuildPatchTool.ini,
					// then we set bChunkWithReuseAgeThreshold to false, which indicates that *all* patch data is valid for reuse
					if (!GConfig->GetFloat(TEXT("PatchGeneration"), TEXT("DataAgeThreshold"), DataAgeThreshold, IniFile))
					{
						GLog->Log(ELogVerbosity::Warning, TEXT("DataAgeThreshold not supplied, so all existing data is eligible for reuse. Note that this process is NOT compatible with any concurrently running compactify processes"));
						DataAgeThreshold = 0.0f;
						bPatchWithReuseAgeThreshold = false;
					}
					break;
				}
			}
		}
	}

	// Initialize the file manager
	IFileManager::Get().ProcessCommandLineOptions();

	// Check for argument error
	if( !bSuccess )
	{
		GLog->Log(ELogVerbosity::Error, TEXT("An error occurred processing arguments"));
		return 1;
	}

	if (ToolMode == EBuildPatchToolMode::Compactify && bPreview && bNoPatchDelete)
	{
		GLog->Log(ELogVerbosity::Error, TEXT("Only one of -preview and -nopatchdelete can be specified"));
		return 5;
	}

	if (!IgnoreListFile.IsEmpty() && !FPaths::FileExists(IgnoreListFile))
	{
		GLog->Logf(ELogVerbosity::Error, TEXT("Provided file ignore list was not found %s"), *IgnoreListFile);
		return 6;
	}

	if (!AttributeListFile.IsEmpty() && !FPaths::FileExists(AttributeListFile))
	{
		GLog->Logf(ELogVerbosity::Error, TEXT("Provided file attribute list was not found %s"), *AttributeListFile);
		return 7;
	}

	// Load the BuildPatchServices Module
	TSharedPtr<IBuildPatchServicesModule> BuildPatchServicesModule = StaticCastSharedPtr<IBuildPatchServicesModule>( FModuleManager::Get().LoadModule( TEXT( "BuildPatchServices" ) ) );

	// Initialise the UObject system and process our uobject classes
	FModuleManager::Get().LoadModule(TEXT("CoreUObject"));
	FCoreDelegates::OnInit.Broadcast();
	ProcessNewlyLoadedUObjects();

	// Setup the module
	BuildPatchServicesModule->SetCloudDirectory( CloudDirectory + TEXT( "/" ) );

	// Run the mode!
	switch (ToolMode)
	{
	case EBuildPatchToolMode::Compactify:
	{
		// Split out our manifests to keep arg (if any) into an array of manifest filenames
		TArray<FString> ManifestsArr;
		if (ManifestsList.Len() > 0)
		{
			ManifestsList.ParseIntoArray(ManifestsArr, TEXT(","), true);
		}
		else if (ManifestsFile.Len() > 0)
		{
			FString ManifestsFilePath = CloudDirectory / ManifestsFile;
			FString Temp;
			if (FFileHelper::LoadFileToString(Temp, *ManifestsFilePath))
			{
				Temp.ReplaceInline(TEXT("\r"), TEXT("\n"));
				Temp.ParseIntoArray(ManifestsArr, TEXT("\n"), true);
			}
			else
			{
				GLog->Log(ELogVerbosity::Error, TEXT("Could not open specified manifests to keep file"));
				BuildPatchServicesModule.Reset();
				FCoreDelegates::OnExit.Broadcast();
				return 2;
			}
		}

		// Determine our mode of operation
		ECompactifyMode::Type CompactifyMode = ECompactifyMode::Full;
		if (bPreview)
		{
			CompactifyMode = ECompactifyMode::Preview;
		}
		else if (bNoPatchDelete)
		{
			CompactifyMode = ECompactifyMode::NoPatchDelete;
		}

		// Run the compactify routine
		bSuccess = BuildPatchServicesModule->CompactifyCloudDirectory(ManifestsArr, DataAgeThreshold, CompactifyMode);
	}
		break;
	case EBuildPatchToolMode::PatchGeneration:
	{
		FBuildPatchSettings Settings;
		Settings.RootDirectory = RootDirectory + TEXT("/");
		Settings.AppID = AppID;
		Settings.AppName = AppName;
		Settings.BuildVersion = BuildVersion;
		Settings.LaunchExe = LaunchExe;
		Settings.LaunchCommand = LaunchCommand;
		Settings.IgnoreListFile = IgnoreListFile;
		Settings.AttributeListFile = AttributeListFile;
		Settings.PrereqName = PrereqName;
		Settings.PrereqPath = PrereqPath;
		Settings.PrereqArgs = PrereqArgs;
		Settings.DataAgeThreshold = DataAgeThreshold;
		Settings.bShouldHonorReuseThreshold = bPatchWithReuseAgeThreshold;
		Settings.CustomFields = CustomFields;

		// Run the build generation
		if (FParse::Param(FCommandLine::Get(), TEXT("nochunks")))
		{
			bSuccess = BuildPatchServicesModule->GenerateFilesManifestFromDirectory(Settings);
		}
		else
		{
			bSuccess = BuildPatchServicesModule->GenerateChunksManifestFromDirectory(Settings);
		}
	}
		break;
	case EBuildPatchToolMode::DataEnumeration:
	{
		// Run the data enumeration routine
		bSuccess = BuildPatchServicesModule->EnumerateManifestData(MoveTemp(ManifestFile), MoveTemp(OutputFile), bIncludeSizes);
	}
		break;
	default:
	{
		GLog->Log(ELogVerbosity::Error, TEXT("Unknown tool mode"));
		BuildPatchServicesModule.Reset();
		FCoreDelegates::OnExit.Broadcast();
	}
		return 3;
	}

	// Release the module ptr
	BuildPatchServicesModule.Reset();

	// Check for processing error
	if (!bSuccess)
	{
		GLog->Log(ELogVerbosity::Error, TEXT("A fatal error occurred executing BuildPatchTool.exe"));
		FCoreDelegates::OnExit.Broadcast();
		return 4;
	}

	FCoreDelegates::OnExit.Broadcast();

	GLog->Log(TEXT("BuildPatchToolMain completed successfuly"));
	return 0;
}

INT32_MAIN_INT32_ARGC_TCHAR_ARGV()
{
	FString CommandLine;
	for( int32 Option = 1; Option < ArgC; Option++ )
	{
		CommandLine += TEXT(" ");
		FString Argument( ArgV[Option] );
		if( Argument.Contains( TEXT(" ") ) )
		{
			if (Argument.Contains(TEXT("=")))
			{
				FString ArgName;
				FString ArgValue;
				Argument.Split( TEXT("="), &ArgName, &ArgValue );
				Argument = FString::Printf( TEXT("%s=\"%s\""), *ArgName, *ArgValue );
			}
			else
			{
				Argument = FString::Printf(TEXT("\"%s\""), *Argument);
			}
		}
		CommandLine += Argument;
	}

	return BuildPatchToolMain( *CommandLine );
}
