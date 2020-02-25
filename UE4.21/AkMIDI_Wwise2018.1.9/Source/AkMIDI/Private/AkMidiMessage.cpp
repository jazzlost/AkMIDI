// Fill out your copyright notice in the Description page of Project Settings.

#include "AkMidiMessage.h"

UAkMidiMessage::UAkMidiMessage(const class FObjectInitializer &ObjectInitializer) : Super(ObjectInitializer),
NoteType(EAkMessageType::AMT_Note_On), Channel(0), NoteOffset(0), Data01(60), Data02(72), bIsDirty(false), MidiMessageBackup(nullptr)
{

}

void UAkMidiMessage::PostLoad()
{
	Super::PostLoad();
	MidiMessageBackup = NewObject<UAkMidiMessage>();
	BackupMidiMessage();

	return;
}



#if WITH_EDITOR
void UAkMidiMessage::PostEditChangeProperty(FPropertyChangedEvent &PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	BackupMidiMessage();

	SetDirtyState(true);
}
#endif

void UAkMidiMessage::SetDirtyState(bool bIsDirty)
{
	bIsDirty = bIsDirty;
}

bool UAkMidiMessage::GetDirtyState()
{
	return bIsDirty;
}

void UAkMidiMessage::BackupMidiMessage()
{
	if (MidiMessageBackup == nullptr)
		return;

	MidiMessageBackup->NoteType = (EAkMessageType)this->NoteType;
	MidiMessageBackup->Channel = this->Channel;
	MidiMessageBackup->NoteOffset = this->NoteOffset;
	MidiMessageBackup->Data01 = this->Data01;
	MidiMessageBackup->Data02 = this->Data02;

	return;
}

void UAkMidiMessage::RecoverMidiMessage()
{
	if (MidiMessageBackup == nullptr)
		return;

	this->NoteType = (EAkMessageType)MidiMessageBackup->NoteType;
	this->Channel = MidiMessageBackup->Channel;
	this->NoteOffset = MidiMessageBackup->NoteOffset;
	this->Data01 = MidiMessageBackup->Data01;
	this->Data02 = MidiMessageBackup->Data02;

	return;
}
