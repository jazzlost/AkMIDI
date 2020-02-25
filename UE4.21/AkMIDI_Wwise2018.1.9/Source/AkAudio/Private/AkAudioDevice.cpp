// Copyright (c) 2006-2012 Audiokinetic Inc. / All Rights Reserved

/*=============================================================================
	AkAudioDevice.cpp: Audiokinetic Audio interface object.

	Unreal is RHS with Y and Z swapped (or technically LHS with flipped axis)

=============================================================================*/

/*------------------------------------------------------------------------------------
	Audio includes.
------------------------------------------------------------------------------------*/

#define AK_ENABLE_ROOMS
#define AK_ENABLE_PORTALS

#include "AkAudioDevice.h"
#include "AkUEFeatures.h"
#include "AkSettings.h"
#include "AkAudioModule.h"
#include "AkAudioBank.h"
#include "AkAudioEvent.h"
#include "AkAuxBus.h"
#include "AkSpotReflector.h"
#include "AkComponent.h"
#include "AkRoomComponent.h"
#include "AkAcousticPortal.h"
#include "EditorSupportDelegates.h"
#include "ISettingsModule.h"
#include "Interfaces/IPluginManager.h"
#include "Runtime/Launch/Resources/Version.h"
#include "AkComponentCallbackManager.h"
#include "FilePackageIO/AkFilePackageLowLevelIO.h"
#include "AkUnrealIOHookDeferred.h"
#include "AkLateReverbComponent.h"
#include "AkCallbackInfoPool.h"

#include "Async/TaskGraphInterfaces.h"
#include "Async/ParallelFor.h"
#include "Misc/ScopeLock.h"
#include "UObject/Object.h"
#include "UObject/UObjectIterator.h"
#include "UObject/UObjectGlobals.h"
#include "GameFramework/WorldSettings.h"
#include "GameFramework/PlayerController.h"
#include "Engine/GameEngine.h"
#include "Camera/PlayerCameraManager.h"
#include "Misc/App.h"
#include "EngineUtils.h"
#include "Model.h"
#include "Components/BrushComponent.h"
#include "HAL/FileManager.h"

#if WITH_EDITOR
#include "LevelEditorViewport.h"
#include "LevelEditor.h"
#include "CameraController.h"
#include "Editor.h"
#include "UnrealEdMisc.h"
#endif

#if PLATFORM_ANDROID && !PLATFORM_LUMIN
#include "Android/AndroidApplication.h"
#endif

// Register plugins that are static linked in this DLL.
#include <AK/Plugin/AkVorbisDecoderFactory.h>
#include <AK/Plugin/AkSilenceSourceFactory.h>
#include <AK/Plugin/AkSineSourceFactory.h>
#include <AK/Plugin/AkToneSourceFactory.h>
#include <AK/Plugin/AkPeakLimiterFXFactory.h>
#include <AK/Plugin/AkMatrixReverbFXFactory.h>
#include <AK/Plugin/AkParametricEQFXFactory.h>
#include <AK/Plugin/AkDelayFXFactory.h>
#include <AK/Plugin/AkExpanderFXFactory.h>
#include <AK/Plugin/AkFlangerFXFactory.h>
#include <AK/Plugin/AkCompressorFXFactory.h>
#include <AK/Plugin/AkGainFXFactory.h>
#include <AK/Plugin/AkHarmonizerFXFactory.h>
#include <AK/Plugin/AkTimeStretchFXFactory.h>
#include <AK/Plugin/AkPitchShifterFXFactory.h>
#include <AK/Plugin/AkStereoDelayFXFactory.h>
#include <AK/Plugin/AkMeterFXFactory.h>
#include <AK/Plugin/AkGuitarDistortionFXFactory.h>
#include <AK/Plugin/AkTremoloFXFactory.h>
#include <AK/Plugin/AkRoomVerbFXFactory.h>
#include <AK/Plugin/AkSynthOneSourceFactory.h>
#include <AK/Plugin/AkRecorderFXFactory.h>
#include <AK/Plugin/AkMotionGeneratorSourceFactory.h>

#if AK_WITH_AKMOTIONSINK
#include <AK/Plugin/AkMotionSinkFactory.h>
#endif

#if AK_WITH_AUROHEADPHONEFX
#include <AK/Plugin/AuroHeadphoneFXFactory.h>
#endif

#if AK_WITH_AKREFLECTFX
#include <AK/Plugin/AkReflectFXFactory.h>
#include <AK/Plugin/AkReflectGameData.h>
#endif

#if AK_WITH_AKCONVOLUTIONREVERBFX
#include <AK/Plugin/AkConvolutionReverbFXFactory.h>
#endif

#if PLATFORM_MAC || PLATFORM_IOS
#include <AK/Plugin/AkAACFactory.h>
#endif

#if PLATFORM_SWITCH
#include <AK/Plugin/AkOpusNXFactory.h>
#else
#include <AK/Plugin/AkOpusDecoderFactory.h>
#endif

#include <AK/SpatialAudio/Common/AkSpatialAudio.h>

// Add additional plug-ins here.

// OCULUS_START
#include "Runtime/HeadMountedDisplay/Public/IHeadMountedDisplayModule.h"
// OCULUS_END

#if PLATFORM_XBOXONE
#include <apu.h>
#endif

DEFINE_LOG_CATEGORY(LogAkAudio);

/*------------------------------------------------------------------------------------
	Statics and Globals
------------------------------------------------------------------------------------*/

bool FAkAudioDevice::m_bSoundEngineInitialized = false;
bool FAkAudioDevice::m_EngineExiting = false;

/*------------------------------------------------------------------------------------
	Defines
------------------------------------------------------------------------------------*/

#define INITBANKNAME (TEXT("Init"))
#define GAME_OBJECT_MAX_STRING_SIZE 512
#define AK_READ_SIZE DVD_MIN_READ_SIZE

/*------------------------------------------------------------------------------------
	Memory hooks
------------------------------------------------------------------------------------*/

namespace AK
{
void *AllocHook(size_t in_size)
{
	return FMemory::Malloc(in_size);
}
void FreeHook(void *in_ptr)
{
	FMemory::Free(in_ptr);
}

#ifdef _WIN32 // only on PC and XBox360
void *VirtualAllocHook(
	void *in_pMemAddress,
	size_t in_size,
	unsigned long in_dwAllocationType,
	unsigned long in_dwProtect)
{
	return VirtualAlloc(in_pMemAddress, in_size, in_dwAllocationType, in_dwProtect);
}
void VirtualFreeHook(
	void *in_pMemAddress,
	size_t in_size,
	unsigned long in_dwFreeType)
{
	VirtualFree(in_pMemAddress, in_size, in_dwFreeType);
}
#endif // only on PC and XBox360

#if PLATFORM_SWITCH
void *AlignedAllocHook(size_t in_size, size_t in_alignment)
{
	return aligned_alloc(in_alignment, in_size);
}

void AlignedFreeHook(void *in_ptr)
{
	free(in_ptr);
}
#endif

#if PLATFORM_XBOXONE
void *APUAllocHook(
	size_t in_size,			  ///< Number of bytes to allocate.
	unsigned int in_alignment ///< Alignment in bytes (must be power of two, greater than or equal to four).
)
{
	void *pReturn = nullptr;
	ApuAlloc(&pReturn, NULL, (UINT32)in_size, in_alignment);
	return pReturn;
}

void APUFreeHook(
	void *in_pMemAddress ///< Virtual address as returned by APUAllocHook.
)
{
	ApuFree(in_pMemAddress);
}
#endif
} // namespace AK

/*------------------------------------------------------------------------------------
	Helpers
------------------------------------------------------------------------------------*/

namespace FAkAudioDevice_Helpers
{
void RegisterGameObject(AkGameObjectID in_gameObjId, const FString &Name)
{
#ifdef AK_OPTIMIZED
	AK::SoundEngine::RegisterGameObj(in_gameObjId);
#else
	if (Name.Len() > 0)
	{
		AK::SoundEngine::RegisterGameObj(in_gameObjId, TCHAR_TO_ANSI(*Name));
	}
	else
	{
		AK::SoundEngine::RegisterGameObj(in_gameObjId);
	}
#endif
}

typedef TMap<AkGlobalCallbackLocation, FAkAudioDeviceDelegates::FOnAkGlobalCallback> FDelegateLocationMap;
FDelegateLocationMap DelegateLocationMap;

void GlobalCallback(AK::IAkGlobalPluginContext *Context, AkGlobalCallbackLocation Location, void *Cookie)
{
	const FAkAudioDeviceDelegates::FOnAkGlobalCallback *Delegate = DelegateLocationMap.Find(Location);
	if (Delegate && Delegate->IsBound())
	{
		Delegate->Broadcast(Context, Location);
	}
}

void UnregisterGlobalCallbackDelegate(FAkAudioDeviceDelegates::FOnAkGlobalCallback *Delegate, FDelegateHandle Handle, AkGlobalCallbackLocation Location)
{
	if (!Delegate)
		return;

	Delegate->Remove(Handle);
	if (Delegate->IsBound())
		return;

	AK::SoundEngine::UnregisterGlobalCallback(GlobalCallback, Location);
}

void UnregisterAllGlobalCallbacks()
{
	for (auto DelegateLocationPair : FAkAudioDevice_Helpers::DelegateLocationMap)
	{
		auto Location = DelegateLocationPair.Key;
		AK::SoundEngine::UnregisterGlobalCallback(GlobalCallback, Location);
	}
	FAkAudioDevice_Helpers::DelegateLocationMap.Empty();
}
} // namespace FAkAudioDevice_Helpers

//Wwise Midi Output Callback
void MidiCallback(AK::IAkGlobalPluginContext *in_pContext, AkGlobalCallbackLocation in_eLocation, void *in_pCookie)
{
	FAkAudioDevice *Device = FAkAudioDevice::Get();

	if (Device)
	{
		Device->OnMessageWaitToSend.ExecuteIfBound((AkAudioSettings *)in_pCookie);
		//UE_LOG(LogTemp, Warning, TEXT("WwiseCallback_Device"));
	}
}

/*------------------------------------------------------------------------------------
	Implementation
------------------------------------------------------------------------------------*/

FDelegateHandle FAkAudioDevice::RegisterGlobalCallback(FAkAudioDeviceDelegates::FOnAkGlobalCallback::FDelegate Callback, AkGlobalCallbackLocation Location)
{
	auto &Delegate = FAkAudioDevice_Helpers::DelegateLocationMap.FindOrAdd(Location);
	FDelegateHandle Handle = Delegate.Add(Callback);
	auto result = AK::SoundEngine::RegisterGlobalCallback(FAkAudioDevice_Helpers::GlobalCallback, Location);
	if (result != AK_Success)
	{
		FAkAudioDevice_Helpers::UnregisterGlobalCallbackDelegate(&Delegate, Handle, Location);
		Handle.Reset();
	}

	return Handle;
}

void FAkAudioDevice::UnregisterGlobalCallback(FDelegateHandle Handle, AkGlobalCallbackLocation Location)
{
	const auto &Delegate = FAkAudioDevice_Helpers::DelegateLocationMap.Find(Location);
	FAkAudioDevice_Helpers::UnregisterGlobalCallbackDelegate(Delegate, Handle, Location);
}

#if WITH_EDITORONLY_DATA
UAkComponent *FAkAudioDevice::CreateListener(UWorld *World, FEditorViewportClient *ViewportClient)
{
	if (!IsRunningGame())
	{
		FString ComponentName = TEXT("AkListener_") + World->GetName();
		if (ViewportClient)
		{
			ComponentName = TEXT("AkListener_") + FString::FromInt(ViewportClient->ViewIndex) + World->GetName();
		}
		UAkComponent *Listener = NewObject<UAkComponent>(World->GetWorldSettings(), FName(*ComponentName), RF_Transient);
		if (Listener != nullptr)
		{
			Listener->MarkAsEditorOnlySubobject();
			Listener->RegisterComponentWithWorld(World);
			AddDefaultListener(Listener);
		}

		return Listener;
	}
	else
	{
		return nullptr;
	}
}

FTransform FAkAudioDevice::GetEditorListenerPosition(int32 ViewIndex) const
{
	if (ViewIndex < ListenerTransforms.Num())
	{
		return ListenerTransforms[ViewIndex];
	}

	return FTransform();
}

#endif
/**
 * Initializes the audio device and creates sources.
 *
 * @return true if initialization was successful, false otherwise
 */
