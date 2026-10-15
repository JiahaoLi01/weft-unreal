#include "Render/WeftSlateDrawer.h"

#include "Render/WeftFramePacket.h"
#include "Render/WeftRenderer.h"
#include "RenderGraphBuilder.h"
#include "RenderGraphUtils.h"
#include "WeftModule.h"

namespace
{
	BEGIN_SHADER_PARAMETER_STRUCT(FWeftPassParameters, )
		RENDER_TARGET_BINDING_SLOTS()
	END_SHADER_PARAMETER_STRUCT()
}

FWeftSlateDrawer::FWeftSlateDrawer(TSharedPtr<FWeftRenderer, ESPMode::ThreadSafe> InRenderer)
	: Renderer(MoveTemp(InRenderer))
{
}

void FWeftSlateDrawer::SetPending(TSharedPtr<FWeftFramePacket, ESPMode::ThreadSafe> Packet, FIntRect Rect)
{
	check(IsInRenderingThread());
	Pending = MoveTemp(Packet);
	PendingRect = Rect;
}

void FWeftSlateDrawer::Draw_RenderThread(FRDGBuilder& GraphBuilder, const FDrawPassInputs& Inputs)
{
	TSharedPtr<FWeftFramePacket, ESPMode::ThreadSafe> Packet = MoveTemp(Pending);
	if (!Packet || !Renderer || !Inputs.OutputTexture)
	{
		return;
	}
	FRDGTexture* Output = Inputs.OutputTexture;
	uint32 Format = 0;
	if (!FWeftRenderer::TargetFormat(Output->Desc.Format, Format))
	{
		if (!bWarnedFormat)
		{
			UE_LOG(LogWeft, Warning, TEXT("Weft: Slate draws into %s, which weft does not draw into; views draw nothing."),
				GetPixelFormatString(Output->Desc.Format));
			bWarnedFormat = true;
		}
		return;
	}
	// The widget's pixels in the output.
	const FIntPoint Offset(FMath::RoundToInt(Inputs.ElementsOffset.X), FMath::RoundToInt(Inputs.ElementsOffset.Y));
	FIntRect Rect(PendingRect.Min + Offset, PendingRect.Max + Offset);
	Rect.Clip(FIntRect(FIntPoint::ZeroValue, Output->Desc.Extent));
	if (Rect.Width() <= 0 || Rect.Height() <= 0 || Rect.Min != PendingRect.Min + Offset)
	{
		// Off the output, or cut at its top or left (the page would draw shifted): nothing this frame.
		return;
	}
	const FIntPoint Size = Rect.Size();
	const bool bDirect = Rect.Min == FIntPoint::ZeroValue;
	FRDGTexture* Target = Output;
	if (!bDirect)
	{
		Target = GraphBuilder.CreateTexture(
			FRDGTextureDesc::Create2D(Size, Output->Desc.Format, FClearValueBinding::None,
				TexCreate_RenderTargetable | TexCreate_ShaderResource),
			TEXT("Weft.View"));
		FRHICopyTextureInfo In;
		In.SourcePosition = FIntVector(Rect.Min.X, Rect.Min.Y, 0);
		In.Size = FIntVector(Size.X, Size.Y, 1);
		AddCopyTexturePass(GraphBuilder, Output, Target, In);
	}
	FWeftPassParameters* Parameters = GraphBuilder.AllocParameters<FWeftPassParameters>();
	Parameters->RenderTargets[0] = FRenderTargetBinding(Target, ERenderTargetLoadAction::ELoad);
	// weft records its own passes into the RHI's command list (no render pass of the graph's), on the thread that
	// executes it, in the graph's order.
	GraphBuilder.AddPass(RDG_EVENT_NAME("Weft"), Parameters,
		ERDGPassFlags::Raster | ERDGPassFlags::SkipRenderPass | ERDGPassFlags::NeverCull | ERDGPassFlags::NeverParallel,
		[Renderer = Renderer, Target, Size, Format, Packet](FRHICommandList& RHICmdList)
		{
			Renderer->Draw(RHICmdList, Target->GetRHI(), Size, Format, Packet);
		});
	if (!bDirect)
	{
		FRHICopyTextureInfo Back;
		Back.DestPosition = FIntVector(Rect.Min.X, Rect.Min.Y, 0);
		Back.Size = FIntVector(Size.X, Size.Y, 1);
		AddCopyTexturePass(GraphBuilder, Target, Output, Back);
	}
}
