#include "Game/RallyGameMode.h"
#include "Game/RallyHUD.h"
#include "Game/RallyDayClock.h"
#include "Game/RallyQuestDirector.h"
#include "Game/EconomyRules.h"
#include "Game/RallyTeamState.h"
#include "Game/RallySaveGame.h"
#include "EngineUtils.h"
#include "Kismet/GameplayStatics.h"
#include "Damage/RallyEngineComponent.h"
#include "Game/RallyStageDirector.h"
#include "Misc/CommandLine.h"
#include "TimerManager.h"
#include "Player/RallyCharacter.h"
#include "Vehicle/RallyCarPawn.h"

ARallyGameMode::ARallyGameMode()
{
    DefaultPawnClass = ARallyCharacter::StaticClass();
    HUDClass = ARallyHUD::StaticClass();
}

void ARallyGameMode::Logout(AController* Exiting)
{
    // The default would destroy the leaving player's pawn, which for a driver is the team car.
    if (ARallyCarPawn* Car = Exiting ? Cast<ARallyCarPawn>(Exiting->GetPawn()) : nullptr)
    {
        Car->ServerDriverLeaving();
    }
    Super::Logout(Exiting);
}

void ARallyGameMode::StartPlay()
{
    Super::StartPlay();
    // One team car on the gravel road, 7 m ahead of the player starts. The crash test puts it in line
    // with the impact barrier (82 m, -14 m), 30 m short of it.
    FActorSpawnParameters Params;
    Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;
    const bool bCrashTest = FParse::Param(FCommandLine::Get(), TEXT("RallyCrashTest"));
    FVector CarStart = bCrashTest ? FVector(5000.0, -1400.0, 120.0) : FVector(1000.0, 0.0, 120.0);
    FRotator CarRotation = FRotator::ZeroRotator;
    // -RallyCarStart=X,Y,Z,Yaw (cm, degrees) puts it elsewhere, for tests on other maps.
    FString Start;
    if (FParse::Value(FCommandLine::Get(), TEXT("RallyCarStart="), Start, false))
    {
        TArray<FString> Values;
        Start.ParseIntoArray(Values, TEXT(","));
        if (Values.Num() >= 3)
        {
            CarStart = FVector(FCString::Atod(*Values[0]), FCString::Atod(*Values[1]), FCString::Atod(*Values[2]));
            CarRotation.Yaw = Values.Num() >= 4 ? FCString::Atod(*Values[3]) : 0.0;
        }
    }
    GetWorld()->SpawnActor<ARallyCarPawn>(ARallyCarPawn::StaticClass(), CarStart, CarRotation, Params);
    // The time of day, and the team's money and post (parcels left beside the car's starting place).
    GetWorld()->SpawnActor<ARallyDayClock>(ARallyDayClock::StaticClass(), FTransform::Identity, Params);
    if (ARallyTeamState* Team = GetWorld()->SpawnActor<ARallyTeamState>(ARallyTeamState::StaticClass(), FTransform::Identity, Params))
        Team->SetDeliveryPoint(CarStart + CarRotation.RotateVector(FVector(-300.0, -350.0, -70.0)));
    if (FParse::Param(FCommandLine::Get(), TEXT("RallyShopTest")))
    {
        FTimerHandle Handle;
        GetWorldTimerManager().SetTimer(Handle, [this] { RunShopTest(0); }, 3.0f, false);
    }
    // Special stages, on maps that have them.
    if (ARallyStageDirector::MapHasStages(GetWorld()))
    {
        GetWorld()->SpawnActor<ARallyStageDirector>(ARallyStageDirector::StaticClass(), FTransform::Identity, Params);
        // The first day's quests (docs/QUESTS.md).
        GetWorld()->SpawnActor<ARallyQuestDirector>(ARallyQuestDirector::StaticClass(), FTransform::Identity, Params);
    }
    if (FParse::Param(FCommandLine::Get(), TEXT("RallyDayTest")))
    {
        FTimerHandle Handle;
        GetWorldTimerManager().SetTimer(Handle, [this] { RunDayTest(0); }, 2.0f, false);
    }
    // The team as it was left, unless this is an automated run.
    FString SaveTest;
    FParse::Value(FCommandLine::Get(), TEXT("RallySaveTest="), SaveTest);
    if (SaveTest == TEXT("check") || (SaveTest.IsEmpty() && !FParse::Param(FCommandLine::Get(), TEXT("unattended"))))
        LoadTeam();
    if (!SaveTest.IsEmpty())
    {
        FTimerHandle Handle;
        const bool bWrite = SaveTest == TEXT("write");
        GetWorldTimerManager().SetTimer(Handle, [this, bWrite] { RunSaveTest(bWrite); }, 4.0f, false);
    }
    float QuitAfter = 0.0f;
    if (FParse::Value(FCommandLine::Get(), TEXT("RallyQuitAfter="), QuitAfter) && QuitAfter > 0.0f)
    {
        FTimerHandle Handle;
        GetWorldTimerManager().SetTimer(Handle, [] { FGenericPlatformMisc::RequestExit(false, TEXT("RallyQuitAfter")); }, QuitAfter, false);
    }
}

