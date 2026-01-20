// Copyright Epic Games, Inc. All Rights Reserved.

#include "NiagaraDataInterfaceTrajectoryBuffer.h"
#include "NiagaraShader.h"
#include "NiagaraComponent.h"
#include "NiagaraSystemInstance.h"
#include "ShaderParameterUtils.h"

#define LOCTEXT_NAMESPACE "NiagaraDataInterfaceTrajectoryBuffer"

// Function name constants
const FName UNiagaraDataInterfaceTrajectoryBuffer::GetPositionAtIndexName(TEXT("GetPositionAtIndex"));
const FName UNiagaraDataInterfaceTrajectoryBuffer::GetNumPositionsName(TEXT("GetNumPositions"));
const FName UNiagaraDataInterfaceTrajectoryBuffer::GetTrajectoryStartIndexName(TEXT("GetTrajectoryStartIndex"));
const FName UNiagaraDataInterfaceTrajectoryBuffer::GetTrajectorySampleCountName(TEXT("GetTrajectorySampleCount"));
const FName UNiagaraDataInterfaceTrajectoryBuffer::GetNumTrajectoriesName(TEXT("GetNumTrajectories"));
const FName UNiagaraDataInterfaceTrajectoryBuffer::GetMaxSamplesPerTrajectoryName(TEXT("GetMaxSamplesPerTrajectory"));

UNiagaraDataInterfaceTrajectoryBuffer::UNiagaraDataInterfaceTrajectoryBuffer()
	: BufferProvider(nullptr)
{
}

void UNiagaraDataInterfaceTrajectoryBuffer::GetFunctions(TArray<FNiagaraFunctionSignature>& OutFunctions)
{
	// GetPositionAtIndex(int Index) -> float3
	{
		FNiagaraFunctionSignature Sig;
		Sig.Name = GetPositionAtIndexName;
		Sig.bMemberFunction = true;
		Sig.bRequiresContext = false;
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("TrajectoryBuffer")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetIntDef(), TEXT("Index")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("Position")));
		OutFunctions.Add(Sig);
	}

	// GetNumPositions() -> int
	{
		FNiagaraFunctionSignature Sig;
		Sig.Name = GetNumPositionsName;
		Sig.bMemberFunction = true;
		Sig.bRequiresContext = false;
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("TrajectoryBuffer")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetIntDef(), TEXT("NumPositions")));
		OutFunctions.Add(Sig);
	}

	// GetTrajectoryStartIndex(int TrajIndex) -> int
	{
		FNiagaraFunctionSignature Sig;
		Sig.Name = GetTrajectoryStartIndexName;
		Sig.bMemberFunction = true;
		Sig.bRequiresContext = false;
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("TrajectoryBuffer")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetIntDef(), TEXT("TrajectoryIndex")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetIntDef(), TEXT("StartIndex")));
		OutFunctions.Add(Sig);
	}

	// GetTrajectorySampleCount(int TrajIndex) -> int
	{
		FNiagaraFunctionSignature Sig;
		Sig.Name = GetTrajectorySampleCountName;
		Sig.bMemberFunction = true;
		Sig.bRequiresContext = false;
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("TrajectoryBuffer")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetIntDef(), TEXT("TrajectoryIndex")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetIntDef(), TEXT("SampleCount")));
		OutFunctions.Add(Sig);
	}

	// GetNumTrajectories() -> int
	{
		FNiagaraFunctionSignature Sig;
		Sig.Name = GetNumTrajectoriesName;
		Sig.bMemberFunction = true;
		Sig.bRequiresContext = false;
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("TrajectoryBuffer")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetIntDef(), TEXT("NumTrajectories")));
		OutFunctions.Add(Sig);
	}

	// GetMaxSamplesPerTrajectory() -> int
	{
		FNiagaraFunctionSignature Sig;
		Sig.Name = GetMaxSamplesPerTrajectoryName;
		Sig.bMemberFunction = true;
		Sig.bRequiresContext = false;
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("TrajectoryBuffer")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetIntDef(), TEXT("MaxSamplesPerTrajectory")));
		OutFunctions.Add(Sig);
	}
}

