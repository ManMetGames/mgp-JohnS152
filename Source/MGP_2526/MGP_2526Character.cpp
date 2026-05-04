// Copyright Epic Games, Inc. All Rights Reserved.

#include "MGP_2526Character.h"
#include "Engine/LocalPlayer.h"
#include "Camera/CameraComponent.h"
#include "Components/CapsuleComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/SpringArmComponent.h"
#include "GameFramework/Controller.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "InputActionValue.h"
#include "MGP_2526.h"
#include "DrawDebugHelpers.h"

#define COLLISION_GRAPPLE ECC_GameTraceChannel2

AMGP_2526Character::AMGP_2526Character()
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
	//Camera offset. It looks strange to have a third person camera being blocked by the player model.
	CameraBoom->SocketOffset = FVector(0.0f, 0.0f, 120.0f);

	// Create a follow camera
	FollowCamera = CreateDefaultSubobject<UCameraComponent>(TEXT("FollowCamera"));
	FollowCamera->SetupAttachment(CameraBoom, USpringArmComponent::SocketName);
	FollowCamera->bUsePawnControlRotation = false;

	// Note: The skeletal mesh and anim blueprint references on the Mesh component (inherited from Character) 
	// are set in the derived blueprint asset named ThirdPersonCharacter (to avoid direct content references in C++)
}

void AMGP_2526Character::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
	// Set up action bindings
	if (UEnhancedInputComponent* EnhancedInputComponent = Cast<UEnhancedInputComponent>(PlayerInputComponent)) {
		
		// Jumping
		EnhancedInputComponent->BindAction(JumpAction, ETriggerEvent::Started, this, &ACharacter::Jump);
		EnhancedInputComponent->BindAction(JumpAction, ETriggerEvent::Completed, this, &ACharacter::StopJumping);

		// Moving
		EnhancedInputComponent->BindAction(MoveAction, ETriggerEvent::Triggered, this, &AMGP_2526Character::Move);
		EnhancedInputComponent->BindAction(MouseLookAction, ETriggerEvent::Triggered, this, &AMGP_2526Character::Look);

		// Looking
		EnhancedInputComponent->BindAction(LookAction, ETriggerEvent::Triggered, this, &AMGP_2526Character::Look);

		//Grappel
		EnhancedInputComponent->BindAction(GrappelAction, ETriggerEvent::Started, this, &AMGP_2526Character::Grappel);
	}
	else
	{
		UE_LOG(LogMGP_2526, Error, TEXT("'%s' Failed to find an Enhanced Input component! This template is built to use the Enhanced Input system. If you intend to use the legacy system, then you will need to update this C++ file."), *GetNameSafe(this));
	}
}

void AMGP_2526Character::Move(const FInputActionValue& Value)
{
	// input is a Vector2D
	FVector2D MovementVector = Value.Get<FVector2D>();

	// route the input
	DoMove(MovementVector.X, MovementVector.Y);
}

void AMGP_2526Character::Look(const FInputActionValue& Value)
{
	// input is a Vector2D
	FVector2D LookAxisVector = Value.Get<FVector2D>();

	// route the input
	DoLook(LookAxisVector.X, LookAxisVector.Y);
}

void AMGP_2526Character::Grappel(const FInputActionValue& Value)
{
	DoGrappel(grappelUpwardsPush, grappelForwardsPush);
}

void AMGP_2526Character::DoMove(float Right, float Forward)
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

void AMGP_2526Character::DoLook(float Yaw, float Pitch)
{
	if (GetController() != nullptr)
	{
		// add yaw and pitch input to controller
		AddControllerYawInput(Yaw);
		AddControllerPitchInput(Pitch);
	}
}

void AMGP_2526Character::DoJumpStart()
{
	// signal the character to jump
	Jump();
}

void AMGP_2526Character::DoJumpEnd()
{
	// signal the character to stop jumping
	StopJumping();
}

void AMGP_2526Character::DoGrappel(float upwardPush, float forwardPush)
{
	FVector direction = FollowCamera->GetForwardVector();

	direction.Z = 0;
	direction.Normalize();

	launchVelocity = direction * forwardPush;
	launchVelocity.Z += upwardPush;

	if (canGrappel && grappelCount > 0) 
	{
		LaunchCharacter(launchVelocity, true, true);
		grappelCount--;
	}	
}

void AMGP_2526Character::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	canGrappel = false;
	float offset = 5.0f;
	
	//A ray, cast every frame, to update the canGrappel bool. I'm doing it this way, rather than checking every time you do the grappel action, because I want to be able to have a ui element use it.
	castRay();
	castRay(-offset, 0);
	castRay(0, -offset);
	castRay(offset, 0);
	castRay(0, offset);
	castRay((offset * 0.75), (offset * 0.75));
	castRay(-(offset * 0.75), -(offset * 0.75));
	castRay((offset * 0.75), -(offset * 0.75));
	castRay(-(offset * 0.75), (offset * 0.75));

	if (GetCharacterMovement()->IsMovingOnGround() && hasReset == false)
	{
		grappelCount = maxGrappel;
		hasReset = true;
	}
	if (GetCharacterMovement()->IsFalling())
	{
		hasReset = false;
	}
	
}

void AMGP_2526Character::castRay(float horizontalOffset, float verticleOffset)
{
	if (FollowCamera != nullptr) 
	{
		//Create start and end points for the ray, giving it the correct rotation.
		FVector rayStart = FollowCamera->GetComponentLocation();
		FRotator forwardRot = FollowCamera->GetComponentRotation();
		FRotator offset(verticleOffset, horizontalOffset, 0);
		FVector direction = (forwardRot + offset).Vector();
		FVector rayEnd = rayStart + (direction * 3000.0f);

		FHitResult hit;

		FCollisionQueryParams params;
		params.AddIgnoredActor(this);


		bool isHit = GetWorld()->LineTraceSingleByChannel(
			hit,
			rayStart,
			rayEnd,
			COLLISION_GRAPPLE,
			params
		);

		if (isHit)
		{
			canGrappel = isHit;
		}
	}
}