bool FAkAudioDevice::Init(void)
{
#if UE_SERVER
	return false;
#endif
	AkBankManager = NULL;
	if (!EnsureInitialized()) // ensure audiolib is initialized
	{
		UE_LOG(LogInit, Log, TEXT("Audiokinetic Audio Device initialization failed."));
		return false;
	}

	// Initialize SoundFrame
#ifdef AK_SOUNDFRAME
	m_pSoundFrame = NULL;
	if (AK::SoundFrame::Create(this, &m_pSoundFrame))
	{
		m_pSoundFrame->Connect();
	}
	FCoreDelegates::OnExit.AddLambda([this]() {
		m_pSoundFrame->Release();
		m_pSoundFrame = nullptr;
	});
#endif

	// OCULUS_START - vhamm - suspend audio when not in focus
	m_isSuspended = false;
	// OCULUS_END

	FWorldDelegates::OnPostWorldInitialization.AddLambda(
		[&](UWorld *World, const UWorld::InitializationValues IVS) {
			World->AddOnActorSpawnedHandler(FOnActorSpawned::FDelegate::CreateRaw(this, &FAkAudioDevice::OnActorSpawned));
		});

	FWorldDelegates::OnWorldCleanup.AddLambda(
		[this](UWorld *World, bool bSessionEnded, bool bCleanupResources) {
			CleanupComponentMapsForWorld(World);
		});

#if WITH_EDITOR
#if UE_4_19_OR_LATER
	FEditorSupportDelegates::PrepareToCleanseEditorObject.AddLambda(
		[this](UObject *Object) {
			auto Level = Cast<ULevel>(Object);
			if (Level != nullptr)
			{
				CleanupComponentMapsForLevel(Level);
			}
		});
#else
	FEditorSupportDelegates::CleanseEditor.AddLambda(
		[this]() {
			RemoveInvalidPrioritizedComponents<UAkRoomComponent>(HighestPriorityRoomComponentMap);
			RemoveInvalidPrioritizedComponents<UAkLateReverbComponent>(HighestPriorityLateReverbComponentMap);
		});
#endif // UE_4_19_OR_LATER
#endif // WITH_EDITOR

	m_SpatialAudioListener = nullptr;

#if WITH_EDITORONLY_DATA
	if (!IsRunningGame())
	{
		static const FName kLevelEditorModuleName = TEXT("LevelEditor");

		auto MapChangedHandler = [this](UWorld *World, EMapChangeType MapChangeType) {
			if (World && World->AllowAudioPlayback() && World->WorldType == EWorldType::Editor)
			{
				if (MapChangeType == EMapChangeType::TearDownWorld)
				{
					if (EditorListener && World == EditorListener->GetWorld())
					{
						EditorListener->DestroyComponent();
						EditorListener = nullptr;
					}
				}
				else if (EditorListener == nullptr && MapChangeType != EMapChangeType::SaveMap)
				{
					EditorListener = CreateListener(World);

					// The Editor Listener should NEVER be the spatial audio listener
					if (m_SpatialAudioListener == EditorListener)
					{
						AK::SpatialAudio::UnregisterListener(m_SpatialAudioListener->GetAkGameObjectID());
						m_SpatialAudioListener = nullptr;
					}
				}
			}
		};

		auto LevelEditorModulePtr = FModuleManager::Get().GetModulePtr<FLevelEditorModule>(kLevelEditorModuleName);
		if (LevelEditorModulePtr)
		{
			LevelEditorModulePtr->OnMapChanged().AddLambda(MapChangedHandler);
		}
		else
		{
			FModuleManager::Get().OnModulesChanged().AddLambda([this, MapChangedHandler](FName InModuleName, EModuleChangeReason Reason) {
				if (InModuleName == kLevelEditorModuleName && Reason == EModuleChangeReason::ModuleLoaded)
				{
					auto &LevelEditorModule = FModuleManager::GetModuleChecked<FLevelEditorModule>(kLevelEditorModuleName);
					LevelEditorModule.OnMapChanged().AddLambda(MapChangedHandler);
				}
			});
		}

		FEditorDelegates::OnEditorCameraMoved.AddLambda(
			[&](const FVector &Location, const FRotator &Rotation, ELevelViewportType ViewportType, int32 ViewIndex) {
#if UE_4_22_OR_LATER
				auto &allViewportClient = GEditor->GetAllViewportClients();
#else
				auto &allViewportClient = GEditor->AllViewportClients;
#endif
				if (allViewportClient[ViewIndex]->Viewport && allViewportClient[ViewIndex]->Viewport->HasFocus())
				{
					if (ListenerTransforms.Num() <= ViewIndex)
					{
						ListenerTransforms.AddDefaulted(ViewIndex - ListenerTransforms.Num() + 1);
					}
					ListenerTransforms[ViewIndex].SetLocation(Location);
					ListenerTransforms[ViewIndex].SetRotation(Rotation.Quaternion());

					UWorld *ViewportWorld = allViewportClient[ViewIndex]->GetWorld();
					if (ViewportWorld && ViewportWorld->WorldType == EWorldType::PIE)
					{
						auto Quat = Rotation.Quaternion();
						AkSoundPosition soundpos;
						FVectorsToAKTransform(
							Location,
							Quat.GetForwardVector(),
							Quat.GetUpVector(),
							soundpos);

						SetPosition(EditorListener, soundpos);
					}
				}
			});

		FEditorDelegates::BeginPIE.AddLambda(
			[&](const bool bIsSimulating) {
				if (!bIsSimulating)
				{
					RemoveDefaultListener(EditorListener);
				}
			});

		FEditorDelegates::EndPIE.AddLambda(
			[&](const bool bIsSimulating) {
				if (!bIsSimulating)
				{
					AddDefaultListener(EditorListener);
					// The Editor Listener should NEVER be the spatial audio listener
					if (m_SpatialAudioListener == EditorListener)
					{
						AK::SpatialAudio::UnregisterListener(m_SpatialAudioListener->GetAkGameObjectID());
						m_SpatialAudioListener = nullptr;
					}
				}
			});

		FEditorDelegates::OnSwitchBeginPIEAndSIE.AddLambda(
			[&](const bool bIsSimulating) {
				if (bIsSimulating)
				{
					AddDefaultListener(EditorListener);
					// The Editor Listener should NEVER be the spatial audio listener
					if (m_SpatialAudioListener == EditorListener)
					{
						AK::SpatialAudio::UnregisterListener(m_SpatialAudioListener->GetAkGameObjectID());
						m_SpatialAudioListener = nullptr;
					}
				}
				else
				{
					RemoveDefaultListener(EditorListener);
				}
			});
	}
#endif

	//Register MIDI Callback Interface For Syncing Render Process
	AkAudioSettings audioSettings;
	AK::SoundEngine::GetAudioSettings(audioSettings);
	AK::SoundEngine::RegisterGlobalCallback(MidiCallback, AkGlobalCallbackLocation_PreProcessMessageQueueForRender, &audioSettings);

	UE_LOG(LogInit, Log, TEXT("Audiokinetic Audio Device initialized."));

	return 1;
}

void FAkAudioDevice::CleanupComponentMapsForWorld(UWorld *World)
{
	HighestPriorityLateReverbComponentMap.Remove(World);
	HighestPriorityRoomComponentMap.Remove(World);
}

#if UE_4_19_OR_LATER
void FAkAudioDevice::CleanupComponentMapsForLevel(ULevel *Level)
{
	UE_LOG(LogTemp, Warning, TEXT("Level Cleanup"));
	RemovePrioritizedComponentsInLevel<UAkRoomComponent>(HighestPriorityRoomComponentMap, Level);
	RemovePrioritizedComponentsInLevel<UAkLateReverbComponent>(HighestPriorityLateReverbComponentMap, Level);
}

template <class COMPONENT_TYPE>
void FAkAudioDevice::RemovePrioritizedComponentsInLevel(TMap<UWorld *, COMPONENT_TYPE *> &HighestPriorityComponentMap, ULevel *Level)
{
	auto World = Level->GetWorld();
	auto ComponentsToRemove = TArray<COMPONENT_TYPE *>();

	COMPONENT_TYPE **TopComponent = HighestPriorityComponentMap.Find(World);
	if (TopComponent)
	{
		COMPONENT_TYPE *CurrentComponent = *TopComponent;
		while (CurrentComponent)
		{
			auto Owner = CurrentComponent->GetOwner();
			if (Owner != nullptr && Owner->GetLevel() == Level)
				ComponentsToRemove.Add(CurrentComponent);
			CurrentComponent = CurrentComponent->NextLowerPriorityComponent;
		}
	}

	for (auto Component : ComponentsToRemove)
		RemovePrioritizedComponentFromList(Component, HighestPriorityComponentMap);
}

#else
template <class COMPONENT_TYPE>
void FAkAudioDevice::RemoveInvalidPrioritizedComponents(TMap<UWorld *, COMPONENT_TYPE *> &HighestPriorityComponentMap)
{
	auto ComponentsToRemove = TArray<COMPONENT_TYPE *>();
	for (auto WorldComponentPair : HighestPriorityComponentMap)
	{
		auto Component = WorldComponentPair.Value;
		if (Component->IsPendingKill())
		{
			ComponentsToRemove.Add(Component);
		}
	}
	for (auto Component : ComponentsToRemove)
		RemovePrioritizedComponentFromList(Component, HighestPriorityComponentMap);
}
#endif // UE_4_19_OR_LATER

/**
 * Update the audio device and calculates the cached inverse transform later
 * on used for spatialization.
 */
bool FAkAudioDevice::Update(float DeltaTime)
{
	if (m_bSoundEngineInitialized)
	{
		// OCULUS_START - vhamm - suspend audio when not in focus
		if (FApp::UseVRFocus())
		{
			if (FApp::HasVRFocus())
			{
				if (m_isSuspended)
				{
					AK::SoundEngine::WakeupFromSuspend();
					m_isSuspended = false;
				}
			}
			else
			{
				if (!m_isSuspended)
				{
					AK::SoundEngine::Suspend(true);
					m_isSuspended = true;
				}
			}
		}
		// OCULUS_END

		AK::SoundEngine::RenderAudio();
	}

	return true;
}

/**
 * Tears down audio device by stopping all sounds, removing all buffers, 
 * destroying all sources, ... Called by both Destroy and ShutdownAfterError
 * to perform the actual tear down.
 */
void FAkAudioDevice::Teardown()
{
	if (m_bSoundEngineInitialized == true)
	{
		// Unload all loaded banks before teardown
		if (AkBankManager)
		{
			const TSet<UAkAudioBank *> *LoadedBanks = AkBankManager->GetLoadedBankList();
			TSet<UAkAudioBank *> LoadedBanksCopy(*LoadedBanks);
			for (TSet<UAkAudioBank *>::TConstIterator LoadIter(LoadedBanksCopy); LoadIter; ++LoadIter)
			{
				if ((*LoadIter) != NULL && (*LoadIter)->IsValidLowLevel())
				{
					(*LoadIter)->Unload();
				}
			}
			delete AkBankManager;
			AkBankManager = nullptr;
		}

		if (CallbackInfoPool)
		{
			CallbackInfoPool->Teardown();

			delete CallbackInfoPool;
			CallbackInfoPool = nullptr;
		}

		m_EngineExiting = true;

		UnloadAllFilePackages();

		AK::Monitor::SetLocalOutput(0, NULL);

#ifndef AK_OPTIMIZED
#if !PLATFORM_LINUX
		//
		// Terminate Communication Services
		//
		AK::Comm::Term();
#endif
#endif // AK_OPTIMIZED

		AK::SoundEngine::UnregisterGameObj(DUMMY_GAMEOBJ);

		//
		// Terminate the music engine
		//
		AK::MusicEngine::Term();

		//
		// Unregister game objects. Since we're about to terminate the sound engine
		// anyway, we don't really have to unregister those game objects here. But
		// in general it is good practice to unregister game objects as soon as they
		// become obsolete, to free up resources.
		//
		if (AK::SoundEngine::IsInitialized())
		{
			//
			// Terminate the sound engine
			//
			AK::SoundEngine::Term();
			FAkAudioDevice_Helpers::UnregisterAllGlobalCallbacks();
		}

		if (CallbackManager)
		{
			delete CallbackManager;
			CallbackManager = nullptr;
		}

		if (LowLevelIOHook)
		{
			LowLevelIOHook->Term();
			delete LowLevelIOHook;
			LowLevelIOHook = nullptr;
		}

		// Terminate the streaming manager
		if (AK::IAkStreamMgr::Get())
		{
			AK::IAkStreamMgr::Get()->Destroy();
		}

		// Terminate the Memory Manager
		AK::MemoryMgr::Term();

		m_bSoundEngineInitialized = false;
	}

	// Terminate SoundFrame
#ifdef AK_SOUNDFRAME
	if (m_pSoundFrame)
	{
		m_pSoundFrame->Release();
		m_pSoundFrame = NULL;
	}
#endif

	FWorldDelegates::LevelRemovedFromWorld.RemoveAll(this);

	UE_LOG(LogInit, Log, TEXT("Audiokinetic Audio Device terminated."));
}

/**
 * Stops all game sounds (and possibly UI) sounds
 *
 * @param bShouldStopUISounds If true, this function will stop UI sounds as well
 */
void FAkAudioDevice::StopAllSounds(bool bShouldStopUISounds)
{
	AK::SoundEngine::StopAll(DUMMY_GAMEOBJ);
	AK::SoundEngine::StopAll();
}

/**
 * Stop all audio associated with a scene
 *
 * @param SceneToFlush		Interface of the scene to flush
 */
void FAkAudioDevice::Flush(UWorld *WorldToFlush)
{
	AK::SoundEngine::StopAll(DUMMY_GAMEOBJ);
	AK::SoundEngine::StopAll();
}

/**
 * Clears all loaded soundbanks
 *
 * @return Result from ak sound engine 
 */
AKRESULT FAkAudioDevice::ClearBanks()
{
	if (m_bSoundEngineInitialized)
	{
		AKRESULT eResult = AK::SoundEngine::ClearBanks();
		if (eResult == AK_Success && AkBankManager != NULL)
		{
			FScopeLock Lock(&AkBankManager->m_BankManagerCriticalSection);
			AkBankManager->ClearLoadedBanks();
		}

		return eResult;
	}
	else
	{
		return AK_Success;
	}
}

/**
 * Load a soundbank
 *
 * @param in_Bank		The bank to load
 * @param in_memPoolId		Memory pool ID (media is stored in the sound engine's default pool if AK_DEFAULT_POOL_ID is passed)
 * @param out_bankID		Returned bank ID
 * @return Result from ak sound engine 
 */
AKRESULT FAkAudioDevice::LoadBank(
	class UAkAudioBank *in_Bank,
	AkMemPoolId in_memPoolId,
	AkBankID &out_bankID)
{
	AKRESULT eResult = LoadBank(in_Bank->GetName(), in_memPoolId, out_bankID);
	if (eResult == AK_Success && AkBankManager != NULL)
	{
		FScopeLock Lock(&AkBankManager->m_BankManagerCriticalSection);
		AkBankManager->AddLoadedBank(in_Bank);
	}
	return eResult;
}

/**
 * Load a soundbank by name
 *
 * @param in_BankName		The name of the bank to load
 * @param in_memPoolId		Memory pool ID (media is stored in the sound engine's default pool if AK_DEFAULT_POOL_ID is passed)
 * @param out_bankID		Returned bank ID
 * @return Result from ak sound engine 
 */
AKRESULT FAkAudioDevice::LoadBank(
	const FString &in_BankName,
	AkMemPoolId in_memPoolId,
	AkBankID &out_bankID)
{
	AKRESULT eResult = AK_Fail;
	if (EnsureInitialized()) // ensure audiolib is initialized
	{
		eResult = AK::SoundEngine::LoadBank(TCHAR_TO_AK(*in_BankName), in_memPoolId, out_bankID);
	}
	return eResult;
}

/**
 * Load a soundbank asynchronously
 *
 * @param in_Bank		The bank to load
 * @param in_pfnBankCallback Callback function
 * @param in_pCookie		Callback cookie (reserved to user, passed to the callback function)
 * @param in_memPoolId		Memory pool ID (media is stored in the sound engine's default pool if AK_DEFAULT_POOL_ID is passed)
 * @param out_bankID		Returned bank ID
 * @return Result from ak sound engine 
 */
AKRESULT FAkAudioDevice::LoadBank(
	class UAkAudioBank *in_Bank,
	AkBankCallbackFunc in_pfnBankCallback,
	void *in_pCookie,
	AkMemPoolId in_memPoolId,
	AkBankID &out_bankID)
{
	if (EnsureInitialized() && in_Bank) // ensure audiolib is initialized
	{
		if (AkBankManager != NULL)
		{
			IAkBankCallbackInfo *cbInfo = new FAkBankFunctionPtrCallbackInfo(in_pfnBankCallback, in_Bank, in_pCookie);

			// Need to hijack the callback, so we can add the bank to the loaded banks list when successful.
			if (cbInfo)
			{
				return AK::SoundEngine::LoadBank(TCHAR_TO_AK(*(in_Bank->GetName())), FAkBankManager::BankLoadCallback, cbInfo, in_memPoolId, out_bankID);
			}
		}
		else
		{
			return AK::SoundEngine::LoadBank(TCHAR_TO_AK(*(in_Bank->GetName())), in_pfnBankCallback, in_pCookie, in_memPoolId, out_bankID);
		}
	}
	return AK_Fail;
}

AKRESULT FAkAudioDevice::LoadBank(
	class UAkAudioBank *in_Bank,
	FWaitEndBankAction *LoadBankLatentAction)
{
	if (EnsureInitialized() && AkBankManager != NULL && in_Bank) // ensure audiolib is initialized
	{
		IAkBankCallbackInfo *cbInfo = new FAkBankLatentActionCallbackInfo(in_Bank, LoadBankLatentAction);

		// Need to hijack the callback, so we can add the bank to the loaded banks list when successful.
		if (cbInfo)
		{
			AkBankID BankId;
			return AK::SoundEngine::LoadBank(TCHAR_TO_AK(*(in_Bank->GetName())), FAkBankManager::BankLoadCallback, cbInfo, AK_DEFAULT_POOL_ID, BankId);
		}
	}
	return AK_Fail;
}