void UNiagaraDataInterfaceTrajectoryBuffer::GetVMExternalFunction(const FVMExternalFunctionBindingInfo& BindingInfo, void* InstanceData, FVMExternalFunction& OutFunc)
{
	// VM functions not supported - GPU only
	OutFunc = FVMExternalFunction();
}

bool UNiagaraDataInterfaceTrajectoryBuffer::Equals(const UNiagaraDataInterface* Other) const
{
	if (!Super::Equals(Other))
	{
		return false;
	}

	const UNiagaraDataInterfaceTrajectoryBuffer* OtherTyped = CastChecked<const UNiagaraDataInterfaceTrajectoryBuffer>(Other);
	return OtherTyped->BufferProvider == BufferProvider;
}

bool UNiagaraDataInterfaceTrajectoryBuffer::CopyToInternal(UNiagaraDataInterface* Destination) const
{
	if (!Super::CopyToInternal(Destination))
	{
		return false;
	}

	UNiagaraDataInterfaceTrajectoryBuffer* DestTyped = CastChecked<UNiagaraDataInterfaceTrajectoryBuffer>(Destination);
	DestTyped->BufferProvider = BufferProvider;
	return true;
}

void UNiagaraDataInterfaceTrajectoryBuffer::ProvidePerInstanceDataForRenderThread(void* DataForRenderThread, void* PerInstanceData, const FNiagaraSystemInstanceID& SystemInstance)
{
	FNiagaraDataInterfaceProxyTrajectoryBuffer::FInstanceData* InstanceData = new (DataForRenderThread) FNiagaraDataInterfaceProxyTrajectoryBuffer::FInstanceData();

	if (BufferProvider && BufferProvider->IsBufferValid())
	{
		FTrajectoryPositionBufferResource* BufferResource = BufferProvider->GetPositionBufferResource();
		if (BufferResource)
		{
			InstanceData->PositionBufferSRV = BufferResource->GetBufferSRV();
			InstanceData->TrajectoryInfoBufferSRV = nullptr; // TODO: Add info buffer if needed
			
			FTrajectoryBufferMetadata Metadata = BufferProvider->GetMetadata();
			InstanceData->NumPositions = Metadata.TotalSampleCount;
			InstanceData->NumTrajectories = Metadata.NumTrajectories;
			InstanceData->MaxSamplesPerTrajectory = Metadata.MaxSamplesPerTrajectory;
		}
	}
}

void UNiagaraDataInterfaceTrajectoryBuffer::GetParameterDefinitionHLSL(const FNiagaraDataInterfaceGPUParamInfo& ParamInfo, FString& OutHLSL)
{
	// Define the structured buffer in HLSL
	OutHLSL += TEXT("StructuredBuffer<float3> {ParameterName}_PositionBuffer;\n");
	OutHLSL += TEXT("int {ParameterName}_NumPositions;\n");
	OutHLSL += TEXT("int {ParameterName}_NumTrajectories;\n");
	OutHLSL += TEXT("int {ParameterName}_MaxSamplesPerTrajectory;\n");
}

