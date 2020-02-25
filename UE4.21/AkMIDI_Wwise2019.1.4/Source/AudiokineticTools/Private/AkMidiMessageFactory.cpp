// Fill out your copyright notice in the Description page of Project Settings.

#include "AkMidiMessageFactory.h"
#include "AkMidiMessage.h"


UAkMidiMessageFactory::UAkMidiMessageFactory(const FObjectInitializer& ObjectInitializer) : Super(ObjectInitializer)
{
	SupportedClass = UAkMidiMessage::StaticClass();

	bCreateNew = true;
	bEditorImport = true;
	bEditAfterNew = true;
}

UObject* UAkMidiMessageFactory::FactoryCreateNew(UClass* Class, UObject* InParent, FName Name, EObjectFlags Flags, UObject* Cpntext, FFeedbackContext* Warn)
{
	UAkMidiMessage* AkMidiMessage = NewObject<UAkMidiMessage>(InParent, Name, Flags);
	return AkMidiMessage;
}