AKRESULT FAkAudioDevice::LoadBankAsync(
	class UAkAudioBank *in_Bank,
	const FOnAkBankCallback &BankLoadedCallback,
	AkMemPoolId in_memPoolId,
	AkBankID &out_bankID)
{
	if (EnsureInitialized() && AkBankManager != NULL && in_Bank) // ensure audiolib is initialized
	{
		IAkBankCallbackInfo *cbInfo = new FAkBankBlueprintDelegateCallbackInfo(in_Bank, BankLoadedCallback);

		// Need to hijack the callback, so we can add the bank to the loaded banks list when successful.
		if (cbInfo)
		{
			return AK::SoundEngine::LoadBank(TCHAR_TO_AK(*(in_Bank->GetName())), FAkBankManager::BankLoadCallback, cbInfo, in_memPoolId, out_bankID);
		}
	}
	return AK_Fail;
}

/**
 * Unload a soundbank
 *
 * @param in_Bank		The bank to unload
 * @param out_pMemPoolId	Returned memory pool ID used with LoadBank() (can pass NULL)
 * @return Result from ak sound engine 
 */
AKRESULT FAkAudioDevice::UnloadBank(
	class UAkAudioBank *in_Bank,
	AkMemPoolId *out_pMemPoolId ///< Returned memory pool ID used with LoadBank() (can pass NULL)
)
{
	if (!in_Bank)
		return AK_Fail;

	AKRESULT eResult = UnloadBank(in_Bank->GetName(), out_pMemPoolId);
	if (eResult == AK_Success && AkBankManager != NULL)
	{
		FScopeLock Lock(&AkBankManager->m_BankManagerCriticalSection);
		AkBankManager->RemoveLoadedBank(in_Bank);
	}
	return eResult;
}

/**
 * Unload a soundbank by its name
 *
 * @param in_BankName		The name of the bank to unload
 * @param out_pMemPoolId	Returned memory pool ID used with LoadBank() (can pass NULL)
 * @return Result from ak sound engine 
 */
AKRESULT FAkAudioDevice::UnloadBank(
	const FString &in_BankName,
	AkMemPoolId *out_pMemPoolId ///< Returned memory pool ID used with LoadBank() (can pass NULL)
)
{
	AKRESULT eResult = AK_Fail;
	if (m_bSoundEngineInitialized)
	{
		eResult = AK::SoundEngine::UnloadBank(TCHAR_TO_AK(*in_BankName), nullptr, out_pMemPoolId);
	}
	return eResult;
}

/**
 * Unload a soundbank asynchronously
 *
 * @param in_Bank		The bank to unload
 * @param in_pfnBankCallback Callback function
 * @param in_pCookie		Callback cookie (reserved to user, passed to the callback function)
 * @return Result from ak sound engine 
 */
AKRESULT FAkAudioDevice::UnloadBank(
	class UAkAudioBank *in_Bank,
	AkBankCallbackFunc in_pfnBankCallback,
	void *in_pCookie)
{
	if (m_bSoundEngineInitialized && in_Bank)
	{
		if (AkBankManager != NULL)
		{
			IAkBankCallbackInfo *cbInfo = new FAkBankFunctionPtrCallbackInfo(in_pfnBankCallback, in_Bank, in_pCookie);

			if (cbInfo)
			{
				return AK::SoundEngine::UnloadBank(TCHAR_TO_AK(*(in_Bank->GetName())), NULL, FAkBankManager::BankUnloadCallback, cbInfo);
			}
		}
		else
		{
			return AK::SoundEngine::UnloadBank(TCHAR_TO_AK(*(in_Bank->GetName())), NULL, in_pfnBankCallback, in_pCookie);
		}
	}
	return AK_Fail;
}

AKRESULT FAkAudioDevice::UnloadBank(
	class UAkAudioBank *in_Bank,
	FWaitEndBankAction *UnloadBankLatentAction)
{
	if (m_bSoundEngineInitialized && AkBankManager != NULL && in_Bank)
	{
		IAkBankCallbackInfo *cbInfo = new FAkBankLatentActionCallbackInfo(in_Bank, UnloadBankLatentAction);

		if (cbInfo)
		{
			return AK::SoundEngine::UnloadBank(TCHAR_TO_AK(*(in_Bank->GetName())), NULL, FAkBankManager::BankUnloadCallback, cbInfo);
		}
	}
	return AK_Fail;
}

AKRESULT FAkAudioDevice::UnloadBankAsync(
	class UAkAudioBank *in_Bank,
	const FOnAkBankCallback &BankUnloadedCallback)
{
	if (m_bSoundEngineInitialized && AkBankManager != NULL && in_Bank)
	{
		IAkBankCallbackInfo *cbInfo = new FAkBankBlueprintDelegateCallbackInfo(in_Bank, BankUnloadedCallback);

		if (cbInfo)
		{
			return AK::SoundEngine::UnloadBank(TCHAR_TO_AK(*(in_Bank->GetName())), NULL, FAkBankManager::BankUnloadCallback, cbInfo);
		}
	}
	return AK_Fail;
}

/**
 * Load the audiokinetic 'init' bank
 *
 * @return Result from ak sound engine 
 */
AKRESULT FAkAudioDevice::LoadInitBank(void)
{
	AkBankID BankID;
	return AK::SoundEngine::LoadBank(TCHAR_TO_AK(INITBANKNAME), AK_DEFAULT_POOL_ID, BankID);
}

bool FAkAudioDevice::LoadAllFilePackages()
{
	TArray<FString> FoundPackages;
	FString BaseBankPath = FAkAudioDevice::Get()->GetBasePath();
	bool eResult = true;
	IFileManager::Get().FindFilesRecursive(FoundPackages, *BaseBankPath, TEXT("*.pck"), true, false);
	for (FString Package : FoundPackages)
	{
		AkUInt32 PackageID;
		FString PackageName = FPaths::GetCleanFilename(Package);

		AKRESULT eTempResult = LowLevelIOHook->LoadFilePackage(TCHAR_TO_AK(*PackageName), PackageID);
		if (eTempResult != AK_Success)
		{
			UE_LOG(LogAkAudio, Error, TEXT("Failed to load file package %s"), *PackageName);
			eResult = false;
		}
	}

	return eResult;
}

bool FAkAudioDevice::UnloadAllFilePackages()
{
	return LowLevelIOHook->UnloadAllFilePackages() == AK_Success;
}

/**
 * Unload the audiokinetic 'init' bank
 *
 * @return Result from ak sound engine 
 */
AKRESULT FAkAudioDevice::UnloadInitBank(void)
{
	return AK::SoundEngine::UnloadBank(TCHAR_TO_AK(INITBANKNAME), NULL);
}

/**
 * Load all banks currently being referenced
 */
void FAkAudioDevice::LoadAllReferencedBanks()
{
	LoadAllFilePackages();
	LoadInitBank();

	// Load any banks that are in memory that haven't been loaded yet
	for (TObjectIterator<UAkAudioBank> It; It; ++It)
	{
		if ((*It)->AutoLoad)
			(*It)->Load();
	}

	OnSoundbanksLoaded.Broadcast();
}

/**
 * Reload all banks currently being referenced
 */
void FAkAudioDevice::ReloadAllReferencedBanks()
{
	if (m_bSoundEngineInitialized)
	{
		StopAllSounds();
		AK::SoundEngine::RenderAudio();
		FPlatformProcess::Sleep(0.1f);
		ClearBanks();
		UnloadAllFilePackages();
		LoadAllReferencedBanks();
	}
}

AkUniqueID FAkAudioDevice::GetIDFromString(const FString &in_string)
{
	if (in_string.IsEmpty())
	{
		return AK_INVALID_UNIQUE_ID;
	}
	else
	{
		return AK::SoundEngine::GetIDFromString(TCHAR_TO_ANSI(*in_string));
	}
}

template <typename FCreateCallbackPackage>
AkPlayingID FAkAudioDevice::PostEvent(
	const FString &in_EventName,
	const AkGameObjectID in_gameObjectID,
	FCreateCallbackPackage CreateCallbackPackage)
{
	AkPlayingID playingID = AK_INVALID_PLAYING_ID;

	if (m_bSoundEngineInitialized && CallbackManager)
	{
		auto pPackage = CreateCallbackPackage(in_gameObjectID);
		if (pPackage)
		{
			playingID = AK::SoundEngine::PostEvent(TCHAR_TO_AK(*in_EventName), in_gameObjectID, pPackage->uUserFlags | AK_EndOfEvent, &FAkComponentCallbackManager::AkComponentCallback, pPackage);
			if (playingID == AK_INVALID_PLAYING_ID)
			{
				CallbackManager->RemoveCallbackPackage(pPackage, in_gameObjectID);
			}
		}
	}

	return playingID;
}

template <typename FCreateCallbackPackage>
AkPlayingID FAkAudioDevice::PostEvent(
	const FString &in_EventName,
	UAkComponent *in_pComponent,
	FCreateCallbackPackage CreateCallbackPackage)
{
	AkPlayingID playingID = AK_INVALID_PLAYING_ID;

	if (m_bSoundEngineInitialized && in_pComponent && CallbackManager)
	{
		if (in_pComponent->VerifyEventName(in_EventName) && in_pComponent->AllowAudioPlayback())
		{
			in_pComponent->UpdateOcclusionObstruction();

			auto gameObjID = in_pComponent->GetAkGameObjectID();

			return PostEvent(in_EventName, gameObjID, CreateCallbackPackage);
		}
	}

	return playingID;
}

/**
 * Post an event to ak soundengine
 *
 * @param in_pEvent			Event to post
 * @param in_pComponent		AkComponent on which to play the event
 * @param in_uFlags			Bitmask: see \ref AkCallbackType
 * @param in_pfnCallback	Callback function
 * @param in_pCookie		Callback cookie that will be sent to the callback function along with additional information.
 * @param in_bStopWhenOwnerDestroyed If true, then the sound should be stopped if the owning actor is destroyed
 * @return ID assigned by ak soundengine
 */
AkPlayingID FAkAudioDevice::PostEvent(
	UAkAudioEvent *in_pEvent,
	AActor *in_pActor,
	AkUInt32 in_uFlags /*= 0*/,
	AkCallbackFunc in_pfnCallback /*= NULL*/,
	void *in_pCookie /*= NULL*/,
	bool in_bStopWhenOwnerDestroyed /*= false*/
)
{
	if (!in_pEvent)
		return AK_INVALID_PLAYING_ID;

	return PostEvent(in_pEvent->GetName(), in_pActor, in_uFlags, in_pfnCallback, in_pCookie, in_bStopWhenOwnerDestroyed);
}

/**
 * Post an event to ak soundengine by name
 *
 * @param in_EventName		Name of the event to post
 * @param in_pComponent		AkComponent on which to play the event
 * @param in_uFlags			Bitmask: see \ref AkCallbackType
 * @param in_pfnCallback	Callback function
 * @param in_pCookie		Callback cookie that will be sent to the callback function along with additional information.
 * @param in_bStopWhenOwnerDestroyed If true, then the sound should be stopped if the owning actor is destroyed
 * @return ID assigned by ak soundengine
 */
AkPlayingID FAkAudioDevice::PostEvent(
	const FString &in_EventName,
	AActor *in_pActor,
	AkUInt32 in_uFlags /*= 0*/,
	AkCallbackFunc in_pfnCallback /*= NULL*/,
	void *in_pCookie /*= NULL*/,
	bool in_bStopWhenOwnerDestroyed /*= false*/
)
{
	if (m_bSoundEngineInitialized)
	{
		if (!in_pActor)
		{
			return PostEvent(in_EventName, DUMMY_GAMEOBJ, [in_pfnCallback, in_pCookie, in_uFlags, this](AkGameObjectID gameObjID) {
				return CallbackManager->CreateCallbackPackage(in_pfnCallback, in_pCookie, in_uFlags, gameObjID);
			});
		}
		else if (!in_pActor->IsActorBeingDestroyed() && !in_pActor->IsPendingKill())
		{
			UAkComponent *pComponent = GetAkComponent(in_pActor->GetRootComponent(), FName(), NULL, EAttachLocation::KeepRelativeOffset);
			if (pComponent)
			{
				pComponent->StopWhenOwnerDestroyed = in_bStopWhenOwnerDestroyed;
				return PostEvent(in_EventName, pComponent, in_uFlags, in_pfnCallback, in_pCookie);
			}
		}
	}

	return AK_INVALID_PLAYING_ID;
}

AkPlayingID FAkAudioDevice::PostEvent(
	const FString &in_EventName,
	AActor *in_pActor,
	const FOnAkPostEventCallback &PostEventCallback,
	AkUInt32 in_uFlags /*= 0*/,
	bool in_bStopWhenOwnerDestroyed /*= false*/
)
{
	if (m_bSoundEngineInitialized)
	{
		if (!in_pActor)
		{
			UE_LOG(LogAkAudio, Error, TEXT("PostEvent accepting a FOnAkPostEventCallback delegate requires a valid actor"));
		}
		else if (!in_pActor->IsActorBeingDestroyed() && !in_pActor->IsPendingKill())
		{
			UAkComponent *pComponent = GetAkComponent(in_pActor->GetRootComponent(), FName(), NULL, EAttachLocation::KeepRelativeOffset);
			if (pComponent)
			{
				pComponent->StopWhenOwnerDestroyed = in_bStopWhenOwnerDestroyed;
				return PostEvent(in_EventName, pComponent, PostEventCallback, in_uFlags);
			}
		}
	}

	return AK_INVALID_PLAYING_ID;
}

AkPlayingID FAkAudioDevice::PostEventLatentAction(
	const FString &EventName,
	AActor *Actor,
	bool bStopWhenOwnerDestroyed,
	FWaitEndOfEventAction *LatentAction)
{
	if (m_bSoundEngineInitialized)
	{
		if (!Actor)
		{
			UE_LOG(LogAkAudio, Error, TEXT("PostEvent accepting a FWaitEndOfEventAction requires a valid actor"));
		}
		else if (!Actor->IsActorBeingDestroyed() && !Actor->IsPendingKill())
		{
			UAkComponent *pComponent = GetAkComponent(Actor->GetRootComponent(), FName(), NULL, EAttachLocation::KeepRelativeOffset);
			if (pComponent)
			{
				pComponent->StopWhenOwnerDestroyed = bStopWhenOwnerDestroyed;
				return PostEventLatentAction(EventName, pComponent, LatentAction);
			}
		}
	}

	return AK_INVALID_PLAYING_ID;
}

/**
 * Post an event to ak soundengine by name
 *
 * @param in_EventName		Name of the event to post
 * @param in_pComponent		AkComponent on which to play the event
 * @param in_uFlags			Bitmask: see \ref AkCallbackType
 * @param in_pfnCallback	Callback function
 * @param in_pCookie		Callback cookie that will be sent to the callback function along with additional information.
 * @return ID assigned by ak soundengine
 */
AkPlayingID FAkAudioDevice::PostEvent(
	const FString &in_EventName,
	UAkComponent *in_pComponent,
	AkUInt32 in_uFlags /*= 0*/,
	AkCallbackFunc in_pfnCallback /*= NULL*/,
	void *in_pCookie /*= NULL*/
)
{

	return PostEvent(in_EventName, in_pComponent, [in_pfnCallback, in_pCookie, in_uFlags, this](AkGameObjectID gameObjID) {
		return CallbackManager->CreateCallbackPackage(in_pfnCallback, in_pCookie, in_uFlags, gameObjID);
	});
}

