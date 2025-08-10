// Copyright Epic Games, Inc. All Rights Reserved.

#include "ZombiengineeringCharacter.h"
#include "Engine/LocalPlayer.h"
#include "Camera/CameraComponent.h"
#include "Components/CapsuleComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/SpringArmComponent.h"
#include "GameFramework/Controller.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "InputActionValue.h"
#include "Kismet/GameplayStatics.h"

DEFINE_LOG_CATEGORY(LogTemplateCharacter);

AZombiengineeringCharacter::AZombiengineeringCharacter()
{
	// Set size for collision capsule
	GetCapsuleComponent()->InitCapsuleSize(42.f, 96.0f);
		
	// Don't rotate when the controller rotates. Let that just affect the camera.
	bUseControllerRotationPitch = false;
	bUseControllerRotationYaw = false;
	bUseControllerRotationRoll = false;

	// Configure character movement
	GetCharacterMovement()->bOrientRotationToMovement = true;
	GetCharacterMovement()->RotationRate = FRotator(0.0f, 500.0f, 0.0f);

	// Note: For faster iteration times these variables, and many more, can be tweaked in the Character Blueprint
	// instead of recompiling to adjust them
	GetCharacterMovement()->JumpZVelocity = 500.f;
	GetCharacterMovement()->AirControl = 0.35f;
	GetCharacterMovement()->MaxWalkSpeed = 500.f;
	GetCharacterMovement()->MinAnalogWalkSpeed = 20.f;
	GetCharacterMovement()->BrakingDecelerationWalking = 2000.f;
	GetCharacterMovement()->BrakingDecelerationFalling = 1500.0f;

	// Create a camera boom (pulls in towards the player if there is a collision)
	CameraBoom = CreateDefaultSubobject<USpringArmComponent>(TEXT("CameraBoom"));
	CameraBoom->SetupAttachment(RootComponent);
	CameraBoom->TargetArmLength = 400.0f;
	CameraBoom->bUsePawnControlRotation = true;

	// Create a follow camera
	FollowCamera = CreateDefaultSubobject<UCameraComponent>(TEXT("FollowCamera"));
	FollowCamera->SetupAttachment(CameraBoom, USpringArmComponent::SocketName);
	FollowCamera->bUsePawnControlRotation = false;

	// Note: The skeletal mesh and anim blueprint references on the Mesh component (inherited from Character) 
	// are set in the derived blueprint asset named ThirdPersonCharacter (to avoid direct content references in C++)
}

void AZombiengineeringCharacter::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
	// Set up action bindings
	if (UEnhancedInputComponent* EnhancedInputComponent = Cast<UEnhancedInputComponent>(PlayerInputComponent)) {
		
		// Jumping
		EnhancedInputComponent->BindAction(JumpAction, ETriggerEvent::Started, this, &ACharacter::Jump);
		EnhancedInputComponent->BindAction(JumpAction, ETriggerEvent::Completed, this, &ACharacter::StopJumping);

		// Moving
		EnhancedInputComponent->BindAction(MoveAction, ETriggerEvent::Triggered, this, &AZombiengineeringCharacter::Move);
		EnhancedInputComponent->BindAction(MouseLookAction, ETriggerEvent::Triggered, this, &AZombiengineeringCharacter::Look);

		// Looking
		EnhancedInputComponent->BindAction(LookAction, ETriggerEvent::Triggered, this, &AZombiengineeringCharacter::Look);

		// Zoom
		EnhancedInputComponent->BindAction(ZoomInAction, ETriggerEvent::Triggered, this, &AZombiengineeringCharacter::ZoomIn);

		EnhancedInputComponent->BindAction(ZoomOutAction, ETriggerEvent::Triggered, this, &AZombiengineeringCharacter::ZoomOut);

		EnhancedInputComponent->BindAction(MainMenuAction, ETriggerEvent::Triggered, this, &AZombiengineeringCharacter::ZoomOut);

		EnhancedInputComponent->BindAction(MainMenuAction, ETriggerEvent::Started, this, &AZombiengineeringCharacter::OnMainMenuAction);

	}
	else
	{
		UE_LOG(LogTemplateCharacter, Error, TEXT("'%s' Failed to find an Enhanced Input component! This template is built to use the Enhanced Input system. If you intend to use the legacy system, then you will need to update this C++ file."), *GetNameSafe(this));
	}
}

void AZombiengineeringCharacter::Move(const FInputActionValue& Value)
{
	// input is a Vector2D
	FVector2D MovementVector = Value.Get<FVector2D>();

	// route the input
	DoMove(MovementVector.X, MovementVector.Y);
}

void AZombiengineeringCharacter::Look(const FInputActionValue& Value)
{
	// input is a Vector2D
	FVector2D LookAxisVector = Value.Get<FVector2D>();

	// route the input
	DoLook(LookAxisVector.X, LookAxisVector.Y);
}

void AZombiengineeringCharacter::ZoomIn(const FInputActionValue& Value)
{
	if (CameraBoom->TargetArmLength > 200.f) CameraBoom->TargetArmLength -= 40.f;
}

void AZombiengineeringCharacter::ZoomOut(const FInputActionValue& Value)
{
	if (CameraBoom->TargetArmLength < 5000.f) CameraBoom->TargetArmLength += 40.f;
}

void AZombiengineeringCharacter::OnMainMenuAction(const FInputActionValue& /*Value*/)
{
	UWorld* World = GetWorld();
	if (!World)
	{
		UE_LOG(LogTemplateCharacter, Error, TEXT("World is null; cannot switch level."));
		return;
	}

	if (!MainMenuLevel.IsValid() && !MainMenuLevel.IsNull())
	{
		// Soft reference set but not loaded yet is fine—we'll open by path below.
	}

	if (MainMenuLevel.IsNull())
	{
		UE_LOG(LogTemplateCharacter, Warning, TEXT("MainMenuLevel is not set. Please assign a level (UWorld) in the character details."));
		return;
	}

	// Use the long package name (e.g. /Game/Maps/MainMenu)
	const FString LevelPath = MainMenuLevel.ToSoftObjectPath().GetLongPackageName();

	UGameplayStatics::OpenLevel(this, FName(*LevelPath));
}

void AZombiengineeringCharacter::DoMove(float Right, float Forward)
{
	if (GetController() != nullptr)
	{
		// find out which way is forward
		const FRotator Rotation = GetController()->GetControlRotation();
		const FRotator YawRotation(0, Rotation.Yaw, 0);

		// get forward vector
		const FVector ForwardDirection = FRotationMatrix(YawRotation).GetUnitAxis(EAxis::X);

		// get right vector 
		const FVector RightDirection = FRotationMatrix(YawRotation).GetUnitAxis(EAxis::Y);

		// add movement 
		AddMovementInput(ForwardDirection, Forward);
		AddMovementInput(RightDirection, Right);
	}
}

void AZombiengineeringCharacter::DoLook(float Yaw, float Pitch)
{
	if (GetController() != nullptr)
	{
		// add yaw and pitch input to controller
		AddControllerYawInput(Yaw);
		AddControllerPitchInput(Pitch);
	}
}

void AZombiengineeringCharacter::DoJumpStart()
{
	// signal the character to jump
	Jump();
}

void AZombiengineeringCharacter::DoJumpEnd()
{
	// signal the character to stop jumping
	StopJumping();
}

