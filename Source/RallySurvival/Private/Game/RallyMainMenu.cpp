#include "Game/RallyMainMenu.h"
#include "Game/RallySessionSubsystem.h"
#include "Engine/GameInstance.h"
#include "Engine/GameViewportClient.h"
#include "GameFramework/PlayerController.h"
#include "HAL/FileManager.h"
#include "Kismet/GameplayStatics.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Misc/CommandLine.h"
#include "Misc/DateTime.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Styling/CoreStyle.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

namespace
{
    TSharedPtr<SWidget> Menu;

    FString SavePath()
    {
        // The game mode's slot (RallyGameMode.cpp), including the save test's override.
        FString Slot = TEXT("RallyTeam");
        FParse::Value(FCommandLine::Get(), TEXT("RallySaveSlot="), Slot);
        return FPaths::ProjectSavedDir() / TEXT("SaveGames") / (Slot + TEXT(".sav"));
    }

    void SetMenuInput(UGameInstance* GameInstance, bool bMenu)
    {
        APlayerController* Player = GameInstance ? GameInstance->GetFirstLocalPlayerController() : nullptr;
        if (!Player) return;
        Player->SetShowMouseCursor(bMenu);
        if (bMenu && Menu.IsValid())
        {
            FInputModeUIOnly Mode;
            Mode.SetWidgetToFocus(Menu);
            Player->SetInputMode(Mode);
        }
        else Player->SetInputMode(FInputModeGameOnly());
    }

    void Play(UGameInstance* GameInstance, bool bFresh)
    {
        if (bFresh) RallyMainMenu::MoveSaveAside();
        RallyMainMenu::Hide(GameInstance);
        URallySessionSubsystem* Session = GameInstance->GetSubsystem<URallySessionSubsystem>();
        // Without Steam the game still plays, alone.
        if (Session && Session->IsSteamOn()) Session->HostSession();
        else UGameplayStatics::OpenLevel(GameInstance, TEXT("/Game/Maps/Countryside/L_Countryside"), true);
    }

    TSharedRef<SWidget> Label(const FString& Text, int32 Size, const FLinearColor& Colour = FLinearColor::White)
    {
        return SNew(STextBlock).Text(FText::FromString(Text)).Font(FCoreStyle::GetDefaultFontStyle("Regular", Size))
            .ColorAndOpacity(Colour).AutoWrapText(true);
    }

    TSharedRef<SWidget> Button(const FString& Text, TFunction<void()> OnClick)
    {
        return SNew(SButton).HAlign(HAlign_Center).ContentPadding(FMargin(24.0f, 8.0f))
            .OnClicked_Lambda([OnClick]() { OnClick(); return FReply::Handled(); })
            [Label(Text, 18)];
    }
}

void RallyMainMenu::Show(UGameInstance* GameInstance, const FString& Reason)
{
    UGameViewportClient* Viewport = GameInstance ? GameInstance->GetGameViewportClient() : nullptr;
    if (!Viewport) return;
    if (Menu.IsValid()) Viewport->RemoveViewportWidgetContent(Menu.ToSharedRef());
    const URallySessionSubsystem* Session = GameInstance->GetSubsystem<URallySessionSubsystem>();
    const bool bSteam = Session && Session->IsSteamOn();
    TSharedRef<SVerticalBox> Items = SNew(SVerticalBox);
    auto Add = [&Items](TSharedRef<SWidget> Widget) { Items->AddSlot().AutoHeight().Padding(0.0f, 6.0f)[Widget]; };
    Add(Label(TEXT("RALLY SURVIVAL"), 32));
    if (!Reason.IsEmpty()) Add(Label(TEXT("The last game ended: ") + Reason, 13, FLinearColor(1.0f, 0.5f, 0.35f)));
    if (FPaths::FileExists(SavePath())) Add(Button(TEXT("Continue"), [GameInstance]() { Play(GameInstance, false); }));
    Add(Button(TEXT("New game"), [GameInstance]() { Play(GameInstance, true); }));
    Add(Label(bSteam
        ? TEXT("Friends join through Steam: Shift+Tab, Friends, invite them once you are in the game, or right-click a "
               "friend who is playing and Join Game.")
        : TEXT("Steam is not running, so this game is just you. Start Steam, then the game, to play with friends."), 13,
        FLinearColor(0.8f, 0.8f, 0.8f)));
    Add(Button(TEXT("Quit"), [GameInstance]()
    {
        APlayerController* Player = GameInstance->GetFirstLocalPlayerController();
        UKismetSystemLibrary::QuitGame(GameInstance, Player, EQuitPreference::Quit, false);
    }));
    Menu = SNew(SBorder).BorderImage(FCoreStyle::Get().GetBrush("BlackBrush")).HAlign(HAlign_Center).VAlign(VAlign_Center)
        [SNew(SBox).WidthOverride(520.0f)[Items]];
    Viewport->AddViewportWidgetContent(Menu.ToSharedRef(), 100);
    SetMenuInput(GameInstance, true);
    UE_LOG(LogTemp, Display, TEXT("RALLY_MENU shown steam=%s save=%s reason=%s"), bSteam ? TEXT("on") : TEXT("off"),
        FPaths::FileExists(SavePath()) ? TEXT("yes") : TEXT("no"), *Reason);
}

void RallyMainMenu::Hide(UGameInstance* GameInstance)
{
    UGameViewportClient* Viewport = GameInstance ? GameInstance->GetGameViewportClient() : nullptr;
    if (Viewport && Menu.IsValid()) Viewport->RemoveViewportWidgetContent(Menu.ToSharedRef());
    SetMenuInput(GameInstance, false);
    Menu.Reset();
}

bool RallyMainMenu::IsShown() { return Menu.IsValid(); }

bool RallyMainMenu::MoveSaveAside()
{
    const FString From = SavePath();
    if (!FPaths::FileExists(From)) return false;
    const FString To = FPaths::GetPath(From) / FPaths::GetBaseFilename(From) + TEXT("_") + FDateTime::Now().ToString(TEXT("%Y%m%d-%H%M%S")) + TEXT(".sav");
    const bool bMoved = IFileManager::Get().Move(*To, *From);
    UE_LOG(LogTemp, Display, TEXT("RALLY_MENU save moved aside to %s: %s"), *To, bMoved ? TEXT("ok") : TEXT("FAILED"));
    return bMoved;
}
