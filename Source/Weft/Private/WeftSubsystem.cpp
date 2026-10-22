#include "WeftSubsystem.h"

#include "Algo/AllOf.h"
#include "Containers/IndirectArray.h"
#include "Dom/JsonObject.h"
#include "Engine/Engine.h"
#include "Misc/FileHelper.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Engine/Texture.h"
#include "Framework/Application/SlateApplication.h"
#include "TextureResource.h"
#include "Misc/Paths.h"
#include "Render/WeftRenderer.h"
#include "RenderingThread.h"
#include "WeftGamepadInput.h"
#include "WeftModule.h"
#include "WeftView.h"

#if WITH_WEFT
#include "weft/weft.h"
#endif

void UWeftSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
#if WITH_WEFT
	if (!FWeftModule::IsAvailable())
	{
		return;
	}
	// The game thread owns the runtime: its Views' pages run there. The platform's fonts, with their generic families
	// and fallbacks (CJK included).
	if (const weft_result Result = weft_runtime_create(nullptr, &Runtime); Result != WEFT_OK)
	{
		UE_LOG(LogWeft, Error, TEXT("Weft: no runtime: %s"), UTF8_TO_TCHAR(weft_result_string(Result)));
		Runtime = nullptr;
		return;
	}
	Renderer = MakeShared<FWeftRenderer, ESPMode::ThreadSafe>();
	// The gamepads, before Slate's widgets and the game see them.
	if (FSlateApplication::IsInitialized())
	{
		GamepadInput = MakeShared<FWeftGamepadInput>([this](TArray<UWeftView*>& Out)
		{
			for (const TWeakObjectPtr<UWeftView>& View : Views)
			{
				if (UWeftView* Alive = View.Get())
				{
					Out.Add(Alive);
				}
			}
		});
		FSlateApplication::Get().RegisterInputPreProcessor(GamepadInput, 0);
	}
#endif
}

void UWeftSubsystem::Deinitialize()
{
#if WITH_WEFT
	if (GamepadInput && FSlateApplication::IsInitialized())
	{
		FSlateApplication::Get().UnregisterInputPreProcessor(GamepadInput);
	}
	GamepadInput.Reset();
	if (Runtime)
	{
		for (const TWeakObjectPtr<UWeftView>& View : Views)
		{
			if (UWeftView* Alive = View.Get())
			{
				Alive->Close();
			}
		}
		Views.Empty();
		// What the render thread still holds of the Views goes first, then the renderer (after the GPU is done with
		// it), then the runtime.
		FlushRenderingCommands();
		if (Renderer)
		{
			Renderer->Shutdown();
			Renderer.Reset();
		}
		if (const weft_result Result = weft_runtime_destroy(Runtime); Result != WEFT_OK)
		{
			UE_LOG(LogWeft, Warning, TEXT("Weft: the runtime stays: %s"), UTF8_TO_TCHAR(weft_result_string(Result)));
		}
		Runtime = nullptr;
	}
#endif
	Super::Deinitialize();
}

UWeftSubsystem* UWeftSubsystem::Get()
{
	return GEngine ? GEngine->GetEngineSubsystem<UWeftSubsystem>() : nullptr;
}

bool UWeftSubsystem::AddFont(const FString& Family, const FString& File, int32 Weight, bool bItalic, int32 FaceIndex,
	const FString& UnicodeRange)
{
#if WITH_WEFT
	if (!Runtime)
	{
		return false;
	}
	const FTCHARToUTF8 Name(*Family);
	const FTCHARToUTF8 Path(*FPaths::ConvertRelativePathToFull(File));
	weft_font_face Face{sizeof(Face)};
	Face.weight = uint32(FMath::Clamp(Weight, 1, 1000));
	Face.italic = bItalic ? 1 : 0;
	Face.face_index = uint32(FMath::Max(FaceIndex, 0));
	Face.family = Name.Get();
	Face.family_size = size_t(Name.Length());
	Face.path = Path.Get();
	Face.path_size = size_t(Path.Length());
	const FTCHARToUTF8 Range(*UnicodeRange);
	if (!UnicodeRange.IsEmpty())
	{
		Face.unicode_range = Range.Get();
		Face.unicode_range_size = size_t(Range.Length());
	}
	if (const weft_result Result = weft_runtime_add_font(Runtime, &Face); Result != WEFT_OK)
	{
		UE_LOG(LogWeft, Warning, TEXT("Weft: font %s (%s) not added: %s"), *Family, *File, UTF8_TO_TCHAR(weft_result_string(Result)));
		return false;
	}
	return true;
#else
	return false;
#endif
}

