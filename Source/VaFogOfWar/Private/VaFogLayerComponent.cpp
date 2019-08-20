// Copyright 2019 Vladimir Alyamkin. All Rights Reserved.

#include "VaFogLayerComponent.h"

#include "VaFogAgentComponent.h"
#include "VaFogBoundsVolume.h"
#include "VaFogController.h"
#include "VaFogDefines.h"
#include "VaFogOfWar.h"
#include "VaFogSettings.h"

#include "DrawDebugHelpers.h"
#include "Engine/Texture2D.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Materials/Material.h"
#include "RHI.h"

#include <functional>
#include <unordered_map>
#include <vector>

DECLARE_CYCLE_STAT(TEXT("UpdateAgents"), STAT_UpdateAgents, STATGROUP_VaFog);
DECLARE_CYCLE_STAT(TEXT("UpdateUpscaleBuffer"), STAT_UpdateUpscaleBuffer, STATGROUP_VaFog);
DECLARE_CYCLE_STAT(TEXT("FetchTexelFromSource"), STAT_FetchTexelFromSource, STATGROUP_VaFog);
DECLARE_CYCLE_STAT(TEXT("DrawVisionCircle"), STAT_DrawVisionCircle, STATGROUP_VaFog);
DECLARE_CYCLE_STAT(TEXT("DrawFieldOfView"), STAT_DrawFieldOfView, STATGROUP_VaFog);
DECLARE_CYCLE_STAT(TEXT("DrawCircle"), STAT_DrawCircle, STATGROUP_VaFog);
DECLARE_CYCLE_STAT(TEXT("Plot4Points"), STAT_Plot4Points, STATGROUP_VaFog);
DECLARE_CYCLE_STAT(TEXT("DrawHorizontalLine"), STAT_DrawHorizontalLine, STATGROUP_VaFog);

namespace std
{
template <>
struct hash<FFogTexel2x2>
{
	size_t
	operator()(const FFogTexel2x2& obj) const
	{
		return hash<int>()(obj.p11 * 1000 + obj.p12 * 100 + obj.p21 * 10 + obj.p22);
	}
};
} // namespace std