AkPlayingID FAkAudioDevice::PostEvent(
	const FString &in_EventName,
	UAkComponent *in_pComponent,
	const FOnAkPostEventCallback &PostEventCallback,
	AkUInt32 in_uFlags /*= 0*/
)
{
	return PostEvent(in_EventName, in_pComponent, [PostEventCallback, in_uFlags, this](AkGameObjectID gameObjID) {
		return CallbackManager->CreateCallbackPackage(PostEventCallback, in_uFlags, gameObjID);
	});
}

AkPlayingID FAkAudioDevice::PostEventLatentAction(
	const FString &in_EventName,
	UAkComponent *in_pComponent,
	FWaitEndOfEventAction *LatentAction)
{
	return PostEvent(in_EventName, in_pComponent, [LatentAction, this](AkGameObjectID gameObjID) {
		return CallbackManager->CreateCallbackPackage(LatentAction, gameObjID);
	});
}

/** Find UAkLateReverbComponents at a given location. */
TArray<class UAkLateReverbComponent *> FAkAudioDevice::FindLateReverbComponentsAtLocation(const FVector &Loc, const UWorld *in_World, int32 depth)
{
	return FindPrioritizedComponentsAtLocation(Loc, in_World, HighestPriorityLateReverbComponentMap, depth);
}

/** Add a UAkLateReverbComponent to the linked list. */
void FAkAudioDevice::AddLateReverbComponentToPrioritizedList(class UAkLateReverbComponent *in_ComponentToAdd)
{
	AddPrioritizedComponentInList(in_ComponentToAdd, HighestPriorityLateReverbComponentMap);
}

/** Remove a UAkLateReverbComponent from the linked list. */
void FAkAudioDevice::RemoveLateReverbComponentFromPrioritizedList(class UAkLateReverbComponent *in_ComponentToRemove)
{
	RemovePrioritizedComponentFromList(in_ComponentToRemove, HighestPriorityLateReverbComponentMap);
}

bool FAkAudioDevice::WorldHasActiveRooms(UWorld *in_World)
{
	UAkRoomComponent **TopComponent = HighestPriorityRoomComponentMap.Find(in_World);

	return TopComponent && *TopComponent;
}

/** Find UAkRoomComponents at a given location. */
TArray<class UAkRoomComponent *> FAkAudioDevice::FindRoomComponentsAtLocation(const FVector &Loc, const UWorld *in_World, int32 depth)
{
	return FindPrioritizedComponentsAtLocation(Loc, in_World, HighestPriorityRoomComponentMap, depth);
}

/** Add a UAkRoomComponent to the linked list. */
void FAkAudioDevice::AddRoomComponentToPrioritizedList(class UAkRoomComponent *in_ComponentToAdd)
{
	AddPrioritizedComponentInList(in_ComponentToAdd, HighestPriorityRoomComponentMap);
}

/** Remove a UAkRoomComponent from the linked list. */
void FAkAudioDevice::RemoveRoomComponentFromPrioritizedList(class UAkRoomComponent *in_ComponentToRemove)
{
	RemovePrioritizedComponentFromList(in_ComponentToRemove, HighestPriorityRoomComponentMap);
}

/** Return true if any UAkRoomComponents have been added to the prioritized list of rooms **/
bool FAkAudioDevice::UsingSpatialAudioRooms(const UWorld *in_World)
{
	return HighestPriorityRoomComponentMap.Find(in_World) != NULL;
}

AKRESULT FAkAudioDevice::ExecuteActionOnEvent(
	const FString &in_EventName,
	AkActionOnEventType in_ActionType,
	AActor *in_pActor,
	AkTimeMs in_uTransitionDuration,
	EAkCurveInterpolation in_eFadeCurve,
	AkPlayingID in_PlayingID)
{
	if (!in_pActor)
	{
		return AK::SoundEngine::ExecuteActionOnEvent(TCHAR_TO_AK(*in_EventName),
													 static_cast<AK::SoundEngine::AkActionOnEventType>(in_ActionType),
													 DUMMY_GAMEOBJ,
													 in_uTransitionDuration,
													 static_cast<AkCurveInterpolation>(in_eFadeCurve),
													 in_PlayingID);
	}
	else if (!in_pActor->IsActorBeingDestroyed() && !in_pActor->IsPendingKill())
	{
		UAkComponent *pComponent = GetAkComponent(in_pActor->GetRootComponent(), FName(), NULL, EAttachLocation::KeepRelativeOffset);
		if (pComponent)
		{
			return AK::SoundEngine::ExecuteActionOnEvent(TCHAR_TO_AK(*in_EventName),
														 static_cast<AK::SoundEngine::AkActionOnEventType>(in_ActionType),
														 pComponent->GetAkGameObjectID(),
														 in_uTransitionDuration,
														 static_cast<AkCurveInterpolation>(in_eFadeCurve),
														 in_PlayingID);
		}
	}

	return AKRESULT::AK_Fail;
}

/** Seek on an event in the ak soundengine.
* @param in_EventName            Name of the event on which to seek.
* @param in_pActor               The associated Actor. If this is nullptr, defaul object will be used.
* @param in_fPercent             Desired percent where playback should restart.
* @param in_bSeekToNearestMarker If true, the final seeking position will be made equal to the nearest marker.
*
* @return Success or failure.
*/
AKRESULT FAkAudioDevice::SeekOnEvent(
	const FString &in_EventName,
	AActor *in_pActor,
	AkReal32 in_fPercent,
	bool in_bSeekToNearestMarker /*= false*/,
	AkPlayingID InPlayingID /*= AK_INVALID_PLAYING_ID*/
)
{
	if (!in_pActor)
	{
		// SeekOnEvent must be bound to a game object. Passing DUMMY_GAMEOBJ as default game object.
		return AK::SoundEngine::SeekOnEvent(TCHAR_TO_AK(*in_EventName), DUMMY_GAMEOBJ, in_fPercent, in_bSeekToNearestMarker, InPlayingID);
	}
	else if (!in_pActor->IsActorBeingDestroyed() && !in_pActor->IsPendingKill())
	{
		UAkComponent *pComponent = GetAkComponent(in_pActor->GetRootComponent(), FName(), NULL, EAttachLocation::KeepRelativeOffset);
		if (pComponent)
		{
			return SeekOnEvent(in_EventName, pComponent, in_fPercent, in_bSeekToNearestMarker, InPlayingID);
		}
	}

	return AKRESULT::AK_Fail;
}

/** Seek on an event in the ak soundengine.
* @param in_EventName            Name of the event on which to seek.
* @param in_pComponent           The associated AkComponent.
* @param in_fPercent             Desired percent where playback should restart.
* @param in_bSeekToNearestMarker If true, the final seeking position will be made equal to the nearest marker.
*
* @return Success or failure.
*/
AKRESULT FAkAudioDevice::SeekOnEvent(
	const FString &in_EventName,
	UAkComponent *in_pComponent,
	AkReal32 in_fPercent,
	bool in_bSeekToNearestMarker /*= false*/,
	AkPlayingID InPlayingID /*= AK_INVALID_PLAYING_ID*/
)
{
	if (m_bSoundEngineInitialized && in_pComponent)
	{
		if (in_pComponent->VerifyEventName(in_EventName) && in_pComponent->AllowAudioPlayback())
		{
			return AK::SoundEngine::SeekOnEvent(TCHAR_TO_AK(*in_EventName), in_pComponent->GetAkGameObjectID(), in_fPercent, in_bSeekToNearestMarker, InPlayingID);
		}
	}
	return AKRESULT::AK_Fail;
}

/** Find Components that are prioritized (either UAkLateReverbComponent or UAkRoomComponent) at a given location

 *
 * @param							Loc	Location at which to find Reverb Volumes
 * @param FoundComponents		Array containing all found components at this location
 */
template <class COMPONENT_TYPE>
TArray<COMPONENT_TYPE *> FAkAudioDevice::FindPrioritizedComponentsAtLocation(const FVector &Loc, const UWorld *in_World, TMap<UWorld *, COMPONENT_TYPE *> &HighestPriorityComponentMap, int32 depth)
{
	TArray<COMPONENT_TYPE *> FoundComponents;

	COMPONENT_TYPE **TopComponent = HighestPriorityComponentMap.Find(in_World);
	if (TopComponent)
	{
		COMPONENT_TYPE *CurrentComponent = *TopComponent;
		while (CurrentComponent)
		{
			if (CurrentComponent->HasEffectOnLocation(Loc) && CurrentComponent->bEnable)
			{
				FoundComponents.Add(CurrentComponent);
				if (depth != FIND_COMPONENTS_DEPTH_INFINITE && FoundComponents.Num() == depth)
					break;
			}

			CurrentComponent = CurrentComponent->NextLowerPriorityComponent;
		}
	}

	return FoundComponents;
}

/** Add a Component that is prioritized (either UAkLateReverbComponent or UAkRoomComponent) in the active linked list. */
template <class COMPONENT_TYPE>
void FAkAudioDevice::AddPrioritizedComponentInList(COMPONENT_TYPE *in_ComponentToAdd, TMap<UWorld *, COMPONENT_TYPE *> &HighestPriorityComponentMap)
{
	UWorld *CurrentWorld = in_ComponentToAdd->GetWorld();
	COMPONENT_TYPE *&HighestPriorityComponent = HighestPriorityComponentMap.FindOrAdd(CurrentWorld);

	if (HighestPriorityComponent == NULL)
	{
		// First volume in the list. Set head.
		HighestPriorityComponent = in_ComponentToAdd;
		in_ComponentToAdd->NextLowerPriorityComponent = NULL;
	}
	else
	{
		COMPONENT_TYPE *CurrentComponent = HighestPriorityComponent;
		COMPONENT_TYPE *PreviousComponent = NULL;

		while (CurrentComponent && CurrentComponent != in_ComponentToAdd) // Don't add twice to the list!
		{
			if (in_ComponentToAdd->Priority > CurrentComponent->Priority)
			{
				// Found our spot in the list!
				if (PreviousComponent)
				{
					PreviousComponent->NextLowerPriorityComponent = in_ComponentToAdd;
				}
				else
				{
					// No previous, so we are at the top.
					HighestPriorityComponent = in_ComponentToAdd;
				}

				in_ComponentToAdd->NextLowerPriorityComponent = CurrentComponent;
				return;
			}

			// List traversal.
			PreviousComponent = CurrentComponent;
			CurrentComponent = CurrentComponent->NextLowerPriorityComponent;
		}

		// We're at the end!
		if (!CurrentComponent)
		{
			// Just to make sure...
			if (PreviousComponent)
			{
				PreviousComponent->NextLowerPriorityComponent = in_ComponentToAdd;
				in_ComponentToAdd->NextLowerPriorityComponent = NULL;
			}
		}
	}
}

/** Remove a Component that is prioritized (either UAkLateReverbComponent or UAkRoomComponent) from the linked list. */
template <class COMPONENT_TYPE>
void FAkAudioDevice::RemovePrioritizedComponentFromList(COMPONENT_TYPE *in_ComponentToRemove, TMap<UWorld *, COMPONENT_TYPE *> &HighestPriorityComponentMap)
{
	UWorld *CurrentWorld = in_ComponentToRemove->GetWorld();
	COMPONENT_TYPE **HighestPriorityComponent = HighestPriorityComponentMap.Find(CurrentWorld);

	if (HighestPriorityComponent)
	{
		COMPONENT_TYPE *CurrentComponent = *HighestPriorityComponent;
		COMPONENT_TYPE *PreviousComponent = NULL;
		while (CurrentComponent)
		{
			if (CurrentComponent == in_ComponentToRemove)
			{
				// Found our volume, remove it from the list
				if (PreviousComponent)
				{
					PreviousComponent->NextLowerPriorityComponent = CurrentComponent->NextLowerPriorityComponent;
				}
				else
				{
					// The one to remove was the highest, reset the head.
					*HighestPriorityComponent = CurrentComponent->NextLowerPriorityComponent;
				}

				break;
			}

			PreviousComponent = CurrentComponent;
			CurrentComponent = CurrentComponent->NextLowerPriorityComponent;
		}

		// Don't leave dangling pointers.
		in_ComponentToRemove->NextLowerPriorityComponent = NULL;

		if (*HighestPriorityComponent == NULL)
		{
			HighestPriorityComponentMap.Remove(CurrentWorld);
		}
	}
}

void FAkAudioDevice::UpdateAllSpatialAudioRooms(UWorld *InWorld)
{
	UAkRoomComponent **ppRoom = HighestPriorityRoomComponentMap.Find(InWorld);
#ifdef AK_ENABLE_PORTALS
	if (ppRoom)
	{
		for (UAkRoomComponent *pRoom = *ppRoom; pRoom != nullptr; pRoom = pRoom->NextLowerPriorityComponent)
		{
			pRoom->UpdateSpatialAudioRoom();
		}
	}
#endif
}

void FAkAudioDevice::UpdateAllSpatialAudioPortals(UWorld *InWorld)
{
#ifdef AK_ENABLE_PORTALS
	for (TActorIterator<AAkAcousticPortal> ActorItr(InWorld); ActorItr; ++ActorItr)
	{
		SetSpatialAudioPortal(*ActorItr);
	}
#endif
}

void FAkAudioDevice::SetSpatialAudioPortal(const AAkAcousticPortal *in_Portal)
{
	if (IsRunningCommandlet())
		return;

#ifdef AK_ENABLE_PORTALS
	AkPortalID portalID = in_Portal->GetPortalID();

	FString nameStr = in_Portal->GetFName().ToString();

	FVector location = in_Portal->GetActorLocation();

	FRotator rotation = in_Portal->GetActorRotation();
	FVector front = rotation.RotateVector(FVector(0.f, 1.f, 0.f));
	FVector up = rotation.RotateVector(FVector(0.f, 0.f, 1.f));

	AkVector Front, Up, Center;
	FVectorToAKVector(front, Front);
	FVectorToAKVector(up, Up);
	FVectorToAKVector(location, Center);

	AkPortalParams params;
	params.Transform.SetPosition(Center);
	params.Transform.SetOrientation(Front, Up);

	FVector scale = in_Portal->GetExtent();
	params.Extent.X = scale.X; // <- don't want to negate the X
	params.Extent.Y = scale.Z;
	params.Extent.Z = scale.Y;

	params.bEnabled = in_Portal->GetCurrentState() == AkAcousticPortalState::Open;
	params.strName = TCHAR_TO_ANSI(*nameStr); // This will copy the string inside operator=

	params.FrontRoom = in_Portal->GetFrontRoom();
	params.BackRoom = in_Portal->GetBackRoom();

	AK::SpatialAudio::SetPortal(portalID, params);
#endif
}

void FAkAudioDevice::RemoveSpatialAudioPortal(const AAkAcousticPortal *in_Portal)
{
	if (IsRunningCommandlet())
		return;

#ifdef AK_ENABLE_PORTALS
	AkPortalID portalID = in_Portal->GetPortalID();
	AK::SpatialAudio::RemovePortal(portalID);

	UpdateAllSpatialAudioRooms(in_Portal->GetWorld());
#endif
}