bool UWeftSubsystem::AddFallbackFamily(const FString& Family)
{
#if WITH_WEFT
	if (!Runtime)
	{
		return false;
	}
	const FTCHARToUTF8 Name(*Family);
	return weft_runtime_add_fallback_family(Runtime, Name.Get(), size_t(Name.Length())) == WEFT_OK;
#else
	return false;
#endif
}

namespace
{
	TSharedPtr<FJsonObject> ReadManifest(const FString& File)
	{
		FString Text;
		TSharedPtr<FJsonObject> Json;
		if (!FFileHelper::LoadFileToString(Text, *File) ||
			!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Json) || !Json)
		{
			UE_LOG(LogWeft, Warning, TEXT("Weft: %s is no JSON manifest"), *File);
			return nullptr;
		}
		return Json;
	}
} // namespace

int32 UWeftSubsystem::AddFonts(const FString& ManifestFile)
{
	const FString File = FPaths::ConvertRelativePathToFull(ManifestFile);
	if (!Runtime || FontManifests.Contains(File))
	{
		return 0;
	}
	const TSharedPtr<FJsonObject> Json = ReadManifest(File);
	if (!Json)
	{
		return 0;
	}
	FontManifests.Add(File);
	const FString Folder = FPaths::GetPath(File);
	int32 Added = 0;
	const TArray<TSharedPtr<FJsonValue>>* Faces = nullptr;
	if (Json->TryGetArrayField(TEXT("faces"), Faces))
	{
		for (const TSharedPtr<FJsonValue>& Value : *Faces)
		{
			const TSharedPtr<FJsonObject>* Face = nullptr;
			if (!Value->TryGetObject(Face))
			{
				continue;
			}
			FString Family;
			FString Path;
			FString Range;
			int32 Weight = 400;
			int32 Index = 0;
			bool bItalic = false;
			(*Face)->TryGetStringField(TEXT("family"), Family);
			(*Face)->TryGetStringField(TEXT("path"), Path);
			(*Face)->TryGetStringField(TEXT("unicodeRange"), Range);
			(*Face)->TryGetNumberField(TEXT("weight"), Weight);
			(*Face)->TryGetNumberField(TEXT("face"), Index);
			(*Face)->TryGetBoolField(TEXT("italic"), bItalic);
			Added += AddFont(Family, FPaths::Combine(Folder, Path), Weight, bItalic, Index, Range) ? 1 : 0;
		}
	}
	TArray<FString> Fallback;
	Json->TryGetStringArrayField(TEXT("fallback"), Fallback);
	for (const FString& Family : Fallback)
	{
		AddFallbackFamily(Family);
	}
	UE_LOG(LogWeft, Log, TEXT("Weft: %d font faces from %s"), Added, *File);
	return Added;
}