// clang-format off
static const std::unordered_map<FFogTexel2x2, FFogTexel4x4> UpscaleTemplate = {{
	// 1
	{	{	0x00, 0x00, 0x00, 0x00	},	{{	{0x00, 0x00, 0x00, 0x00},
											{0x00, 0x00, 0x00, 0x00},
											{0x00, 0x00, 0x00, 0x00},
											{0x00, 0x00, 0x00, 0x00}	}}	},
	// 2
	{	{	0x00, 0xFF, 0x00, 0x00	},	{{	{0x00, 0x00, 0x80, 0xFF}, 
											{0x00, 0x00, 0x00, 0x80},
											{0x00, 0x00, 0x00, 0x00}, 
											{0x00, 0x00, 0x00, 0x00}	}}	},
	// 3
	{	{	0x00, 0x00, 0xFF, 0x00	},	{{	{0x00, 0x00, 0x00, 0x00}, 
											{0x00, 0x00, 0x00, 0x00},
											{0x80, 0x00, 0x00, 0x00}, 
											{0xFF, 0x80, 0x00, 0x00}	}}	},
	// 4
	{	{	0x00, 0xFF, 0xFF, 0x00	},	{{	{0x00, 0x00, 0x80, 0xFF}, 
											{0x00, 0x00, 0x00, 0x80},
											{0x80, 0x00, 0x00, 0x00}, 
											{0xFF, 0x80, 0x00, 0x00}	}}	},
	// 5
	{	{	0x00, 0x00, 0x00, 0xFF	},	{{	{0x00, 0x00, 0x00, 0x00}, 
											{0x00, 0x00, 0x00, 0x00},
											{0x00, 0x00, 0x00, 0x80}, 
											{0x00, 0x00, 0x80, 0xFF}	}}	},
	// 6
	{	{	0x00, 0xFF, 0x00, 0xFF	},	{{	{0x00, 0x00, 0xFF, 0xFF}, 
											{0x00, 0x00, 0xFF, 0xFF},
											{0x00, 0x00, 0xFF, 0xFF}, 
											{0x00, 0x00, 0xFF, 0xFF}	}}	},
	// 7
	{	{	0x00, 0x00, 0xFF, 0xFF	},	{{	{0x00, 0x00, 0x00, 0x00}, 
											{0x00, 0x00, 0x00, 0x00},
											{0xFF, 0xFF, 0xFF, 0xFF}, 
											{0xFF, 0xFF, 0xFF, 0xFF}	}}	},
	// 8
	{	{	0x00, 0xFF, 0xFF, 0xFF	},	{{	{0x00, 0x80, 0xFF, 0xFF}, 
											{0x80, 0xFF, 0xFF, 0xFF},
											{0xFF, 0xFF, 0xFF, 0xFF}, 
											{0xFF, 0xFF, 0xFF, 0xFF}	}}	},
	// 9
	{	{	0xFF, 0x00, 0x00, 0x00	},	{{	{0xFF, 0x80, 0x00, 0x00}, 
											{0x80, 0x00, 0x00, 0x00},
											{0x00, 0x00, 0x00, 0x00}, 
											{0x00, 0x00, 0x00, 0x00}	}}	},
	// 10
	{	{	0xFF, 0xFF, 0x00, 0x00	},	{{	{0xFF, 0xFF, 0xFF, 0xFF}, 
											{0xFF, 0xFF, 0xFF, 0xFF},
											{0x00, 0x00, 0x00, 0x00}, 
											{0x00, 0x00, 0x00, 0x00}	}}	},
	// 11
	{	{	0xFF, 0x00, 0xFF, 0x00	},	{{	{0xFF, 0xFF, 0x00, 0x00}, 
											{0xFF, 0xFF, 0x00, 0x00},
											{0xFF, 0xFF, 0x00, 0x00}, 
											{0xFF, 0xFF, 0x00, 0x00}	}}	},
	// 12
	{	{	0xFF, 0xFF, 0xFF, 0x00	},	{{	{0xFF, 0xFF, 0xFF, 0xFF}, 
											{0xFF, 0xFF, 0xFF, 0xFF},
											{0xFF, 0xFF, 0xFF, 0x80}, 
											{0xFF, 0xFF, 0x80, 0x00}	}}	},
	// 14
	{	{	0xFF, 0x00, 0x00, 0xFF	},	{{	{0xFF, 0x80, 0x00, 0x00}, 
											{0x80, 0x00, 0x00, 0x00},
											{0x00, 0x00, 0x00, 0x80}, 
											{0x00, 0x00, 0x80, 0xFF}	}}	},
	// 14
	{	{	0xFF, 0xFF, 0x00, 0xFF	},	{{	{0xFF, 0xFF, 0xFF, 0xFF}, 
											{0xFF, 0xFF, 0xFF, 0xFF},
											{0x80, 0xFF, 0xFF, 0xFF}, 
											{0x00, 0x80, 0xFF, 0xFF}	}}	},
	// 15
	{	{	0xFF, 0x00, 0xFF, 0xFF	},	{{	{0xFF, 0xFF, 0x80, 0x00}, 
											{0xFF, 0xFF, 0xFF, 0x80},
											{0xFF, 0xFF, 0xFF, 0xFF}, 
											{0xFF, 0xFF, 0xFF, 0xFF}	}}	},
	// 16
	{	{	0xFF, 0xFF, 0xFF, 0xFF	},	{{	{0xFF, 0xFF, 0xFF, 0xFF}, 
											{0xFF, 0xFF, 0xFF, 0xFF},
											{0xFF, 0xFF, 0xFF, 0xFF}, 
											{0xFF, 0xFF, 0xFF, 0xFF}	}}	},
}};

