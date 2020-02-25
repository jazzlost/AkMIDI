// Copyright 1998-2018 Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class AkMIDI : ModuleRules
{
#if WITH_FORWARDED_MODULE_RULES_CTOR
   public AkMIDI(ReadOnlyTargetRules Target) : base(Target)
#else
    public AkMIDI(TargetInfo Target)
#endif
	{
        PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;
		
		PublicIncludePaths.AddRange(
			new string[] {
				// ... add public include paths required here ...
			}
			);
				
		
		PrivateIncludePaths.AddRange(
			new string[] {
                "AkMIDI/Classes"
				// ... add other private include paths required here ...
			}
			);
			
		
		PublicDependencyModuleNames.AddRange(
			new string[]
			{
                "Core",
				// ... add other public dependencies that you statically link with here ...
			}
			);
			
		
		PrivateDependencyModuleNames.AddRange(
			new string[]
			{
				"CoreUObject",
				"Engine",
				"Slate",
				"SlateCore",
                "AkAudio",
				// ... add private dependencies that you statically link with here ...	
			}
			);
		
		
		DynamicallyLoadedModuleNames.AddRange(
			new string[]
			{
				// ... add any modules that your module loads dynamically here ...
			}
			);

        // Windows
        if (Target.Platform == UnrealTargetPlatform.Win32 || Target.Platform == UnrealTargetPlatform.Win64)
        {
            PublicDefinitions.Add("__WINDOWS_MM__=1");
            PublicAdditionalLibraries.Add("winmm.lib");
        }
        // MAC / IOS
        else if (Target.Platform == UnrealTargetPlatform.Mac || Target.Platform == UnrealTargetPlatform.IOS)
        {
            PublicDefinitions.Add("__MACOSX_CORE__=1");
            PublicIncludePaths.AddRange(new string[] { "Runtime/Core/Public/Apple" });

            if (Target.Platform == UnrealTargetPlatform.Mac)
                PublicIncludePaths.AddRange(new string[] { "Runtime/Core/Public/Mac" });
            else
                PublicIncludePaths.AddRange(new string[] { "Runtime/Core/Public/IOS" });

            PublicFrameworks.AddRange(new string[]
            {
                "CoreMIDI", "CoreAudio", "CoreFoundation"
            });
        }
        // LINUX
        else if (Target.Platform == UnrealTargetPlatform.Linux)
        {
            PublicDefinitions.Add("__LINUX_ALSA__=1");

            PublicIncludePaths.Add("Runtime/Core/Public/Linux");

            PublicAdditionalLibraries.AddRange(
            new string[]
            {
                "libasound2.so", "libpthread.so"
            });
        }
    }
}
