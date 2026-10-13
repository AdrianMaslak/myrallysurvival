#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Info.h"
#include "Game/StageRules.h"
#include <vector>
#include "RallyStageDirector.generated.h"

class ARallyCarPawn;

// A stage as the map exports it (Private/Game/CountrysideStages.inl, scripts/export_stage_routes.py).
struct FRallyStageSource
{
    const TCHAR* Id;
    const TCHAR* Name;
    const TCHAR* Surface;
    double WidthM;
    const double (*Points)[3];
    int32 Count;
};

// The run in progress on the team car (docs/STAGES.md). Times are the server's world time.
USTRUCT()
struct FRallyStageRun
{
    GENERATED_BODY()
    UPROPERTY() int8 Stage = -1;
    UPROPERTY() uint8 Phase = 0;
    UPROPERTY() float CountdownStart = 0.0f;
    UPROPERTY() float StartTime = 0.0f;
    UPROPERTY() float EndTime = 0.0f;
    UPROPERTY() float PenaltyS = 0.0f;
    // Counts every start, so rivals' times differ from one attempt to the next.
    UPROPERTY() uint8 Attempt = 0;
};

USTRUCT()
struct FRallyStageResult
{
    GENERATED_BODY()
    UPROPERTY() int8 Stage = -1;
    // Clock time plus penalties; a retired car gets the stage's maximum time.
    UPROPERTY() float TimeS = 0.0f;
    UPROPERTY() float PenaltyS = 0.0f;
    UPROPERTY() bool bRetired = false;
    UPROPERTY() uint8 Attempt = 0;
};

// Special stages on maps that have them: stops at the start line, counts down, times the team car to the
// flying finish (or closes the stage on it at the maximum time), and keeps the results. The server runs
// it; every machine reads the replicated run and works out the clock, the pace notes and the rivals.
UCLASS()
class RALLYSURVIVAL_API ARallyStageDirector : public AInfo
{
    GENERATED_BODY()

public:
    enum class EPhase : uint8 { Idle, Countdown, Running, Finished, Retired };

    ARallyStageDirector();
    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
    virtual void Tick(float DeltaSeconds) override;

    struct FStage
    {
        FString Id, Name, Surface;
        double WidthM = 6.0;
        std::vector<Rally::Stage::FPoint> Route;
        std::vector<double> Distance;
        // Unsigned curvature at each sample (1/m), for whoever drives it.
        std::vector<double> Curvature;
        std::vector<Rally::Stage::FCall> Calls;
        double LengthM = 0.0;
        double ParS = 0.0;
    };
    static ARallyStageDirector* Get(const UWorld* World);
    // Whether this map has stages the director knows.
    static bool MapHasStages(const UWorld* World);
    int32 NumStages() const { return Stages.Num(); }
    const FStage& GetStage(int32 Index) const { return Stages[Index]; }
    const FRallyStageRun& GetRun() const { return Run; }
    EPhase GetPhase() const { return static_cast<EPhase>(Run.Phase); }
    const TArray<FRallyStageResult>& GetResults() const { return Results; }
    // The server's world time on this machine.
    double ServerNow() const;
    // On the clock now: counting up while running, the final time once finished.
    double GetClock() const;
    // Whole seconds left in the countdown (5 .. 1), 0 at the start.
    int32 GetCountdown() const;
    // Metres along a stage's route nearest a world location, searching onwards from Hint (a sample index,
    // updated).
    double Progress(int32 Stage, const FVector& World, int32& InOutHint) const;
    // A point Ahead metres further along the route from Along (world, cm).
    FVector PointAt(int32 Stage, double AlongM) const;
    // How far behind (negative) or past a stage's start or finish line a world location is (m), and how far
    // to the side of the road's centre.
    double PastLine(int32 Stage, bool bFinish, const FVector& World, double& OutSideM) const;
    // Seconds the team took and the rivals' times on a result's attempt: each crew with time and whether
    // retired, fastest first; the team is the crew named "You".
    struct FStanding { FString Crew; double TimeS; bool bRetired; };
    TArray<FStanding> Standings(const FRallyStageResult& Result) const;

private:
    void LoadStages();
    void TickServer(float DeltaSeconds);
    ARallyCarPawn* FindCar() const;
    void SetPhase(EPhase Phase);
    TArray<FStage> Stages;
    UPROPERTY(Replicated) FRallyStageRun Run;
    UPROPERTY(Replicated) TArray<FRallyStageResult> Results;
    // Server: how long the car has stood still behind a start line, and where it is along the route.
    double StagedFor = 0.0;
    int32 StagedStage = INDEX_NONE;
    int32 RunHint = 0;
    bool bJumped = false;
};