/** Get a sorted list of AkAuxSendValue at a location
 *
 * @param					Loc	Location at which to find Reverb Volumes
 * @param AkReverbVolumes	Array of AkAuxSendValue at this location
 */
void FAkAudioDevice::GetAuxSendValuesAtLocation(FVector Loc, TArray<AkAuxSendValue> &AkAuxSendValues, const UWorld *in_World)
{
	// Check if there are AkReverbVolumes at this location
	TArray<UAkLateReverbComponent *> FoundComponents = FindPrioritizedComponentsAtLocation(Loc, in_World, HighestPriorityLateReverbComponentMap);

	// Sort the found Volumes
	if (FoundComponents.Num() > 1)
	{
		FoundComponents.Sort([](const UAkLateReverbComponent &A, const UAkLateReverbComponent &B) {
			return A.Priority > B.Priority;
		});
	}

	// Apply the found Aux Sends
	AkAuxSendValue TmpSendValue;
	// Build a list to set as AuxBusses
	for (uint8 Idx = 0; Idx < FoundComponents.Num() && Idx < MaxAuxBus; Idx++)
	{
		TmpSendValue.listenerID = AK_INVALID_GAME_OBJECT;
		TmpSendValue.auxBusID = FoundComponents[Idx]->GetAuxBusId();
		TmpSendValue.fControlValue = FoundComponents[Idx]->SendLevel;
		AkAuxSendValues.Add(TmpSendValue);
	}
}

/**
 * Post an event and location to ak soundengine
 *
 * @param in_pEvent			Name of the event to post
 * @param in_Location		Location at which to play the event
 * @return ID assigned by ak soundengine
 */
AkPlayingID FAkAudioDevice::PostEventAtLocation(
	UAkAudioEvent *in_pEvent,
	FVector in_Location,
	FRotator in_Orientation,
	UWorld *in_World)
{
	AkPlayingID playingID = AK_INVALID_PLAYING_ID;

	if (in_pEvent)
	{
		playingID = PostEventAtLocation(in_pEvent->GetName(), in_Location, in_Orientation, in_World);
	}

	return playingID;
}

/**
 * Post an event by name at location to ak soundengine
 *
 * @param in_pEvent			Name of the event to post
 * @param in_Location		Location at which to play the event
 * @return ID assigned by ak soundengine
 */
AkPlayingID FAkAudioDevice::PostEventAtLocation(
	const FString &in_EventName,
	FVector in_Location,
	FRotator in_Orientation,
	UWorld *in_World)
{
	AkPlayingID playingID = AK_INVALID_PLAYING_ID;

	if (m_bSoundEngineInitialized)
	{
		const AkGameObjectID objId = (AkGameObjectID)&in_EventName;
		FAkAudioDevice_Helpers::RegisterGameObject(objId, in_EventName);

		{
			AkEmitterSettings settings;
			settings.useImageSources = false;
			settings.reflectAuxBusID = AK_INVALID_UNIQUE_ID;
			settings.name = TCHAR_TO_ANSI(*in_EventName); // This will copy the string inside operator=
			AK::SpatialAudio::RegisterEmitter(objId, settings);
		}

		TArray<AkAuxSendValue> AkReverbVolumes;
		GetAuxSendValuesAtLocation(in_Location, AkReverbVolumes, in_World);
		AK::SpatialAudio::SetEmitterAuxSendValues(objId, AkReverbVolumes.GetData(), AkReverbVolumes.Num());

		AkRoomID RoomID;
		TArray<UAkRoomComponent *> AkRooms = FindPrioritizedComponentsAtLocation(in_Location, in_World, HighestPriorityRoomComponentMap, 1);
		if (AkRooms.Num() > 0)
			RoomID = AkRooms[0]->GetRoomID();

		SetInSpatialAudioRoom(objId, RoomID);

		AkSoundPosition soundpos;
		FQuat tempQuat(in_Orientation);
		FVectorsToAKTransform(in_Location, tempQuat.GetForwardVector(), tempQuat.GetUpVector(), soundpos);

		AK::SpatialAudio::SetPosition(objId, soundpos);

		playingID = AK::SoundEngine::PostEvent(TCHAR_TO_AK(*in_EventName), objId);

		AK::SoundEngine::UnregisterGameObj(objId);
	}
	return playingID;
}

UAkComponent *FAkAudioDevice::SpawnAkComponentAtLocation(class UAkAudioEvent *in_pAkEvent, class UAkAuxBus *EarlyReflectionsBus, FVector Location, FRotator Orientation, bool AutoPost, const FString &EventName, const FString &EarlyReflectionsBusName, bool AutoDestroy, UWorld *in_World)
{
	UAkComponent *AkComponent = NULL;
	if (in_World)
	{
		AkComponent = NewObject<UAkComponent>(in_World->GetWorldSettings());
	}
	else
	{
		AkComponent = NewObject<UAkComponent>();
	}

	if (AkComponent)
	{
		AkComponent->AkAudioEvent = in_pAkEvent;
		AkComponent->EventName = EventName;
		AkComponent->SetWorldLocationAndRotation(Location, Orientation.Quaternion());
		if (in_World)
		{
			AkComponent->RegisterComponentWithWorld(in_World);
		}

		AkComponent->SetAutoDestroy(AutoDestroy);

		AkComponent->UseEarlyReflections(EarlyReflectionsBus, 1, 1.f, 100000.f, false, EarlyReflectionsBusName);

		if (AutoPost)
		{
			if (AkComponent->PostAssociatedAkEvent(0, FOnAkPostEventCallback()) == AK_INVALID_PLAYING_ID && AutoDestroy)
			{
				AkComponent->ConditionalBeginDestroy();
				AkComponent = NULL;
			}
		}
	}

	return AkComponent;
}

/**
 * Post a trigger to ak soundengine
 *
 * @param in_pszTrigger		Name of the trigger
 * @param in_pAkComponent	AkComponent on which to post the trigger
 * @return Result from ak sound engine
 */
AKRESULT FAkAudioDevice::PostTrigger(
	const TCHAR *in_pszTrigger,
	AActor *in_pActor)
{
	AkGameObjectID GameObjID = AK_INVALID_GAME_OBJECT;
	AKRESULT eResult = GetGameObjectID(in_pActor, GameObjID);
	if (m_bSoundEngineInitialized && eResult == AK_Success)
	{
		eResult = AK::SoundEngine::PostTrigger(TCHAR_TO_AK(in_pszTrigger), GameObjID);
	}
	return eResult;
}

/**
 * Set a RTPC in ak soundengine
 *
 * @param in_pszRtpcName	Name of the RTPC
 * @param in_value			Value to set
 * @param in_pActor			Actor on which to set the RTPC
 * @return Result from ak sound engine
 */
AKRESULT FAkAudioDevice::SetRTPCValue(
	const TCHAR *in_pszRtpcName,
	AkRtpcValue in_value,
	int32 in_interpolationTimeMs = 0,
	AActor *in_pActor = NULL)
{
	AKRESULT eResult = AK_Success;
	if (m_bSoundEngineInitialized)
	{
		AkGameObjectID GameObjID = AK_INVALID_GAME_OBJECT; // RTPC at global scope is supported
		if (in_pActor)
		{
			eResult = GetGameObjectID(in_pActor, GameObjID);
			if (eResult != AK_Success)
				return eResult;
		}

		eResult = AK::SoundEngine::SetRTPCValue(TCHAR_TO_AK(in_pszRtpcName), in_value, GameObjID, in_interpolationTimeMs);
	}
	return eResult;
}

/**
 * Set a state in ak soundengine
 *
 * @param in_pszStateGroup	Name of the state group
 * @param in_pszState		Name of the state
 * @return Result from ak sound engine
 */
AKRESULT FAkAudioDevice::SetState(
	const TCHAR *in_pszStateGroup,
	const TCHAR *in_pszState)
{
	AKRESULT eResult = AK_Success;
	if (m_bSoundEngineInitialized)
	{
		auto StateGroupID = AK::SoundEngine::GetIDFromString(TCHAR_TO_AK(in_pszStateGroup));
		auto StateID = AK::SoundEngine::GetIDFromString(TCHAR_TO_AK(in_pszState));
		eResult = AK::SoundEngine::SetState(StateGroupID, StateID);
	}
	return eResult;
}

/**
 * Set a switch in ak soundengine
 *
 * @param in_pszSwitchGroup	Name of the switch group
 * @param in_pszSwitchState	Name of the switch
 * @param in_pComponent		AkComponent on which to set the switch
 * @return Result from ak sound engine
 */
AKRESULT FAkAudioDevice::SetSwitch(
	const TCHAR *in_pszSwitchGroup,
	const TCHAR *in_pszSwitchState,
	AActor *in_pActor)
{
	AkGameObjectID GameObjID = DUMMY_GAMEOBJ;
	// Switches must be bound to a game object. passing DUMMY_GAMEOBJ as default game object.
	AKRESULT eResult = GetGameObjectID(in_pActor, GameObjID);
	if (m_bSoundEngineInitialized && eResult == AK_Success)
	{
		auto SwitchGroupID = AK::SoundEngine::GetIDFromString(TCHAR_TO_AK(in_pszSwitchGroup));
		auto SwitchStateID = AK::SoundEngine::GetIDFromString(TCHAR_TO_AK(in_pszSwitchState));
		eResult = AK::SoundEngine::SetSwitch(SwitchGroupID, SwitchStateID, GameObjID);
	}
	return eResult;
}

static AK::SoundEngine::MultiPositionType GetSoundEngineMultiPositionType(AkMultiPositionType in_eType)
{
	switch (in_eType)
	{
	case AkMultiPositionType::SingleSource:
		return AK::SoundEngine::MultiPositionType_SingleSource;
	case AkMultiPositionType::MultiSources:
		return AK::SoundEngine::MultiPositionType_MultiSources;
	case AkMultiPositionType::MultiDirections:
		return AK::SoundEngine::MultiPositionType_MultiDirections;
		// Unknown multi position type!
	default:
		AKASSERT(false);
		return AK::SoundEngine::MultiPositionType_SingleSource;
	}
}

/** Sets multiple positions to a single game object.
*  Setting multiple positions on a single game object is a way to simulate multiple emission sources while using the resources of only one voice.
*  This can be used to simulate wall openings, area sounds, or multiple objects emitting the same sound in the same area.
*  Note: Calling AK::SoundEngine::SetMultiplePositions() with only one position is the same as calling AK::SoundEngine::SetPosition()
*  @param in_pGameObjectAkComponent Game Object AkComponent.
*  @param in_pPositions Array of positions to apply.
*  @param in_eMultiPositionType Position type
*  @return AK_Success when successful, AK_InvalidParameter if parameters are not valid.
*
*/
AKRESULT FAkAudioDevice::SetMultiplePositions(
	UAkComponent *in_pGameObjectAkComponent,
	TArray<FTransform> in_aPositions,
	AkMultiPositionType in_eMultiPositionType /*= AkMultiPositionType::MultiDirections*/
)
{
	if (!in_pGameObjectAkComponent)
	{
		return AK_Fail;
	}

	if (in_pGameObjectAkComponent->bUseSpatialAudio)
	{
		UE_LOG(LogAkAudio, Error, TEXT("Multiple positions are not supported by Spatial Audio. Please set UseSpatialAudio to false on the %s AkComponent"), *(in_pGameObjectAkComponent->GetName()));
		return AK_Fail;
	}

	const int numPositions = in_aPositions.Num();
	TArray<AkSoundPosition> aPositions;
	aPositions.Empty();
	for (int i = 0; i < numPositions; ++i)
	{
		AkSoundPosition soundpos;
		FAkAudioDevice::FVectorsToAKTransform(in_aPositions[i].GetLocation(), in_aPositions[i].GetRotation().GetForwardVector(), in_aPositions[i].GetRotation().GetUpVector(), soundpos);
		aPositions.Add(soundpos);
	}
	return AK::SoundEngine::SetMultiplePositions(in_pGameObjectAkComponent->GetAkGameObjectID(), aPositions.GetData(),
												 aPositions.Num(), GetSoundEngineMultiPositionType(in_eMultiPositionType));
}

/** Sets multiple positions to a single game object, with flexible assignment of input channels.
*  Setting multiple positions on a single game object is a way to simulate multiple emission sources while using the resources of only one voice.
*  This can be used to simulate wall openings, area sounds, or multiple objects emitting the same sound in the same area.
*  Note: Calling AK::SoundEngine::SetMultiplePositions() with only one position is the same as calling AK::SoundEngine::SetPosition()
*  @param in_pGameObjectAkComponent Game Object AkComponent.
*  @param in_aChannelConfigurations Array of channel configurations for each position.
*  @param in_pPositions Array of positions to apply.
*  @param in_eMultiPositionType Position type
*  @return AK_Success when successful, AK_InvalidParameter if parameters are not valid.
*/
AKRESULT FAkAudioDevice::SetMultiplePositions(
	UAkComponent *in_pGameObjectAkComponent,
	TArray<AkChannelConfiguration> in_aChannelConfigurations,
	TArray<FTransform> in_aPositions,
	AkMultiPositionType in_eMultiPositionType /*= AkMultiPositionType::MultiDirections*/
)
{
	if (!in_pGameObjectAkComponent)
	{
		return AK_Fail;
	}

	if (in_pGameObjectAkComponent->bUseSpatialAudio)
	{
		UE_LOG(LogAkAudio, Error, TEXT("Multiple positions are not supported by Spatial Audio. Please set UseSpatialAudio to false on the %s AkComponent"), *(in_pGameObjectAkComponent->GetName()));
		return AK_Fail;
	}

	const int numPositions = in_aPositions.Num();
	TArray<AkChannelEmitter> emitters;
	emitters.Empty();
	for (int i = 0; i < numPositions; ++i)
	{
		AkSoundPosition soundpos;
		FAkAudioDevice::FVectorsToAKTransform(in_aPositions[i].GetLocation(), in_aPositions[i].GetRotation().GetForwardVector(), in_aPositions[i].GetRotation().GetUpVector(), soundpos);
		AkChannelConfig config;
		GetChannelConfig(in_aChannelConfigurations[i], config);
		emitters.Add(AkChannelEmitter());
		emitters[i].uInputChannels = config.uChannelMask;
		emitters[i].position = soundpos;
	}
	return AK::SoundEngine::SetMultiplePositions(in_pGameObjectAkComponent->GetAkGameObjectID(), emitters.GetData(),
												 emitters.Num(), GetSoundEngineMultiPositionType(in_eMultiPositionType));
}

/** Sets multiple positions to a single game object.
*  Setting multiple positions on a single game object is a way to simulate multiple emission sources while using the resources of only one voice.
*  This can be used to simulate wall openings, area sounds, or multiple objects emitting the same sound in the same area.
*  Note: Calling AK::SoundEngine::SetMultiplePositions() with only one position is the same as calling AK::SoundEngine::SetPosition()
*  @param in_GameObjectID Game Object identifier.
*  @param in_pPositions Array of positions to apply.
*  @param in_NumPositions Number of positions specified in the provided array.
*  @param in_eMultiPositionType Position type
*  @return AK_Success when successful, AK_InvalidParameter if parameters are not valid.
*
*/
AKRESULT FAkAudioDevice::SetMultiplePositions(
	AkGameObjectID in_GameObjectID,
	const AkSoundPosition *in_pPositions,
	AkUInt16 in_NumPositions,
	AK::SoundEngine::MultiPositionType in_eMultiPositionType /*= AK::SoundEngine::MultiDirections*/
)
{
	return AK::SoundEngine::SetMultiplePositions(in_GameObjectID, in_pPositions, in_NumPositions, in_eMultiPositionType);
}

