// Fill out your copyright notice in the Description page of Project Settings.

#include "AkMidiFunctionLibrary.h"

UAkMidiFunctionLibrary::UAkMidiFunctionLibrary(FObjectInitializer const& ObjectInitializer) : Super(ObjectInitializer)
{
}


UAkMidiMessage* UAkMidiFunctionLibrary::CreateAkMidiMessage(
	TEnumAsByte<EAkMessageType> NoteType,
	uint8 Channel,
	uint8 NoteOffset,
	uint8 Data01,
	uint8 Data02)
{
	UAkMidiMessage* MidiMessage = NewObject<UAkMidiMessage>();
	if (MidiMessage)
	{
		MidiMessage->NoteType = NoteType;
		MidiMessage->Channel = Channel;
		MidiMessage->NoteOffset = NoteOffset;
		MidiMessage->Data01 = Data01;
		MidiMessage->Data02 = Data02;
	}

	return MidiMessage;
}

bool UAkMidiFunctionLibrary::PostMidiEvent(
	UAkMidiComponent* MidiComponent,
	TArray<UAkMidiMessage*> AkMidiMessages,
	UAkAudioEvent *AkEvent)
{
	if (MidiComponent == nullptr || AkMidiMessages.Num() <= 0)
		return false;

	return MidiComponent->PostMidiEvent(AkMidiMessages, AkEvent);
}

bool UAkMidiFunctionLibrary::StopMidiEvent(
	UAkMidiComponent* MidiComponent,
	UAkAudioEvent *AkEvent)
{
	if (MidiComponent == nullptr)
		return false;

	MidiComponent->StopMidiEvent(AkEvent);

	return true;
}

bool UAkMidiFunctionLibrary::OpenMidiDevice(
	UAkMidiComponent* MidiComponent,
	EIOType PortType,
	uint8 Port)
{
	if (MidiComponent == nullptr)
		return false;

	if(PortType == EIOType::IO_Input)
		MidiComponent->OpenMidiInputDevice(Port);
	else if(PortType == EIOType::IO_Output)
		MidiComponent->OpenMidiOutputDevice(Port);
	else
	{
		MidiComponent->OpenMidiInputDevice(Port);
		MidiComponent->OpenMidiOutputDevice(Port);
	}

	return true;
}

bool UAkMidiFunctionLibrary::CloseMidiDevice(UAkMidiComponent* MidiComponent, EIOType ClosePort)
{
	if (MidiComponent == nullptr)
		return false;

	MidiComponent->CloseMidiDevice(ClosePort);

	return true;
}

void UAkMidiFunctionLibrary::ModifyAkMidiMessage(UAkMidiMessage* MidiMessage, TEnumAsByte<EMessageParamType> NoteValueType, uint8 value)
{
	if (MidiMessage == nullptr)
		return;

	switch (NoteValueType)
	{
	case EMessageParamType::MPT_Channel:
		MidiMessage->Channel = value;
		break;
	case EMessageParamType::MPT_NoteOffset:
		MidiMessage->NoteOffset = value;
		break;
	case EMessageParamType::MPT_Data01:
		MidiMessage->Data01 = value;
		break;
	case EMessageParamType::MPT_Data02:
		MidiMessage->Data02 = value;
		break;
	default:
		MidiMessage->Data01 = value;
	}

	MidiMessage->BackupMidiMessage();

	return;
}