FString ARallyGameMode::SlotName()
{
    FString Slot = TEXT("RallyTeam");
    FParse::Value(FCommandLine::Get(), TEXT("RallySaveSlot="), Slot);
    return Slot;
}

bool ARallyGameMode::SaveTeam(const FString& Why)
{
    // Automated runs leave the player's save alone; only the save test writes.
    const bool bSaveTest = FString(FCommandLine::Get()).Contains(TEXT("RallySaveTest="));
    if (FParse::Param(FCommandLine::Get(), TEXT("unattended")) && !bSaveTest) return false;
    TActorIterator<ARallyCarPawn> It(GetWorld());
    if (!It) return false;
    ARallyCarPawn* Car = *It;
    URallySaveGame* Save = Cast<URallySaveGame>(UGameplayStatics::CreateSaveGameObject(URallySaveGame::StaticClass()));
    Save->Why = Why;
    Save->SavedAt = FDateTime::Now();
    Save->Map = GetWorld()->GetMapName();
    Save->CarTransform = Car->GetActorTransform();
    Save->BiasSteps = Car->GetBiasSteps();
    Save->Parts = Car->GetDamage()->GetConditions();
    Save->Panels = Car->GetPanels()->SaveConditions();
    Save->JackCorner = static_cast<int8>(Car->GetPanels()->GetJackCorner());
    Save->Dents = Car->GetBodyDeform()->GetDents();
    Save->Engine = Car->GetEngine()->SaveState();
    Save->LooseParts = Car->GetPanels()->SaveParts();
    if (const ARallyStageDirector* Director = ARallyStageDirector::Get(GetWorld()))
    {
        Save->Results = Director->GetResults();
        Save->StageAttempt = Director->GetRun().Attempt;
    }
    if (const ARallyDayClock* Clock = ARallyDayClock::Get(GetWorld()))
    {
        Save->Day = Clock->GetDay();
        Save->Hour = static_cast<float>(Clock->GetHour());
    }
    if (const ARallyTeamState* Team = ARallyTeamState::Get(GetWorld()))
    {
        Save->KittySk = Team->GetKittySk();
        Save->Orders = Team->GetOrders();
    }
    if (const ARallyQuestDirector* Quests = ARallyQuestDirector::Get(GetWorld())) Save->QuestStep = Quests->GetStep();
    const bool bSaved = UGameplayStatics::SaveGameToSlot(Save, SlotName(), 0);
    UE_LOG(LogTemp, Display, TEXT("RALLY_SAVE %s (%s): %d parts lying about, %d dents, %d stage results"), bSaved ? TEXT("saved") : TEXT("FAILED"),
        *Why, Save->LooseParts.Num(), Save->Dents.Num(), Save->Results.Num());
    return bSaved;
}