static const std::vector<FFogOctantTransform> OctantTransforms = {
	{ 1,  0,  0,  1 },
	{ 1,  0,  0, -1 },
	{-1,  0,  0,  1 },
	{-1,  0,  0, -1 },
	{ 0,  1,  1,  0 },
	{ 0,  1, -1,  0 },
	{ 0, -1,  1,  0 },
	{ 0, -1, -1,  0 }
};
// clang-format on

UVaFogLayerComponent::UVaFogLayerComponent(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	bAutoActivate = true;
	bWantsInitializeComponent = true;
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.TickGroup = TG_DuringPhysics;

	SourceBuffer = nullptr;
	ObstaclesBuffer = nullptr;
	UpscaleBuffer = nullptr;

	SourceW = 0;
	SourceH = 0;
	SourceBufferLength = 0;

	UpscaleW = 0;
	UpscaleH = 0;
	UpscaleBufferLength = 0;

	bDebugAgents = false;
	DebugAgentsColor = FColor::Red;
	bDebugBuffers = false;
}

void UVaFogLayerComponent::InitializeComponent()
{
	Super::InitializeComponent();

	// Prepare radius strategies
	RadiusStrategies.Reserve(static_cast<int32>(EVaFogRadiusStrategy::Max));
	RadiusStrategies.Emplace(EVaFogRadiusStrategy::Circle, MakeShared<FVaFogRadiusStrategy_Circle>());
	RadiusStrategies.Emplace(EVaFogRadiusStrategy::Square, MakeShared<FVaFogRadiusStrategy_Square>());
	RadiusStrategies.Emplace(EVaFogRadiusStrategy::SquareStepped, MakeShared<FVaFogRadiusStrategy_SquareStepped>());

	// Cache texture size values
	int32 CachedTextureResolution = FVaFogOfWarModule::Get().GetSettings()->FogLayerResolution;
	check(FMath::IsPowerOfTwo(CachedTextureResolution));
	int32 CachedUpscaleResolution = CachedTextureResolution * 4;

	// Create texture buffer and initialize it
	check(!SourceBuffer);
	SourceW = CachedTextureResolution;
	SourceH = CachedTextureResolution;
	SourceBuffer = new uint8[SourceW * SourceH];
	SourceBufferLength = SourceW * SourceH * sizeof(uint8);
	FMemory::Memzero(SourceBuffer, SourceBufferLength);

	// Create texture buffer for upscaled texture and initialize it
	check(!UpscaleBuffer);
	UpscaleW = CachedUpscaleResolution;
	UpscaleH = CachedUpscaleResolution;
	UpscaleBuffer = new uint8[UpscaleW * UpscaleH];
	UpscaleBufferLength = UpscaleW * UpscaleH * sizeof(uint8);
	FMemory::Memzero(UpscaleBuffer, UpscaleBufferLength);

	// Fillup Obstacles buffer
	ObstaclesBuffer = new uint8[UpscaleBufferLength];
	FMemory::Memzero(ObstaclesBuffer, UpscaleBufferLength);

	// Prepare debug textures if required
	if (bDebugBuffers)
	{
		SourceUpdateRegion = FUpdateTextureRegion2D(0, 0, 0, 0, SourceW, SourceH);
		SourceTexture = UTexture2D::CreateTransient(SourceW, SourceH, EPixelFormat::PF_G8);
		SourceTexture->CompressionSettings = TextureCompressionSettings::TC_Grayscale;
		SourceTexture->SRGB = false;
		SourceTexture->Filter = TextureFilter::TF_Nearest;
		SourceTexture->AddressX = TextureAddress::TA_Clamp;
		SourceTexture->AddressY = TextureAddress::TA_Clamp;
		SourceTexture->UpdateResource();

		ObstaclesTexture = UTexture2D::CreateTransient(SourceW, SourceH, EPixelFormat::PF_G8);
		ObstaclesTexture->CompressionSettings = TextureCompressionSettings::TC_Grayscale;
		ObstaclesTexture->SRGB = false;
		ObstaclesTexture->Filter = TextureFilter::TF_Nearest;
		ObstaclesTexture->AddressX = TextureAddress::TA_Clamp;
		ObstaclesTexture->AddressY = TextureAddress::TA_Clamp;
		ObstaclesTexture->UpdateResource();
	}

	// Upscale texture is the one we export to user
	UpscaleUpdateRegion = FUpdateTextureRegion2D(0, 0, 0, 0, UpscaleW, UpscaleH);
	UpscaleTexture = UTexture2D::CreateTransient(UpscaleW, UpscaleH, EPixelFormat::PF_G8);
	UpscaleTexture->CompressionSettings = TextureCompressionSettings::TC_Grayscale;
	UpscaleTexture->SRGB = false;
	UpscaleTexture->Filter = TextureFilter::TF_Nearest;
	UpscaleTexture->AddressX = TextureAddress::TA_Clamp;
	UpscaleTexture->AddressY = TextureAddress::TA_Clamp;
	UpscaleTexture->UpdateResource();

	UVaFogController::Get(this)->OnFogLayerAdded(this);
}

