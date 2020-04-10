// Copyright (c) 2006-2012 Audiokinetic Inc. / All Rights Reserved

/*=============================================================================
AssetTypeActions_AkAcousticTexture.h:
=============================================================================*/
#pragma once

#include "AssetTypeActions_Base.h"
#include "AkMidiMessage.h"

class FAssetTypeActions_AkMidiMessage : public FAssetTypeActions_Base
{
public:
	FAssetTypeActions_AkMidiMessage(EAssetTypeCategories::Type InAssetCategory) { MyAssetCategory = InAssetCategory; }

	// IAssetTypeActions Implementation
	virtual FText GetName() const override { return NSLOCTEXT("AssetTypeActions", "AssetTypeActions_AkMidiMessage", "Audiokinetic Midi Message"); }
	virtual FColor GetTypeColor() const override { return FColor(200, 50, 100); }
	virtual UClass* GetSupportedClass() const override { return UAkMidiMessage::StaticClass(); }
	virtual bool HasActions(const TArray<UObject*>& InObjects) const override { return false; }
	virtual void OpenAssetEditor(const TArray<UObject*>& InObjects, TSharedPtr<class IToolkitHost> EditWithinLevelEditor = TSharedPtr<IToolkitHost>());
	virtual bool CanFilter() override { return true; }
	virtual uint32 GetCategories() override { return MyAssetCategory; }
	virtual bool ShouldForceWorldCentric() { return true; }
	virtual class UThumbnailInfo* GetThumbnailInfo(UObject* Asset) const override { return NULL; };

private:
	EAssetTypeCategories::Type MyAssetCategory;
};