bool ARallyGameMode::LoadTeam()
{
    if (!UGameplayStatics::DoesSaveGameExist(SlotName(), 0)) return false;
    const URallySaveGame* Save = Cast<URallySaveGame>(UGameplayStatics::LoadGameFromSlot(SlotName(), 0));
    TActorIterator<ARallyCarPawn> It(GetWorld());
    if (!Save || !It) return false;
    if (Save->Map != GetWorld()->GetMapName())
    {
        UE_LOG(LogTemp, Display, TEXT("RALLY_SAVE saved on %s, not this map: the car is left as it is"), *Save->Map);
        return false;
    }
    ARallyCarPawn* Car = *It;
    Car->TeleportTo(Save->CarTransform.GetLocation(), Save->CarTransform.Rotator(), false, true);
    Car->GetDamage()->RestoreConditions(Save->Parts);
    Car->GetPanels()->Restore(Save->Panels, Save->JackCorner, Save->LooseParts);
    Car->GetBodyDeform()->ClearDents();
    for (const FRallyDent& Dent : Save->Dents) Car->GetBodyDeform()->AddDent(Dent);
    Car->GetEngine()->RestoreState(Save->Engine);
    Car->SetBiasSteps(Save->BiasSteps);
    if (ARallyStageDirector* Director = ARallyStageDirector::Get(GetWorld())) Director->RestoreResults(Save->Results, Save->StageAttempt);
    if (ARallyDayClock* Clock = ARallyDayClock::Get(GetWorld())) Clock->SetTime(Save->Day, Save->Hour);
    if (ARallyTeamState* Team = ARallyTeamState::Get(GetWorld())) Team->Restore(Save->KittySk, Save->Orders);
    if (ARallyQuestDirector* Quests = ARallyQuestDirector::Get(GetWorld())) Quests->SetStep(Save->QuestStep);
    Car->RefreshCondition();
    UE_LOG(LogTemp, Display, TEXT("RALLY_SAVE loaded (%s, %s): %d parts lying about, %d dents, %d stage results"), *Save->Why,
        *Save->SavedAt.ToString(), Save->LooseParts.Num(), Save->Dents.Num(), Save->Results.Num());
    return true;
}

