// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Factories/Factory.h"
#include "AkMidiMessageFactory.generated.h"

/**
 * 
 */
UCLASS(hidecategories=Object)
class AUDIOKINETICTOOLS_API UAkMidiMessageFactory : public UFactory
{
	GENERATED_UCLASS_BODY()

#if CPP
		virtual UObject* FactoryCreateNew(UClass* Class, UObject* InParent, FName Name, EObjectFlags Flags, UObject* Cpntext, FFeedbackContext* Warn) override;
#endif
	
};