int32 UWeftSubsystem::AddEffects(const FString& ManifestFile)
{
#if WITH_WEFT
	const FString File = FPaths::ConvertRelativePathToFull(ManifestFile);
	const TSharedPtr<FJsonObject> Json = Runtime ? ReadManifest(File) : nullptr;
	const TArray<TSharedPtr<FJsonValue>>* List = nullptr;
	if (!Json || !Json->TryGetArrayField(TEXT("effects"), List))
	{
		return 0;
	}
	const FString Folder = FPaths::GetPath(File);
	int32 Added = 0;
	for (const TSharedPtr<FJsonValue>& Value : *List)
	{
		const TSharedPtr<FJsonObject>* Effect = nullptr;
		FString Name;
		if (!Value->TryGetObject(Effect) || !(*Effect)->TryGetStringField(TEXT("name"), Name) || Effects.Contains(Name))
		{
			continue;
		}
		// What the descriptor points at, in place and alive for the call.
		TIndirectArray<FTCHARToUTF8> Texts;
		TIndirectArray<TArray<uint8>> Files;
		const auto Text = [&](const FString& In) -> const FTCHARToUTF8&
		{
			Texts.Add(new FTCHARToUTF8(*In));
			return Texts.Last();
		};
		const auto NewFile = [&]() -> TArray<uint8>&
		{
			Files.Add(new TArray<uint8>());
			return Files.Last();
		};
		TArray<weft_effect_parameter> Parameters;
		const TArray<TSharedPtr<FJsonValue>>* ParameterList = nullptr;
		if ((*Effect)->TryGetArrayField(TEXT("parameters"), ParameterList))
		{
			for (const TSharedPtr<FJsonValue>& P : *ParameterList)
			{
				const TSharedPtr<FJsonObject>* Parameter = nullptr;
				if (!P->TryGetObject(Parameter))
				{
					continue;
				}
				FString ParameterName;
				FString Type;
				FString Initial;
				(*Parameter)->TryGetStringField(TEXT("name"), ParameterName);
				(*Parameter)->TryGetStringField(TEXT("type"), Type);
				(*Parameter)->TryGetStringField(TEXT("initial"), Initial);
				const uint32 Kind = Type == TEXT("number") ? WEFT_EFFECT_NUMBER
					: Type == TEXT("vec2")                 ? WEFT_EFFECT_VEC2
					: Type == TEXT("vec3")                 ? WEFT_EFFECT_VEC3
					: Type == TEXT("vec4")                 ? WEFT_EFFECT_VEC4
					: Type == TEXT("color")                ? WEFT_EFFECT_COLOR
														   : 0u;
				const FTCHARToUTF8& N = Text(ParameterName);
				const FTCHARToUTF8& I = Text(Initial);
				Parameters.Add({N.Get(), size_t(N.Length()), I.Get(), size_t(I.Length()), Kind, 0});
			}
		}
		TArray<weft_effect_texture> EffectTextures;
		const TArray<TSharedPtr<FJsonValue>>* TextureList = nullptr;
		if ((*Effect)->TryGetArrayField(TEXT("textures"), TextureList))
		{
			for (const TSharedPtr<FJsonValue>& T : *TextureList)
			{
				const TSharedPtr<FJsonObject>* Texture = nullptr;
				if (!T->TryGetObject(Texture))
				{
					continue;
				}
				FString TextureName;
				FString Path;
				FString Format;
				(*Texture)->TryGetStringField(TEXT("name"), TextureName);
				(*Texture)->TryGetStringField(TEXT("path"), Path);
				(*Texture)->TryGetStringField(TEXT("format"), Format);
				TArray<uint8>& Bytes = NewFile();
				FFileHelper::LoadFileToArray(Bytes, *FPaths::Combine(Folder, Path));
				const FTCHARToUTF8& N = Text(TextureName);
				EffectTextures.Add({N.Get(), size_t(N.Length()), Bytes.GetData(), size_t(Bytes.Num()),
					(Format == TEXT("srgb") ? WEFT_EFFECT_TEXTURE_SRGB : WEFT_EFFECT_TEXTURE_DATA) | WEFT_EFFECT_TEXTURE_PNG, 0, 0,
					0});
			}
		}
		FString Bytecode;
		(*Effect)->TryGetStringField(TEXT("bytecode"), Bytecode);
		const FString Prefix = FPaths::Combine(Folder, Bytecode);
		const auto Code = [&](const TCHAR* Suffix) -> const TArray<uint8>&
		{
			TArray<uint8>& Bytes = NewFile();
			FFileHelper::LoadFileToArray(Bytes, *(Prefix + Suffix), FILEREAD_Silent);
			return Bytes;
		};
		const TArray<uint8>& DxbcEncoded = Code(TEXT(".encoded.dxbc"));
		const TArray<uint8>& DxbcLinear = Code(TEXT(".linear.dxbc"));
		const TArray<uint8>& SpirvEncoded = Code(TEXT(".encoded.spv"));
		const TArray<uint8>& SpirvLinear = Code(TEXT(".linear.spv"));
		bool bAnimated = false;
		(*Effect)->TryGetBoolField(TEXT("animated"), bAnimated);
		bool bBackdrop = false;
		(*Effect)->TryGetBoolField(TEXT("backdrop"), bBackdrop);
		double BackdropMargin = 0;
		(*Effect)->TryGetNumberField(TEXT("backdropMargin"), BackdropMargin);
		const FTCHARToUTF8& EffectName = Text(Name);
		weft_effect_desc Desc{sizeof(Desc)};
		Desc.animated = bAnimated ? 1 : 0;
		Desc.backdrop = bBackdrop ? 1 : 0;
		Desc.backdrop_margin = bBackdrop ? float(BackdropMargin) : 0.f;
		Desc.name = EffectName.Get();
		Desc.name_size = size_t(EffectName.Length());
		Desc.parameters = Parameters.GetData();
		Desc.parameter_count = uint32(Parameters.Num());
		Desc.textures = EffectTextures.GetData();
		Desc.texture_count = uint32(EffectTextures.Num());
		Desc.dxbc_encoded = DxbcEncoded.GetData();
		Desc.dxbc_encoded_size = size_t(DxbcEncoded.Num());
		Desc.dxbc_linear = DxbcLinear.GetData();
		Desc.dxbc_linear_size = size_t(DxbcLinear.Num());
		Desc.spirv_encoded = SpirvEncoded.GetData();
		Desc.spirv_encoded_size = size_t(SpirvEncoded.Num());
		Desc.spirv_linear = SpirvLinear.GetData();
		Desc.spirv_linear_size = size_t(SpirvLinear.Num());
		if (const weft_result Result = weft_runtime_add_effect(Runtime, &Desc); Result != WEFT_OK)
		{
			UE_LOG(LogWeft, Warning, TEXT("Weft: effect %s not added: %s"), *Name, UTF8_TO_TCHAR(weft_result_string(Result)));
			continue;
		}
		Effects.Add(Name);
		++Added;
	}
	UE_LOG(LogWeft, Log, TEXT("Weft: %d effects from %s"), Added, *File);
	return Added;
#else
	return 0;
#endif
}

