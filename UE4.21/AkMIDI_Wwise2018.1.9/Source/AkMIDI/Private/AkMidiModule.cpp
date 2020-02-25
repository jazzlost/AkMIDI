// Copyright 1998-2018 Epic Games, Inc. All Rights Reserved.

#include "AkMIDIModule.h"

IMPLEMENT_MODULE(IAkMidiModule, AkMIDI)

FAkMidiModule* FAkMidiModule::AkMidiModuleIntance = nullptr;


void FAkMidiModule::StartupModule()
{
	// This code will execute after your module is loaded into memory; the exact timing is specified in the .uplugin file per-module
	if (AkMidiModuleIntance)
	{
		return;
	}

	AkMidiModuleIntance = this;
}

void FAkMidiModule::ShutdownModule()
{
	// This function may be called during shutdown to clean up your module.  For modules that support dynamic reloading,
	// we call this function before unloading the module.
	AkMidiModuleIntance = nullptr;
}

//FAkAudioDevice * FAkMidiModule::GetAkAudioDevice()
//{
//	return AkAudioDevice;
//}