void ARallyGameMode::RunSaveTest(bool bWrite)
{
    TActorIterator<ARallyCarPawn> It(GetWorld());
    if (!It) return;
    ARallyCarPawn* Car = *It;
    URallyPanelComponent* Panels = Car->GetPanels();
    const int32 Strut = URallyPanelComponent::GearSlot(URallyPanelComponent::EGear::Strut, 1);
    const int32 Turbo = URallyPanelComponent::ModuleSlot(URallyPanelComponent::EModule::Turbo);
    if (bWrite)
    {
        // A known state: a dented front, the front bumper off and lying beside the car, a wheel bolt out, a
        // rally strut set hard, the turbo wound up and damaged, the radiator damaged and the coolant low,
        // the car moved.
        Car->GetDamage()->ApplyPartDamage(ERallyPart::Radiator, 0.4f);
        Car->GetDamage()->ApplyPartDamage(ERallyPart::Turbo, 0.25f);
        FRallyDent Dent;
        Dent.Point = FVector(200, 30, 10); Dent.Extent = FVector(12); Dent.Direction = FVector(-1, 0, 0);
        Dent.Depth = 9; Dent.Radius = 40; Dent.PartsMask = Panels->AttachedDentMask();
        Car->GetBodyDeform()->AddDent(Dent);
        Panels->DebugDetach(7);
        Panels->DebugSetFixingOut(URallyPanelComponent::FirstWheel + 1, 2);
        Panels->DebugSetGrade(Strut, 3, 7, -2);
        Panels->DebugSetGrade(Turbo, 1, 11, 0);
        Car->GetEngine()->DebugSetCoolant(3.0);
        Car->TeleportTo(Car->GetActorLocation() + FVector(300, 200, 0), Car->GetActorRotation() + FRotator(0, 30, 0), false, true);
        FTimerHandle Handle;
        GetWorldTimerManager().SetTimer(Handle, [this] {
            const bool bSaved = SaveTeam(TEXT("save test"));
            UE_LOG(LogTemp, Display, TEXT("RALLY_SAVETEST_WRITE %s"), bSaved ? TEXT("done") : TEXT("FAILED"));
            FGenericPlatformMisc::RequestExit(false, TEXT("RallySaveTest"));
        }, 3.0f, false);
        return;
    }
    // Compare with what the write run left.
    TArray<FString> Fails;
    const auto Expect = [&Fails](bool bOk, const TCHAR* What) { if (!bOk) Fails.Add(What); };
    FRallyPartCondition Radiator, TurboHealth;
    Car->GetDamage()->GetPartCondition(ERallyPart::Radiator, Radiator);
    Car->GetDamage()->GetPartCondition(ERallyPart::Turbo, TurboHealth);
    Expect(FMath::IsNearlyEqual(Radiator.Health, 0.6f, 0.01f) && FMath::IsNearlyEqual(TurboHealth.Health, 0.75f, 0.01f), TEXT("part health"));
    Expect(Panels->IsDetached(7), TEXT("front bumper still off"));
    bool bBumperLying = false;
    for (TActorIterator<ARallyDebrisActor> Part(GetWorld()); Part; ++Part)
        if (Part->GetSourceCar() == Car && Part->GetPanel() == 7 && Panels->GetDebris(7) == *Part) bBumperLying = true;
    Expect(bBumperLying, TEXT("front bumper lying about, linked to its place"));
    Expect(Panels->IsFixingOut(URallyPanelComponent::FirstWheel + 1, 2) && Panels->CountOut(URallyPanelComponent::FirstWheel + 1) == 1, TEXT("wheel bolt out"));
    Expect(Panels->GetGrade(Strut) == Rally::Grade::EGrade::Rally && Panels->GetClicks(Strut) == 7 && Panels->GetCollarCm(Strut) == -2, TEXT("rally strut settings"));
    Expect(Panels->GetBoostTenths(Turbo) == 11, TEXT("turbo boost"));
    Expect(FMath::IsNearlyEqual(Car->GetEngine()->GetCoolantL(), 3.0, 0.15), TEXT("coolant"));
    Expect(Car->GetBodyDeform()->GetDents().Num() >= 1, TEXT("dent"));
    int32 Spares = 0;
    for (TActorIterator<ARallyDebrisActor> Part(GetWorld()); Part; ++Part)
        if (Part->GetSourceCar() == Car && Part->GetMount() == ERallyPartMount::Stowed) ++Spares;
    Expect(Spares >= 10, TEXT("boot kit stowed"));
    UE_LOG(LogTemp, Display, TEXT("RALLY_SAVETEST_CHECK %s %s spares=%d coolant=%.1f"), Fails.IsEmpty() ? TEXT("PASS") : TEXT("FAIL"),
        *FString::Join(Fails, TEXT(", ")), Spares, Car->GetEngine()->GetCoolantL());
    FGenericPlatformMisc::RequestExit(false, TEXT("RallySaveTest"));
}

void ARallyGameMode::TrySleep()
{
    // Everyone in the world's seats and asleep, nobody left standing about.
    int32 Players = 0, Asleep = 0;
    for (TActorIterator<ARallyCharacter> It(GetWorld()); It; ++It)
    {
        ++Players;
        Asleep += It->IsSleeping() ? 1 : 0;
    }
    UE_LOG(LogTemp, Display, TEXT("RALLY_DAY %d of %d asleep"), Asleep, Players);
    if (Players == 0 || Asleep < Players) return;
    ARallyDayClock* Clock = ARallyDayClock::Get(GetWorld());
    if (!Clock) return;
    Clock->SleepUntilMorning();
    for (TActorIterator<ARallyCharacter> It(GetWorld()); It; ++It) It->SetSleeping(false);
    SaveTeam(FString::Printf(TEXT("slept, %s"), *Clock->Describe()));
}