void UVaFogLayerComponent::UninitializeComponent()
{
	Super::UninitializeComponent();

	if (SourceBuffer)
	{
		delete[] SourceBuffer;
		SourceBuffer = nullptr;
	}

	if (ObstaclesBuffer)
	{
		delete[] ObstaclesBuffer;
		ObstaclesBuffer = nullptr;
	}

	if (UpscaleBuffer)
	{
		delete[] UpscaleBuffer;
		UpscaleBuffer = nullptr;
	}

	if (SourceTexture)
	{
		SourceTexture = nullptr;
	}

	if (ObstaclesTexture)
	{
		ObstaclesTexture = nullptr;
	}

	if (UpscaleTexture)
	{
		SourceTexture = nullptr;
	}

	if (UVaFogController::Get(this, EGetWorldErrorMode::LogAndReturnNull))
	{
		UVaFogController::Get(this)->OnFogLayerRemoved(this);
	}
}

void UVaFogLayerComponent::TickComponent(float DeltaTime, enum ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	UpdateAgents();
	UpdateUpscaleBuffer();

	if (bDebugBuffers)
	{
		UpdateTextureFromBuffer(SourceTexture, SourceBuffer, SourceBufferLength, SourceUpdateRegion);
		UpdateTextureFromBuffer(ObstaclesTexture, ObstaclesBuffer, SourceBufferLength, SourceUpdateRegion);
	}

	UpdateTextureFromBuffer(UpscaleTexture, UpscaleBuffer, UpscaleBufferLength, UpscaleUpdateRegion);

	// Cleanup buffer for scouting
	if (LayerChannel == EVaFogLayerChannel::Scouting)
	{
		FMemory::Memzero(SourceBuffer, SourceBufferLength);
	}
}

void UVaFogLayerComponent::UpdateAgents()
{
	SCOPE_CYCLE_COUNTER(STAT_UpdateAgents);

	auto FogVolume = UVaFogController::Get(this)->GetFogVolume();
	for (auto FogAgent : FogAgents)
	{
		FIntPoint AgentLocation = FogVolume->TransformWorldToLayer(FogAgent->GetComponentTransform().GetLocation());
		//UE_LOG(LogVaFog, Warning, TEXT("[%s] Agent [%s] location: %s"), *VA_FUNC_LINE, *FogAgent->GetName(), *AgentLocation.ToString());

		if (bDebugAgents)
		{
			DrawDebugSphere(GetWorld(), FogAgent->GetComponentTransform().GetLocation(), FogAgent->VisionRadius, 32, DebugAgentsColor, false, 0.0f);
		}

		check(FogAgent->VisionRadius >= 0);
		if (FogAgent->VisionRadius == 0)
		{
			Reveal(SourceBuffer, AgentLocation.X, AgentLocation.Y);
		}
		else
		{
			FFogDrawContext DrawContext;
			DrawContext.TargetBuffer = SourceBuffer;
			DrawContext.CenterX = AgentLocation.X;
			DrawContext.CenterY = AgentLocation.Y;
			DrawContext.Radius = FogVolume->ScaleDistanceToLayer(FogAgent->VisionRadius);
			DrawContext.RadiusStrategy = FogAgent->RadiusStrategy;

			DrawVisionCircle(DrawContext);
		}
	}
}

