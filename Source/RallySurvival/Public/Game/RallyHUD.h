#pragma once

#include "CoreMinimal.h"
#include "GameFramework/HUD.h"
#include "RallyHUD.generated.h"

// A dot in the middle of the screen while on foot, so players see what they are pointing at; it turns
// yellow and grows when what is in view can be used (the prompt line says how).
UCLASS()
class RALLYSURVIVAL_API ARallyHUD : public AHUD
{
    GENERATED_BODY()

public:
    virtual void DrawHUD() override;
    virtual void Tick(float DeltaSeconds) override;
    // The controls card, on F1. The first steps get a one-line hint instead (FirstStepsHint).
    void ToggleHelp() { bHelp = !bHelp; }
    // The cup's standings on screen (J).
    void ToggleCup() { bCup = !bCup; }
    // Puts the letter on screen away (Enter).
    void DismissLetter();

private:
    void DrawNetwork();
    bool bNetwork = false;
    double NetLastFrame = 0.0;
    double NetElapsed = 0.0;
    double NetPeakFrame = 0.0;
    int32 NetFrames = 0;
    TArray<FString> NetLines;
    // Special stages (ARallyStageDirector): what the stage is near its start, the countdown, the clock and
    // pace notes while running, the results at the end.
    void DrawStage();
    void DrawLine(const FString& Text, float CentreX, float Y, float Size, const FLinearColor& Color);
    int32 PaceHint = 0;
    void DrawHelp();
    bool bHelp = false;
    // Until the car reaches the first stage's start: the keys for what the player is doing now.
    FString FirstStepsHint() const;
    // What is under the crosshair, the tool in hand, and the driver's readout.
    void DrawPrompts(float Ui);
    void DrawLetter(const FString& Text, float Ui);
    float DismissedLetter = -1.0f;
    bool bCup = false;
    void DrawCup();
    // -RallyStartShots: photographs the first minute of play as a new player sees it, then quits.
    int32 StartShot = 0;
    int32 StartRecording = 0;
    void TickStartShots();
};
