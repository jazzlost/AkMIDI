// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "AkMidiComponent.h"
#include "AkMidiFxLibrary.generated.h"

/**
 * 
 */
UCLASS(Blueprintable)
class AKMIDI_API UAkMidiFxLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()
	
public:
	UFUNCTION(BlueprintCallable, BlueprintCosmetic, Category = "AkMIDI|FxLibrary")
	static UAkMidiMessage* Octave(UAkMidiMessage* MidiMessage, int Octave);

	UFUNCTION(BlueprintCallable, BlueprintCosmetic, Category = "AkMIDI|FxLibrary")
	static UAkMidiMessage* VelocityCompression(UAkMidiMessage* MidiMessage, uint8 LowVelocity, uint8 HighVelocity);


};