bool UNiagaraDataInterfaceTrajectoryBuffer::GetFunctionHLSL(const FNiagaraDataInterfaceGPUParamInfo& ParamInfo, const FNiagaraDataInterfaceGeneratedFunction& FunctionInfo, int FunctionInstanceIndex, FString& OutHLSL)
{
	FString ParameterName = ParamInfo.DataInterfaceHLSLSymbol;

	if (FunctionInfo.DefinitionName == GetPositionAtIndexName)
	{
		OutHLSL += FString::Printf(TEXT("void %s(int Index, out float3 Position)\n"), *FunctionInfo.InstanceName);
		OutHLSL += TEXT("{\n");
		OutHLSL += FString::Printf(TEXT("    if (Index >= 0 && Index < %s_NumPositions)\n"), *ParameterName);
		OutHLSL += FString::Printf(TEXT("        Position = %s_PositionBuffer[Index];\n"), *ParameterName);
		OutHLSL += TEXT("    else\n");
		OutHLSL += TEXT("        Position = float3(0, 0, 0);\n");
		OutHLSL += TEXT("}\n");
		return true;
	}
	else if (FunctionInfo.DefinitionName == GetNumPositionsName)
	{
		OutHLSL += FString::Printf(TEXT("void %s(out int NumPositions)\n"), *FunctionInfo.InstanceName);
		OutHLSL += TEXT("{\n");
		OutHLSL += FString::Printf(TEXT("    NumPositions = %s_NumPositions;\n"), *ParameterName);
		OutHLSL += TEXT("}\n");
		return true;
	}
	else if (FunctionInfo.DefinitionName == GetTrajectoryStartIndexName)
	{
		OutHLSL += FString::Printf(TEXT("void %s(int TrajectoryIndex, out int StartIndex)\n"), *FunctionInfo.InstanceName);
		OutHLSL += TEXT("{\n");
		OutHLSL += FString::Printf(TEXT("    StartIndex = TrajectoryIndex * %s_MaxSamplesPerTrajectory;\n"), *ParameterName);
		OutHLSL += TEXT("}\n");
		return true;
	}
	else if (FunctionInfo.DefinitionName == GetTrajectorySampleCountName)
	{
		OutHLSL += FString::Printf(TEXT("void %s(int TrajectoryIndex, out int SampleCount)\n"), *FunctionInfo.InstanceName);
		OutHLSL += TEXT("{\n");
		OutHLSL += FString::Printf(TEXT("    SampleCount = %s_MaxSamplesPerTrajectory;\n"), *ParameterName);
		OutHLSL += TEXT("}\n");
		return true;
	}
	else if (FunctionInfo.DefinitionName == GetNumTrajectoriesName)
	{
		OutHLSL += FString::Printf(TEXT("void %s(out int NumTrajectories)\n"), *FunctionInfo.InstanceName);
		OutHLSL += TEXT("{\n");
		OutHLSL += FString::Printf(TEXT("    NumTrajectories = %s_NumTrajectories;\n"), *ParameterName);
		OutHLSL += TEXT("}\n");
		return true;
	}
	else if (FunctionInfo.DefinitionName == GetMaxSamplesPerTrajectoryName)
	{
		OutHLSL += FString::Printf(TEXT("void %s(out int MaxSamplesPerTrajectory)\n"), *FunctionInfo.InstanceName);
		OutHLSL += TEXT("{\n");
		OutHLSL += FString::Printf(TEXT("    MaxSamplesPerTrajectory = %s_MaxSamplesPerTrajectory;\n"), *ParameterName);
		OutHLSL += TEXT("}\n");
		return true;
	}

	return false;
}

//////////////////////////////////////////////////////////////////////////
// Proxy Implementation

void FNiagaraDataInterfaceProxyTrajectoryBuffer::ConsumePerInstanceDataFromGameThread(void* PerInstanceData, const FNiagaraSystemInstanceID& Instance)
{
	FInstanceData* SourceData = static_cast<FInstanceData*>(PerInstanceData);
	FInstanceData& TargetData = SystemInstancesToInstanceData.FindOrAdd(Instance);

	TargetData.PositionBufferSRV = SourceData->PositionBufferSRV;
	TargetData.TrajectoryInfoBufferSRV = SourceData->TrajectoryInfoBufferSRV;
	TargetData.NumPositions = SourceData->NumPositions;
	TargetData.NumTrajectories = SourceData->NumTrajectories;
	TargetData.MaxSamplesPerTrajectory = SourceData->MaxSamplesPerTrajectory;

	SourceData->~FInstanceData();
}

#undef LOCTEXT_NAMESPACE