void ARallyGameMode::RunDayTest(int32 Step)
{
    ARallyDayClock* Clock = ARallyDayClock::Get(GetWorld());
    TActorIterator<ARallyCarPawn> It(GetWorld());
    if (!Clock || !It) return;
    ARallyCarPawn* Car = *It;
    const auto Next = [this](int32 NextStep, float Delay) {
        FTimerHandle Handle;
        GetWorldTimerManager().SetTimer(Handle, [this, NextStep] { RunDayTest(NextStep); }, Delay, false);
    };
    switch (Step)
    {
    case 0:
        // Late evening; the driver walks to the car meanwhile.
        Clock->SetTime(1, 21.5);
        UE_LOG(LogTemp, Display, TEXT("RALLY_DAYTEST evening %s"), *Clock->Describe());
        Next(1, 12.0f);
        break;
    case 1:
        if (Car->IsSeatFree(0)) { Next(1, 2.0f); break; }
        Car->SetLights(true);
        Next(2, 2.0f);
        break;
    case 2:
        FScreenshotRequest::RequestScreenshot(TEXT("RallyDay_night.png"), true, false);
        Next(3, 2.0f);
        break;
    case 3:
        // Everyone asleep in the car: the night passes and the team is saved.
        for (TActorIterator<ARallyCharacter> Who(GetWorld()); Who; ++Who) Who->SetSleeping(true);
        TrySleep();
        Car->SetLights(false);
        Next(4, 3.0f);
        break;
    case 4:
    {
        FScreenshotRequest::RequestScreenshot(TEXT("RallyDay_morning.png"), true, false);
        DayTestMorning = Clock->GetDay() == 2 && Clock->GetHour() >= ARallyDayClock::WakeHour && Clock->GetHour() < ARallyDayClock::WakeHour + 0.2;
        UE_LOG(LogTemp, Display, TEXT("RALLY_DAYTEST morning %s %s"), DayTestMorning ? TEXT("ok") : TEXT("WRONG"), *Clock->Describe());
        // Before dawn, out of the car: the cold sets in.
        Clock->SetTime(Clock->GetDay(), 4.0);
        if (ARallyCharacter* Driver = Car->GetDriverCharacter())
        {
            Car->ServerExitSeat(Driver, true);
            // A bottle and a carton at their feet, for later.
            DayTestBottle = Car->GetPanels()->DebugSpawnPart(URallyPanelComponent::DrinkSlot, 1, Driver->GetActorLocation() + Driver->GetActorForwardVector() * 60.0);
            DayTestCarton = Car->GetPanels()->DebugSpawnPart(URallyPanelComponent::SmokeSlot, 1, Driver->GetActorLocation() - Driver->GetActorForwardVector() * 60.0);
        }
        Next(5, 20.0f);
        break;
    }
    default:
    {
        // Cold after twenty seconds outside; then hurt past bearing: hospital, the service yard, the bill.
        TActorIterator<ARallyCharacter> Who(GetWorld());
        ARallyTeamState* Team = ARallyTeamState::Get(GetWorld());
        bool bCold = false, bHospital = false;
        if (Who && Team)
        {
            bCold = Who->GetCold() > 0.01 && !Who->GetSeatedCar();
            // A swig of slivovica and a cigarette: drunker, warmer, calm.
            const double ColdBefore = Who->GetCold();
            bool bVices = false;
            if (ARallyDebrisActor* Bottle = DayTestBottle.Get())
            {
                Bottle->PickUp(*Who);
                bVices = Who->UseCarriedVice() && FMath::IsNearlyEqual(Who->GetDrunk(), 0.25, 0.01) && Who->GetCold() < ColdBefore;
                Bottle->Drop();
            }
            if (ARallyDebrisActor* Carton = DayTestCarton.Get())
            {
                Carton->PickUp(*Who);
                bVices = bVices && Who->UseCarriedVice() && Who->GetCalm() > 0.9;
                Carton->Drop();
            }
            UE_LOG(LogTemp, Display, TEXT("RALLY_DAYTEST vices=%d drunk=%.2f calm=%.2f"), bVices ? 1 : 0, Who->GetDrunk(), Who->GetCalm());
            bCold = bCold && bVices;
            const int32 Kitty = Team->GetKittySk();
            Who->AddInjury(1.0);
            bHospital = FMath::IsNearlyEqual(Who->GetInjury(), 0.25, 0.01) && Team->GetKittySk() == Kitty - 3000 && Team->InServiceYard(*Who);
            UE_LOG(LogTemp, Display, TEXT("RALLY_DAYTEST cold=%.3f hospital=%d"), Who->GetCold(), bHospital ? 1 : 0);
        }
        const bool bPass = DayTestMorning && bCold && bHospital;
        UE_LOG(LogTemp, Display, TEXT("RALLY_DAYTEST_%s %s"), bPass ? TEXT("PASS") : TEXT("FAIL"), *Clock->Describe());
        FTimerHandle Handle;
        GetWorldTimerManager().SetTimer(Handle, [] { FGenericPlatformMisc::RequestExit(false, TEXT("RallyDayTest")); }, 1.0f, false);
        break;
    }
    }
}

