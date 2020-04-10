// Fill out your copyright notice in the Description page of Project Settings.

#include "AkMidiFxLibrary.h"
#include "Math/UnrealMathUtility.h"

UAkMidiMessage* UAkMidiFxLibrary::Octave(UAkMidiMessage* MidiMessage, int Octave)
{
	if (MidiMessage == nullptr)
		return MidiMessage;

	FMath::Clamp<int>(Octave, -12, 12);

	if (MidiMessage->NoteType == EAkMessageType::AMT_Note_On)
	{
		MidiMessage->Data01 += Octave * 12;
	}
	else
	{
		return MidiMessage;
	}

	return MidiMessage;

}

UAkMidiMessage* UAkMidiFxLibrary::VelocityCompression(UAkMidiMessage* MidiMessage, uint8 LowVelocity, uint8 HighVelocity)
{
	if (MidiMessage == nullptr)
		return MidiMessage;

	if (LowVelocity >= HighVelocity)
		return MidiMessage;

	if (MidiMessage->NoteType == EAkMessageType::AMT_Note_On || MidiMessage->NoteType == EAkMessageType::AMT_Note_Off)
	{
		uint8 NewVelocity = FMath::Clamp<uint8>(MidiMessage->Data02, LowVelocity, HighVelocity);
		MidiMessage->Data02 = NewVelocity;
		return MidiMessage;
	}
	else
	{
		return MidiMessage;
	}

}