void UVaFogLayerComponent::UpdateObstacle(UVaFogAgentComponent* FogAgent, bool bObstacleIsActive, AVaFogBoundsVolume* FogVolume)
{
	check(FogAgent);
	check(FogVolume);

	FIntPoint AgentLocation = FogVolume->TransformWorldToLayer(FogAgent->GetOwner()->GetActorLocation());

	// Every obstacle updates single cell only
	check(AgentLocation.X >= 0 && AgentLocation.X < SourceW && AgentLocation.Y >= 0 && AgentLocation.Y < SourceH);
	ObstaclesBuffer[AgentLocation.Y * SourceW + AgentLocation.X] = (bObstacleIsActive) ? 0xFF : 0x00;
}

void UVaFogLayerComponent::UpdateUpscaleBuffer()
{
	SCOPE_CYCLE_COUNTER(STAT_UpdateUpscaleBuffer);

	FFogTexel2x2 SourceTexel;
	FFogTexel4x4 UpscaleTexel;

	for (int32 x = 0; x < SourceW; ++x)
	{
		for (int32 y = 0; y < SourceH; ++y)
		{
			// Fetch original texture pixel and its neighbors
			SourceTexel = FetchTexelFromSource(x, y);
			UpscaleTexel = UpscaleTemplate.at(SourceTexel);

			// Apply texel to upscale buffer based on template
			for (int32 i = 0; i < 4; ++i)
			{
				FMemory::Memcpy(&UpscaleBuffer[(4 * y + i) * UpscaleW + 4 * x], &UpscaleTexel.pixels[i], 4 * sizeof(uint8));
			}
		}
	}
}

void UVaFogLayerComponent::DrawVisionCircle(const FFogDrawContext& DrawContext)
{
	SCOPE_CYCLE_COUNTER(STAT_DrawVisionCircle);

	// http://www.adammil.net/blog/v125_Roguelike_Vision_Algorithms.html
	// Shadow casting (point-to-tile or point-to-point)
	// Pros: Fast. Expanding pillar shadows. Expansive walls. Continuous point visibility.
	// Cons: Diagonal vision much narrower than cardinal. Blind corners. Beam expands too much through a door. Asymmetrical. Nontrivial to eliminate all artifacts.

	Reveal(DrawContext.TargetBuffer, DrawContext.CenterX, DrawContext.CenterY);

	// Scan each octant
	for (int32 i = 0; i < 8; ++i)
	{
		DrawFieldOfView(DrawContext, 1, 1.f, 0.f, OctantTransforms[i]);
	}
}