void ARallyGameMode::RunShopTest(int32 Step)
{
    ARallyTeamState* Team = ARallyTeamState::Get(GetWorld());
    ARallyDayClock* Clock = ARallyDayClock::Get(GetWorld());
    if (!Team || !Clock) return;
    const auto Next = [this](int32 NextStep) {
        FTimerHandle Handle;
        GetWorldTimerManager().SetTimer(Handle, [this, NextStep] { RunShopTest(NextStep); }, 2.0f, false);
    };
    const auto Finish = [](bool bPass, const FString& Why) {
        UE_LOG(LogTemp, Display, TEXT("RALLY_SHOPTEST_%s %s"), bPass ? TEXT("PASS") : TEXT("FAIL"), *Why);
        FGenericPlatformMisc::RequestExit(false, TEXT("RallyShopTest"));
    };
    // The rally front strut is the third line; the rally turbo cannot be afforded after it.
    const int32 RallyStrut = 2, RallyTurbo = 11;
    if (Step == 0)
    {
        const int32 Before = Team->GetKittySk();
        if (!Team->Order(RallyStrut) || Team->GetKittySk() != Before - Rally::Economy::Catalogue()[RallyStrut].PriceSk) { Finish(false, TEXT("order not paid")); return; }
        if (Team->Order(RallyTurbo)) { Finish(false, TEXT("ordered beyond the kitty")); return; }
        // The next morning's post.
        Clock->SetTime(Clock->GetDay() + 1, 8.5);
        Next(1);
        return;
    }
    bool bDelivered = false;
    for (TActorIterator<ARallyDebrisActor> It(GetWorld()); It; ++It)
        if (It->GetPanel() == URallyPanelComponent::SlotForKind(TEXT("strut")) && It->GetGrade() == 3 && It->GetMount() == ERallyPartMount::Loose) bDelivered = true;
    const int32 Before = Team->GetKittySk();
    Team->AddMoney(Rally::Economy::PrizeSk(1, false), TEXT("test stage win"));
    // A damaged radiator, the car still in the service yard: the workshop fits a new one, 2 500 Sk and half an hour.
    TActorIterator<ARallyCarPawn> CarIt(GetWorld());
    bool bWorkshop = false;
    if (CarIt)
    {
        CarIt->GetDamage()->ApplyPartDamage(ERallyPart::Radiator, 0.5f);
        CarIt->GetPanels()->DebugDetach(4);
        const double HourBefore = Clock->GetHour();
        const int32 KittyBefore = Team->GetKittySk();
        bWorkshop = CarIt->GetPanels()->WorkshopRepair(nullptr, URallyPanelComponent::RadiatorSlot);
        FRallyPartCondition Radiator;
        CarIt->GetDamage()->GetPartCondition(ERallyPart::Radiator, Radiator);
        bWorkshop = bWorkshop && Radiator.Health >= 1.0f && Team->GetKittySk() == KittyBefore - 2500 && Clock->GetHour() > HourBefore + 0.45;
    }
    // The delivered strut into the boot (the lid opened): the car is that much heavier.
    bool bStowed = false;
    if (CarIt)
    {
        URallyPanelComponent* Panels = CarIt->GetPanels();
        Panels->DebugSetOpen(5, true);
        for (TActorIterator<ARallyDebrisActor> It(GetWorld()); It; ++It)
            if (It->GetPanel() == URallyPanelComponent::SlotForKind(TEXT("strut")) && It->GetGrade() == 3 && It->GetMount() == ERallyPartMount::Loose)
            {
                bStowed = Panels->Stow(nullptr, *It) && FMath::IsNearlyEqual(Panels->GetBootLoadKg() - Panels->GetBootKitKg(), It->GetMassKg(), 0.1);
                break;
            }
    }
    // Two steps of brake bias forward: the front brakes take 66.5 percent.
    bool bBias = false;
    if (CarIt)
    {
        CarIt->SetBiasSteps(2);
        bBias = CarIt->GetBiasSteps() == 2;
    }
    // Taken 100 m out, then towed back: 2 000 Sk.
    bool bTowed = false;
    if (CarIt)
    {
        CarIt->TeleportTo(CarIt->GetActorLocation() + FVector(10000.0, 0.0, 0.0), CarIt->GetActorRotation(), false, true);
        bTowed = Tow(*CarIt) && Team->InServiceYard(*CarIt);
    }
    Finish(bDelivered && bWorkshop && bTowed && bStowed && bBias && Team->GetOrders().IsEmpty() && Team->GetKittySk() == Before + 6000 - 2500 - 2000,
        FString::Printf(TEXT("delivered=%d workshop=%d towed=%d stowed=%d orders=%d kitty=%d"), bDelivered ? 1 : 0, bWorkshop ? 1 : 0, bTowed ? 1 : 0,
            bStowed ? 1 : 0,
            Team->GetOrders().Num(), Team->GetKittySk()));
}

