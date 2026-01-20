// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "NiagaraDataInterface.h"
#include "NiagaraCommon.h"
#include "TrajectoryBufferProvider.h"
#include "NiagaraDataInterfaceTrajectoryBuffer.generated.h"

/**
 * Lightweight Niagara Data Interface for Trajectory Position Buffer
 * 
 * Provides direct GPU buffer access to trajectory position data in Niagara HLSL.
 * This NDI eliminates the need for custom C++ buffer binding code by exposing
 * the structured buffer directly to Niagara's parameter system.
 * 
 * Features:
 * - Direct GPU buffer binding (no CPU overhead)
 * - Automatic SRV creation and management
 * - Blueprint-assignable buffer provider
 * - Read-only structured buffer access
 * - Compatible with GPU Compute simulation
 * 
 * HLSL Functions Exposed:
 * - GetPositionAtIndex(int Index) → float3
 * - GetNumPositions() → int
 * - GetTrajectoryStartIndex(int TrajIndex) → int
 * - GetTrajectorySampleCount(int TrajIndex) → int
 * - GetNumTrajectories() → int
 * - GetMaxSamplesPerTrajectory() → int
 * 
 * Usage:
 * 1. Add this DI as a User Parameter in Niagara System
 * 2. In Blueprint, set BufferProvider to your UTrajectoryBufferProvider component
 * 3. Call UpdateFromDataset() on the provider to load data
 * 4. In Niagara HLSL, use the exposed functions to access positions
 * 
 * Example HLSL:
 * ```hlsl
 * float3 Position = TrajectoryBuffer.GetPositionAtIndex(Particles.StartIndex + Particles.SampleOffset);
 * ```
 */
UCLASS(EditInlineNew, Category = "Trajectory Data", meta = (DisplayName = "Trajectory Position Buffer"))
class TRAJECTORYDATA_API UNiagaraDataInterfaceTrajectoryBuffer : public UNiagaraDataInterface
{
	GENERATED_BODY()

public:
	UNiagaraDataInterfaceTrajectoryBuffer();

	//~ Begin UNiagaraDataInterface Interface
	virtual void GetFunctions(TArray<FNiagaraFunctionSignature>& OutFunctions) override;
	virtual void GetVMExternalFunction(const FVMExternalFunctionBindingInfo& BindingInfo, void* InstanceData, FVMExternalFunction& OutFunc) override;
	virtual bool CanExecuteOnTarget(ENiagaraSimTarget Target) const override { return Target == ENiagaraSimTarget::GPUComputeSim; }
	virtual bool Equals(const UNiagaraDataInterface* Other) const override;
	virtual bool CopyToInternal(UNiagaraDataInterface* Destination) const override;
	//~ End UNiagaraDataInterface Interface

	//~ Begin UNiagaraDataInterface GPU Simulation Interface
	virtual void ProvidePerInstanceDataForRenderThread(void* DataForRenderThread, void* PerInstanceData, const FNiagaraSystemInstanceID& SystemInstance) override;
	virtual void GetParameterDefinitionHLSL(const FNiagaraDataInterfaceGPUParamInfo& ParamInfo, FString& OutHLSL) override;
	virtual bool GetFunctionHLSL(const FNiagaraDataInterfaceGPUParamInfo& ParamInfo, const FNiagaraDataInterfaceGeneratedFunction& FunctionInfo, int FunctionInstanceIndex, FString& OutHLSL) override;
	//~ End UNiagaraDataInterface GPU Simulation Interface

public:
	/**
	 * Buffer provider component that contains the trajectory data
	 * Set this in Blueprint or C++ to connect the NDI to your data source
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Trajectory Data")
	TObjectPtr<UTrajectoryBufferProvider> BufferProvider;

protected:
	// Function names for HLSL generation
	static const FName GetPositionAtIndexName;
	static const FName GetNumPositionsName;
	static const FName GetTrajectoryStartIndexName;
	static const FName GetTrajectorySampleCountName;
	static const FName GetNumTrajectoriesName;
	static const FName GetMaxSamplesPerTrajectoryName;
};

/**
 * Proxy for GPU simulation
 * Manages the render thread representation of the data interface
 */
struct FNiagaraDataInterfaceProxyTrajectoryBuffer : public FNiagaraDataInterfaceProxy
{
	// Per-instance data passed to render thread
	struct FInstanceData
	{
		FShaderResourceViewRHIRef PositionBufferSRV;
		FShaderResourceViewRHIRef TrajectoryInfoBufferSRV;
		int32 NumPositions = 0;
		int32 NumTrajectories = 0;
		int32 MaxSamplesPerTrajectory = 0;
	};

	// Map of system instances to their data
	TMap<FNiagaraSystemInstanceID, FInstanceData> SystemInstancesToInstanceData;

	virtual int32 PerInstanceDataPassedToRenderThreadSize() const override { return sizeof(FInstanceData); }
	virtual void ConsumePerInstanceDataFromGameThread(void* PerInstanceData, const FNiagaraSystemInstanceID& Instance) override;
};