/** Sets multiple positions to a single game object, with flexible assignment of input channels.
*  Setting multiple positions on a single game object is a way to simulate multiple emission sources while using the resources of only one voice.
*  This can be used to simulate wall openings, area sounds, or multiple objects emitting the same sound in the same area.
*  Note: Calling AK::SoundEngine::SetMultiplePositions() with only one position is the same as calling AK::SoundEngine::SetPosition()
*  @param in_GameObjectID Game Object identifier.
*  @param in_pPositions Array of positions to apply.
*  @param in_NumPositions Number of positions specified in the provided array.
*  @param in_eMultiPositionType Position type
*  @return AK_Success when successful, AK_InvalidParameter if parameters are not valid.
*/
AKRESULT FAkAudioDevice::SetMultiplePositions(
	AkGameObjectID in_GameObjectID,
	const AkChannelEmitter *in_pPositions,
	AkUInt16 in_NumPositions,
	AK::SoundEngine::MultiPositionType in_eMultiPositionType /*= AK::SoundEngine::MultiDirections*/
)
{
	return AK::SoundEngine::SetMultiplePositions(in_GameObjectID, in_pPositions, in_NumPositions, in_eMultiPositionType);
}

/**
 * Set auxiliary sends
 *
 * @param in_GameObjId		Wwise Game Object ID
 * @param in_AuxSendValues	Array of AkAuxSendValue, containins all Aux Sends to set on the game objectt
 * @return Result from ak sound engine
 */
AKRESULT FAkAudioDevice::SetAuxSends(
	const UAkComponent *in_akComponent,
	TArray<AkAuxSendValue> &in_AuxSendValues)
{
	AKRESULT eResult = AK_Success;
	if (m_bSoundEngineInitialized)
	{
		if (in_akComponent->bUseSpatialAudio)
			eResult = AK::SpatialAudio::SetEmitterAuxSendValues(in_akComponent->GetAkGameObjectID(), in_AuxSendValues.GetData(), in_AuxSendValues.Num());
		else
			eResult = AK::SoundEngine::SetGameObjectAuxSendValues(in_akComponent->GetAkGameObjectID(), in_AuxSendValues.GetData(), in_AuxSendValues.Num());
	}

	return eResult;
}

void FAkAudioDevice::GetChannelConfig(AkChannelConfiguration ChannelConfiguration, AkChannelConfig &config)
{
	switch (ChannelConfiguration)
	{
	case AkChannelConfiguration::Ak_LFE:
		config.SetStandard(AK_SPEAKER_SETUP_0POINT1);
		break;
	case AkChannelConfiguration::Ak_1_0:
		config.SetStandard(AK_SPEAKER_SETUP_MONO);
		break;
	case AkChannelConfiguration::Ak_2_0:
		config.SetStandard(AK_SPEAKER_SETUP_STEREO);
		break;
	case AkChannelConfiguration::Ak_2_1:
		config.SetStandard(AK_SPEAKER_SETUP_2POINT1);
		break;
	case AkChannelConfiguration::Ak_3_0:
		config.SetStandard(AK_SPEAKER_SETUP_3STEREO);
		break;
	case AkChannelConfiguration::Ak_3_1:
		config.SetStandard(AK_SPEAKER_SETUP_3POINT1);
		break;
	case AkChannelConfiguration::Ak_4_0:
		config.SetStandard(AK_SPEAKER_SETUP_4);
		break;
	case AkChannelConfiguration::Ak_4_1:
		config.SetStandard(AK_SPEAKER_SETUP_4POINT1);
		break;
	case AkChannelConfiguration::Ak_5_0:
		config.SetStandard(AK_SPEAKER_SETUP_5);
		break;
	case AkChannelConfiguration::Ak_5_1:
		config.SetStandard(AK_SPEAKER_SETUP_5POINT1);
		break;
	case AkChannelConfiguration::Ak_7_1:
		config.SetStandard(AK_SPEAKER_SETUP_7POINT1);
		break;
	case AkChannelConfiguration::Ak_5_1_2:
		config.SetStandard(AK_SPEAKER_SETUP_DOLBY_5_1_2);
		break;
	case AkChannelConfiguration::Ak_7_1_2:
		config.SetStandard(AK_SPEAKER_SETUP_DOLBY_7_1_2);
		break;
	case AkChannelConfiguration::Ak_7_1_4:
		config.SetStandard(AK_SPEAKER_SETUP_DOLBY_7_1_4);
		break;
	case AkChannelConfiguration::Ak_Auro_9_1:
		config.SetStandard(AK_SPEAKER_SETUP_AURO_9POINT1);
		break;
	case AkChannelConfiguration::Ak_Auro_10_1:
		config.SetStandard(AK_SPEAKER_SETUP_AURO_10POINT1);
		break;
	case AkChannelConfiguration::Ak_Auro_11_1:
		config.SetStandard(AK_SPEAKER_SETUP_AURO_11POINT1);
		break;
	case AkChannelConfiguration::Ak_Auro_13_1:
		config.SetStandard(AK_SPEAKER_SETUP_AURO_13POINT1_751);
		break;
	case AkChannelConfiguration::Ak_Ambisonics_1st_order:
		config.SetAmbisonic(4);
		break;
	case AkChannelConfiguration::Ak_Ambisonics_2nd_order:
		config.SetAmbisonic(9);
		break;
	case AkChannelConfiguration::Ak_Ambisonics_3rd_order:
		config.SetAmbisonic(16);
		break;
	case AkChannelConfiguration::Ak_Parent:
	default:
		config.Clear();
		break;
	}
}

/**
* Set spatial audio room
*
* @param in_GameObjId		Wwise Game Object ID
* @param in_RoomID	ID of the room that the game object is inside.
* @return Result from ak sound engine
*/
AKRESULT FAkAudioDevice::SetInSpatialAudioRoom(
	const AkGameObjectID in_GameObjId,
	AkRoomID in_RoomID)
{
	AKRESULT eResult = AK_Success;
#ifdef AK_ENABLE_ROOMS
	if (m_bSoundEngineInitialized)
	{
		eResult = AK::SpatialAudio::SetGameObjectInRoom(in_GameObjId, in_RoomID);
	}
#endif
	return eResult;
}

AKRESULT FAkAudioDevice::SetBusConfig(
	const FString &in_BusName,
	AkChannelConfig in_Config)
{
	AKRESULT eResult = AK_Fail;
	if (in_BusName.IsEmpty())
	{
		return eResult;
	}

	if (m_bSoundEngineInitialized)
	{
		AkUniqueID BusId = GetIDFromString(in_BusName);
		eResult = AK::SoundEngine::SetBusConfig(BusId, in_Config);
	}

	return eResult;
}

AKRESULT FAkAudioDevice::SetPanningRule(
	AkPanningRule in_ePanningRule)
{
	AKRESULT eResult = AK_Fail;
	if (m_bSoundEngineInitialized)
	{
		eResult = AK::SoundEngine::SetPanningRule(in_ePanningRule);
	}

	return eResult;
}

AkOutputDeviceID FAkAudioDevice::GetOutputID(
	const FString &in_szShareSet,
	AkUInt32 in_idDevice)
{
	return AK::SoundEngine::GetOutputID(TCHAR_TO_AK(*in_szShareSet), in_idDevice);
}

AKRESULT FAkAudioDevice::GetSpeakerAngles(
	TArray<AkReal32> &out_pfSpeakerAngles,
	AkReal32 &out_fHeightAngle,
	AkOutputDeviceID in_idOutput)
{
	AKRESULT eResult = AK_Fail;

	if (m_bSoundEngineInitialized)
	{
		AkUInt32 numSpeakers;

		// Retrieve the number of speaker and height angle
		eResult = AK::SoundEngine::GetSpeakerAngles(NULL, numSpeakers, out_fHeightAngle);
		if (eResult != AK_Success)
			return eResult;

		// Retrieve the speaker angles
		out_pfSpeakerAngles.SetNum(numSpeakers);
		eResult = AK::SoundEngine::GetSpeakerAngles(out_pfSpeakerAngles.GetData(), numSpeakers, out_fHeightAngle, in_idOutput);
	}

	return eResult;
}

AKRESULT FAkAudioDevice::SetSpeakerAngles(
	const TArray<AkReal32> &in_pfSpeakerAngles,
	AkReal32 in_fHeightAngle,
	AkOutputDeviceID in_idOutput)
{
	AKRESULT eResult = AK_Fail;

	if (m_bSoundEngineInitialized)
	{
		AkReal32 *speakerAngles = const_cast<AkReal32 *>(in_pfSpeakerAngles.GetData());
		eResult = AK::SoundEngine::SetSpeakerAngles(speakerAngles, in_pfSpeakerAngles.Num(), in_fHeightAngle, in_idOutput);
	}

	return eResult;
}

AKRESULT FAkAudioDevice::SetGameObjectOutputBusVolume(
	const UAkComponent *in_pEmitter,
	const UAkComponent *in_pListener,
	float in_fControlValue)
{
	AKRESULT eResult = AK_Success;

	if (m_bSoundEngineInitialized)
	{
		const AkGameObjectID emitterId = in_pEmitter ? in_pEmitter->GetAkGameObjectID() : DUMMY_GAMEOBJ;
		const AkGameObjectID listenerId = in_pListener ? in_pListener->GetAkGameObjectID() : DUMMY_GAMEOBJ;
		eResult = AK::SoundEngine::SetGameObjectOutputBusVolume(emitterId, listenerId, in_fControlValue);
	}

	return eResult;
}

/**
 * Obtain a pointer to the singleton instance of FAkAudioDevice
 *
 * @return Pointer to the singleton instance of FAkAudioDevice
 */
FAkAudioDevice *FAkAudioDevice::Get()
{
	if (m_EngineExiting)
	{
		return nullptr;
	}

	static FName AkAudioName = TEXT("AkAudio");
	if (FAkAudioModule::AkAudioModuleIntance == nullptr || !FModuleManager::Get().IsModuleLoaded(AkAudioName))
	{
		FModuleManager::LoadModulePtr<FAkAudioModule>(AkAudioName);
	}

	return FAkAudioModule::AkAudioModuleIntance ? FAkAudioModule::AkAudioModuleIntance->GetAkAudioDevice() : nullptr;
}

/**
 * Stop all audio associated with a game object
 *
 * @param in_GameObjID		ID of the game object
 */
void FAkAudioDevice::StopGameObject(UAkComponent *in_pComponent)
{
	AkGameObjectID gameObjId = DUMMY_GAMEOBJ;
	if (in_pComponent)
	{
		gameObjId = in_pComponent->GetAkGameObjectID();
	}
	if (m_bSoundEngineInitialized)
	{
		AK::SoundEngine::StopAll(gameObjId);
	}
}

/**
 * Stop all audio associated with a playing ID
 *
 * @param in_playingID		Playing ID to stop
 * @param in_uTransitionDuration Fade duration
 * @param in_eFadeCurve          Curve type to be used for the transition
 */
void FAkAudioDevice::StopPlayingID(AkPlayingID in_playingID,
								   AkTimeMs in_uTransitionDuration /*= 0*/,
								   AkCurveInterpolation in_eFadeCurve /*= AkCurveInterpolation_Linear*/)
{
	if (m_bSoundEngineInitialized)
	{
		AK::SoundEngine::StopPlayingID(in_playingID, in_uTransitionDuration, in_eFadeCurve);
	}
}

/**
 * Register an ak audio component with ak sound engine
 *
 * @param in_pComponent		Pointer to the component to register
 */
void FAkAudioDevice::RegisterComponent(UAkComponent *in_pComponent)
{
	if (m_bSoundEngineInitialized && in_pComponent)
	{
		if (in_pComponent->UseDefaultListeners())
			m_defaultEmitters.Add(in_pComponent);

		FString WwiseGameObjectName = TEXT("");
		in_pComponent->GetAkGameObjectName(WwiseGameObjectName);

		const AkGameObjectID gameObjId = in_pComponent->GetAkGameObjectID();
		FAkAudioDevice_Helpers::RegisterGameObject(gameObjId, WwiseGameObjectName);

		if (in_pComponent->bUseSpatialAudio)
			RegisterSpatialAudioEmitter(in_pComponent);

		if (CallbackManager != nullptr)
			CallbackManager->RegisterGameObject(gameObjId);
	}
}

/**
 * Unregister an ak audio component with ak sound engine
 *
 * @param in_pComponent		Pointer to the component to unregister
 */
void FAkAudioDevice::UnregisterComponent(UAkComponent *in_pComponent)
{
	if (m_bSoundEngineInitialized && in_pComponent)
	{
		const AkGameObjectID gameObjId = in_pComponent->GetAkGameObjectID();
		AK::SoundEngine::UnregisterGameObj(gameObjId);

		if (CallbackManager != nullptr)
		{
			CallbackManager->UnregisterGameObject(gameObjId);
		}

		if (in_pComponent->bUseSpatialAudio)
			UnregisterSpatialAudioEmitter(in_pComponent);
	}

	if (m_defaultListeners.Contains(in_pComponent))
	{
		RemoveDefaultListener(in_pComponent);
	}

	if (in_pComponent->UseDefaultListeners())
	{
		m_defaultEmitters.Remove(in_pComponent);
	}

	check(!m_defaultListeners.Contains(in_pComponent) && !m_defaultEmitters.Contains(in_pComponent));

	if (m_SpatialAudioListener == in_pComponent)
		m_SpatialAudioListener = nullptr;
}

/**
* Register an ak audio component with ak spatial audio
*
* @param in_pComponent		Pointer to the component to register
*/
void FAkAudioDevice::RegisterSpatialAudioEmitter(UAkComponent *in_pComponent)
{
	if (!in_pComponent->HasBeenCreated())
		return;

	ensureMsgf(in_pComponent->bUseSpatialAudio, TEXT("Attempted to register a spatial audio emitter on a AkComponent (%s) which doesn't use Spatial Audio"), *(in_pComponent->GetName()));
	AkEmitterSettings settings;

	settings.reflectionsOrder = in_pComponent->EarlyReflectionOrder;
	settings.reflectionsAuxBusGain = in_pComponent->EarlyReflectionBusSendGain;
	settings.useImageSources = in_pComponent->EnableSpotReflectors;
	settings.reflectionMaxPathLength = in_pComponent->EarlyReflectionMaxPathLength;
	settings.reflectorFilterMask = (AkUInt32)in_pComponent->ReflectionFilter;
	settings.roomReverbAuxBusGain = in_pComponent->roomReverbAuxBusGain;
	settings.diffractionMaxEdges = in_pComponent->diffractionMaxEdges;
	settings.diffractionMaxPaths = in_pComponent->diffractionMaxPaths;
	settings.diffractionMaxPathLength = in_pComponent->diffractionMaxPathLength;

	if (in_pComponent->EarlyReflectionAuxBus)
		settings.reflectAuxBusID = in_pComponent->EarlyReflectionAuxBus->GetAuxBusId();
	else if (!in_pComponent->EarlyReflectionAuxBusName.IsEmpty())
		settings.reflectAuxBusID = AK::SoundEngine::GetIDFromString(TCHAR_TO_ANSI(*in_pComponent->EarlyReflectionAuxBusName));
	else
		settings.reflectAuxBusID = AK_INVALID_UNIQUE_ID;

	FString name = in_pComponent->GetFName().ToString();
	settings.name = TCHAR_TO_ANSI(*name); // This will copy the string inside operator=

	AK::SpatialAudio::RegisterEmitter(in_pComponent->GetAkGameObjectID(), settings);
}