bool ARallyGameMode::Tow(ARallyCarPawn* Car)
{
    ARallyTeamState* Team = ARallyTeamState::Get(GetWorld());
    if (!Car || !Team || Team->InServiceYard(Car)) return false;
    if (Team->GetKittySk() < Rally::Economy::TowSk)
    {
        UE_LOG(LogTemp, Display, TEXT("RALLY_TOW the kitty cannot pay for a tow (%d Sk)"), Rally::Economy::TowSk);
        return false;
    }
    if (ARallyStageDirector* Stages = ARallyStageDirector::Get(GetWorld())) Stages->RetireRun(TEXT("towed"));
    Team->AddMoney(-Rally::Economy::TowSk, TEXT("tow to the service yard"));
    if (ARallyDayClock* Clock = ARallyDayClock::Get(GetWorld()))
    {
        const double Hour = Clock->GetHour() + Rally::Economy::TowHours;
        Clock->SetTime(Clock->GetDay() + (Hour >= 24.0 ? 1 : 0), FMath::Fmod(Hour, 24.0));
    }
    // Set down in the yard as it is, crew aboard, facing as it stood at the start.
    const FVector Yard = Team->GetServiceYard() + FVector(300.0, 350.0, 120.0);
    Car->TeleportTo(Yard, FRotator(0.0, Car->GetActorRotation().Yaw, 0.0), false, true);
    UE_LOG(LogTemp, Display, TEXT("RALLY_TOW towed to the service yard"));
    return true;
}
