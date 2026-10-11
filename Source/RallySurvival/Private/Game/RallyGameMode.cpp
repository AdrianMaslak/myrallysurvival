#include "Game/RallyGameMode.h"
#include "Vehicle/RallyCarPawn.h"

ARallyGameMode::ARallyGameMode()
{
    DefaultPawnClass = ARallyCarPawn::StaticClass();
}