AKRESULT FAkAudioDevice::SetGeometry(AkGeometrySetID AcousticZoneID, const AkGeometryParams &Params)
{
	AKRESULT eResult = AK_Fail;
	if (m_bSoundEngineInitialized)
	{
		eResult = AK::SpatialAudio::SetGeometry(AcousticZoneID, Params);
	}

	return eResult;
}

AKRESULT FAkAudioDevice::RemoveGeometrySet(AkGeometrySetID AcousticZoneID)
{
	AKRESULT eResult = AK_Fail;
	if (m_bSoundEngineInitialized)
	{
		eResult = AK::SpatialAudio::RemoveGeometry(AcousticZoneID);
	}

	return eResult;
}

/**
* Unregister an ak audio component with ak spatial audio
*
* @param in_pComponent		Pointer to the component to unregister
*/
void FAkAudioDevice::UnregisterSpatialAudioEmitter(UAkComponent *in_pComponent)
{
	AK::SpatialAudio::UnregisterEmitter(in_pComponent->GetAkGameObjectID());
}

void FAkAudioDevice::UpdateDefaultActiveListeners()
{
	if (m_bSoundEngineInitialized)
	{
		auto NumDefaultListeners = m_defaultListeners.Num();
		auto pListenerIds = (AkGameObjectID *)alloca(NumDefaultListeners * sizeof(AkGameObjectID));
		int index = 0;
		for (auto DefaultListenerIter = m_defaultListeners.CreateConstIterator(); DefaultListenerIter; ++DefaultListenerIter)
			pListenerIds[index++] = (*DefaultListenerIter)->GetAkGameObjectID();

		AK::SoundEngine::SetDefaultListeners(pListenerIds, NumDefaultListeners);
	}
}

AKRESULT FAkAudioDevice::SetPosition(UAkComponent *in_akComponent, const AkTransform &in_SoundPosition)
{
	if (m_bSoundEngineInitialized)
	{
		if (in_akComponent->bUseSpatialAudio)
			return AK::SpatialAudio::SetPosition(in_akComponent->GetAkGameObjectID(), in_SoundPosition);
		else
			return AK::SoundEngine::SetPosition(in_akComponent->GetAkGameObjectID(), in_SoundPosition);
	}

	return AK_Fail;
}

AKRESULT FAkAudioDevice::AddRoom(UAkRoomComponent *in_pRoom, const AkRoomParams &in_RoomParams)
{
	AKRESULT result = AK_Fail;
	if (m_bSoundEngineInitialized)
	{
		result = AK::SpatialAudio::SetRoom(in_pRoom->GetRoomID(), in_RoomParams);
		if (result == AK_Success)
		{
			AddRoomComponentToPrioritizedList(in_pRoom);
		}
	}
	return result;
}

AKRESULT FAkAudioDevice::UpdateRoom(UAkRoomComponent *in_pRoom, const AkRoomParams &in_RoomParams)
{
	AKRESULT result = AK_Fail;
	if (m_bSoundEngineInitialized)
	{
		result = AK::SpatialAudio::SetRoom(in_pRoom->GetRoomID(), in_RoomParams);
	}
	return result;
}

AKRESULT FAkAudioDevice::RemoveRoom(UAkRoomComponent *in_pRoom)
{
	AKRESULT result = AK_Fail;
	if (m_bSoundEngineInitialized)
	{
		result = AK::SpatialAudio::RemoveRoom(in_pRoom->GetRoomID());
		if (result == AK_Success)
		{
			RemoveRoomComponentFromPrioritizedList(in_pRoom);
		}
	}

	return result;
}

AKRESULT FAkAudioDevice::SetImageSource(AAkSpotReflector *in_pSpotReflector, const AkImageSourceSettings &in_ImageSourceInfo, AkUniqueID in_AuxBusID, AkRoomID in_RoomID)
{
	if (m_bSoundEngineInitialized)
	{
		return AK::SpatialAudio::SetImageSource(in_pSpotReflector->GetImageSourceID(), in_ImageSourceInfo, in_AuxBusID, in_RoomID, AK_INVALID_GAME_OBJECT);
	}

	return AK_Fail;
}

AKRESULT FAkAudioDevice::RemoveImageSource(AAkSpotReflector *in_pSpotReflector, AkUniqueID in_AuxBusID)
{
	if (m_bSoundEngineInitialized)
	{
		return AK::SpatialAudio::RemoveImageSource(in_pSpotReflector->GetImageSourceID(), in_AuxBusID);
	}

	return AK_Fail;
}

void FAkAudioDevice::SetListeners(UAkComponent *in_pEmitter, const TArray<UAkComponent *> &in_listenerSet)
{
	check(!in_pEmitter->UseDefaultListeners());

	m_defaultEmitters.Remove(in_pEmitter); //This emitter is no longer using the default listener set.

	auto NumListeners = in_listenerSet.Num();
	auto pListenerIds = (AkGameObjectID *)alloca(NumListeners * sizeof(AkGameObjectID));
	int index = 0;
	for (const auto &Listener : in_listenerSet)
		pListenerIds[index++] = Listener->GetAkGameObjectID();

	AK::SoundEngine::SetListeners(in_pEmitter->GetAkGameObjectID(), pListenerIds, NumListeners);
}

bool FAkAudioDevice::SetSpatialAudioListener(UAkComponent *in_pListener)
{
#if WITH_EDITOR
	if (in_pListener == EditorListener)
	{
		return false;
	}
#endif
	m_SpatialAudioListener = in_pListener;

	AK::SpatialAudio::RegisterListener((AkGameObjectID)m_SpatialAudioListener);
	return true;
}

UAkComponent *FAkAudioDevice::GetSpatialAudioListener() const
{
	return m_SpatialAudioListener;
}

UAkComponent *FAkAudioDevice::GetAkComponent(class USceneComponent *AttachToComponent, FName AttachPointName, const FVector *Location, EAttachLocation::Type LocationType)
{
	if (!AttachToComponent)
	{
		return NULL;
	}

	UAkComponent *AkComponent = NULL;
	FAttachmentTransformRules AttachRules = FAttachmentTransformRules::KeepRelativeTransform;

	if (GEngine && AK::SoundEngine::IsInitialized())
	{
		AActor *Actor = AttachToComponent->GetOwner();
		if (Actor)
		{
			if (Actor->IsPendingKill())
			{
				// Avoid creating component if we're trying to play a sound on an already destroyed actor.
				return NULL;
			}

			TArray<UAkComponent *> AkComponents;
			Actor->GetComponents(AkComponents);
			for (int32 CompIdx = 0; CompIdx < AkComponents.Num(); CompIdx++)
			{
				UAkComponent *pCompI = AkComponents[CompIdx];
				if (pCompI && pCompI->IsRegistered())
				{
					if (AttachToComponent == pCompI)
					{
						return pCompI;
					}

					if (AttachToComponent != pCompI->GetAttachParent() || AttachPointName != pCompI->GetAttachSocketName())
					{
						continue;
					}

					// If a location is requested, try to match location.
					if (Location)
					{
						if (LocationType == EAttachLocation::KeepWorldPosition)
						{
							AttachRules = FAttachmentTransformRules::KeepWorldTransform;
							if (!FVector::PointsAreSame(*Location, pCompI->GetComponentLocation()))
								continue;
						}
						else
						{
							AttachRules = FAttachmentTransformRules::KeepRelativeTransform;
							if (!FVector::PointsAreSame(*Location, pCompI->RelativeLocation))
								continue;
						}
					}

					// AkComponent found which exactly matches the attachment: reuse it.
					return pCompI;
				}
			}
		}
		else
		{
			// Try to find if there is an AkComponent attached to AttachToComponent (will be the case if AttachToComponent has no owner)
			const TArray<USceneComponent *> AttachChildren = AttachToComponent->GetAttachChildren();
			for (int32 CompIdx = 0; CompIdx < AttachChildren.Num(); CompIdx++)
			{
				UAkComponent *pCompI = Cast<UAkComponent>(AttachChildren[CompIdx]);
				if (pCompI && pCompI->IsRegistered())
				{
					// There is an associated AkComponent to AttachToComponent, no need to add another one.
					return pCompI;
				}
			}
		}

		if (AkComponent == NULL)
		{
			if (Actor)
			{
				AkComponent = NewObject<UAkComponent>(Actor);
			}
			else
			{
				AkComponent = NewObject<UAkComponent>();
			}
		}

		check(AkComponent);

		if (Location)
		{
			if (LocationType == EAttachLocation::KeepWorldPosition)
			{
				AttachRules = FAttachmentTransformRules::KeepWorldTransform;
				AkComponent->SetWorldLocation(*Location);
			}
			else
			{
				AttachRules = FAttachmentTransformRules::KeepRelativeTransform;
				AkComponent->SetRelativeLocation(*Location);
			}
		}

		AkComponent->RegisterComponentWithWorld(AttachToComponent->GetWorld());
		AkComponent->AttachToComponent(AttachToComponent, AttachRules, AttachPointName);
	}

	return (AkComponent);
}

/**
* Cancel the callback cookie for a dispatched event
*
* @param in_cookie			The cookie to cancel
*/
void FAkAudioDevice::CancelEventCallbackCookie(void *in_cookie)
{
	if (m_bSoundEngineInitialized)
	{
		CallbackManager->CancelEventCallback(in_cookie);
	}
}

/**
* Cancel the callback cookie for a dispatched event
*
* @param in_cookie			The cookie to cancel
*/
void FAkAudioDevice::CancelEventCallbackDelegate(const FOnAkPostEventCallback &in_Delegate)
{
	if (m_bSoundEngineInitialized)
	{
		CallbackManager->CancelEventCallback(in_Delegate);
	}
}

AKRESULT FAkAudioDevice::SetAttenuationScalingFactor(AActor *Actor, float ScalingFactor)
{
	AKRESULT eResult = AK_Fail;
	if (m_bSoundEngineInitialized)
	{
		AkGameObjectID GameObjID = DUMMY_GAMEOBJ;
		eResult = GetGameObjectID(Actor, GameObjID);
		if (eResult == AK_Success)
		{
			eResult = AK::SoundEngine::SetScalingFactor(GameObjID, ScalingFactor);
		}
	}

	return eResult;
}

AKRESULT FAkAudioDevice::SetAttenuationScalingFactor(UAkComponent *AkComponent, float ScalingFactor)
{
	AKRESULT eResult = AK_Fail;
	if (m_bSoundEngineInitialized && AkComponent)
	{
		eResult = AK::SoundEngine::SetScalingFactor(AkComponent->GetAkGameObjectID(), ScalingFactor);
	}
	return eResult;
}

#ifdef AK_SOUNDFRAME

/**
 * Called when sound frame connects 
 *
 * @param in_bConnect		True if Wwise is connected, False if it is not
 */
void FAkAudioDevice::OnConnect(
	bool in_bConnect ///< True if Wwise is connected, False if it is not
)
{
	if (in_bConnect == true)
	{
		UE_LOG(LogAkAudio,
			   Log,
			   TEXT("SoundFrame successfully connected."));
	}
	else
	{
		UE_LOG(LogAkAudio,
			   Log,
			   TEXT("SoundFrame failed to connect."));
	}
}

/**
 * Event notification. This method is called when an event is added, removed, changed, or pushed.
 *
 * @param in_eNotif			Notification type
 * @param in_eventID		Unique ID of the event
 */
void FAkAudioDevice::OnEventNotif(
	Notif in_eNotif,
	AkUniqueID in_eventID)
{
#if WITH_EDITORONLY_DATA
	if (in_eNotif == IClient::Notif_Changed && !IsRunningGame())
	{
		FEditorSupportDelegates::RedrawAllViewports.Broadcast();
	}
#endif
}

/**
 * Sound object notification. This method is called when a sound object is added, removed, or changed.
 *
 * @param in_eNotif			Notification type
 * @param in_soundObjectID	Unique ID of the sound object
 */
void FAkAudioDevice::OnSoundObjectNotif(
	Notif in_eNotif,
	AkUniqueID in_soundObjectID)
{
#if WITH_EDITORONLY_DATA
	if (in_eNotif == IClient::Notif_Changed && !IsRunningGame())
	{
		FEditorSupportDelegates::RedrawAllViewports.Broadcast();
	}
#endif
}

#endif

#if PLATFORM_WINDOWS || PLATFORM_MAC
static void UELocalOutputFunc(
	AK::Monitor::ErrorCode in_eErrorCode,
	const AkOSChar *in_pszError,
	AK::Monitor::ErrorLevel in_eErrorLevel,
	AkPlayingID in_playingID,
	AkGameObjectID in_gameObjID)
{
	wchar_t *szWideError;
#if PLATFORM_MAC
	CONVERT_OSCHAR_TO_WIDE(in_pszError, szWideError);
#else
	szWideError = (wchar_t *)in_pszError;
#endif

	if (!IsRunningCommandlet())
	{
		if (in_eErrorLevel == AK::Monitor::ErrorLevel_Message)
		{
			UE_LOG(LogAkAudio, Log, TEXT("%s"), szWideError);
		}
		else
		{
			UE_LOG(LogAkAudio, Error, TEXT("%s"), szWideError);
		}
	}
}
#endif

thread_local uint32 threadIndex = UINT32_MAX;
FThreadSafeCounter g_nextThreadIndex(0);

static void AkUE4_ParallelForFunc(void *data, AkUInt32 beginIndex, AkUInt32 endIndex, AkUInt32 /*tileSize*/, AkParallelForFunc func, void *userData, const char *in_szDebugName)
{
	check(func);
	check(endIndex >= beginIndex);

	if (func != nullptr && endIndex - beginIndex > 0)
	{
		ParallelFor(endIndex - beginIndex,
					[data, beginIndex, func, userData](int32 Index) {
						check(data);

						AkTaskContext ctx;
						threadIndex = (threadIndex == UINT32_MAX) ? g_nextThreadIndex.Add(1) : threadIndex;
						ctx.uIdxThread = threadIndex;
						func(data, beginIndex + Index, beginIndex + Index + 1, ctx, userData);
					});
	}
}