void UVaFogLayerComponent::DrawFieldOfView(const FFogDrawContext& DrawContext, int32 Y, float Start, float End, FFogOctantTransform Transform)
{
	if (Start < End)
	{
		return;
	}

	SCOPE_CYCLE_COUNTER(STAT_DrawFieldOfView);

	int32 RadiusSquared = FMath::Square(DrawContext.Radius);
	float NewStart = 0.0f;
	bool bBlocked = false;

	int32 DeltaY = 0;
	int32 CurrentX = 0;
	int32 CurrentY = 0;

	// @TODO Make in deterministic https://github.com/ufna/VaFogOfWar/issues/57
	float LeftSlope = 0.f;
	float RightSlope = 0.f;

	for (int32 Distance = Y; Distance <= DrawContext.Radius && !bBlocked; ++Distance)
	{
		DeltaY = -Distance;
		for (int32 DeltaX = -Distance; DeltaX <= 0; DeltaX++)
		{
			CurrentX = DrawContext.CenterX + DeltaX * Transform.xx + DeltaY * Transform.xy;
			CurrentY = DrawContext.CenterY + DeltaX * Transform.yx + DeltaY * Transform.yy;
			LeftSlope = (DeltaX - 0.5f) / (DeltaY + 0.5f);
			RightSlope = (DeltaX + 0.5f) / (DeltaY - 0.5f);

			if (!(CurrentX >= 0 && CurrentY >= 0 && CurrentX < SourceW && CurrentY < SourceH) ||
				Start < RightSlope)
			{
				continue;
			}
			else if (End > LeftSlope)
			{
				break;
			}

			// @TODO Make radius strategies configurable https://github.com/ufna/VaFogOfWar/issues/58
			// Check if it's within the lightable area and light if needed
			if (RadiusStrategies[DrawContext.RadiusStrategy]->IsInRadius(DrawContext.CenterX, DrawContext.CenterY, DrawContext.Radius, CurrentX, CurrentY))
			{
				Reveal(DrawContext.TargetBuffer, CurrentX, CurrentY);
			}

			// Check if previous cell was a blocking one
			if (bBlocked)
			{
				if (IsBlocked(CurrentX, CurrentY))
				{
					NewStart = RightSlope;
					continue;
				}
				else
				{
					bBlocked = false;
					Start = NewStart;
				}
			}
			else
			{
				// Hit a wall within sight line
				if (IsBlocked(CurrentX, CurrentY) && Distance < DrawContext.Radius)
				{
					bBlocked = true;
					DrawFieldOfView(DrawContext, Distance + 1, Start, LeftSlope, Transform);
					NewStart = RightSlope;
				}
			}
		}
	}
}

void UVaFogLayerComponent::Reveal(uint8* TargetBuffer, int32 X, int32 Y)
{
	check(X >= 0 && X < SourceW && Y >= 0 && Y < SourceH);
	TargetBuffer[Y * SourceW + X] = 0xFF;
}

bool UVaFogLayerComponent::IsBlocked(int32 X, int32 Y)
{
	check(X >= 0 && X < SourceW && Y >= 0 && Y < SourceH);
	return ObstaclesBuffer[Y * SourceW + X] == 0xFF;
}

void UVaFogLayerComponent::DrawCircle(const FFogDrawContext& DrawContext)
{
	SCOPE_CYCLE_COUNTER(STAT_DrawCircle);

	if (DrawContext.Radius > SourceW)
	{
		UE_LOG(LogVaFog, Warning, TEXT("[%s] Vision radius %d is larger than source width %d"), *VA_FUNC_LINE, DrawContext.Radius, SourceW);
	}

	int32 RadiusError = -DrawContext.Radius;
	int32 X = FMath::Min(DrawContext.Radius, SourceW);
	int32 Y = 0;

	while (X >= Y)
	{
		int lastY = Y;

		RadiusError += Y;
		++Y;
		RadiusError += Y;

		Plot4Points(DrawContext, X, lastY);

		if (RadiusError >= 0)
		{
			if (X != lastY)
				Plot4Points(DrawContext, lastY, X);

			RadiusError -= X;
			--X;
			RadiusError -= X;
		}
	}
}

void UVaFogLayerComponent::Plot4Points(const FFogDrawContext& DrawContext, int32 X, int32 Y)
{
	SCOPE_CYCLE_COUNTER(STAT_Plot4Points);

	DrawHorizontalLine(DrawContext.TargetBuffer, DrawContext.CenterX - X, DrawContext.CenterY + Y, DrawContext.CenterX + X);

	if (Y != 0)
	{
		DrawHorizontalLine(DrawContext.TargetBuffer, DrawContext.CenterX - X, DrawContext.CenterY - Y, DrawContext.CenterX + X);
	}
}

void UVaFogLayerComponent::DrawHorizontalLine(uint8* TargetBuffer, int32 x0, int32 y0, int32 x1)
{
	if (y0 < 0 || y0 >= SourceH || x0 >= SourceW || x1 < 0)
		return;

	SCOPE_CYCLE_COUNTER(STAT_DrawHorizontalLine);

	int32 x0opt = FMath::Max(x0, 0);
	int32 x1opt = FMath::Clamp(x1, x0opt, SourceW - 1);

	FMemory::Memset(&TargetBuffer[y0 * SourceW + x0opt], 0xFF, x1opt - x0opt + 1);
}

