// Copyright 1998-2018 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"
#include "AkAudioDevice.h"

class IAkMidiModule : public IModuleInterface
{
public:

	static inline IAkMidiModule& Get()
	{
		return FModuleManager::LoadModuleChecked<IAkMidiModule>("AkMIDI");
	}

	static inline bool IsAvailable()
	{
		return FModuleManager::Get().IsModuleLoaded("AkMIDI");
	}
};

class FAkMidiModule : public IAkMidiModule
{
public:
	/** IModuleInterface implementation */
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

	//FAkAudioDevice * GetAkAudioDevice();

	static FAkMidiModule* AkMidiModuleIntance;
};