bool FAkAudioDevice::EnsureInitialized()
{
	// We don't want sound in those cases.
	if (FParse::Param(FCommandLine::Get(), TEXT("nosound")) || FApp::IsBenchmarking() || IsRunningDedicatedServer() || IsRunningCommandlet())
	{
		return false;
	}

	if (m_bSoundEngineInitialized)
	{
		return true;
	}

#if PLATFORM_XBOXONE
#ifndef AK_OPTIMIZED
	try
	{
		// Make sure networkmanifest.xml is loaded by instantiating a Microsoft.Xbox.Networking object.
		auto secureDeviceAssociationTemplate = Windows::Xbox::Networking::SecureDeviceAssociationTemplate::GetTemplateByName("WwiseDiscovery");
	}
	catch (...)
	{
		UE_LOG(LogAkAudio, Log, TEXT("Could not find Wwise network ports in AppxManifest. Network communication will not be available."));
	}
#endif
#endif

	UE_LOG(LogAkAudio,
		   Log,
		   TEXT("Wwise(R) SDK Version %d.%d.%d Build %d. Copyright (c) 2006-%d Audiokinetic Inc."),
		   AK_WWISESDK_VERSION_MAJOR,
		   AK_WWISESDK_VERSION_MINOR,
		   AK_WWISESDK_VERSION_SUBMINOR,
		   AK_WWISESDK_VERSION_BUILD,
		   AK_WWISESDK_VERSION_MAJOR);

	AkMemSettings memSettings;
	memSettings.uMaxNumPools = 256;

	if (AK::MemoryMgr::Init(&memSettings) != AK_Success)
	{
		return false;
	}

	AkStreamMgrSettings stmSettings;
	AK::StreamMgr::GetDefaultSettings(stmSettings);
	AK::IAkStreamMgr *pStreamMgr = AK::StreamMgr::Create(stmSettings);
	if (!pStreamMgr)
	{
		return false;
	}

	AkDeviceSettings deviceSettings;
	AK::StreamMgr::GetDefaultDeviceSettings(deviceSettings);

	deviceSettings.uGranularity = AK_UNREAL_IO_GRANULARITY;
	deviceSettings.uSchedulerTypeFlags = AK_SCHEDULER_DEFERRED_LINED_UP;
	deviceSettings.uMaxConcurrentIO = AK_UNREAL_MAX_CONCURRENT_IO;

#if PLATFORM_MAC
	deviceSettings.threadProperties.uStackSize = 4 * 1024 * 1024; // From FRunnableThreadMac
#elif PLATFORM_APPLE
	deviceSettings.threadProperties.uStackSize = 256 * 1024; // From FRunnableThreadApple
#elif PLATFORM_SWITCH
	deviceSettings.threadProperties.uStackSize = 1 * 1024 * 1024;
#endif

	LowLevelIOHook = new CAkFilePackageLowLevelIO<CAkUnrealIOHookDeferred, CAkDiskPackage, AkFileCustomParamPolicy>();
	if (!LowLevelIOHook->Init(deviceSettings))
	{
		delete LowLevelIOHook;
		LowLevelIOHook = nullptr;
		return false;
	}

	AkInitSettings initSettings;
	AkPlatformInitSettings platformInitSettings;
	AK::SoundEngine::GetDefaultInitSettings(initSettings);
	AK::SoundEngine::GetDefaultPlatformInitSettings(platformInitSettings);

	initSettings.eFloorPlane = AkFloorPlane_XY;

#if !(PLATFORM_ANDROID || PLATFORM_IOS)
	// Keep default size on mobile platforms.
	platformInitSettings.uLEngineDefaultPoolSize = 128 * 1024 * 1024;
#endif

#if PLATFORM_ANDROID && !PLATFORM_LUMIN
	extern JavaVM *GJavaVM;
	platformInitSettings.pJavaVM = GJavaVM;
	platformInitSettings.jNativeActivity = FAndroidApplication::GetGameActivityThis();
#endif
#if defined AK_WIN
	// OCULUS_START vhamm audio redirect with build of wwise >= 2015.1.5
	if (IHeadMountedDisplayModule::IsAvailable())
	{
		FString AudioOutputDevice;
		IHeadMountedDisplayModule &Hmd = IHeadMountedDisplayModule::Get();
		AudioOutputDevice = Hmd.GetAudioOutputDevice();
		if (!AudioOutputDevice.IsEmpty())
			initSettings.settingsMainOutput.idDevice = AK::GetDeviceIDFromName((wchar_t *)*AudioOutputDevice);
	}
	// OCULUS_END

#endif

	const UAkSettings *AkSettings = GetDefault<UAkSettings>();
	if (AkSettings && AkSettings->bEnableMultiCoreRendering)
	{
		initSettings.taskSchedulerDesc.fcnParallelFor = AkUE4_ParallelForFunc;

		check(FTaskGraphInterface::Get().IsRunning());
		check(FPlatformProcess::SupportsMultithreading());
		check(ENamedThreads::bHasHighPriorityThreads);

		initSettings.taskSchedulerDesc.uNumSchedulerWorkerThreads = FTaskGraphInterface::Get().GetNumWorkerThreads();
	}

	if (AK::SoundEngine::Init(&initSettings, &platformInitSettings) != AK_Success)
	{
		return false;
	}

	AkMusicSettings musicInit;
	AK::MusicEngine::GetDefaultInitSettings(musicInit);

	if (AK::MusicEngine::Init(&musicInit) != AK_Success)
	{
		return false;
	}

	AkSpatialAudioInitSettings spatialAudioInit;
	if (AK::SpatialAudio::Init(spatialAudioInit) != AK_Success)
	{
		return false;
	}

#if PLATFORM_WINDOWS || PLATFORM_MAC
	// Enable AK error redirection to UE log.
	AK::Monitor::SetLocalOutput(AK::Monitor::ErrorLevel_All, UELocalOutputFunc);
#endif

#ifndef AK_OPTIMIZED
#if !PLATFORM_LINUX
	//
	// Initialize communications, not in release build, and only for a game (and not the project selection screen, for example)
	//
#if UE_4_18_OR_LATER
	const bool HasProjectName = FApp::HasProjectName();
#else
	const bool HasProjectName = FApp::HasGameName();
#endif // UE_4_18_OR_LATER

	if (HasProjectName)
	{
#if UE_4_18_OR_LATER
		FString GameName = FApp::GetProjectName();
#else
		FString GameName = FApp::GetGameName();
#endif // UE_4_18_OR_LATER

#if WITH_EDITORONLY_DATA
		if (!IsRunningGame())
			GameName += TEXT(" (Editor)");
#endif
		AkCommSettings commSettings;
		AK::Comm::GetDefaultInitSettings(commSettings);
#if PLATFORM_SWITCH
		commSettings.bInitSystemLib = false;
#endif
		FCStringAnsi::Strcpy(commSettings.szAppNetworkName, AK_COMM_SETTINGS_MAX_STRING_SIZE, TCHAR_TO_ANSI(*GameName));
		if (AK::Comm::Init(commSettings) != AK_Success)
		{
			UE_LOG(LogInit, Warning, TEXT("Could not initialize communication. GameName is %s"), *GameName);
			//return false;
		}
	}
#endif
#endif // AK_OPTIMIZED

	//
	// Setup banks path
	//
	SetBankDirectory();

	// Init dummy game object
	AK::SoundEngine::RegisterGameObj(DUMMY_GAMEOBJ, "Unreal Global");
#if WITH_EDITOR
	if (!IsRunningGame())
	{
		AkGameObjectID tempID = DUMMY_GAMEOBJ;
		AK::SoundEngine::SetListeners(DUMMY_GAMEOBJ, &tempID, 1);
	}
#endif

	m_bSoundEngineInitialized = true;

	AkBankManager = new FAkBankManager;

	CallbackInfoPool = new AkCallbackInfoPool;

	LoadAllReferencedBanks();

	// Go get the max number of Aux busses
	MaxAuxBus = AK_MAX_AUX_PER_OBJ;
	if (AkSettings)
	{
		MaxAuxBus = AkSettings->MaxSimultaneousReverbVolumes;
	}

	CallbackManager = new FAkComponentCallbackManager();
	if (CallbackManager == nullptr)
	{
		return false;
	}

	return true;
}

void FAkAudioDevice::AddDefaultListener(UAkComponent *in_pListener)
{
	bool bAlreadyInSet;
	m_defaultListeners.Add(in_pListener, &bAlreadyInSet);
	if (!bAlreadyInSet)
	{
		for (auto &Emitter : m_defaultEmitters)
			Emitter->OnDefaultListenerAdded(in_pListener);

		in_pListener->IsDefaultListener = true;
		UpdateDefaultActiveListeners();

		if (m_SpatialAudioListener == nullptr)
			SetSpatialAudioListener(in_pListener);
	}
}

void FAkAudioDevice::RemoveDefaultListener(UAkComponent *in_pListener)
{
	for (auto &Emitter : m_defaultEmitters)
	{
		Emitter->OnListenerUnregistered(in_pListener);
	}

	m_defaultListeners.Remove(in_pListener);
	in_pListener->IsDefaultListener = false;
	UpdateDefaultActiveListeners();

	// We are setting Aux Sends with the SpatialAudio API, and that requires a Spatial Audio listener.
	// When running dedicated server, Unreal creates a camera manager (default listener 1 gets set as spatial audio listener), then another one (default listener 2), and then destroys the first. This leaves us with a default listener, but no spatial audio listener. This fix targets that issue.
	if (m_SpatialAudioListener == in_pListener && m_defaultListeners.Num() > 0)
	{
		for (auto listener : m_defaultListeners)
		{
			if (SetSpatialAudioListener(m_defaultListeners.Array()[0]))
			{
				break;
			}
		}
	}
}

void FAkAudioDevice::OnActorSpawned(AActor *SpawnedActor)
{
	APlayerCameraManager *AsPlayerCameraManager = Cast<APlayerCameraManager>(SpawnedActor);
	if (AsPlayerCameraManager && AsPlayerCameraManager->GetWorld()->AllowAudioPlayback())
	{
		APlayerController *CameraOwner = Cast<APlayerController>(AsPlayerCameraManager->GetOwner());
		if (CameraOwner && CameraOwner->IsLocalPlayerController())
		{
			UAkComponent *pAkComponent = NewObject<UAkComponent>(SpawnedActor);
			if (pAkComponent != nullptr)
			{
				pAkComponent->RegisterComponentWithWorld(SpawnedActor->GetWorld());
				pAkComponent->AttachToComponent(SpawnedActor->GetRootComponent(), FAttachmentTransformRules::KeepWorldTransform, FName());
				AddDefaultListener(pAkComponent);
			}
		}
	}
}

FString FAkAudioDevice::GetBasePath()
{
#if UE_4_18_OR_LATER
	const auto ContentPath = FPaths::ProjectContentDir();
#else
	const auto ContentPath = FPaths::GameContentDir();
#endif // UE_4_18_OR_LATER

	FString BasePath = FPaths::Combine(*ContentPath, TEXT("WwiseAudio"));

#if defined AK_WIN
	BasePath = FPaths::Combine(*BasePath, TEXT("Windows/"));
#elif defined AK_LINUX
	BasePath = FPaths::Combine(*BasePath, TEXT("Linux/"));
#elif defined AK_MAC_OS_X
	BasePath = FPaths::Combine(*BasePath, TEXT("Mac/"));
#elif defined AK_PS4
	BasePath = FPaths::Combine(*BasePath, TEXT("PS4/"));
#elif defined AK_XBOXONE
	BasePath = FPaths::Combine(*BasePath, TEXT("XboxOne/"));
#elif defined AK_LUMIN
	BasePath = FPaths::Combine(*BasePath, TEXT("Lumin/"));
#elif defined AK_ANDROID
	BasePath = FPaths::Combine(*BasePath, TEXT("Android/"));
#elif defined AK_IOS
	BasePath = FPaths::Combine(*BasePath, TEXT("iOS/"));
#elif defined AK_NX
	BasePath = FPaths::Combine(*BasePath, TEXT("Switch/"));
#else
#error "AkAudio integration is unsupported for this platform"
#endif

	return BasePath;
}

void FAkAudioDevice::SetBankDirectory()
{
	const FString BasePath = GetBasePath();

	UE_LOG(LogInit, Log, TEXT("Audiokinetic Audio Device setting bank directory to %s."), *BasePath);

	if (LowLevelIOHook)
	{
		LowLevelIOHook->SetBasePath(BasePath);
	}

	AK::StreamMgr::SetCurrentLanguage(AKTEXT("English(US)"));
}

/**
 * Allocates memory from permanent pool. This memory will NEVER be freed.
 *
 * @param	Size	Size of allocation.
 *
 * @return pointer to a chunk of memory with size Size
 */
void *FAkAudioDevice::AllocatePermanentMemory(int32 Size, bool &AllocatedInPool)
{
	return 0;
}

AKRESULT FAkAudioDevice::GetGameObjectID(AActor *in_pActor, AkGameObjectID &io_GameObject)
{
	if (IsValid(in_pActor))
	{
		UAkComponent *pComponent = GetAkComponent(in_pActor->GetRootComponent(), FName(), NULL, EAttachLocation::KeepRelativeOffset);
		if (pComponent)
		{
			io_GameObject = pComponent->GetAkGameObjectID();
			return AK_Success;
		}
		else
			return AK_Fail;
	}

	// we do not modify io_GameObject, letting it to the specified default value.
	return AK_Success;
}

AKRESULT FAkAudioDevice::GetGameObjectID(AActor *in_pActor, AkGameObjectID &io_GameObject, bool in_bStopWhenOwnerDestroyed)
{
	if (IsValid(in_pActor))
	{
		UAkComponent *pComponent = GetAkComponent(in_pActor->GetRootComponent(), FName(), NULL, EAttachLocation::KeepRelativeOffset);
		if (pComponent)
		{
			pComponent->StopWhenOwnerDestroyed = in_bStopWhenOwnerDestroyed;
			io_GameObject = pComponent->GetAkGameObjectID();
			return AK_Success;
		}
		else
			return AK_Fail;
	}

	// we do not modify io_GameObject, letting it to the specified default value.
	return AK_Success;
}

void FAkAudioDevice::StartOutputCapture(const FString &Filename)
{
	if (m_bSoundEngineInitialized)
	{
		AK::SoundEngine::StartOutputCapture(TCHAR_TO_AK(*Filename));
	}
}

void FAkAudioDevice::StopOutputCapture()
{
	if (m_bSoundEngineInitialized)
	{
		AK::SoundEngine::StopOutputCapture();
	}
}

void FAkAudioDevice::StartProfilerCapture(const FString &Filename)
{
	if (m_bSoundEngineInitialized)
	{
		AK::SoundEngine::StartProfilerCapture(TCHAR_TO_AK(*Filename));
	}
}

void FAkAudioDevice::AddOutputCaptureMarker(const FString &MarkerText)
{
	if (m_bSoundEngineInitialized)
	{
		AK::SoundEngine::AddOutputCaptureMarker(TCHAR_TO_ANSI(*MarkerText));
	}
}

void FAkAudioDevice::StopProfilerCapture()
{
	if (m_bSoundEngineInitialized)
	{
		AK::SoundEngine::StopProfilerCapture();
	}
}

AKRESULT FAkAudioDevice::PostMidiEvent(
	UAkAudioEvent* in_Event,
	AkGameObjectID in_gameObjectID,
	AkMIDIPost* in_pPosts,
	AkUInt16 in_uNumPosts)
{
	AkUniqueID EventID = GetIDFromString(in_Event->GetName());
	return AK::SoundEngine::PostMIDIOnEvent(EventID, in_gameObjectID, in_pPosts, in_uNumPosts);
}

AKRESULT FAkAudioDevice::StopMidiEvent(UAkAudioEvent* in_Event, AkGameObjectID in_gameObjectID)
{
	AkUniqueID EventID = GetIDFromString(in_Event->GetName());
	return AK::SoundEngine::StopMIDIOnEvent(EventID, in_gameObjectID);
}
// end