FFogTexel2x2 UVaFogLayerComponent::FetchTexelFromSource(int32 W, int32 H)
{
	//SCOPE_CYCLE_COUNTER(STAT_FetchTexelFromSource);

	// Clamp neighbor coords if necessary
	int32 NeighborW = FMath::Min(W + 1, SourceW - 1);
	int32 NeighborH = FMath::Min(H + 1, SourceH - 1);

	FFogTexel2x2 Texel;
	Texel.p11 = SourceBuffer[H * SourceW + W];
	Texel.p12 = SourceBuffer[H * SourceW + NeighborW];
	Texel.p21 = SourceBuffer[NeighborH * SourceW + W];
	Texel.p22 = SourceBuffer[NeighborH * SourceW + NeighborW];

	return Texel;
}

void UVaFogLayerComponent::AddFogAgent(UVaFogAgentComponent* InFogAgent)
{
	switch (InFogAgent->InteractionType)
	{
	case EVaFogAgentType::Dispel:
		FogAgents.AddUnique(InFogAgent);
		break;

	case EVaFogAgentType::Obstacle:
		ObstacleAgents.AddUnique(InFogAgent);
		UpdateObstacle(InFogAgent, true, UVaFogController::Get(this)->GetFogVolume());
		break;

	default:
		unimplemented();
	}
}

void UVaFogLayerComponent::RemoveFogAgent(UVaFogAgentComponent* InFogAgent)
{
	int32 NumRemoved = 0;

	switch (InFogAgent->InteractionType)
	{
	case EVaFogAgentType::Dispel:
		NumRemoved = FogAgents.Remove(InFogAgent);
		break;

	case EVaFogAgentType::Obstacle:
		NumRemoved = ObstacleAgents.Remove(InFogAgent);
		UpdateObstacle(InFogAgent, false, UVaFogController::Get(this)->GetFogVolume());
		break;

	default:
		unimplemented();
	}

	if (NumRemoved == 0)
	{
		UE_LOG(LogVaFog, Error, TEXT("[%s] No cached data found for: %s"), *VA_FUNC_LINE, *InFogAgent->GetName());
	}
}

void UVaFogLayerComponent::UpdateTextureFromBuffer(UTexture2D* DestinationTexture, uint8* SrcBuffer, int32 SrcBufferLength, FUpdateTextureRegion2D& UpdateTextureRegion)
{
	struct FTextureData
	{
		FTexture2DResource* Texture2DResource;
		FUpdateTextureRegion2D* Region;
		uint32 SrcPitch;
		uint8* SrcData;
	};

	// Copy original data fro GPU
	uint8* Buffer = new uint8[SrcBufferLength];
	FMemory::Memcpy(Buffer, SrcBuffer, SrcBufferLength);

	FTextureData* TextureData = new FTextureData();
	TextureData->Texture2DResource = (FTexture2DResource*)DestinationTexture->Resource;
	TextureData->SrcPitch = UpdateTextureRegion.Width;
	TextureData->SrcData = Buffer;
	TextureData->Region = &UpdateTextureRegion;

	ENQUEUE_RENDER_COMMAND(UpdateTexture)
	(
		[TextureData](FRHICommandListImmediate& RHICmdList) {
			int32 CurrentFirstMip = TextureData->Texture2DResource->GetCurrentFirstMip();
			if (CurrentFirstMip <= 0)
			{
				RHIUpdateTexture2D(
					TextureData->Texture2DResource->GetTexture2DRHI(),
					0 - CurrentFirstMip,
					*TextureData->Region,
					TextureData->SrcPitch,
					TextureData->SrcData);
			}
			delete[] TextureData->SrcData;
			delete TextureData;
		});
}
