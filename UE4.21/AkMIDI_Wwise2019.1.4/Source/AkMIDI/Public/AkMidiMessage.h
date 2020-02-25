// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "UObject/NoExportTypes.h"
#include "AK/SoundEngine/Common/AkMidiTypes.h"
#include "AkMidiMessage.generated.h"

/**
 * 
 */
UENUM(BlueprintType)
enum class EAkMessageType : uint8
{
	/*
	*	Note Off Event
	*	[Data1=Note, Data2=Velocity]
	*/
	AMT_Note_Off = 8  UMETA(DisplayName = "Note Off"),
	/*
	*	Note On
	*	[Data1=Note, Data2=Velocity]
	*	Note: Velocity of 0 = same as Note Off
	*/
	AMT_Note_On = 9  UMETA(DisplayName = "Note On"),
	/*
	*	Note Aftertouch Event
	*	[Data1=Note, Data2=Amount]
	*/
	AMT_AfterTouch  UMETA(DisplayName = "Note Aftertouch"),
	/*
	*	Controller Event
	*	[Data1=Type, Data2=Amount]
	*/
	AMT_CC  UMETA(DisplayName = "Continueous Change"),
	/*
	*	Program(Instrument) Change Event
	*	[Data1=Number, Data2=0]
	*/
	AMT_Program_Change  UMETA(DisplayName = "Program Change"),
	/*
	*	Channel Aftertouch Event
	*	[Data1=Amount, Data2=0]
	*/
	AMT_Channel_AfterTouch  UMETA(DisplayName = "Channel Aftertouch"),
	/*
	*	Pitch Bend Event
	*	[Data1=LowBit, Data2=HighBit]
	*/
	AMT_Pitch_Bend  UMETA(DisplayName = "Pitch Bend")
};

UENUM(BlueprintType)
enum class EMessageParamType : uint8
{
	MPT_NoteOffset  UMETA(DisplayName = "NoteOffset"),
	
	//0 - 16
	MPT_Channel     UMETA(DisplayName = "Channel"),

	/*
	*Note Off				- Note Value
	*Note On				- Note Value
	*Note Aftertouch		- Note Value
	*Continueous Change		- Change Type
	*Program Change			- Program Value
	*Channel Aftertouch		- Amount
	*Pitch Bend				- LowBit Value
	*/
	MPT_Data01  UMETA(DisplayName = "Data01"),

	/*
	*Note Off				- Velocity
	*Note On				- Velocity
	*Note Aftertouch		- Amount
	*Continueous Change		- Amount
	*Program Change			- 0
	*Channel Aftertouch		- 0
	*Pitch Bend				- HighBit Value
	*/
	MPT_Data02  UMETA(DisplayName = "Data02")
}; 

UCLASS(Blueprintable, BlueprintType, ClassGroup = "AkMIDI")
class AKMIDI_API UAkMidiMessage : public UObject
{
	GENERATED_BODY()

public:
	UAkMidiMessage(const class FObjectInitializer &ObjectInitializer);

	// MIDI Note Type
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AkMIDI|AkMidiMessage")
	TEnumAsByte<EAkMessageType> NoteType;

	//MIDI Note Channel 0-16
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AkMIDI|AkMidiMessage")
	uint8 Channel;


	//MIDI Note Post On The Offset Of Samples Per Frame 0-127
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AkMIDI|AkMidiMessage")
	uint8 NoteOffset;

	/*
	*Note Off				- Note Value
	*Note On				- Note Value
	*Note Aftertouch		- Note Value
	*Continueous Change		- Change Type
	*Program Change			- Program Value
	*Channel Aftertouch		- Amount
	*Pitch Bend				- LowBit Value
	*/
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AkMIDI|AkMidiMessage")
	uint8 Data01;

	/*
	*Note Off				- Velocity
	*Note On				- Velocity
	*Note Aftertouch		- Amount
	*Continueous Change		- Amount
	*Program Change			- 0
	*Channel Aftertouch		- 0
	*Pitch Bend				- HighBit Value
	*/
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AkMIDI|AkMidiMessage")
	uint8 Data02;

public:

	void SetDirtyState(bool bIsDirty);

	bool GetDirtyState();

	void BackupMidiMessage();

	void RecoverMidiMessage();

protected:
#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent &PropertyChangedEvent) override;
#endif

	virtual void PostLoad() override;


private:
	bool bIsDirty;

	UAkMidiMessage* MidiMessageBackup;
};
