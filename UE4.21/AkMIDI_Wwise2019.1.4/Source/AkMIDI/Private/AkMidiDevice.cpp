// Fill out your copyright notice in the Description page of Project Settings.
#include "AkMidiDevice.h"



UAkMidiDevice::UAkMidiDevice(const class FObjectInitializer &ObjectInitializer):
	Super(ObjectInitializer)
{

}

UAkMidiDevice::~UAkMidiDevice()
{
	if (MidiIn != nullptr)
	{
		delete MidiIn;
		MidiIn = nullptr;
	}

	if (MidiOut != nullptr)
	{
		delete MidiOut;
		MidiOut = nullptr;
	}
}


void UAkMidiDevice::GetMidiDevice(TArray<FMidiDevice> &InputDevice, TArray<FMidiDevice> &OutputDevices)
{
	MidiIn = new RtMidiIn();
	MidiOut = new RtMidiOut();

	uint8 size = MidiIn->getPortCount();
	for (int i = 0; i < size; i++) 
	{
		FString name = FString(MidiIn->getPortName(i).c_str());
		uint8 port = i;
		InputDevice.Add(FMidiDevice(name, port));
	}

	size = MidiOut->getPortCount();
	for (int i = 0; i < size; i++) 
	{
		FString name = FString(MidiOut->getPortName(i).c_str());
		uint8 port = i;
		OutputDevices.Add(FMidiDevice(name, port));
	}
}

void UAkMidiDevice::OpenInput(uint8 Port)
{
	if (MidiIn == nullptr)
		return;

	MidiIn->openPort(Port);
}

void UAkMidiDevice::CloseInput()
{
	if (MidiIn == nullptr)
		return;

	if(MidiIn->isPortOpen())
		MidiIn->closePort();

	return;
}

void UAkMidiDevice::OpenOutput(uint8 Port)
{
	if (MidiOut == nullptr)
		return;

	MidiOut->openPort(Port);
}

void UAkMidiDevice::CloseOutput()
{
	if (MidiOut == nullptr)
		return;

	if(MidiOut->isPortOpen())
		MidiOut->closePort();

	return;
}

void UAkMidiDevice::SendMidiMessage(const unsigned char *Message, size_t size)
{

}

void UAkMidiDevice::SendRawMidiMessage(const TArray<uint8> &Message)
{

}