bool UWeftSubsystem::SetTexture(const FString& Name, UTexture* Texture)
{
	const bool bValid = !Name.IsEmpty() && Name.Len() <= 64 &&
		Algo::AllOf(Name, [](TCHAR C) { return (FChar::IsAlnum(C) && C < 128) || C == TEXT('-') || C == TEXT('_'); });
	if (!bValid)
	{
		UE_LOG(LogWeft, Warning, TEXT("Weft: \"%s\" is not a texture name (letters, digits, - and _, at most 64)."), *Name);
		return false;
	}
	if (Texture)
	{
		Textures.Add(Name, Texture);
	}
	else
	{
		Textures.Remove(Name);
	}
	return true;
}

void UWeftSubsystem::GetTextures(TArray<TPair<FString, FTextureResource*>>& Out) const
{
	Out.Reset();
	for (const TPair<FString, TWeakObjectPtr<UTexture>>& Pair : Textures)
	{
		if (UTexture* Texture = Pair.Value.Get())
		{
			if (FTextureResource* Resource = Texture->GetResource())
			{
				Out.Emplace(Pair.Key, Resource);
			}
		}
	}
}

UWeftView* UWeftSubsystem::CreateView()
{
	if (!Runtime)
	{
		return nullptr;
	}
	UWeftView* View = NewObject<UWeftView>(this);
	if (!View->Open(Runtime))
	{
		return nullptr;
	}
	Views.RemoveAll([](const TWeakObjectPtr<UWeftView>& Old) { return !Old.IsValid(); });
	Views.Add(View);
	return View;
}
