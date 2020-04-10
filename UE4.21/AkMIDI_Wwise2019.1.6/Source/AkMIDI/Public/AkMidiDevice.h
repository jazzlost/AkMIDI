#pragma once

#include "CoreMinimal.h"
#include "UObject/NoExportTypes.h"
#include "RtMidi.h"
#include "AkMidiMessage.h"
#include "Containers/Queue.h"
#include "AkMidiDevice.generated.h"



USTRUCT(BlueprintType)
struct FMidiDevice
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "AkMIDI|Device")
    FString Name;
	
	UPROPERTY(BlueprintReadOnly, Category = "AkMIDI|Device")
	uint8 Port;

	FMidiDevice()
    {
        this->Name = "Wwise";
        this->Port = 127;
    }

	FMidiDevice(FString name, uint8 port)
    {
        this->Name = name;
        this->Port = port;
    }
};

UENUM(BlueprintType)
enum class EIOType : uint8
{
	IO_Input   UMETA(DisplayName = "Input"),
	IO_Output  UMETA(DisplayName = "Output"),
	IO_Both    UMETA(DisplayName = "Input&Output")
};



UCLASS(Blueprintable,BlueprintType, ClassGroup = "AkMIDI")
class AKMIDI_API UAkMidiDevice : public UObject
{
	GENERATED_BODY()

public:
	UAkMidiDevice(const class FObjectInitializer &ObjectInitializer);
	~UAkMidiDevice();

    void GetMidiDevice(TArray<FMidiDevice> &InputDevice, TArray<FMidiDevice> &OutputDevices);
	void OpenInput(uint8 Port = 0);
	void CloseInput();
    void OpenOutput(uint8 Port = 0);
    void CloseOutput();

    void SendMidiMessage(const unsigned char *Message, size_t size);
    void SendRawMidiMessage(const TArray<uint8> &Message);

	RtMidiIn* GetRtMidiIn() { return MidiIn; }
	RtMidiOut* GetRtMidiOut() { return MidiOut; }


private:

	RtMidiIn* MidiIn;
	RtMidiOut* MidiOut;
};
